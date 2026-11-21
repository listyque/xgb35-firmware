import { Ionicons } from "@expo/vector-icons";
import { useTranslation } from "react-i18next";
import { Pressable } from "react-native";
import { Text } from "@/components/common/Text";
import useRouter from "@/hooks/useAppRouter";
import { folderHref } from "@/utils/library/folders";

export const LibraryFoldersButton = ({ libraryId }: { libraryId: string }) => {
  const router = useRouter();
  const { t } = useTranslation();

  return (
    <Pressable
      accessibilityRole='button'
      className='flex-row items-center self-start gap-2 rounded-lg bg-neutral-900 px-3 py-3'
      onPress={() => router.push(folderHref({ Id: libraryId }))}
    >
      <Ionicons name='folder-outline' size={20} color='#e5e5e5' />
      <Text>{t("library.folders")}</Text>
    </Pressable>
  );
};
