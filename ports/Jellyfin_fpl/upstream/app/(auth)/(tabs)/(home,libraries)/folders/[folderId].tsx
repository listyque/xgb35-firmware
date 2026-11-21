import { useLocalSearchParams, useSegments } from "expo-router";
import { FolderBrowser } from "@/components/library/FolderBrowser";

export default function FolderPage() {
  const fromHome = (useSegments() as string[])[2] === "(home)";
  const { folderId, title, catalog } = useLocalSearchParams<{
    folderId: string;
    title?: string;
    catalog?: "music" | "library";
  }>();

  return (
    <FolderBrowser
      key={folderId}
      folderId={folderId}
      title={title}
      catalog={catalog}
      fromHome={fromHome}
    />
  );
}
