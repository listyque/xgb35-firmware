import {
  BottomSheetBackdrop,
  type BottomSheetBackdropProps,
  BottomSheetModal,
  BottomSheetScrollView,
  BottomSheetTextInput,
} from "@gorhom/bottom-sheet";
import type {
  BaseItemDto,
  MediaSourceInfo,
} from "@jellyfin/sdk/lib/generated-client/models";
import { type Href } from "expo-router";
import { t } from "i18next";
import { useAtom } from "jotai";
import type React from "react";
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import {
  Platform,
  Pressable,
  Switch,
  View,
  type ViewProps,
} from "react-native";
import { toast } from "sonner-native";
import { HEADER_ICON_SIZE } from "@/components/common/HeaderButton";
import { HeaderIcon } from "@/components/common/HeaderIcon";
import { Colors } from "@/constants/Colors";
import { LAST_DOWNLOAD_BITRATE_KEY } from "@/constants/Playback";
import useRouter from "@/hooks/useAppRouter";
import useDefaultPlaySettings from "@/hooks/useDefaultPlaySettings";
import { useDownload } from "@/providers/DownloadProvider";
import { lacksMediaSource } from "@/providers/Downloads/downloadRequest";
import {
  DOWNLOAD_BITRATES,
  type DownloadQuality,
  downloadCodecs,
  downloadHeights,
} from "@/providers/Downloads/quality";
import { apiAtom, userAtom } from "@/providers/JellyfinProvider";
import { type Settings, useSettings } from "@/utils/atoms/settings";
import { MAX_BITRATE, toDownloadBitrate } from "@/utils/bitrate";
import { getDefaultPlaySettings } from "@/utils/jellyfin/getDefaultPlaySettings";
import { writeToLog } from "@/utils/log";
import { storage } from "@/utils/mmkv";
import { SUBTITLES_OFF } from "@/utils/subtitles/subtitleIndex";
import { AudioTrackSelector } from "./AudioTrackSelector";
import { type Bitrate, BitrateSelector } from "./BitrateSelector";
import { Button } from "./Button";
import {
  SingleItemDownloadedIcon,
  SingleItemDownloadIcon,
} from "./common/DownloadIcons";
import { Text } from "./common/Text";
import { Loader } from "./Loader";
import { MediaSourceSelector } from "./MediaSourceSelector";
import ProgressCircle from "./ProgressCircle";
import { RoundButton } from "./RoundButton";
import { SubtitleTrackSelector } from "./SubtitleTrackSelector";

/**
 * The progress ring is drawn well under `HEADER_ICON_SIZE`. A stroked circle
 * fills its box edge to edge where a glyph's strokes leave gaps, so at an equal
 * size the ring reads heavier than the icons beside it.
 *
 * Diameter and stroke are separate knobs: shrinking the circle alone leaves a
 * chunkier-looking ring, and thinning the stroke alone does not make it smaller.
 * The stroke is an absolute width rather than a share of the diameter, so
 * resizing the ring does not change its weight.
 *
 * Keep the diameter a whole point. The library draws the stroke tangent to the
 * SVG's edge — `radius` is `size / 2 - width / 2`, so the outer edge lands
 * exactly on the boundary with no slack — and a fractional size rounds to
 * device pixels unevenly, shaving that edge into a visible cutoff.
 */
const PROGRESS_RING_SCALE = 0.79;
const PROGRESS_RING_SIZE = Math.round(HEADER_ICON_SIZE * PROGRESS_RING_SCALE);
const PROGRESS_RING_WIDTH = 2.5;
const DOWNLOAD_FORMAT_KEY = "downloads.lastFormat";
const originalQuality: DownloadQuality = {
  original: true,
  bitrate: MAX_BITRATE,
};
type Format = Omit<DownloadQuality, "bitrate">;

function lastFormat(): Format {
  try {
    return JSON.parse(storage.getString(DOWNLOAD_FORMAT_KEY) ?? "{}");
  } catch {
    return {};
  }
}

function lastBitrate(): Bitrate | undefined {
  const value = storage.getNumber(LAST_DOWNLOAD_BITRATE_KEY);
  return (
    DOWNLOAD_BITRATES.find((bitrate) => (bitrate.value ?? 0) === value) ??
    (value ? { key: `${value / 1_000_000} Mb/s`, value } : undefined)
  );
}

const codecLabels = { h264: "H.264", hevc: "H.265 / HEVC", av1: "AV1" };

function Choices<T extends string | number>({
  label,
  options,
  value,
  onChange,
}: {
  label: string;
  options: { value: T; label: string }[];
  value: T;
  onChange: (value: T) => void;
}) {
  return (
    <View className='gap-1'>
      <Text className='text-xs text-neutral-400'>{label}</Text>
      <View className='flex-row flex-wrap gap-2'>
        {options.map((option) => {
          const checked = value === option.value;
          return (
            <Pressable
              key={option.value}
              accessibilityRole='radio'
              accessibilityState={{ checked }}
              onPress={() => onChange(option.value)}
              className={`rounded-lg border px-3 py-2 ${
                checked
                  ? "border-purple-500 bg-purple-900/40"
                  : "border-neutral-800 bg-neutral-900"
              }`}
            >
              <Text className='text-sm'>{option.label}</Text>
            </Pressable>
          );
        })}
      </View>
    </View>
  );
}

const parseCustomBitrate = (input: string): Bitrate | undefined => {
  if (!/^\d+(?:[.,]\d+)?$/.test(input)) return;
  const mbps = Number(input.replace(",", "."));
  const value = Math.round(mbps * 1_000_000);
  // Jellyfin's MaxStreamingBitrate is a signed 32-bit integer in bit/s.
  if (value > 0 && value <= 2_147_483_647) {
    return { key: `${mbps} Mb/s`, value };
  }
};

export type SelectedOptions = {
  bitrate: Bitrate;
  mediaSource: MediaSourceInfo | undefined;
  audioIndex: number | undefined;
  subtitleIndex: number;
};

/** A track picked in the sheet, with the version it was picked on. */
interface TrackPick {
  source: MediaSourceInfo;
  index: number;
}

interface TrackPicks {
  audio?: TrackPick;
  subtitle?: TrackPick;
}

/**
 * The tracks to download from `version`. Stream indexes are per version, so
 * each pick is matched again from the version it was made on: a version that
 * lacks the track falls back to its own default and the pick stays as it was.
 */
const tracksOnVersion = (
  item: BaseItemDto,
  version: MediaSourceInfo,
  settings: Settings | null,
  { audio, subtitle }: TrackPicks,
): { audioIndex?: number; subtitleIndex: number } => {
  const { audioIndex, subtitleIndex } = getDefaultPlaySettings(
    { ...item, MediaSources: [version] },
    settings,
    {
      indexes: { audioIndex: audio?.index, subtitleIndex: subtitle?.index },
      pickedOn: { audio: audio?.source, subtitle: subtitle?.source },
    },
  );
  return { audioIndex, subtitleIndex: subtitleIndex ?? SUBTITLES_OFF };
};

interface DownloadProps extends ViewProps {
  items?: BaseItemDto[];
  loadItems?: () => Promise<BaseItemDto[]>;
  buttonLabel?: string;
  MissingDownloadIconComponent: () => React.ReactElement;
  DownloadedIconComponent: () => React.ReactElement;
  title?: string;
  subtitle?: string;
  size?: "default" | "large";
  showStatus?: boolean;
}

export const DownloadItems: React.FC<DownloadProps> = ({
  items: suppliedItems,
  loadItems,
  buttonLabel,
  MissingDownloadIconComponent,
  DownloadedIconComponent,
  title = "Download",
  subtitle = "",
  size = "default",
  showStatus = false,
  ...props
}) => {
  const [api] = useAtom(apiAtom);
  const [user] = useAtom(userAtom);
  const { settings } = useSettings();
  const router = useRouter();
  const [downloadUnwatchedOnly, setDownloadUnwatchedOnly] = useState(false);
  const [bitrateInput, setBitrateInput] = useState("");
  const [format, setFormat] = useState(lastFormat);
  const [loadedItems, setLoadedItems] = useState<BaseItemDto[]>([]);
  const [loadingItems, setLoadingItems] = useState(false);
  const items = suppliedItems ?? loadedItems;
  const folderDownload = !!loadItems;
  const submitting = useRef(false);
  const customBitrate = parseCustomBitrate(bitrateInput.trim());
  const invalidBitrate =
    !format.original && !!bitrateInput.trim() && !customBitrate;

  const { processes, enqueueDownloads, downloadedItems } = useDownload();
  const downloadedFiles = downloadedItems;

  const [selectedOptions, setSelectedOptions] = useState<
    SelectedOptions | undefined
  >(undefined);
  // A default is not a choice, so only a picked track follows a version change.
  const [picks, setPicks] = useState<TrackPicks>({});

  const userCanDownload = useMemo(
    () => user?.Policy?.EnableContentDownloading,
    [user],
  );

  const bottomSheetModalRef = useRef<BottomSheetModal>(null);

  const handlePresentModalPress = useCallback(() => {
    const lastValue = storage.getNumber(LAST_DOWNLOAD_BITRATE_KEY);
    const preset = DOWNLOAD_BITRATES.find(
      ({ value }) => (value ?? 0) === lastValue,
    );
    setBitrateInput(lastValue && !preset ? String(lastValue / 1_000_000) : "");
    setFormat(lastFormat());
    if (preset)
      setSelectedOptions(
        (previous) => previous && { ...previous, bitrate: preset },
      );
    bottomSheetModalRef.current?.present();
  }, []);

  const handleSheetChanges = useCallback((_index: number) => {
    // Modal state tracking handled by BottomSheetModal
  }, []);

  const closeModal = useCallback(() => {
    bottomSheetModalRef.current?.dismiss();
  }, []);

  const itemIds = useMemo(() => items.map((i) => i.Id), [items]);

  const itemsNotDownloaded = useMemo(() => {
    const excluded = new Set(downloadedFiles?.map((file) => file.item.Id));
    return items.filter((item) => !excluded.has(item.Id));
  }, [items, downloadedFiles]);

  // The sheet offers a source and tracks when one item is left to download,
  // and that item is the one they are for. In a season with the rest already
  // downloaded it is rarely the first of the list.
  const optionsItem =
    itemsNotDownloaded.length === 1 ? itemsNotDownloaded[0] : items[0];

  const {
    defaultAudioIndex,
    defaultBitrate: defaultPlaybackBitrate,
    defaultMediaSource,
    defaultSubtitleIndex,
  } = useDefaultPlaySettings(optionsItem, settings);
  // The sheet offers no Auto: a download is made at Max instead.
  const defaultBitrate = toDownloadBitrate(defaultPlaybackBitrate);
  const bitrate = customBitrate ?? selectedOptions?.bitrate ?? defaultBitrate;
  const savedBitrate = lastBitrate();
  const quickQuality = {
    ...lastFormat(),
    bitrate: savedBitrate ?? defaultBitrate,
  };
  const quickLabel = [
    quickQuality.height && `${quickQuality.height}p`,
    quickQuality.codec && codecLabels[quickQuality.codec],
    quickQuality.container && "MP4",
    quickQuality.bitrate.value === undefined
      ? t("item_card.download.no_bitrate_limit")
      : quickQuality.bitrate.key,
  ]
    .filter(Boolean)
    .join(" · ");

  // Initialize selectedOptions with default values
  useEffect(() => {
    setSelectedOptions((previous) => ({
      bitrate: folderDownload
        ? (previous?.bitrate ?? defaultBitrate)
        : defaultBitrate,
      mediaSource: defaultMediaSource ?? undefined,
      subtitleIndex: defaultSubtitleIndex ?? -1,
      audioIndex: defaultAudioIndex,
    }));
    setPicks({});
  }, [
    defaultAudioIndex,
    defaultBitrate,
    defaultSubtitleIndex,
    defaultMediaSource,
    folderDownload,
  ]);

  const itemsToDownload = useMemo(() => {
    const pending = new Set(processes.map((process) => process.item.Id));
    return itemsNotDownloaded.filter(
      (item) =>
        !pending.has(item.Id) &&
        (!downloadUnwatchedOnly || !item.UserData?.Played),
    );
  }, [itemsNotDownloaded, downloadUnwatchedOnly, processes]);

  const allItemsDownloaded = useMemo(() => {
    if (items.length === 0) return false;
    return itemsNotDownloaded.length === 0;
  }, [items, itemsNotDownloaded]);
  const itemsProcesses = useMemo(
    () =>
      processes?.filter((p) => p?.item?.Id && itemIds.includes(p.item.Id)) ||
      [],
    [processes, itemIds],
  );

  const progress = useMemo(() => {
    if (itemIds.length === 1)
      return itemsProcesses.reduce((acc, p) => acc + (p.progress || 0), 0);
    if (!itemIds.length) return 0;
    return (
      ((itemIds.length - itemsNotDownloaded.length) * 100 +
        itemsProcesses.reduce((sum, p) => sum + p.progress, 0)) /
      itemIds.length
    );
  }, [itemsProcesses, itemIds, itemsNotDownloaded.length]);

  const navigateToDownloads = () => router.push("/downloaded");

  const onDownloadedPress = () => {
    const firstItem = items?.[0];
    router.push(
      firstItem.Type !== "Episode"
        ? "/downloaded"
        : ({
            pathname: "/series/[id]",
            params: {
              id: firstItem.SeriesId!,
              seasonIndex: firstItem.ParentIndexNumber?.toString(),
              offline: "true",
            },
          } as Href),
    );
  };

  const acceptDownloadOptions = (
    quality: DownloadQuality,
    remember: boolean,
  ) => {
    if (!itemsToDownload.length || !api || !user?.Id) return;
    if (!userCanDownload) {
      toast.error(
        t("home.downloads.toasts.you_are_not_allowed_to_download_files"),
      );
      return;
    }

    const single = !folderDownload && itemsNotDownloaded.length === 1;
    if (single && lacksMediaSource(1, selectedOptions?.mediaSource)) {
      writeToLog("WARN", "Download turned down: item has no media source", {
        itemType: itemsToDownload[0]?.Type,
      });
      toast.error(t("home.downloads.toasts.no_media_source_to_download"));
      return;
    }

    const tracks =
      single && selectedOptions
        ? {
            mediaSourceId: selectedOptions.mediaSource?.Id,
            audioIndex: selectedOptions.audioIndex,
            subtitleIndex: selectedOptions.subtitleIndex,
          }
        : undefined;
    const count = enqueueDownloads(
      itemsToDownload.map((item) => ({ item, quality, tracks })),
    );
    if (remember && count) {
      const { bitrate, ...format } = quality;
      storage.set(LAST_DOWNLOAD_BITRATE_KEY, bitrate.value ?? 0);
      storage.set(DOWNLOAD_FORMAT_KEY, JSON.stringify(format));
    }
    closeModal();
    if (count) toast.success(t("home.downloads.queue_added", { count }));
  };

  const renderBackdrop = useCallback(
    (props: BottomSheetBackdropProps) => (
      <BottomSheetBackdrop
        {...props}
        disappearsOnIndex={-1}
        appearsOnIndex={0}
      />
    ),
    [],
  );

  const renderButtonContent = () => {
    if (
      itemsProcesses.some(
        ({ status }) =>
          status === "queued" || status === "paused" || status === "error",
      )
    ) {
      return <HeaderIcon name='queued' />;
    }
    // For single item downloads, show progress if item is being processed
    // For multi-item downloads (season/series), show progress only if 2+ items are in progress or queued
    const shouldShowProgress =
      itemIds.length === 1
        ? itemsProcesses.length > 0
        : itemsProcesses.length > 1;

    if (processes.length > 0 && shouldShowProgress) {
      return progress === 0 ? (
        <Loader />
      ) : (
        <ProgressCircle
          size={PROGRESS_RING_SIZE}
          fill={progress}
          width={PROGRESS_RING_WIDTH}
          tintColor={Colors.primary}
          backgroundColor='white'
        />
      );
    }

    if (allItemsDownloaded) {
      return <DownloadedIconComponent />;
    }

    return <MissingDownloadIconComponent />;
  };

  const onButtonPress = () => {
    if (submitting.current) return;
    if (loadItems) {
      submitting.current = true;
      setLoadingItems(true);
      void loadItems()
        .then((result) => {
          setLoadedItems(result);
          handlePresentModalPress();
        })
        .catch(() => toast.error(t("common.something_went_wrong")))
        .finally(() => {
          submitting.current = false;
          setLoadingItems(false);
        });
    } else if (processes && itemsProcesses.length > 0) {
      navigateToDownloads();
    } else if (allItemsDownloaded) {
      onDownloadedPress();
    } else {
      handlePresentModalPress();
    }
  };

  const status = !showStatus
    ? null
    : allItemsDownloaded
      ? t("home.downloads.live_activity.completed")
      : itemsProcesses.some(({ status }) => status === "paused")
        ? t("home.downloads.queue_paused")
        : itemsProcesses.some(({ status }) => status === "error")
          ? t("home.downloads.queue_failed")
          : itemsProcesses.some(({ status }) => status === "queued")
            ? t("home.downloads.live_activity.queued")
            : itemsProcesses.length
              ? t("home.downloads.live_activity.downloading")
              : null;

  return (
    <View {...props}>
      {buttonLabel ? (
        <Button
          color='black'
          onPress={onButtonPress}
          loading={loadingItems}
          disabled={loadingItems}
          iconLeft={<MissingDownloadIconComponent />}
        >
          {buttonLabel}
        </Button>
      ) : (
        <RoundButton
          size={size}
          onPress={onButtonPress}
          accessibilityRole='button'
          accessibilityLabel={status ?? t("item_card.download.download_button")}
        >
          {renderButtonContent()}
        </RoundButton>
      )}
      {status && (
        <Text
          accessibilityLiveRegion='polite'
          className='max-w-[72px] text-xs text-neutral-400'
        >
          {status}
        </Text>
      )}
      <BottomSheetModal
        ref={bottomSheetModalRef}
        enableDynamicSizing
        handleIndicatorStyle={{
          backgroundColor: "white",
        }}
        backgroundStyle={{
          backgroundColor: "#171717",
        }}
        onChange={handleSheetChanges}
        backdropComponent={renderBackdrop}
        enablePanDownToClose
        enableDismissOnClose
        android_keyboardInputMode='adjustResize'
        keyboardBehavior='interactive'
        keyboardBlurBehavior='restore'
      >
        <BottomSheetScrollView keyboardShouldPersistTaps='handled'>
          <View className='flex flex-col space-y-4 px-4 pb-8 pt-2'>
            <View>
              <Text className='font-bold text-2xl text-neutral-100'>
                {title}
              </Text>
              <Text className='text-neutral-300'>
                {loadItems && !itemsToDownload.length
                  ? t("library.nothing_to_download")
                  : subtitle ||
                    t("item_card.download.download_x_item", {
                      item_count: itemsToDownload.length,
                    })}
              </Text>
            </View>
            {(loadItems || itemsNotDownloaded.length > 1) && (
              <View className='flex-row items-center justify-between py-2'>
                <Text>{t("item_card.download.download_unwatched_only")}</Text>
                <Switch
                  onValueChange={setDownloadUnwatchedOnly}
                  value={downloadUnwatchedOnly}
                />
              </View>
            )}
            <Button
              color='purple'
              disabled={!itemsToDownload.length}
              onPress={() => acceptDownloadOptions(originalQuality, false)}
            >
              {t("item_card.download.download_original")}
            </Button>
            {savedBitrate && !quickQuality.original && (
              <Button
                color='black'
                disabled={!itemsToDownload.length}
                onPress={() => acceptDownloadOptions(quickQuality, false)}
              >
                {t("item_card.download.quick_download", {
                  quality: quickLabel,
                })}
              </Button>
            )}
            <View className='flex flex-col space-y-2 w-full'>
              <Choices
                label={t("item_card.download.file_format")}
                value={
                  format.original ? "original" : (format.container ?? "auto")
                }
                options={[
                  { value: "auto", label: t("item_card.quality_auto") },
                  {
                    value: "original",
                    label: t("item_card.download.original_file"),
                  },
                  { value: "mp4", label: "MP4" },
                ]}
                onChange={(value) =>
                  setFormat((previous) => ({
                    ...previous,
                    original: value === "original",
                    container: value === "mp4" ? value : undefined,
                  }))
                }
              />
              {!format.original && (
                <>
                  <Choices
                    label={t("item_card.download.resolution")}
                    options={[
                      { value: 0, label: t("item_card.quality_auto") },
                      ...downloadHeights.map((value) => ({
                        value,
                        label: `${value}p`,
                      })),
                    ]}
                    value={format.height ?? 0}
                    onChange={(height) =>
                      setFormat((previous) => ({
                        ...previous,
                        height: height || undefined,
                      }))
                    }
                  />
                  <Choices
                    label={t("item_card.download.video_codec")}
                    options={[
                      { value: "auto", label: t("item_card.quality_auto") },
                      ...downloadCodecs.map((value) => ({
                        value,
                        label: codecLabels[value],
                      })),
                    ]}
                    value={format.codec ?? "auto"}
                    onChange={(codec) =>
                      setFormat((previous) => ({
                        ...previous,
                        codec: codec === "auto" ? undefined : codec,
                      }))
                    }
                  />
                  <View className='items-start'>
                    <BitrateSelector
                      onChange={(val) => {
                        setBitrateInput("");
                        setSelectedOptions(
                          (prev) => prev && { ...prev, bitrate: val },
                        );
                      }}
                      selected={selectedOptions?.bitrate}
                    />
                  </View>
                  <View>
                    <Text className='mb-1 text-xs text-neutral-400'>
                      {t("item_card.download.custom_bitrate")}
                    </Text>
                    <BottomSheetTextInput
                      accessibilityLabel={t(
                        "item_card.download.custom_bitrate",
                      )}
                      className='rounded-xl border border-neutral-800 bg-neutral-900 px-3 py-2 text-white'
                      keyboardType='decimal-pad'
                      value={bitrateInput}
                      onChangeText={setBitrateInput}
                      placeholder='1.5'
                      placeholderTextColor='#a3a3a3'
                    />
                    {invalidBitrate && (
                      <Text className='mt-1 text-xs text-red-400'>
                        {t("item_card.download.invalid_bitrate")}
                      </Text>
                    )}
                  </View>
                </>
              )}
              {!loadItems && itemsNotDownloaded.length === 1 && (
                <View>
                  <View className='items-start'>
                    <MediaSourceSelector
                      item={optionsItem}
                      onChange={(val) =>
                        setSelectedOptions((prev) => {
                          if (!prev) return prev;
                          return {
                            ...prev,
                            mediaSource: val,
                            ...tracksOnVersion(
                              optionsItem,
                              val,
                              settings,
                              picks,
                            ),
                          };
                        })
                      }
                      selected={selectedOptions?.mediaSource}
                    />
                  </View>
                  {selectedOptions?.mediaSource && (
                    <View className='flex flex-col space-y-2 items-start'>
                      <AudioTrackSelector
                        source={selectedOptions.mediaSource}
                        onChange={(val) => {
                          setSelectedOptions(
                            (prev) =>
                              prev && {
                                ...prev,
                                audioIndex: val,
                              },
                          );
                          const source = selectedOptions.mediaSource;
                          if (source) {
                            setPicks((prev) => ({
                              ...prev,
                              audio: { source, index: val },
                            }));
                          }
                        }}
                        selected={selectedOptions.audioIndex}
                      />
                      <SubtitleTrackSelector
                        source={selectedOptions.mediaSource}
                        onChange={(val) => {
                          setSelectedOptions(
                            (prev) =>
                              prev && {
                                ...prev,
                                subtitleIndex: val,
                              },
                          );
                          const source = selectedOptions.mediaSource;
                          if (source) {
                            setPicks((prev) => ({
                              ...prev,
                              subtitle: { source, index: val },
                            }));
                          }
                        }}
                        selected={selectedOptions.subtitleIndex}
                      />
                    </View>
                  )}
                </View>
              )}
            </View>

            <Button
              disabled={invalidBitrate || !itemsToDownload.length}
              onPress={() =>
                acceptDownloadOptions({ ...format, bitrate }, true)
              }
              color='black'
            >
              {t("item_card.download.download_button")}
            </Button>
          </View>
        </BottomSheetScrollView>
      </BottomSheetModal>
    </View>
  );
};

export const DownloadSingleItem: React.FC<{
  size?: "default" | "large";
  item: BaseItemDto;
  showStatus?: boolean;
}> = ({ item, size = "default", showStatus }) => {
  if (Platform.isTV) return;

  return (
    <DownloadItems
      size={size}
      showStatus={showStatus}
      title={
        item.Type === "Episode"
          ? t("item_card.download.download_episode")
          : t("item_card.download.download_movie")
      }
      subtitle={item.Name!}
      items={[item]}
      MissingDownloadIconComponent={SingleItemDownloadIcon}
      DownloadedIconComponent={SingleItemDownloadedIcon}
    />
  );
};
