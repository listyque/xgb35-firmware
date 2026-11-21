import type { BaseItemDto } from "@jellyfin/sdk/lib/generated-client/models";
import { Stack } from "expo-router";
import { useAtomValue } from "jotai";
import { useEffect, useMemo, useState } from "react";
import { useTranslation } from "react-i18next";
import { FlatList, Keyboard, Pressable, View } from "react-native";
import { useSafeAreaInsets } from "react-native-safe-area-context";
import { Text } from "@/components/common/Text";
import { FolderItemRow } from "@/components/library/FolderItemRow";
import { FolderSearchInput } from "@/components/library/FolderSearchInput";
import useRouter from "@/hooks/useAppRouter";
import { useNetworkStatus } from "@/hooks/useNetworkStatus";
import { useDownload } from "@/providers/DownloadProvider";
import {
  getAllDownloadedItems,
  saveDownloadedFolderPaths,
} from "@/providers/Downloads/database";
import { loadMissingDownloadFolderPaths } from "@/providers/Downloads/folderPath";
import { apiAtom } from "@/providers/JellyfinProvider";
import {
  buildDownloadFolderTree,
  getDownloadedSubfolders,
} from "@/utils/downloads/folderTree";
import { findFolders } from "@/utils/library/folders";
import { DownloadCard } from "./DownloadCard";

export function OfflineDownloadsBrowser({
  folderId,
  onDelete,
}: {
  folderId?: string;
  onDelete: (item: BaseItemDto) => void;
}) {
  const { t } = useTranslation();
  const router = useRouter();
  const insets = useSafeAreaInsets();
  const { downloadedItems, processes, triggerRefresh } = useDownload();
  const { serverConnected } = useNetworkStatus();
  const api = useAtomValue(apiAtom);
  const [search, setSearch] = useState("");
  const [section, setSection] = useState<"queue" | "files">();
  const showQueue =
    !folderId &&
    (section ?? (processes.length ? "queue" : "files")) === "queue";
  const searching = !!search.trim();
  const needsPaths = downloadedItems.some(
    (download) => download.folderPath === undefined,
  );

  useEffect(() => {
    if (folderId || !api || serverConnected !== true || !needsPaths) return;
    const controller = new AbortController();
    void loadMissingDownloadFolderPaths(
      api,
      getAllDownloadedItems(),
      controller.signal,
    ).then((paths) => {
      if (
        !controller.signal.aborted &&
        paths.size &&
        saveDownloadedFolderPaths(paths)
      )
        triggerRefresh();
    });
    return () => controller.abort();
  }, [api, folderId, needsPaths, serverConnected, triggerRefresh]);

  const unknownLabel = t("home.downloads.unknown_folder");
  const { root, byId } = useMemo(
    () => buildDownloadFolderTree(downloadedItems, unknownLabel),
    [downloadedItems, unknownLabel],
  );
  const current = folderId ? byId.get(folderId) : root;
  const items: BaseItemDto[] = useMemo(() => {
    if (!current) return [];
    if (searching) return findFolders(getDownloadedSubfolders(current), search);
    return [
      ...current.folders.map((folder) => ({
        Id: folder.id,
        Name: folder.name,
        Type: "Folder" as const,
      })),
      ...current.items,
    ];
  }, [current, search, searching]);

  return (
    <View className='flex-1'>
      <Stack.Screen
        options={{
          title: current?.name || t("home.downloads.downloads_title"),
        }}
      />
      {!folderId && (
        <View className='flex-row gap-2 px-4 py-2'>
          {(["queue", "files"] as const).map((tab) => {
            const selected = showQueue === (tab === "queue");
            return (
              <Pressable
                key={tab}
                accessibilityRole='tab'
                accessibilityState={{ selected }}
                onPress={() => setSection(tab)}
                className={`rounded-lg px-4 py-2.5 ${selected ? "bg-purple-900/60" : "bg-neutral-900"}`}
              >
                <Text>
                  {tab === "queue"
                    ? t("home.downloads.queue_count", {
                        count: processes.length,
                      })
                    : t("home.downloads.downloaded_section")}
                </Text>
              </Pressable>
            );
          })}
        </View>
      )}
      {showQueue ? (
        <FlatList
          key='queue'
          data={processes}
          keyExtractor={(item) => item.id}
          contentContainerStyle={{
            gap: 8,
            paddingLeft: insets.left + 16,
            paddingRight: insets.right + 16,
            paddingBottom: 24,
          }}
          renderItem={({ item }) => <DownloadCard process={item} />}
          ListEmptyComponent={
            <Text className='py-4 text-neutral-400'>
              {t("home.downloads.no_active_downloads")}
            </Text>
          }
        />
      ) : (
        <>
          <FolderSearchInput value={search} onChange={setSearch} />
          <FlatList
            key={search}
            data={items}
            keyExtractor={(item) => item.Id!}
            keyboardShouldPersistTaps='handled'
            contentContainerStyle={{
              paddingLeft: insets.left + 16,
              paddingRight: insets.right + 16,
              paddingBottom: 24,
            }}
            ListEmptyComponent={
              <Text className='my-4 text-neutral-400'>
                {t(
                  searching
                    ? "library.no_matching_folders"
                    : "home.downloads.no_downloaded_items",
                )}
              </Text>
            }
            renderItem={({ item }) => {
              const folder = byId.has(item.Id!);
              return (
                <FolderItemRow
                  item={item}
                  folder={folder}
                  offline
                  onLongPress={folder ? undefined : () => onDelete(item)}
                  onOpen={() => {
                    Keyboard.dismiss();
                    if (folder) {
                      router.push({
                        pathname:
                          "/(auth)/(tabs)/(offline-downloads)/downloaded/[folderId]",
                        params: { folderId: item.Id! },
                      });
                    } else {
                      router.push({
                        pathname:
                          "/(auth)/(tabs)/(offline-downloads)/items/page",
                        params: { id: item.Id!, offline: "true" },
                      });
                    }
                  }}
                />
              );
            }}
          />
        </>
      )}
    </View>
  );
}
