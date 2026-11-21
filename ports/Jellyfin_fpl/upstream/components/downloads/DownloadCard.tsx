import { Ionicons } from "@expo/vector-icons";
import { Image } from "expo-image";
import { useMemo } from "react";
import { useTranslation } from "react-i18next";
import {
  ActivityIndicator,
  Alert,
  TouchableOpacity,
  type TouchableOpacityProps,
  View,
} from "react-native";
import { toast } from "sonner-native";
import { Text } from "@/components/common/Text";
import useRouter from "@/hooks/useAppRouter";
import { useNetworkAwareQueryClient } from "@/hooks/useNetworkAwareQueryClient";
import { useDownload } from "@/providers/DownloadProvider";
import { calculateSmoothedETA } from "@/providers/Downloads/hooks/useDownloadSpeedCalculator";
import { JobStatus } from "@/providers/Downloads/types";
import { estimateTranscodeSize } from "@/utils/downloadSize";
import { storage } from "@/utils/mmkv";
import { formatTimeString } from "@/utils/time";

const bytesToMB = (bytes: number) => {
  return bytes / 1024 / 1024;
};

const formatBytes = (bytes: number): string => {
  if (bytes >= 1024 * 1024 * 1024) {
    return `${(bytes / (1024 * 1024 * 1024)).toFixed(2)} GB`;
  }
  return `${(bytes / (1024 * 1024)).toFixed(0)} MB`;
};

interface DownloadCardProps extends TouchableOpacityProps {
  process: JobStatus;
}

export const DownloadCard = ({ process, ...props }: DownloadCardProps) => {
  const { t } = useTranslation();
  const { cancelDownload, pauseDownload, resumeDownload } = useDownload();
  const router = useRouter();
  const queryClient = useNetworkAwareQueryClient();

  const handleDelete = async (id: string) => {
    try {
      await cancelDownload(id);
      // cancelDownload already shows a toast, so don't show another one
      queryClient.invalidateQueries({ queryKey: ["downloads"] });
    } catch (error) {
      console.error("Error deleting download:", error);
      toast.error(t("home.downloads.toasts.could_not_delete_download"));
    }
  };

  const eta = useMemo(() => {
    if (!process?.estimatedTotalSizeBytes || !process?.bytesDownloaded) {
      return null;
    }

    const secondsRemaining = calculateSmoothedETA(
      process.id,
      process.bytesDownloaded,
      process.estimatedTotalSizeBytes,
    );

    if (!secondsRemaining || secondsRemaining <= 0) {
      return null;
    }

    return formatTimeString(secondsRemaining, "s");
  }, [process?.id, process?.bytesDownloaded, process?.estimatedTotalSizeBytes]);

  const estimatedSize = useMemo(() => {
    if (process?.estimatedTotalSizeBytes)
      return process.estimatedTotalSizeBytes;

    return estimateTranscodeSize(
      process?.maxBitrate?.value,
      process?.mediaSource?.Bitrate,
      process?.item?.RunTimeTicks,
    );
  }, [
    process?.maxBitrate?.value,
    process?.mediaSource?.Bitrate,
    process?.item?.RunTimeTicks,
    process?.estimatedTotalSizeBytes,
  ]);

  const isTranscoding = process?.isTranscoding || false;
  const downloading = process.status === "downloading";
  const paused = process.status === "paused";
  const failed = process.status === "error";

  const downloadedAmount = useMemo(() => {
    if (!process?.bytesDownloaded) return null;
    return formatBytes(process.bytesDownloaded);
  }, [process?.bytesDownloaded]);

  const base64Image = useMemo(() => {
    try {
      const itemId = process?.item?.Id;
      if (!itemId) return undefined;
      return storage.getString(itemId);
    } catch {
      return undefined;
    }
  }, [process?.item?.Id]);

  // Sanitize progress to ensure it's within valid bounds
  const sanitizedProgress = useMemo(() => {
    if (
      typeof process?.progress !== "number" ||
      Number.isNaN(process.progress)
    ) {
      return 0;
    }
    return Math.max(0, Math.min(100, process.progress));
  }, [process?.progress]);

  // Return null after all hooks have been called
  if (!process?.item?.Id) {
    return null;
  }

  return (
    <TouchableOpacity
      onPress={() => router.push(`/(auth)/items/page?id=${process.item.Id}`)}
      className='relative bg-neutral-900 border border-neutral-800 rounded-2xl overflow-hidden'
      {...props}
    >
      {process.status === "downloading" && (
        <View
          className={`bg-purple-600 h-1 absolute bottom-0 left-0 ${isTranscoding ? "animate-pulse" : ""}`}
          style={{
            width:
              sanitizedProgress > 0
                ? `${Math.max(5, sanitizedProgress)}%`
                : "5%",
          }}
        />
      )}

      {/* Action buttons in bottom right corner */}
      <View className='absolute bottom-2 right-2 z-10 flex-row items-center gap-1'>
        <TouchableOpacity
          accessibilityRole='button'
          accessibilityLabel={t(
            paused || failed
              ? "home.downloads.queue_resume"
              : "home.downloads.queue_pause",
          )}
          onPress={() => {
            if (paused || failed) resumeDownload(process.id);
            else if (downloading)
              Alert.alert(
                t("home.downloads.queue_pause"),
                t("home.downloads.queue_pause_restart"),
                [
                  { text: t("common.cancel"), style: "cancel" },
                  {
                    text: t("home.downloads.queue_pause"),
                    onPress: () => void pauseDownload(process.id),
                  },
                ],
              );
            else void pauseDownload(process.id);
          }}
          className='min-h-11 min-w-11 items-center justify-center rounded-full bg-neutral-800'
        >
          <Ionicons
            name={paused || failed ? "play" : "pause"}
            size={20}
            color='#e5e5e5'
          />
        </TouchableOpacity>
        <TouchableOpacity
          accessibilityRole='button'
          accessibilityLabel={t("home.downloads.queue_cancel")}
          onPress={() => handleDelete(process.id)}
          className='min-h-11 min-w-11 items-center justify-center rounded-full bg-neutral-800'
        >
          <Ionicons name='close' size={22} color='#fca5a5' />
        </TouchableOpacity>
      </View>

      <View className='px-3 py-1.5 flex flex-col w-full'>
        <View className='flex flex-row items-center w-full'>
          {base64Image && (
            <View className='w-14 aspect-[10/15] rounded-lg overflow-hidden mr-4'>
              <Image
                source={{
                  uri: `data:image/jpeg;base64,${base64Image}`,
                }}
                style={{
                  width: "100%",
                  height: "100%",
                }}
                contentFit='cover'
              />
            </View>
          )}
          <View className='mb-1 min-h-12 flex-1 pr-24'>
            {!!process.item.SeriesName && (
              <Text className='text-xs text-neutral-400' numberOfLines={1}>
                {process.item.SeriesName}
              </Text>
            )}
            <Text className='font-semibold shrink'>{process.item.Name}</Text>
            {!downloading && (
              <Text className='mt-1 text-xs text-neutral-400'>
                {t(
                  paused
                    ? "home.downloads.queue_paused"
                    : failed
                      ? "home.downloads.queue_failed"
                      : "home.downloads.live_activity.queued",
                )}
              </Text>
            )}

            {isTranscoding && (
              <View className='bg-purple-600/20 px-2 py-0.5 rounded-md mt-1 self-start'>
                <Text className='text-xs text-purple-400'>
                  {t("home.downloads.transcoding")}
                </Text>
              </View>
            )}

            {/* Row 1: Progress + Downloaded/Total */}
            {downloading && (
              <View className='flex flex-row items-center gap-x-2 mt-1.5'>
                {sanitizedProgress === 0 ? (
                  <ActivityIndicator size={"small"} color={"white"} />
                ) : (
                  <Text className='text-xs font-semibold'>
                    {sanitizedProgress.toFixed(0)}%
                  </Text>
                )}
                {downloadedAmount && (
                  <Text className='text-xs opacity-75'>
                    {downloadedAmount}
                    {estimatedSize
                      ? ` / ${isTranscoding ? "~" : ""}${formatBytes(estimatedSize)}`
                      : ""}
                  </Text>
                )}
              </View>
            )}

            {/* Row 2: Speed + ETA */}
            <View className='flex flex-row items-center gap-x-2 mt-0.5'>
              {process.speed && process.speed > 0 && (
                <Text className='text-xs text-purple-400'>
                  {bytesToMB(process.speed).toFixed(2)} MB/s
                </Text>
              )}
              {eta && (
                <Text className='text-xs text-green-400'>
                  {t("home.downloads.eta", { eta: eta })}
                </Text>
              )}
            </View>
          </View>
        </View>
      </View>
    </TouchableOpacity>
  );
};
