import type { Api } from "@jellyfin/sdk";
import type { LibraryApiGetItemsRequest } from "@jellyfin/sdk/lib/generated-client/api/library-api";
import type {
  BaseItemDto,
  BaseItemKind,
} from "@jellyfin/sdk/lib/generated-client/models";
import { getLibraryApi } from "@jellyfin/sdk/lib/utils/api";

export const FOLDER_TYPES: readonly BaseItemKind[] = [
  "Folder",
  "CollectionFolder",
  "PhotoAlbum",
  "MusicAlbum",
  "Series",
  "Season",
];

export const opensAsFolders = (item: BaseItemDto) =>
  !!item.Id &&
  item.CollectionType !== "livetv" &&
  item.CollectionType !== "boxsets" &&
  item.CollectionType !== "playlists";

export const folderHref = (
  item: BaseItemDto,
  fromHome = false,
  catalog?: "music" | "library",
) => ({
  pathname: fromHome
    ? ("/(auth)/(tabs)/(home)/folders/[folderId]" as const)
    : ("/(auth)/(tabs)/(libraries)/folders/[folderId]" as const),
  params: {
    folderId: item.Id!,
    title: item.Name || "",
    ...(catalog && { catalog }),
  },
});

const FOLDER_INDEX_PAGE_SIZE = 200;
const DOWNLOADABLE_TYPES: BaseItemKind[] = [
  "Movie",
  "Episode",
  "Video",
  "MusicVideo",
  "Audio",
  "Trailer",
];

async function loadFolderPages(
  api: Api,
  userId: string,
  parentId: string | undefined,
  request: Pick<
    LibraryApiGetItemsRequest,
    "includeItemTypes" | "fields" | "enableUserData" | "isPlaceHolder"
  >,
  signal?: AbortSignal,
) {
  const library = getLibraryApi(api);
  const itemsById = new Map<string, BaseItemDto>();
  for (let startIndex = 0; ; startIndex += FOLDER_INDEX_PAGE_SIZE) {
    const { data } = await library.getItems(
      {
        userId,
        parentId,
        recursive: true,
        collapseBoxSetItems: false,
        ...request,
        sortBy: ["SortName"],
        startIndex,
        limit: FOLDER_INDEX_PAGE_SIZE,
        enableTotalRecordCount: false,
      },
      { signal },
    );
    const items = data.Items ?? [];
    const previousCount = itemsById.size;
    for (const item of items) {
      if (
        item.Id &&
        item.Type &&
        request.includeItemTypes?.includes(item.Type) &&
        !item.IsPlaceHolder
      )
        itemsById.set(item.Id, item);
    }
    if (
      items.length < FOLDER_INDEX_PAGE_SIZE ||
      itemsById.size === previousCount
    )
      break;
  }
  return [...itemsById.values()];
}

export const loadFolderIndex = (
  api: Api,
  userId: string,
  parentId: string | undefined,
  signal: AbortSignal,
) =>
  loadFolderPages(
    api,
    userId,
    parentId,
    { includeItemTypes: [...FOLDER_TYPES], enableUserData: false },
    signal,
  );

export const loadFolderDownloadItems = (
  api: Api,
  userId: string,
  parentId: string,
) =>
  loadFolderPages(api, userId, parentId, {
    includeItemTypes: DOWNLOADABLE_TYPES,
    fields: ["MediaSources", "MediaStreams"],
    enableUserData: true,
    isPlaceHolder: false,
  });

export async function getLastPlayedFolder(
  api: Api,
  userId: string,
  signal: AbortSignal,
) {
  const library = getLibraryApi(api);
  const { data } = await library.getItems(
    {
      userId,
      includeItemTypes: DOWNLOADABLE_TYPES,
      recursive: true,
      sortBy: ["DatePlayed"],
      sortOrder: ["Descending"],
      fields: ["ParentId"],
      enableUserData: true,
      enableTotalRecordCount: false,
      limit: 1,
    },
    { signal },
  );
  const item = data.Items?.[0];
  const parentId = item?.ParentId ?? item?.SeasonId ?? item?.AlbumId;
  if (!item?.UserData?.LastPlayedDate || !parentId) return null;
  const parent = await library.getItem(
    { userId, itemId: parentId },
    { signal },
  );
  return parent.data.Id ? parent.data : null;
}

const normalizeName = (name: string) =>
  name
    .normalize("NFKC")
    .toLowerCase()
    .replaceAll("ё", "е")
    .replace(/[^\p{L}\p{N}]+/gu, " ")
    .trim();

export function findFolders(folders: BaseItemDto[], search: string) {
  const query = normalizeName(search);
  if (!query) return [];
  const words = query.split(" ");
  return folders
    .flatMap((item) => {
      const name = normalizeName(item.Name ?? "");
      if (!words.every((word) => name.includes(word))) return [];
      return [
        { item, rank: name === query ? 0 : name.startsWith(query) ? 1 : 2 },
      ];
    })
    .sort((a, b) => a.rank - b.rank)
    .map(({ item }) => item);
}
