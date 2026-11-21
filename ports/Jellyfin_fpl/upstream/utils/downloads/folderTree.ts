import type { BaseItemDto } from "@jellyfin/sdk/lib/generated-client/models";
import type {
  DownloadedItem,
  DownloadFolder,
} from "@/providers/Downloads/types";

export type DownloadFolderNode = {
  id: string;
  name: string;
  folders: DownloadFolderNode[];
  items: BaseItemDto[];
};

function unknownPath(
  item: BaseItemDto,
  unknownLabel: string,
): DownloadFolder[] {
  const path = [{ id: "unknown", name: unknownLabel }];
  if (item.SeriesId && item.SeriesName) {
    path.push({ id: item.SeriesId, name: item.SeriesName });
    if (item.SeasonName) {
      path.push({
        id:
          item.SeasonId ??
          `season-${item.ParentIndexNumber ?? item.SeasonName}`,
        name: item.SeasonName,
      });
    }
  } else if (item.AlbumId && item.Album) {
    path.push({ id: item.AlbumId, name: item.Album });
  }
  return path;
}

export function buildDownloadFolderTree(
  downloads: DownloadedItem[],
  unknownLabel: string,
) {
  const root: DownloadFolderNode = { id: "", name: "", folders: [], items: [] };
  const byId = new Map([[root.id, root]]);
  for (const { item, folderPath } of downloads) {
    if (!item.Id) continue;
    let parent = root;
    for (const folder of folderPath ?? unknownPath(item, unknownLabel)) {
      const id = `${parent.id}:${encodeURIComponent(folder.id)}`;
      let node = byId.get(id);
      if (!node) {
        node = { id, name: folder.name, folders: [], items: [] };
        byId.set(id, node);
        parent.folders.push(node);
      }
      parent = node;
    }
    parent.items.push(item);
  }

  const compare = (a: string, b: string) =>
    a.localeCompare(b, undefined, { numeric: true, sensitivity: "base" });
  for (const folder of byId.values()) {
    folder.folders.sort((a, b) => compare(a.name, b.name));
    folder.items.sort((a, b) => compare(a.Name ?? "", b.Name ?? ""));
  }
  return { root, byId };
}

export function getDownloadedSubfolders(
  folder: DownloadFolderNode,
): BaseItemDto[] {
  return folder.folders.flatMap((subfolder) => [
    { Id: subfolder.id, Name: subfolder.name, Type: "Folder" },
    ...getDownloadedSubfolders(subfolder),
  ]);
}
