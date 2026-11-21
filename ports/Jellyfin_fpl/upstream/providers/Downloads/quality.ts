import { BITRATES, type Bitrate } from "@/constants/Playback";

export const DOWNLOAD_BITRATES: Bitrate[] = [
  BITRATES[0],
  ...[60, 40, 25, 16, 12].map((mbps) => ({
    key: `${mbps} Mb/s`,
    value: mbps * 1_000_000,
  })),
  ...BITRATES.slice(1),
];

export const downloadHeights = [2160, 1440, 1080, 720, 480, 360] as const;
export const downloadCodecs = ["h264", "hevc", "av1"] as const;

export type DownloadQuality = {
  bitrate: Bitrate;
  height?: (typeof downloadHeights)[number];
  codec?: (typeof downloadCodecs)[number];
  container?: "mp4";
  original?: boolean;
};
