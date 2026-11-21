import { getLibraryApi } from "@jellyfin/sdk/lib/utils/api";
import { useInfiniteQuery, useQuery } from "@tanstack/react-query";
import { type Href, Stack } from "expo-router";
import { useAtomValue } from "jotai";
import { useMemo, useState } from "react";
import { useTranslation } from "react-i18next";
import { FlatList, Keyboard, Pressable, View } from "react-native";
import { useSafeAreaInsets } from "react-native-safe-area-context";
import {
  SingleItemDownloadedIcon,
  SingleItemDownloadIcon,
} from "@/components/common/DownloadIcons";
import { HeaderButton } from "@/components/common/HeaderButton";
import { Text } from "@/components/common/Text";
import { getItemNavigation } from "@/components/common/TouchableItemRouter";
import { DownloadItems } from "@/components/DownloadItem";
import { Loader } from "@/components/Loader";
import useRouter from "@/hooks/useAppRouter";
import { apiAtom, userAtom } from "@/providers/JellyfinProvider";
import {
  FOLDER_TYPES,
  findFolders,
  folderHref,
  loadFolderDownloadItems,
  loadFolderIndex,
} from "@/utils/library/folders";
import { FolderItemRow } from "./FolderItemRow";
import { FolderSearchInput } from "./FolderSearchInput";

const PAGE_SIZE = 50;

export function FolderBrowser({
  folderId,
  title,
  catalog,
  fromHome = false,
  homeSearch,
}: {
  folderId?: string;
  title?: string;
  catalog?: "music" | "library";
  fromHome?: boolean;
  homeSearch?: string;
}) {
  const api = useAtomValue(apiAtom);
  const user = useAtomValue(userAtom);
  const userId = user?.Id;
  const { t } = useTranslation();
  const router = useRouter();
  const insets = useSafeAreaInsets();
  const [localSearch, setLocalSearch] = useState("");
  const search = (homeSearch ?? localSearch).trim();
  const searching = !!search;

  const query = useInfiniteQuery({
    queryKey: ["library-items", "folders", api?.basePath, userId, folderId],
    enabled: !!api && !!userId && !!folderId,
    initialPageParam: 0,
    queryFn: async ({ pageParam, signal }) => {
      const { data } = await getLibraryApi(api!).getItems(
        {
          parentId: folderId,
          userId,
          recursive: false,
          collapseBoxSetItems: false,
          fields: ["MediaSources", "MediaStreams"],
          enableUserData: true,
          sortBy: ["IsFolder", "SortName"],
          startIndex: pageParam,
          limit: PAGE_SIZE,
          enableTotalRecordCount: false,
        },
        { signal },
      );
      return data.Items ?? [];
    },
    getNextPageParam: (items, _, offset) =>
      items.length === PAGE_SIZE ? offset + items.length : undefined,
  });

  const index = useQuery({
    queryKey: [
      "library-items",
      "folder-index",
      api?.basePath,
      userId,
      folderId,
    ],
    enabled: searching && !!api && !!userId,
    queryFn: ({ signal }) => loadFolderIndex(api!, userId!, folderId, signal),
    staleTime: 5 * 60_000,
  });

  const items = useMemo(
    () =>
      searching
        ? findFolders(index.data ?? [], search)
        : (query.data?.pages.flat().filter((item) => !!item.Id) ?? []),
    [index.data, query.data, search, searching],
  );
  const activeQuery = searching ? index : query;

  return (
    <View className='flex-1'>
      {homeSearch === undefined && (
        <>
          <Stack.Screen
            options={{
              title: title || t("library.folders"),
              headerRight:
                catalog && folderId
                  ? () => (
                      <HeaderButton
                        variant='text'
                        accessibilityRole='button'
                        onPress={() =>
                          router.push({
                            pathname:
                              catalog === "music"
                                ? "/(auth)/(tabs)/(libraries)/music/[libraryId]/suggestions"
                                : "/(auth)/(tabs)/(libraries)/[libraryId]",
                            params: { libraryId: folderId },
                          })
                        }
                      >
                        <Text>{t("library.catalog")}</Text>
                      </HeaderButton>
                    )
                  : undefined,
            }}
          />
          <FolderSearchInput value={localSearch} onChange={setLocalSearch} />
          {!searching &&
            api &&
            userId &&
            folderId &&
            user?.Policy?.EnableContentDownloading && (
              <DownloadItems
                style={{
                  marginLeft: insets.left + 16,
                  marginRight: insets.right + 16,
                  marginBottom: 8,
                }}
                title={t("library.download_folder")}
                buttonLabel={t("library.download_folder")}
                loadItems={() => loadFolderDownloadItems(api, userId, folderId)}
                MissingDownloadIconComponent={SingleItemDownloadIcon}
                DownloadedIconComponent={SingleItemDownloadedIcon}
              />
            )}
        </>
      )}
      <FlatList
        key={search}
        testID='folder-browser-list'
        data={items}
        keyExtractor={(item) => item.Id!}
        keyboardShouldPersistTaps='handled'
        contentContainerStyle={{
          paddingLeft: insets.left + 16,
          paddingRight: insets.right + 16,
          paddingBottom: 24,
        }}
        renderItem={({ item }) => {
          const folder = !!item.Type && FOLDER_TYPES.includes(item.Type);

          return (
            <FolderItemRow
              item={item}
              folder={folder}
              onOpen={() => {
                Keyboard.dismiss();
                router.push(
                  folder
                    ? folderHref(item, fromHome)
                    : (getItemNavigation(
                        item,
                        fromHome ? "(home)" : "(libraries)",
                      ) as Href),
                );
              }}
            />
          );
        }}
        onEndReached={() => {
          if (
            !searching &&
            query.hasNextPage &&
            !query.isFetching &&
            !query.isFetchNextPageError
          ) {
            void query.fetchNextPage();
          }
        }}
        ListEmptyComponent={
          <View className='py-6'>
            {activeQuery.isPending ? (
              <Loader />
            ) : (
              <Text className='text-neutral-400'>
                {activeQuery.isError
                  ? t("common.something_went_wrong")
                  : t(
                      searching
                        ? "library.no_matching_folders"
                        : "home.no_items",
                    )}
              </Text>
            )}
          </View>
        }
        ListFooterComponent={
          !searching && query.isFetchingNextPage ? (
            <Loader />
          ) : activeQuery.isError ? (
            <Pressable
              accessibilityRole='button'
              className='self-start rounded-lg bg-neutral-900 px-4 py-3'
              onPress={() =>
                void (!searching && query.isFetchNextPageError
                  ? query.fetchNextPage()
                  : activeQuery.refetch())
              }
            >
              <Text>{t("home.retry")}</Text>
            </Pressable>
          ) : undefined
        }
      />
    </View>
  );
}
