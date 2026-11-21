import { getLibraryApi, getUserViewApi } from "@jellyfin/sdk/lib/utils/api";
import { applicationId, nativeBuildVersion } from "expo-application";
import { File, Paths } from "expo-file-system";
import { requireOptionalNativeModule } from "expo-modules-core";
import { useAtomValue } from "jotai";
import { useRef, useState } from "react";
import { useTranslation } from "react-i18next";
import { Platform } from "react-native";
import { toast } from "sonner-native";
import { ListGroup } from "@/components/list/ListGroup";
import { ListItem } from "@/components/list/ListItem";
import { apiAtom, userAtom } from "@/providers/JellyfinProvider";
import { getJellyfinHeadersForUrl } from "@/utils/customHeaders";
import {
  LOCAL_UPDATE_LIBRARY,
  parseLocalUpdateName,
} from "@/utils/jellyfin/localUpdate";

const installer =
  Platform.OS === "android" &&
  applicationId === "com.fredrikburmester.streamyfin.local"
    ? requireOptionalNativeModule<{
        prepare(uri: string, version: number, sha256: string): Promise<void>;
        install(version: number): Promise<boolean>;
      }>("StreamyfinLocalUpdate")
    : null;
const updateNotice = { id: "streamyfin-local-update" };

export function LocalUpdateSetting() {
  const { t } = useTranslation();
  const api = useAtomValue(apiAtom);
  const user = useAtomValue(userAtom);
  const working = useRef(false);
  const [busy, setBusy] = useState<"checking" | "downloading">();
  const [pending, setPending] = useState<number>();
  if (!installer) return null;

  const checkUpdate = async () => {
    if (working.current || !api || !user?.Id) return;
    working.current = true;
    setBusy("checking");
    try {
      const install = async (version: number) => {
        if (!(await installer.install(version))) {
          toast.info(
            t("home.settings.local_update.allow_install"),
            updateNotice,
          );
        }
      };
      if (pending) {
        await install(pending);
        return;
      }

      const { data: views } = await getUserViewApi(api).getUserViews({
        userId: user.Id,
      });
      const library = views.Items?.find(
        ({ Name, CollectionType }) =>
          Name === LOCAL_UPDATE_LIBRARY && CollectionType === "books",
      );
      if (!library?.Id) {
        toast.info(
          t("home.settings.local_update.library_missing"),
          updateNotice,
        );
        return;
      }
      const { data } = await getLibraryApi(api).getItems({
        userId: user.Id,
        parentId: library.Id,
        recursive: true,
        includeItemTypes: ["Book"],
        fields: ["Path"],
      });
      const updates = (data.Items ?? []).flatMap(({ Id, Name, Path }) => {
        const fileName = Path?.split(/[\\/]/).pop();
        const update =
          parseLocalUpdateName(fileName) ?? parseLocalUpdateName(Name);
        return Id && update ? [{ ...update, id: Id }] : [];
      });
      const current = Number(nativeBuildVersion);
      const latest = updates.sort((a, b) => b.version - a.version)[0];
      if (!latest) {
        toast.info(
          t("home.settings.local_update.bundle_missing"),
          updateNotice,
        );
        return;
      }
      if (latest.version <= current) {
        toast.success(t("home.settings.local_update.up_to_date"), updateNotice);
        return;
      }

      const url = api.getUri(
        `/Items/${encodeURIComponent(latest.id)}/Download`,
      );
      const archive = new File(Paths.cache, "streamyfin-update-download.epub");
      setBusy("downloading");
      try {
        await File.downloadFileAsync(url, archive, {
          idempotent: true,
          headers: {
            ...getJellyfinHeadersForUrl(url, api.basePath),
            Authorization: api.authorizationHeader,
          },
        });
        await installer.prepare(archive.uri, latest.version, latest.sha256);
      } finally {
        if (archive.exists) archive.delete();
      }
      setPending(latest.version);
      await install(latest.version);
    } catch {
      toast.error(t("home.settings.local_update.failed"), updateNotice);
    } finally {
      setBusy(undefined);
      working.current = false;
    }
  };

  return (
    <ListGroup className='mb-4'>
      <ListItem
        title={t(
          pending
            ? "home.settings.local_update.install"
            : "home.settings.local_update.check",
        )}
        subtitle={
          busy
            ? t(
                busy === "downloading"
                  ? "home.settings.local_update.downloading"
                  : "home.settings.local_update.checking",
              )
            : undefined
        }
        icon='refresh'
        disabled={!!busy || !api || !user?.Id}
        onPress={checkUpdate}
      />
    </ListGroup>
  );
}
