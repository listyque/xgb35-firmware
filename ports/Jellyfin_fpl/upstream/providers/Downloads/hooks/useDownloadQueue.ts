import type { Api } from "@jellyfin/sdk";
import { getLibraryApi } from "@jellyfin/sdk/lib/utils/api";
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { useTranslation } from "react-i18next";
import { toast } from "sonner-native";
import type { Settings } from "@/utils/atoms/settings";
import { getDefaultPlaySettings } from "@/utils/jellyfin/getDefaultPlaySettings";
import { getDownloadStreamUrl } from "@/utils/jellyfin/media/getStreamUrl";
import { storage } from "@/utils/mmkv";
import { getDownloadedItemById } from "../database";
import type { DownloadRequest } from "../downloadRequest";
import { getPendingDownload } from "../pendingDownloads";
import type { JobStatus } from "../types";
import type { useDownloadOperations } from "./useDownloadOperations";

const QUEUE_KEY = "downloads.queue.v1";

type QueueEntry = DownloadRequest & {
  id: string;
  owner: string;
  status: "queued" | "paused" | "error";
};

function readQueue(): QueueEntry[] {
  try {
    const entries = JSON.parse(storage.getString(QUEUE_KEY) ?? "[]");
    return Array.isArray(entries) ? entries : [];
  } catch {
    return [];
  }
}

type Operations = ReturnType<typeof useDownloadOperations>;

export function useDownloadQueue({
  api,
  userId,
  serverId,
  canTransfer = true,
  settings,
  processes,
  startBackgroundDownload,
  cancelDownload,
}: Pick<Operations, "startBackgroundDownload" | "cancelDownload"> & {
  api?: Api | null;
  userId?: string | null;
  serverId?: string | null;
  canTransfer?: boolean;
  settings: Settings | null;
  processes: JobStatus[];
}) {
  const { t } = useTranslation();
  const owner =
    api && userId ? JSON.stringify([serverId ?? api.basePath, userId]) : null;
  const currentOwner = useRef(owner);
  currentOwner.current = owner;
  const [queue, setQueue] = useState(readQueue);
  const latest = useRef(queue);
  const working = useRef<QueueEntry | null>(null);
  const [busy, setBusy] = useState(false);

  const changeQueue = useCallback(
    (change: (entries: QueueEntry[]) => QueueEntry[]) => {
      const next = change(latest.current);
      storage.set(QUEUE_KEY, JSON.stringify(next));
      latest.current = next;
      setQueue(next);
    },
    [],
  );

  const enqueueDownloads = useCallback(
    (requests: DownloadRequest[]) => {
      if (!owner) return 0;
      const known = new Set([
        ...processes.map(({ id }) => id),
        ...latest.current
          .filter((entry) => entry.owner === owner)
          .map(({ id }) => id),
      ]);
      const added: QueueEntry[] = [];
      for (const request of requests) {
        const id = request.item.Id;
        if (!id || known.has(id) || getDownloadedItemById(id)) continue;
        known.add(id);
        added.push({ ...request, id, owner, status: "queued" });
      }
      if (added.length) changeQueue((entries) => [...entries, ...added]);
      return added.length;
    },
    [changeQueue, owner, processes],
  );

  const matching = useCallback(
    (id: string) =>
      latest.current.find((entry) => entry.id === id && entry.owner === owner),
    [owner],
  );

  const cancel = useCallback(
    async (id: string) => {
      const entry = matching(id);
      if (entry)
        changeQueue((entries) => entries.filter((job) => job !== entry));
      await cancelDownload(id);
    },
    [cancelDownload, changeQueue, matching],
  );

  const pauseDownload = useCallback(
    async (id: string) => {
      if (!owner || getDownloadedItemById(id)) return;
      const entry = matching(id) ?? working.current;
      const pending = getPendingDownload(id);
      const request =
        entry?.id === id && entry.owner === owner
          ? entry
          : pending && {
              item: pending.item,
              quality: pending.request?.quality ?? {
                bitrate: pending.maxBitrate,
              },
              tracks: pending.request?.tracks ?? {
                mediaSourceId: pending.mediaSource.Id,
                audioIndex: pending.audioStreamIndex,
                subtitleIndex: pending.subtitleStreamIndex ?? -1,
              },
            };
      if (!request) return;
      const paused: QueueEntry = { ...request, owner, id, status: "paused" };
      changeQueue((entries) => {
        const original = entries.find(
          (job) => job.id === id && job.owner === owner,
        );
        return original
          ? entries.map((job) => (job === original ? paused : job))
          : [paused, ...entries];
      });
      // shortcut: restart the file on resume until native can retain partial downloads.
      await cancelDownload(id, true);
    },
    [cancelDownload, changeQueue, matching, owner],
  );

  const resumeDownload = useCallback(
    (id: string) => {
      const entry = matching(id);
      if (entry && entry.status !== "queued") {
        changeQueue((entries) =>
          entries.map((job) =>
            job === entry ? { ...job, status: "queued" } : job,
          ),
        );
      }
    },
    [changeQueue, matching],
  );

  useEffect(() => {
    if (
      !api ||
      !userId ||
      !owner ||
      !canTransfer ||
      busy ||
      working.current ||
      processes.some(
        ({ status }) => status === "queued" || status === "downloading",
      )
    )
      return;
    const entry = queue.find(
      (job) => job.owner === owner && job.status === "queued",
    );
    if (!entry) return;
    const isCurrent = () =>
      currentOwner.current === owner && latest.current.includes(entry);
    if (getDownloadedItemById(entry.id) || getPendingDownload(entry.id)) {
      changeQueue((entries) => entries.filter((job) => job !== entry));
      return;
    }
    working.current = entry;
    setBusy(true);
    void (async () => {
      try {
        let item = entry.item;
        if (!item.Chapters || !item.MediaSources?.length) {
          try {
            const { data } = await getLibraryApi(api).getItem({
              itemId: entry.id,
              userId,
            });
            item = data ?? item;
          } catch {
            // PlaybackInfo can still resolve the default version for an ordinary download.
          }
        }
        if (!isCurrent()) return;
        const defaults = getDefaultPlaySettings(item, settings);
        const tracks = entry.tracks ?? {
          mediaSourceId: defaults.mediaSource?.Id,
          audioIndex: defaults.audioIndex,
          subtitleIndex: defaults.subtitleIndex ?? -1,
        };
        const download = await getDownloadStreamUrl({
          api,
          item,
          userId,
          mediaSourceId: tracks.mediaSourceId,
          audioStreamIndex: tracks.audioIndex ?? -1,
          subtitleStreamIndex: tracks.subtitleIndex,
          maxStreamingBitrate: entry.quality.original
            ? undefined
            : entry.quality.bitrate.value,
          audioMode: settings?.audioTranscodeMode,
          quality: entry.quality,
        });
        if (!isCurrent()) return;
        if (!download?.url || !download.mediaSource) {
          throw new Error(
            "Jellyfin did not offer the selected download format",
          );
        }
        await startBackgroundDownload(
          download.url,
          item,
          download.mediaSource,
          entry.quality.bitrate,
          tracks.audioIndex,
          tracks.subtitleIndex,
          { quality: entry.quality, tracks },
        );
        if (isCurrent())
          changeQueue((entries) => entries.filter((job) => job !== entry));
      } catch {
        if (!isCurrent()) return;
        changeQueue((entries) =>
          entries.map((job) =>
            job === entry ? { ...job, status: "error" } : job,
          ),
        );
        toast.error(
          t("home.downloads.queue_transfer_failed", { name: entry.item.Name }),
        );
      } finally {
        working.current = null;
        setBusy(false);
      }
    })();
  }, [
    api,
    busy,
    canTransfer,
    changeQueue,
    owner,
    processes,
    queue,
    settings,
    startBackgroundDownload,
    t,
    userId,
  ]);

  const visibleProcesses = useMemo(() => {
    const active = new Set(processes.map(({ id }) => id));
    return [
      ...processes.filter(
        ({ status }) => status !== "completed" && status !== "error",
      ),
      ...queue
        .filter((entry) => entry.owner === owner && !active.has(entry.id))
        .map(
          (entry): JobStatus => ({
            id: entry.id,
            itemId: entry.id,
            item: entry.item,
            inputUrl: "",
            deviceId: "",
            progress: 0,
            mediaSource: {},
            maxBitrate: entry.quality.bitrate,
            status: entry.status,
            timestamp: new Date(0),
          }),
        ),
    ];
  }, [owner, processes, queue]);

  return {
    visibleProcesses,
    enqueueDownloads,
    cancelDownload: cancel,
    pauseDownload,
    resumeDownload,
  };
}
