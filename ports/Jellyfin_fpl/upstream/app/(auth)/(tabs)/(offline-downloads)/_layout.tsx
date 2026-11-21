import { Stack } from "expo-router";
import { useTranslation } from "react-i18next";
import {
  commonScreenOptions,
  stackScreenOptions,
} from "@/components/stacks/NestedTabPageStack";

export const unstable_settings = { anchor: "downloaded" };

export default function DownloadsLayout() {
  const { t } = useTranslation();
  return (
    <Stack screenOptions={{ ...stackScreenOptions, ...commonScreenOptions }}>
      <Stack.Screen
        name='downloaded'
        options={{ title: t("home.downloads.downloads_title") }}
      />
      <Stack.Screen name='items/page' />
      <Stack.Screen name='downloaded/[folderId]' />
      <Stack.Screen name='series/[id]' />
    </Stack>
  );
}
