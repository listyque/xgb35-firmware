import { QueryClient, QueryClientProvider } from "@tanstack/react-query";
import {
  fireEvent,
  render,
  screen,
  waitFor,
} from "@testing-library/react-native";
import { getDefaultStore } from "jotai";
import { HomeFolders } from "@/components/home/HomeFolders";
import { apiAtom } from "@/providers/JellyfinProvider";
import type { makeApi } from "@/test-utils/jellyfinApi";
import { secondsToTicks } from "@/utils/time";
import { FolderBrowser } from "./FolderBrowser";

jest.mock("@/providers/JellyfinProvider", () => {
  const { atom } = jest.requireActual("jotai");
  const { makeApi } = jest.requireActual("@/test-utils/jellyfinApi");
  return { apiAtom: atom(makeApi()), userAtom: atom({ Id: "user-1" }) };
});
jest.mock("expo-router", () => ({ Stack: { Screen: () => null } }));
jest.mock(
  "expo-router/react-navigation",
  () => jest.requireActual("@/test-utils/expoRouter").reactNavigationModule,
);
jest.mock("react-i18next", () => ({
  useTranslation: () => ({
    t: (key: string, params?: { time?: string }) =>
      params?.time ? `${key} ${params.time}` : key,
  }),
}));
jest.mock("@/hooks/useItemActionSheet", () => ({
  useItemActionSheet: () => () => {},
}));
const mockPush = jest.fn();
jest.mock("@/hooks/useAppRouter", () => () => ({ push: mockPush }));
jest.mock("@/components/common/ServerImage", () => ({ Image: () => null }));
jest.mock("@/utils/atoms/settings", () => ({
  useSettings: () => ({ settings: null }),
}));
jest.mock("@/components/PlayButton", () => ({
  PlayButton: () => {
    const { Text } = jest.requireActual("react-native");
    return <Text>Play</Text>;
  },
}));
jest.mock("@/components/DownloadItem", () => ({
  DownloadSingleItem: () => {
    const { Text } = jest.requireActual("react-native");
    return <Text>Download</Text>;
  },
}));

const api = getDefaultStore().get(apiAtom) as ReturnType<typeof makeApi>;
beforeEach(() => {
  api.mock.reset();
  mockPush.mockClear();
});

const makeClient = () =>
  new QueryClient({
    defaultOptions: { queries: { gcTime: Infinity, retry: false } },
  });

test("pages through a folder and opens only the selected subfolder", async () => {
  const client = makeClient();
  const firstPage = [
    { Type: "Folder", Id: "subfolder", Name: "Concerts" },
    ...Array.from({ length: 49 }, (_, i) => ({
      Type: "Video",
      Id: `video-${i}`,
      Name: `Video ${i}`,
    })),
  ];
  const requests: URLSearchParams[] = [];
  api.mock.onGet(/\/Items\?/).reply(({ url }) => {
    const params = new URL(url!).searchParams;
    requests.push(params);
    if (params.get("parentId") === "subfolder") {
      return [
        200,
        {
          Items: [
            {
              Type: "Episode",
              Id: "video",
              Name: "Encore",
              UserData: {
                PlaybackPositionTicks: secondsToTicks(754),
                Played: false,
              },
            },
            {
              Type: "Movie",
              Id: "finished",
              Name: "Finale",
              UserData: { Played: true },
            },
            {
              Type: "Movie",
              Id: "new",
              Name: "Premiere",
              UserData: { Played: false },
            },
          ],
        },
      ];
    }
    return [200, { Items: params.get("startIndex") === "0" ? firstPage : [] }];
  });

  const showFolder = (folderId: string) => (
    <QueryClientProvider client={client}>
      <FolderBrowser key={folderId} folderId={folderId} />
    </QueryClientProvider>
  );
  const result = await render(showFolder("library"));
  const folder = await screen.findByRole("button", { name: "Concerts" });
  expect(Object.fromEntries(requests[0])).toMatchObject({
    parentId: "library",
    userId: "user-1",
    recursive: "false",
    startIndex: "0",
    limit: "50",
  });
  await fireEvent(screen.getByTestId("folder-browser-list"), "endReached");
  await waitFor(() => expect(requests[1]?.get("startIndex")).toBe("50"));

  await fireEvent.press(folder);
  expect(mockPush).toHaveBeenLastCalledWith({
    pathname: "/(auth)/(tabs)/(libraries)/folders/[folderId]",
    params: { folderId: "subfolder", title: "Concerts" },
  });
  await result.rerender(showFolder("subfolder"));
  await fireEvent.press(await screen.findByRole("button", { name: "Encore" }));
  expect(screen.getAllByText("Play")).toHaveLength(3);
  expect(screen.getAllByText("Download")).toHaveLength(3);
  expect(screen.getByText("library.stopped_at 12:34")).toBeTruthy();
  expect(screen.getByText("library.watched")).toBeTruthy();
  expect(screen.getByText("library.unwatched")).toBeTruthy();
  expect(requests[2].get("parentId")).toBe("subfolder");
  expect(requests[2].get("startIndex")).toBe("0");
  expect(mockPush).toHaveBeenLastCalledWith({
    pathname: "/items/page",
    params: { id: "video" },
  });
});

test("opens libraries from Home and finds a nested folder by name beyond the first page", async () => {
  const filler = Array.from({ length: 200 }, (_, i) => ({
    Type: "Folder",
    Id: `folder-${i}`,
    Name: `Folder ${i}`,
  }));
  api.mock.onGet(/\/Items\?/).reply(({ url }) => {
    const query = new URL(url!).searchParams;
    if (query.get("sortBy") === "DatePlayed") {
      return [
        200,
        {
          Items: [
            {
              Id: "most-recent",
              ParentId: "last-folder",
              UserData: { LastPlayedDate: "2026-11-15T00:00:00Z" },
            },
          ],
        },
      ];
    }
    const offset = query.get("startIndex");
    return [
      200,
      {
        Items:
          offset === "0"
            ? filler
            : [
                { Type: "Movie", Id: "movie", Name: "Съёмки 2005" },
                { Type: "Folder", Id: "nested", Name: "Съёмки_2005" },
              ],
      },
    ];
  });
  api.mock.onGet(/\/Items\/last-folder\?/).reply(200, {
    Id: "last-folder",
    Name: "Last concert folder",
    Type: "Folder",
  });

  await render(
    <QueryClientProvider client={makeClient()}>
      <HomeFolders
        libraries={[
          { Id: "library", Name: "Archive", CollectionType: "homevideos" },
        ]}
      />
    </QueryClientProvider>,
  );
  await fireEvent.press(screen.getByRole("button", { name: "Archive" }));
  expect(mockPush).toHaveBeenLastCalledWith({
    pathname: "/(auth)/(tabs)/(home)/folders/[folderId]",
    params: { folderId: "library", title: "Archive", catalog: "library" },
  });
  await fireEvent.press(await screen.findByText("Last concert folder"));
  expect(mockPush).toHaveBeenLastCalledWith({
    pathname: "/(auth)/(tabs)/(home)/folders/[folderId]",
    params: { folderId: "last-folder", title: "Last concert folder" },
  });

  const search = screen.getByLabelText("library.search_folders");
  await fireEvent.changeText(search, "съем");
  await screen.findByText("Съёмки_2005");
  await fireEvent.changeText(search, "СЪЕМКИ 2005");
  expect(screen.queryByText("Съёмки 2005")).toBeNull();
  const folderRequests = api.mock.history.get
    .map(({ url }) => new URL(url!).searchParams)
    .filter((params) => params.get("includeItemTypes")?.includes("Folder"));
  expect(folderRequests).toHaveLength(2);
  expect(folderRequests[1].get("recursive")).toBe("true");

  await fireEvent.press(screen.getByRole("button", { name: "Съёмки_2005" }));
  expect(mockPush).toHaveBeenLastCalledWith({
    pathname: "/(auth)/(tabs)/(home)/folders/[folderId]",
    params: { folderId: "nested", title: "Съёмки_2005" },
  });
  await fireEvent.press(
    screen.getByRole("button", { name: "library.clear_folder_search" }),
  );
  expect(screen.getByText("Archive")).toBeTruthy();
});
