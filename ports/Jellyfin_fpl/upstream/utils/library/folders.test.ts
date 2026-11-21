import { makeApi } from "@/test-utils/jellyfinApi";
import { loadFolderDownloadItems } from "./folders";

test("loads every page of a folder's downloadable descendants without duplicates", async () => {
  const api = makeApi();
  const firstPage = Array.from({ length: 200 }, (_, i) => ({
    Id: `video-${i}`,
    Type: "Video",
  }));
  api.mock.onGet(/\/Items\?/).reply(({ url }) => {
    const params = new URL(url!).searchParams;
    return [
      200,
      {
        Items:
          params.get("startIndex") === "0"
            ? firstPage
            : [
                firstPage[0],
                { Id: "nested-episode", Type: "Episode" },
                { Id: "missing", Type: "Episode", IsPlaceHolder: true },
                { Id: "nested-folder", Type: "Folder" },
              ],
      },
    ];
  });

  const items = await loadFolderDownloadItems(api, "user-1", "media-folder");

  expect(items).toHaveLength(201);
  expect(items.at(-1)?.Id).toBe("nested-episode");
  const lastRequest = new URL(api.mock.history.get[1].url!).searchParams;
  expect(Object.fromEntries(lastRequest)).toMatchObject({
    parentId: "media-folder",
    userId: "user-1",
    recursive: "true",
    startIndex: "200",
    enableUserData: "true",
    isPlaceHolder: "false",
  });
});
