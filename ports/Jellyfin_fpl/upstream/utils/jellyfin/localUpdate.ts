export const LOCAL_UPDATE_LIBRARY = "Streamyfin Local Updates";

export function parseLocalUpdateName(name: string | null | undefined) {
  const match = name?.match(
    /^Streamyfin Local Update (\d+) ([a-f0-9]{64})(?:\.epub)?$/,
  );
  if (!match) return null;
  const version = Number(match[1]);
  if (!Number.isSafeInteger(version) || version < 1 || version > 2_100_000_000)
    return null;
  return { version, sha256: match[2] };
}
