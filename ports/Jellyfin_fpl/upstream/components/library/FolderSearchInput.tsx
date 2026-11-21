import { Ionicons } from "@expo/vector-icons";
import { useHeaderHeight } from "expo-router/react-navigation";
import { useTranslation } from "react-i18next";
import { Platform, Pressable, TextInput, View } from "react-native";
import { useSafeAreaInsets } from "react-native-safe-area-context";

export function FolderSearchInput({
  value,
  onChange,
  inPanel = false,
}: {
  value: string;
  onChange: (value: string) => void;
  inPanel?: boolean;
}) {
  const { t } = useTranslation();
  const insets = useSafeAreaInsets();
  const headerHeight = useHeaderHeight();
  const label = t("library.search_folders");
  return (
    <View
      style={
        inPanel
          ? undefined
          : {
              paddingLeft: insets.left + 16,
              paddingRight: insets.right + 16,
              paddingTop: (Platform.OS === "ios" ? headerHeight : 0) + 8,
              paddingBottom: 8,
            }
      }
    >
      <View
        style={{
          flexDirection: "row",
          alignItems: "center",
          gap: 10,
          borderRadius: 12,
          backgroundColor: "#1c1b20",
          paddingLeft: 12,
        }}
      >
        <Ionicons name='search' size={20} color='#a3a3a3' />
        <TextInput
          style={{
            flex: 1,
            minWidth: 0,
            minHeight: 44,
            paddingVertical: 8,
            paddingRight: 12,
            fontSize: 15,
            color: "white",
          }}
          accessibilityLabel={label}
          placeholder={label}
          placeholderTextColor='#a3a3a3'
          value={value}
          onChangeText={onChange}
          autoCorrect={false}
          autoCapitalize='none'
          returnKeyType='search'
        />
        {!!value && (
          <Pressable
            accessibilityRole='button'
            accessibilityLabel={t("library.clear_folder_search")}
            onPress={() => onChange("")}
            className='h-11 w-11 items-center justify-center'
          >
            <Ionicons name='close-circle' size={20} color='#a3a3a3' />
          </Pressable>
        )}
      </View>
    </View>
  );
}
