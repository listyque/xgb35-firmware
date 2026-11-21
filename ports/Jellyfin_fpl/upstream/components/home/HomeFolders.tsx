import { Ionicons } from "@expo/vector-icons";
import type { BaseItemDto } from "@jellyfin/sdk/lib/generated-client/models";
import { useQuery } from "@tanstack/react-query";
import { useHeaderHeight } from "expo-router/react-navigation";
import { useAtomValue } from "jotai";
import { type PropsWithChildren, useState } from "react";
import { useTranslation } from "react-i18next";
import { Platform, Pressable, ScrollView, View } from "react-native";
import { useSafeAreaInsets } from "react-native-safe-area-context";
import { Text } from "@/components/common/Text";
import { FolderBrowser } from "@/components/library/FolderBrowser";
import { FolderSearchInput } from "@/components/library/FolderSearchInput";
import useRouter from "@/hooks/useAppRouter";
import { apiAtom, userAtom } from "@/providers/JellyfinProvider";
import {
  folderHref,
  getLastPlayedFolder,
  opensAsFolders,
} from "@/utils/library/folders";

export function HomeFolders({
  libraries,
  hiddenLibraries,
  children,
}: PropsWithChildren<{
  libraries?: BaseItemDto[] | null;
  hiddenLibraries?: string[];
}>) {
  const [search, setSearch] = useState("");
  const router = useRouter();
  const insets = useSafeAreaInsets();
  const headerHeight = useHeaderHeight();
  const { t } = useTranslation();
  const api = useAtomValue(apiAtom);
  const userId = useAtomValue(userAtom)?.Id;
  const { data: lastFolder } = useQuery({
    queryKey: ["home", "lastPlayedFolder", api?.basePath, userId],
    enabled: !!api && !!userId,
    queryFn: ({ signal }) => getLastPlayedFolder(api!, userId!, signal),
  });
  const visibleLibraries = libraries?.filter(
    (item) => opensAsFolders(item) && !hiddenLibraries?.includes(item.Id!),
  );
  const searching = !!search.trim();

  return (
    <View className='flex-1'>
      <View
        style={{
          paddingLeft: insets.left + 16,
          paddingRight: insets.right + 16,
          paddingTop: (Platform.OS === "ios" ? headerHeight : 0) + 12,
          paddingBottom: 8,
          gap: 10,
        }}
      >
        <FolderSearchInput value={search} onChange={setSearch} inPanel />
        {!searching && lastFolder && (
          <Pressable
            style={{
              flexDirection: "row",
              alignItems: "center",
              gap: 10,
              borderRadius: 12,
              borderWidth: 1,
              borderColor: "#312638",
              backgroundColor: "#1b171f",
              paddingHorizontal: 12,
              paddingVertical: 9,
            }}
            accessibilityRole='button'
            onPress={() => router.push(folderHref(lastFolder, true))}
          >
            <Ionicons name='time-outline' size={20} color='#d8b4fe' />
            <View style={{ flex: 1, minWidth: 0, gap: 2 }}>
              <Text style={{ fontSize: 11, color: "#b7aabd" }}>
                {t("library.last_played_folder")}
              </Text>
              <Text
                numberOfLines={1}
                style={{ fontSize: 14, fontWeight: "600" }}
              >
                {lastFolder.Name}
              </Text>
            </View>
            <Ionicons name='chevron-forward' size={18} color='#a3a3a3' />
          </Pressable>
        )}
        {!searching && !!visibleLibraries?.length && (
          <ScrollView
            horizontal
            style={{ flexGrow: 0 }}
            showsHorizontalScrollIndicator={false}
            contentContainerStyle={{ gap: 8 }}
          >
            {visibleLibraries.map((library) => (
              <Pressable
                key={library.Id}
                accessibilityRole='button'
                style={{
                  minHeight: 44,
                  maxWidth: 230,
                  flexDirection: "row",
                  alignItems: "center",
                  gap: 7,
                  borderRadius: 10,
                  borderWidth: 1,
                  borderColor: "#29282d",
                  backgroundColor: "#17171a",
                  paddingHorizontal: 11,
                }}
                onPress={() =>
                  router.push(
                    folderHref(
                      library,
                      true,
                      library.CollectionType === "music" ? "music" : "library",
                    ),
                  )
                }
              >
                <Ionicons name='folder-outline' size={17} color='#c4bdca' />
                <Text numberOfLines={1} style={{ flexShrink: 1, fontSize: 13 }}>
                  {library.Name}
                </Text>
              </Pressable>
            ))}
          </ScrollView>
        )}
      </View>
      {searching ? <FolderBrowser fromHome homeSearch={search} /> : children}
    </View>
  );
}
