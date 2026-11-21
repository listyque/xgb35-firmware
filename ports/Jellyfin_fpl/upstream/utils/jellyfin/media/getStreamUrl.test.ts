import { stubReactNative } from "@/test-utils/reactNative";

stubReactNative();
jest.mock(
  "expo",
  () => jest.requireActual("@/test-utils/stubModules").expoWithoutNativeModule,
);

import { bodyContaining, makeApi } from "@/test-utils/jellyfinApi";
import { isExpectedError } from "@/utils/errors";
import { getDownloadStreamUrl, getStreamUrl } from "./getStreamUrl";

test("downloads a progressive stream with the requested codec and height cap", async () => {
  const api = makeApi();
  api.mock
    .onPost(
      "https://jellyfin.example.com/Items/item-1/PlaybackInfo",
      bodyContaining({
        EnableDirectPlay: false,
        EnableDirectStream: false,
        MaxStreamingBitrate: 2_000_000,
      }),
    )
    .reply(200, {
      PlaySessionId: "session-1",
      MediaSources: [
        {
          Id: "media-1",
          MediaStreams: [{ Type: "Video", Height: 2160 }],
          TranscodingUrl:
            "/Videos/item-1/stream.mp4?VideoCodec=hevc&ApiKey=token",
        },
      ],
    });

  const result = await getDownloadStreamUrl({
    api,
    item: { Id: "item-1", Type: "Movie" },
    userId: "user-1",
    maxStreamingBitrate: 2_000_000,
    quality: {
      height: 720,
      codec: "hevc",
      bitrate: { key: "2 Mb/s", value: 2_000_000 },
    },
  });

  const url = new URL(result!.url!);
  const request = JSON.parse(api.mock.history.post[0].data);
  expect(request.DeviceProfile.TranscodingProfiles).toEqual(
    expect.arrayContaining([
      expect.objectContaining({ Container: "mp4", VideoCodec: "hevc" }),
    ]),
  );
  expect(url.searchParams.get("MaxHeight")).toBe("720");
  expect(url.searchParams.get("AllowVideoStreamCopy")).toBe("false");
  expect(url.searchParams.get("VideoCodec")).toBe("hevc");
  expect(url.searchParams.get("ApiKey")).toBe("token");
});

test("can download the original container without requesting transcoding", async () => {
  const api = makeApi();
  const result = await getDownloadStreamUrl({
    api,
    item: {
      Id: "item-1",
      Type: "Movie",
      MediaSources: [{ Id: "media-1", Container: "mkv" }],
    },
    userId: "user-1",
    quality: { original: true, bitrate: { key: "Max", value: undefined } },
  });

  expect(new URL(result!.url!).pathname).toBe("/Items/media-1/Download");
  expect(result?.mediaSource?.Container).toBe("mkv");
  expect(api.mock.history.post).toHaveLength(0);
});

test("allows direct downloading when a local file already matches the requested quality", async () => {
  const api = makeApi();
  const source = {
    Id: "media-1",
    Container: "mp4",
    Bitrate: 2_000_000,
    MediaStreams: [{ Type: "Video", Height: 720, Codec: "h264" }],
  } as const;
  const mediaSource = { ...source, MediaStreams: [...source.MediaStreams] };
  api.mock
    .onPost(
      "https://jellyfin.example.com/Items/item-1/PlaybackInfo",
      bodyContaining({ EnableDirectPlay: true }),
    )
    .reply(200, { MediaSources: [mediaSource] });
  const result = await getDownloadStreamUrl({
    api,
    item: { Id: "item-1", Type: "Movie", MediaSources: [mediaSource] },
    userId: "user-1",
    mediaSourceId: "media-1",
    maxStreamingBitrate: 2_000_000,
    quality: {
      container: "mp4",
      height: 1080,
      codec: "h264",
      bitrate: { key: "2 Mb/s", value: 2_000_000 },
    },
  });

  expect(new URL(result!.url!).pathname).toBe("/Items/media-1/Download");
});

test("allows copying a video track under the height ceiling into a different container", async () => {
  const api = makeApi({
    MediaSources: [
      {
        Id: "media-1",
        Container: "mkv",
        MediaStreams: [{ Type: "Video", Height: 720, Codec: "h264" }],
        TranscodingUrl: "/Videos/item-1/stream.mp4?VideoCodec=h264",
      },
    ],
  });
  const result = await getDownloadStreamUrl({
    api,
    item: { Id: "item-1", Type: "Movie" },
    userId: "user-1",
    quality: {
      container: "mp4",
      height: 1080,
      bitrate: { key: "Max", value: undefined },
    },
  });

  const url = new URL(result!.url!);
  expect(url.searchParams.get("MaxHeight")).toBe("1080");
  expect(url.searchParams.has("AllowVideoStreamCopy")).toBe(false);
});

describe("getStreamUrl", () => {
  test("direct play URL carries the source container and the ApiKey", async () => {
    const api = makeApi();
    api.mock
      .onPost("https://jellyfin.example.com/Items/item-1/PlaybackInfo")
      .reply(200, {
        PlaySessionId: "session-1",
        MediaSources: [{ Id: "media-1", Container: "mkv" }],
      });

    const result = await getStreamUrl({
      api,
      item: { Id: "item-1", Type: "Movie" },
      userId: "user-1",
      startTimeTicks: 0,
      deviceProfile: {},
    });

    const url = new URL(result!.url!);
    expect(url.searchParams.get("container")).toBe("mkv");
    expect(url.searchParams.get("ApiKey")).toBe("SECRET_TOKEN");
  });

  // A release build's console goes to logcat, where a user copying their logs
  // into an issue would hand out the session's token with the stream URL.
  test("logs the stream URL without its ApiKey", async () => {
    const log = jest.spyOn(console, "log").mockImplementation(() => {});
    const api = makeApi();
    api.mock
      .onPost("https://jellyfin.example.com/Items/item-1/PlaybackInfo")
      .reply(200, {
        PlaySessionId: "session-1",
        MediaSources: [{ Id: "media-1", Container: "mkv" }],
      });

    await getStreamUrl({
      api,
      item: { Id: "item-1", Type: "Movie" },
      userId: "user-1",
      startTimeTicks: 0,
      deviceProfile: {},
    });

    const logged = log.mock.calls.flat().join(" ");
    log.mockRestore();
    expect(logged).toContain("/Videos/item-1/stream");
    expect(logged).not.toContain("SECRET_TOKEN");
  });

  // An external stream's Path can carry the provider's credentials in its
  // userinfo or its path, where redactCredentials does not look.
  test("does not log a remote stream's URL", async () => {
    const log = jest.spyOn(console, "log").mockImplementation(() => {});
    const api = makeApi();
    const path =
      "http://iptv-user:iptv-pass@iptv.example.com/live/iptv-user/iptv-pass/42.ts";
    api.mock
      .onPost("https://jellyfin.example.com/Items/item-1/PlaybackInfo")
      .reply(200, {
        PlaySessionId: "session-1",
        MediaSources: [
          { Id: "media-1", IsRemote: true, Protocol: "Http", Path: path },
        ],
      });

    const result = await getStreamUrl({
      api,
      item: { Id: "item-1", Type: "Movie" },
      userId: "user-1",
      startTimeTicks: 0,
      deviceProfile: {},
    });

    const logged = log.mock.calls.flat().join(" ");
    log.mockRestore();
    expect(result?.url).toBe(path);
    expect(logged).not.toContain("iptv-pass");
  });

  // #1693: Live TV loaded forever. Without the opened live stream's id,
  // Jellyfin answers the direct play request with a 400.
  test("live TV direct play URL carries the opened live stream", async () => {
    const api = makeApi();
    api.mock.onPost(/\/Items\/channel-1\/PlaybackInfo/).reply(200, {
      PlaySessionId: "session-1",
      MediaSources: [
        { Id: "native-1", Container: "ts", LiveStreamId: "live-1" },
      ],
    });

    const result = await getStreamUrl({
      api,
      item: { Id: "program-1", Type: "Program", ChannelId: "channel-1" },
      userId: "user-1",
      startTimeTicks: 0,
      deviceProfile: {},
    });

    const url = new URL(result!.url!);
    expect(url.pathname).toBe("/Videos/channel-1/stream");
    expect(url.searchParams.get("liveStreamId")).toBe("live-1");
    expect(url.searchParams.get("playSessionId")).toBe("session-1");
  });

  // #1693 again, from the TV channel grid: it opens a channel itself, not one
  // of its programs, so its stream is opened by the general PlaybackInfo
  // request.
  test("a channel opened directly carries its opened live stream too", async () => {
    const api = makeApi();
    api.mock.onPost(/\/Items\/channel-1\/PlaybackInfo/).reply(200, {
      PlaySessionId: "session-1",
      MediaSources: [
        { Id: "native-1", Container: "ts", LiveStreamId: "live-1" },
      ],
    });

    const result = await getStreamUrl({
      api,
      item: { Id: "channel-1", Type: "TvChannel" },
      userId: "user-1",
      startTimeTicks: 0,
      deviceProfile: {},
    });

    const url = new URL(result!.url!);
    expect(url.pathname).toBe("/Videos/channel-1/stream");
    expect(url.searchParams.get("liveStreamId")).toBe("live-1");
  });

  // REACT-NATIVE-54 / REACT-NATIVE-5H: Jellyfin answers PlaybackInfo with a
  // 400 for an item that has no stream. Seen in production for a Book opened
  // from a home row (Android) and for a Season opened on Apple TV, each one
  // a Sentry event although nothing in the app had failed.
  test.each(["Book", "Season"] as const)(
    "refuses a %s as an expected outcome, without asking the server",
    async (type) => {
      const api = makeApi();
      api.mock
        .onPost("https://jellyfin.example.com/Items/item-1/PlaybackInfo")
        .reply(400);

      const error = await getStreamUrl({
        api,
        item: { Id: "item-1", Type: type },
        userId: "user-1",
        startTimeTicks: 0,
        deviceProfile: {},
      }).catch((e: unknown) => e);

      expect(error).toBeInstanceOf(Error);
      expect(isExpectedError(error)).toBe(true);
      expect(api.mock.history.post).toHaveLength(0);
    },
  );
});

describe("getDownloadStreamUrl", () => {
  const MAX = { key: "Max", value: undefined };
  const LIMITED = { key: "4mbps", value: 4_000_000 };

  const download = async (
    api: ReturnType<typeof makeApi>,
    quality: { key: string; value: number | undefined },
  ) => {
    const result = await getDownloadStreamUrl({
      api,
      item: { Id: "item-1", Type: "Movie" },
      userId: "user-1",
      mediaSourceId: "media-1",
      maxStreamingBitrate: quality.value,
      audioStreamIndex: 0,
      subtitleStreamIndex: 0,
    });
    return new URL(result!.url!);
  };

  test("returns null when the server offers no media source", async () => {
    const api = makeApi();
    api.mock
      .onPost(
        "https://jellyfin.example.com/Items/item-1/PlaybackInfo",
        bodyContaining({ DeviceProfile: { Name: "1. MPV Download" } }),
      )
      .reply(200, { PlaySessionId: "session-1", MediaSources: [] });

    const result = await getDownloadStreamUrl({
      api,
      item: { Id: "item-1", Type: "Movie" },
      userId: "user-1",
      audioStreamIndex: 0,
      subtitleStreamIndex: -1,
    });

    expect(result).toBeNull();
  });

  describe("the server says the player can play the original (no TranscodingUrl)", () => {
    const playerCanPlayTheOriginal = {
      PlaySessionId: "session-1",
      MediaSources: [{ Id: "media-1", Bitrate: 3_000_000 }],
    };

    test("downloads the original file when the user picks Max quality", async () => {
      const api = makeApi();
      api.mock
        .onPost(
          "https://jellyfin.example.com/Items/item-1/PlaybackInfo",
          bodyContaining({ DeviceProfile: { Name: "1. MPV Download" } }),
        )
        .reply(200, playerCanPlayTheOriginal);

      const url = await download(api, MAX);

      expect(url.pathname).toBe("/Items/media-1/Download");
    });

    test("downloads the original file when the source already fits under the user's limited quality", async () => {
      const api = makeApi();
      api.mock
        .onPost(
          "https://jellyfin.example.com/Items/item-1/PlaybackInfo",
          bodyContaining({
            DeviceProfile: { Name: "1. MPV Download" },
            MaxStreamingBitrate: 4_000_000,
          }),
        )
        .reply(200, playerCanPlayTheOriginal);

      const url = await download(api, LIMITED);

      expect(url.pathname).toBe("/Items/media-1/Download");
    });

    test("refuses an original above the requested cap when the server offers no transcode", async () => {
      const api = makeApi({
        MediaSources: [{ Id: "media-1", Bitrate: 5_000_000 }],
      });

      await expect(
        getDownloadStreamUrl({
          api,
          item: { Id: "item-1", Type: "Movie" },
          userId: "user-1",
          maxStreamingBitrate: LIMITED.value,
        }),
      ).resolves.toBeNull();
    });

    test("imposes no bitrate cap of its own when the user picks Max quality", async () => {
      const api = makeApi();
      api.mock
        .onPost(
          "https://jellyfin.example.com/Items/item-1/PlaybackInfo",
          bodyContaining({ DeviceProfile: { Name: "1. MPV Download" } }),
        )
        .reply(200, playerCanPlayTheOriginal);

      await download(api, MAX);

      const negotiatedCaps = api.mock.history.post.map((request) => {
        const profile = JSON.parse(request.data).DeviceProfile;
        return {
          MaxStreamingBitrate: profile.MaxStreamingBitrate,
          MaxStaticBitrate: profile.MaxStaticBitrate,
        };
      });
      expect(negotiatedCaps).toEqual([
        { MaxStreamingBitrate: 999_999_999, MaxStaticBitrate: 999_999_999 },
      ]);
    });

    test("authenticates the URL with the ApiKey query parameter", async () => {
      const api = makeApi();
      api.mock
        .onPost(
          "https://jellyfin.example.com/Items/item-1/PlaybackInfo",
          bodyContaining({ DeviceProfile: { Name: "1. MPV Download" } }),
        )
        .reply(200, playerCanPlayTheOriginal);

      const url = await download(api, MAX);

      expect(url.searchParams.get("ApiKey")).toBe("SECRET_TOKEN");
    });
  });

  describe("the server says the original needs transcoding (TranscodingUrl present)", () => {
    const downloadGetsAProgressiveMp4 = {
      PlaySessionId: "session-1",
      MediaSources: [
        {
          Id: "media-1",
          TranscodingUrl:
            "/videos/media-1/stream.mp4?DeviceId=device-1&PlaySessionId=session-1",
        },
      ],
    };

    test("downloads the progressive mp4 when the source exceeds the user's limited quality", async () => {
      const api = makeApi();
      api.mock
        .onPost(
          "https://jellyfin.example.com/Items/item-1/PlaybackInfo",
          bodyContaining({
            DeviceProfile: { Name: "1. MPV Download" },
            MaxStreamingBitrate: 4_000_000,
          }),
        )
        .reply(200, downloadGetsAProgressiveMp4);

      const url = await download(api, LIMITED);

      expect(url.href).toBe(
        "https://jellyfin.example.com/videos/media-1/stream.mp4?DeviceId=device-1&PlaySessionId=session-1",
      );
    });

    test("downloads the TranscodingUrl exactly as the server sent it, even when the user picks no subtitle (streaming rewrites SubtitleMethod, downloads must not)", async () => {
      const api = makeApi();
      api.mock
        .onPost(
          "https://jellyfin.example.com/Items/item-1/PlaybackInfo",
          bodyContaining({ DeviceProfile: { Name: "1. MPV Download" } }),
        )
        .reply(200, {
          PlaySessionId: "session-1",
          MediaSources: [
            {
              Id: "media-1",
              TranscodingUrl:
                "/videos/media-1/stream.mp4?DeviceId=device-1&SubtitleMethod=Encode&PlaySessionId=session-1",
            },
          ],
        });

      const result = await getDownloadStreamUrl({
        api,
        item: { Id: "item-1", Type: "Movie" },
        userId: "user-1",
        mediaSourceId: "media-1",
        audioStreamIndex: 0,
        subtitleStreamIndex: -1,
      });

      expect(result?.url).toBe(
        "https://jellyfin.example.com/videos/media-1/stream.mp4?DeviceId=device-1&SubtitleMethod=Encode&PlaySessionId=session-1",
      );
    });

    test("downloads the progressive mp4 even when the user picks Max quality (transcode forced by burn-in, codecs or server policy)", async () => {
      const api = makeApi();
      api.mock
        .onPost(
          "https://jellyfin.example.com/Items/item-1/PlaybackInfo",
          bodyContaining({ DeviceProfile: { Name: "1. MPV Download" } }),
        )
        .reply(200, downloadGetsAProgressiveMp4);

      const url = await download(api, MAX);

      expect(url.href).toBe(
        "https://jellyfin.example.com/videos/media-1/stream.mp4?DeviceId=device-1&PlaySessionId=session-1",
      );
    });
  });
});
