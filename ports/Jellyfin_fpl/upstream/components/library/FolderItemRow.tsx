import { Ionicons } from "@expo/vector-icons";
import type { BaseItemDto } from "@jellyfin/sdk/lib/generated-client/models";
import { useAtomValue } from "jotai";
import { memo } from "react";
import { useTranslation } from "react-i18next";
import { Pressable, View } from "react-native";
import { localArtwork } from "@/components/cards/adapters/downloads";
import { Image } from "@/components/common/ServerImage";
import { Text } from "@/components/common/Text";
import { DownloadSingleItem } from "@/components/DownloadItem";
import { PlayButton } from "@/components/PlayButton";
import useDefaultPlaySettings from "@/hooks/useDefaultPlaySettings";
import { apiAtom } from "@/providers/JellyfinProvider";
import { useSettings } from "@/utils/atoms/settings";
import { getWideImageUrl } from "@/utils/jellyfin/image/getWideImageUrl";
import { toImagePixels } from "@/utils/jellyfin/image/imagePixels";
import { isPlayableItem } from "@/utils/jellyfin/media/isPlayableItem";
import { SUBTITLES_OFF } from "@/utils/subtitles/subtitleIndex";
import { formatDuration } from "@/utils/time";

const PREVIEW_WIDTH = 96;
const PREVIEW_HEIGHT = 54;

function FolderMediaActions({
  item,
  offline,
}: {
  item: BaseItemDto;
  offline: boolean;
}) {
  const { settings } = useSettings();
  const {
    defaultBitrate,
    defaultMediaSource,
    defaultAudioIndex,
    defaultSubtitleIndex,
  } = useDefaultPlaySettings(item, settings);

  return (
    <View className='flex-row items-center gap-1'>
      <PlayButton
        compact
        item={item}
        selectedOptions={{
          bitrate: defaultBitrate,
          mediaSource: defaultMediaSource ?? undefined,
          audioIndex: defaultAudioIndex,
          subtitleIndex: defaultSubtitleIndex ?? SUBTITLES_OFF,
        }}
      />
      {!offline && <DownloadSingleItem item={item} showStatus />}
    </View>
  );
}

export const FolderItemRow = memo(function FolderItemRow({
  item,
  folder,
  onOpen,
  onLongPress,
  offline = false,
}: {
  item: BaseItemDto;
  folder: boolean;
  onOpen: () => void;
  onLongPress?: () => void;
  offline?: boolean;
}) {
  const api = useAtomValue(apiAtom);
  const { t } = useTranslation();
  const playable = !folder && !item.IsPlaceHolder && isPlayableItem(item);
  const watchState =
    playable &&
    item.UserData &&
    (item.MediaType === "Video" ||
      item.Type === "Movie" ||
      item.Type === "Episode" ||
      item.Type === "Video" ||
      item.Type === "MusicVideo");
  const position = item.UserData?.PlaybackPositionTicks ?? 0;
  const watched = !!item.UserData?.Played;
  const status = watchState
    ? watched
      ? t("library.watched")
      : position > 0
        ? t("library.stopped_at", { time: formatDuration(position) })
        : t("library.unwatched")
    : null;
  const name = item.Name || item.Id;
  const image = offline
    ? localArtwork(item.Id)
    : !folder || item.ImageTags?.Primary || item.ImageTags?.Thumb
      ? getWideImageUrl({
          api,
          item,
          useEpisodePoster: true,
          fillWidth: toImagePixels(PREVIEW_WIDTH),
          fillHeight: toImagePixels(PREVIEW_HEIGHT),
        })
      : undefined;

  return (
    <View className='flex-row items-center gap-2 border-b border-neutral-800 py-2'>
      <Pressable
        accessibilityRole='button'
        accessibilityLabel={name}
        className='min-w-0 flex-1 flex-row items-center gap-3'
        onPress={onOpen}
        onLongPress={onLongPress}
      >
        <View
          className='items-center justify-center overflow-hidden rounded-lg bg-neutral-900'
          style={{ width: PREVIEW_WIDTH, height: PREVIEW_HEIGHT }}
        >
          <Ionicons
            name={folder ? "folder" : "film-outline"}
            size={23}
            color='#a3a3a3'
          />
          {image && (
            <Image
              source={{ uri: image }}
              style={{ position: "absolute", width: "100%", height: "100%" }}
              contentFit='cover'
              recyclingKey={item.Id}
            />
          )}
        </View>
        <View className='min-w-0 flex-1'>
          <Text className='font-semibold' numberOfLines={2}>
            {name}
          </Text>
          {status && (
            <Text
              className={
                watched
                  ? "mt-1 text-xs text-emerald-400"
                  : position > 0
                    ? "mt-1 text-xs text-purple-300"
                    : "mt-1 text-xs text-neutral-400"
              }
            >
              {status}
            </Text>
          )}
          {playable && !!item.RunTimeTicks && (
            <Text className='mt-1 text-xs text-neutral-400'>
              {formatDuration(item.RunTimeTicks)}
            </Text>
          )}
        </View>
        {!playable && (
          <Ionicons name='chevron-forward' size={17} color='#a3a3a3' />
        )}
      </Pressable>
      {playable && <FolderMediaActions item={item} offline={offline} />}
    </View>
  );
});
