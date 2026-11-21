import { TouchableOpacity, View } from "react-native";
import { Text } from "./Text";

type Props = {
  title: string;
  actionLabel?: string;
  actionDisabled?: boolean;
  onPressAction?: () => void;
  /** Drawn at the end of the row — a count pill, a small action. */
  accessory?: React.ReactNode;
};

export const SectionHeader: React.FC<Props> = ({
  title,
  actionLabel,
  actionDisabled = false,
  onPressAction,
  accessory,
}) => {
  const shouldShowAction = Boolean(actionLabel) && Boolean(onPressAction);

  return (
    <View
      style={{
        paddingHorizontal: 16,
        marginBottom: 4,
        flexDirection: "row",
        alignItems: "center",
        gap: 10,
      }}
    >
      <Text
        numberOfLines={2}
        style={{
          flex: 1,
          minWidth: 0,
          fontSize: 17,
          lineHeight: 23,
          fontWeight: "600",
          color: "#f4f4f5",
        }}
      >
        {title}
      </Text>
      {accessory}
      {shouldShowAction && (
        <TouchableOpacity
          onPress={onPressAction}
          disabled={actionDisabled}
          accessibilityRole='button'
          accessibilityLabel={actionLabel}
          style={{ flexShrink: 0, minHeight: 36, justifyContent: "center" }}
          hitSlop={4}
        >
          <Text
            style={{
              color: actionDisabled ? "rgba(255,255,255,0.4)" : "#c4a2f4",
              fontSize: 13,
              fontWeight: "600",
            }}
          >
            {actionLabel}
          </Text>
        </TouchableOpacity>
      )}
    </View>
  );
};
