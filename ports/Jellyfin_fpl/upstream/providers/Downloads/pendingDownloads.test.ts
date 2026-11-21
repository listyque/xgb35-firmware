import { DOCUMENTS } from "@/test-utils/fileSystem";
import {
  clearAllDownloadedItems,
  getDownloadedItemById,
  saveDownloadedFolderPaths,
} from "./database";

import {
  finalizePendingDownload,
  type PendingDownload,
  pendingDownloadFileUri,
} from "./pendingDownloads";

test("keeps folder paths when a pending download is finalized and backfills older records", () => {
  clearAllDownloadedItems();
  const folderPath = [{ id: "library", name: "Archive" }];
  const mediaSource = { Id: "source" };
  finalizePendingDownload(
    {
      ...record("new.mp4"),
      item: { Id: "new", Type: "Video" },
      mediaSource,
      folderPath,
    },
    100,
  );
  finalizePendingDownload(
    { ...record("old.mp4"), item: { Id: "old", Type: "Video" }, mediaSource },
    50,
  );

  expect(getDownloadedItemById("new")?.folderPath).toEqual(folderPath);
  expect(
    saveDownloadedFolderPaths(
      new Map([
        ["old", folderPath],
        ["missing", folderPath],
      ]),
    ),
  ).toBe(true);
  expect(getDownloadedItemById("old")).toMatchObject({
    folderPath,
    videoFileSize: 50,
  });
  expect(getDownloadedItemById("missing")).toBeUndefined();
});

const record = (videoFileName: string) =>
  ({ itemId: "item-1", videoFileName }) as PendingDownload;

describe("the video file of a pending download", () => {
  it("is the stored name, directly in Documents", () => {
    expect(pendingDownloadFileUri(record("show_s01e01.mp4"))).toBe(
      `${DOCUMENTS}/show_s01e01.mp4`,
    );
  });

  // The name is read back from storage, and a record saved before the name builders cleaned
  // what the server sends can still carry a path. Re-enqueueing such a record would hand
  // native a destination outside Documents, and finalizing it would store that path as the
  // download's video, which a later delete then removes.
  it.each([
    ["a parent-directory prefix", "../../Library/x.mp4"],
    ["a nested path", "sub/dir.mp4"],
    ["a backslash", "sub\\dir.mp4"],
    ["only a parent directory", ".."],
    ["nothing in it", ""],
  ])("is refused when the stored name has %s", (_label, videoFileName) => {
    expect(() => pendingDownloadFileUri(record(videoFileName))).toThrow();
  });
});
