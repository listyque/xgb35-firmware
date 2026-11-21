import type { Api } from "@jellyfin/sdk";
import { getLibraryApi } from "@jellyfin/sdk/lib/utils/api";
import { DOWNLOAD_FOLDER_LOOKUP_TIMEOUT_MS } from "@/constants/Playback";
import { FOLDER_TYPES } from "@/utils/library/folders";
import type { DownloadedItem, DownloadFolder } from "./types";

export async function getDownloadFolderPath(
  api: Api,
  itemId: string,
  signal?: AbortSignal,
): Promise<DownloadFolder[]> {
  const { data } = await getLibraryApi(api).getAncestors(
    { itemId },
    { signal, timeout: DOWNLOAD_FOLDER_LOOKUP_TIMEOUT_MS },
  );
  // Jellyfin returns the immediate parent first and includes internal roots.
  return data
    .slice()
    .reverse()
    .flatMap((parent) =>
      parent.Id &&
      parent.Name &&
      parent.Type &&
      (FOLDER_TYPES.includes(parent.Type) ||
        parent.Type === "UserView" ||
        parent.Type === "MusicArtist")
        ? [{ id: parent.Id, name: parent.Name }]
        : [],
    );
}

export async function loadMissingDownloadFolderPaths(
  api: Api,
  downloads: DownloadedItem[],
  signal: AbortSignal,
) {
  const paths = new Map<string, DownloadFolder[]>();
  const byParent = new Map<string, DownloadFolder[]>();
  for (const { item, folderPath } of downloads) {
    if (signal.aborted) break;
    if (folderPath !== undefined || !item.Id) continue;
    const parentId = item.ParentId ?? item.SeasonId ?? item.AlbumId;
    try {
      const path =
        (parentId && byParent.get(parentId)) ||
        (await getDownloadFolderPath(api, item.Id, signal));
      paths.set(item.Id, path);
      if (parentId) byParent.set(parentId, path);
    } catch {
      // The download may belong to an unavailable server. Keep it in offline storage.
    }
  }
  return paths;
}
