import type { DownloadedItem } from "@/providers/Downloads/types";
import { buildDownloadFolderTree, getDownloadedSubfolders } from "./folderTree";

test("rebuilds a browsable tree from local downloads and keeps same-named folders apart", () => {
  const saved = [
    {
      item: { Id: "a", Name: "Film 10" },
      folderPath: [
        { id: "one", name: "Movies" },
        { id: "archive-a", name: "Archive" },
      ],
    },
    {
      item: { Id: "b", Name: "Film 2" },
      folderPath: [
        { id: "one", name: "Movies" },
        { id: "archive-a", name: "Archive" },
      ],
    },
    {
      item: { Id: "c", Name: "Film 3" },
      folderPath: [
        { id: "two", name: "Home" },
        { id: "archive-b", name: "Archive" },
      ],
    },
    { item: { Id: "old", Name: "Previously downloaded" } },
  ] as DownloadedItem[];

  const { root, byId } = buildDownloadFolderTree(saved, "Unknown folder");
  expect(root.folders.map((folder) => folder.name)).toEqual([
    "Home",
    "Movies",
    "Unknown folder",
  ]);
  const archives = getDownloadedSubfolders(root).filter(
    (item) => item.Name === "Archive",
  );
  expect(
    archives.map(({ Id }) => byId.get(Id!)?.items.map((item) => item.Id)),
  ).toEqual([["c"], ["b", "a"]]);
  expect(root.folders[2].items[0].Id).toBe("old");
});
