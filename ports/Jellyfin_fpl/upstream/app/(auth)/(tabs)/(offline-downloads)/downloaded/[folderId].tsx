import { useLocalSearchParams } from "expo-router";
import DownloadsPage from "@/app/(auth)/(tabs)/(home)/downloads/index";

export default function DownloadedFolder() {
  const { folderId } = useLocalSearchParams<{ folderId: string }>();
  return <DownloadsPage key={folderId} folderView folderId={folderId} />;
}
