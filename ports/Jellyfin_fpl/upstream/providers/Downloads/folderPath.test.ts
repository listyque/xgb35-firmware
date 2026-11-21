import { makeApi } from "@/test-utils/jellyfinApi";
import {
  getDownloadFolderPath,
  loadMissingDownloadFolderPaths,
} from "./folderPath";
import type { DownloadedItem } from "./types";

const ancestors = [
  { Id: "season", Name: "Season 2", Type: "Season" },
  { Id: "show", Name: "Concerts", Type: "Series" },
  { Id: "library", Name: "Archive", Type: "CollectionFolder" },
  { Id: "root", Name: "Root", Type: "UserRootFolder" },
];

test("stores the server's folders from the library to the immediate parent", async () => {
  const api = makeApi();
  api.mock.onGet(/\/Items\/episode\/Ancestors/).reply(200, ancestors);
  await expect(getDownloadFolderPath(api, "episode")).resolves.toEqual([
    { id: "library", name: "Archive" },
    { id: "show", name: "Concerts" },
    { id: "season", name: "Season 2" },
  ]);
});

test("looks up a shared parent once and preserves downloads with no result from the server", async () => {
  const api = makeApi();
  api.mock.onGet(/\/Items\/episode\/Ancestors/).reply(200, ancestors);
  api.mock.onGet(/\/Items\/missing\/Ancestors/).reply(404);
  const saved = [
    { item: { Id: "episode", ParentId: "season" } },
    { item: { Id: "another", ParentId: "season" } },
    { item: { Id: "known" }, folderPath: [] },
    { item: { Id: "missing" } },
  ] as DownloadedItem[];

  const paths = await loadMissingDownloadFolderPaths(
    api,
    saved,
    new AbortController().signal,
  );
  expect([...paths.keys()]).toEqual(["episode", "another"]);
  expect(paths.get("another")).toEqual(paths.get("episode"));
  expect(api.mock.history.get).toHaveLength(2);
});
