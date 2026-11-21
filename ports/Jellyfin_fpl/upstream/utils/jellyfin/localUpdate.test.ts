import { parseLocalUpdateName } from "./localUpdate";

const sha256 = "a".repeat(64);

test("reads update version and digest from the Jellyfin book path or title", () => {
  const title = `Streamyfin Local Update 22 ${sha256}`;
  expect(parseLocalUpdateName(title)).toEqual({ version: 22, sha256 });
  expect(parseLocalUpdateName(`${title}.epub`)).toEqual({
    version: 22,
    sha256,
  });
});

test.each([
  `Streamyfin Local Update 0 ${sha256}`,
  `Streamyfin Local Update 2100000001 ${sha256}`,
  `Streamyfin Local Update 22 ${sha256}.apk`,
  "Streamyfin Local Update 22 abc",
])("rejects an invalid update name %s", (name) => {
  expect(parseLocalUpdateName(name)).toBeNull();
});
