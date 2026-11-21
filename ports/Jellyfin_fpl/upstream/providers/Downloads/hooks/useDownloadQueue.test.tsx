import { act, renderHook } from "@testing-library/react-native";
import { makeApi } from "@/test-utils/jellyfinApi";
import { clearMmkv } from "@/test-utils/mmkv";
import { getDownloadStreamUrl } from "@/utils/jellyfin/media/getStreamUrl";
import type { DownloadRequest } from "../downloadRequest";
import type { JobStatus } from "../types";
import { useDownloadQueue } from "./useDownloadQueue";

jest.mock("../database", () => ({ getDownloadedItemById: jest.fn() }));
jest.mock("../pendingDownloads", () => ({ getPendingDownload: jest.fn() }));
jest.mock("@/utils/jellyfin/media/getStreamUrl", () => ({
  getDownloadStreamUrl: jest.fn(),
}));

const request = (id: string): DownloadRequest => ({
  item: {
    Id: id,
    Name: id,
    Type: "Movie",
    Chapters: [],
    MediaSources: [{ Id: id }],
  },
  quality: { bitrate: { key: "Max", value: undefined } },
});

test("lists the full batch, persists paused entries, and starts only one at a time", async () => {
  clearMmkv();
  const api = makeApi();
  const start = jest.fn();
  const cancel = jest.fn();
  const active = {
    id: "active",
    item: { Id: "active" },
    status: "downloading",
  } as JobStatus;
  const renderQueue = (processes: JobStatus[]) =>
    renderHook(() =>
      useDownloadQueue({
        api,
        userId: "user",
        settings: null,
        processes,
        startBackgroundDownload: start,
        cancelDownload: cancel,
      }),
    );
  const first = await renderQueue([active]);
  await act(async () => {
    expect(
      first.result.current.enqueueDownloads(
        ["one", "two", "three"].map(request),
      ),
    ).toBe(3);
  });
  expect(first.result.current.visibleProcesses.map(({ id }) => id)).toEqual([
    "active",
    "one",
    "two",
    "three",
  ]);
  expect(getDownloadStreamUrl).not.toHaveBeenCalled();
  await act(async () => {
    await first.result.current.pauseDownload("one");
    await first.result.current.cancelDownload("three");
  });
  await first.unmount();

  jest.mocked(getDownloadStreamUrl).mockReturnValueOnce(new Promise(() => {}));
  const second = await renderQueue([]);
  expect(
    second.result.current.visibleProcesses.map(({ id, status }) => [
      id,
      status,
    ]),
  ).toEqual([
    ["one", "paused"],
    ["two", "queued"],
  ]);
  expect(getDownloadStreamUrl).toHaveBeenCalledTimes(1);
  expect(getDownloadStreamUrl).toHaveBeenCalledWith(
    expect.objectContaining({ item: expect.objectContaining({ Id: "two" }) }),
  );
  await act(async () => second.result.current.resumeDownload("one"));
  expect(second.result.current.visibleProcesses[0].status).toBe("queued");
  expect(getDownloadStreamUrl).toHaveBeenCalledTimes(1);
});
