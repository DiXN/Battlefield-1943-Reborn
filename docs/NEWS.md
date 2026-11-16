# The news window

TekRantGaming, 15 Nov 2026: a news window that opens on the main menu at every launch, like the news screens other
games have for their shops and battle passes, with the state of the servers, upcoming things and our own news, in
text, pictures and YouTube videos that play by themselves. His pick of two mockups: N1 "Dispatch Board"
(`runs/ui-designs/build_news.py`, `out/n1_news.png`).

Code: `bf/src/news.cpp` (the feed and the pictures), `bf/src/news_video.cpp` (the YouTube player),
`bf/src/game_menu_news.inc` (the window), the NEWS row in `game_menu_home.inc`.

## How to post news (for TekRantGaming)

The news is one file, `news.json`, on its own branch `news` of the public repository
TekRantGaming/Battlefield-1943-Reborn. The game reads it from
`https://raw.githubusercontent.com/TekRantGaming/Battlefield-1943-Reborn/news/news.json` at every start. Posting needs
no update of the game, and no server address appears anywhere.

1. The branch exists since 16 Nov 2026 (public 5441b047): an orphan branch with only `news.json` and a README, so
   none of the source is on it. Its first feed is a copy of `runs/news_branch/news.json` (five items: the window
   itself, where to play, Discord, reporting bugs, the public source; no pictures, no video, no dates coming up).
2. Edit `news.json` on the `news` branch in GitHub's editor (the pencil) and commit. The game sees it within about
   five minutes (GitHub's cache), at the next start.
3. Pictures: upload them to the same branch (for example `images/coral-night.jpg`), then use their raw address:
   `https://raw.githubusercontent.com/TekRantGaming/Battlefield-1943-Reborn/news/images/coral-night.jpg`.
   JPEG or PNG, 16:9 (1280 x 720 is plenty), under 8 MB. Use a new file name for a new picture: the game keeps each
   address's picture once downloaded.
4. Videos: put the YouTube id (the 11 characters after `v=`) or the whole YouTube address in `youtube`. The video
   must allow embedding (the default on YouTube).
5. To check a feed before posting it, save it on your PC and start the game with
   `--bf_news_feed="C:\path\to\news.json"`. Pictures named without an address (`"image": "coral.jpg"`) are read from
   beside that file. A mistake in the file shows in the log: `BF news: the feed ... does not read: a comma or ]
   expected (a missing comma?) at line 12`. With a broken feed the game simply shows no news.

Example:

```json
{
  "edition": "15 Nov 2026 edition",
  "status": "US Central: maintenance 02:00 UTC",
  "items": [
    {
      "id": "wake-full-house",
      "kind": "video",
      "title": "Full house on Wake",
      "text": "Every Reborn server filled up on Saturday night for the first time.",
      "date": "2026-11-15",
      "youtube": "https://www.youtube.com/watch?v=XXXXXXXXXXX",
      "lead": true
    },
    {
      "id": "coral-night-21-nov",
      "kind": "event",
      "kicker": "Upcoming · Sat 21 Nov",
      "title": "Community night on Coral Sea",
      "text": "All Maps 1 runs Coral Sea only from 19:00 UTC. Bring a squad.",
      "date": "2026-11-14",
      "image": "https://raw.githubusercontent.com/TekRantGaming/Battlefield-1943-Reborn/news/images/coral.jpg",
      "until": "2026-11-21",
      "priority": 5
    },
    {
      "id": "patch-0-4-7",
      "kind": "update",
      "title": "Smoother Iwo Jima, fewer stalls",
      "text": "Server fixes for full servers. Read the full list.",
      "date": "2026-11-17",
      "link": "https://github.com/TekRantGaming/Battlefield-1943-Reborn/releases",
      "button": "Read the notes",
      "priority": 4
    }
  ],
  "upcoming": [
    { "date": "2026-11-17", "text": "Update 0.4.7 out for everyone" },
    { "date": "2026-11-21", "text": "Community night, Coral Sea, 19:00 UTC" },
    { "label": "Dec", "text": "Leaderboards return to the menu" }
  ]
}
```

## The format

Top level (all optional):

| Field | What it does |
|---|---|
| `edition` | The header's line after NEWS ("15 Nov 2026 edition"). Empty: the newest item's date. |
| `status` | A short line under the servers box ("US Central: maintenance 02:00 UTC"). |
| `items` | The stories, below. |
| `upcoming` | The COMING UP list, below. |

Each item (`title` is the only one needed):

| Field | What it does |
|---|---|
| `id` | Unique and never reused: "new since your last visit" counts by it. Default: the title. |
| `title` | The headline (the game's own lettering in the lead, shrunk to fit). |
| `text` | The story. The lead shows about four lines, a card two. |
| `kind` | `video`, `update`, `event` or `news`: the small red line's word when there is no `kicker`. |
| `kicker` | The small red line over the title ("Patch notes · 0.4.7"). Default: the kind and the date. |
| `date` | `YYYY-MM-DD`, shown as 15 NOV. Newer first among items of the same priority. |
| `image` | A picture's https address on GitHub (`raw.githubusercontent.com`, `github.com`, `user-images.githubusercontent.com`, `objects.githubusercontent.com`) or YouTube's picture hosts. Others are left out. A video without an image shows its YouTube thumbnail. |
| `youtube` | A YouTube id or address: the video plays in the lead. |
| `link` | An https address the green button opens in the browser. |
| `button` | The green button's label (default OPEN for a link, WATCH ON YOUTUBE for a video). |
| `priority` | A number, higher first. |
| `lead` | `true`: this item is the lead story (else the first after sorting). |
| `from`, `until` | `YYYY-MM-DD` (UTC): the item shows only from / until that day. |

Each `upcoming` entry: `text`, and `date` (`YYYY-MM-DD`: shown as TUE 17, gone once past) or `label` ("Dec",
"Soon"). Soonest first, the undated ones last. At most four show. With no upcoming entries the cards take the whole
row (four instead of three).

At most 16 items are read. The servers box is not in the feed: it is live from the server list (the Reborn servers,
busiest first, with the players playing; "CLOSED" with the backend's maintenance notice while one is up).

## Where the feed comes from, and why

A JSON file in the public GitHub repository, on its own branch, read from raw.githubusercontent.com:

- posting is a commit in GitHub's editor, no client update and no deploy;
- the address is GitHub's, so no server address of ours is in the game or in public text;
- the branch is apart from `main` and `in-dev`, so releases and merges never touch it, and the news never shows up in
  a release's history;
- the Kong Worker would have needed a deploy through Kong's chat for each post, and an address of ours in the game.

`bf_news_feed` (a cvar, any build) names another feed: an https address, `http://127.0.0.1:<port>/...` for tests, or a
file on the PC (a preview). A feed that reads is kept in the user folder (`cache/news/feed.json`); pictures in
`cache/news/pic_<hash>.img`, each fetched once, the ones no feed names any more removed.

## When it opens

- Once a run, on the first main menu, when this run's feed has arrived (it is fetched on a worker at the main menu;
  a few seconds; each step of the request gives up after 8 s) and the player is still on the main menu, within 30 seconds of reaching it.
- Never over another pop-up. After an update, What's new comes first and the news opens when it is closed; the new
  version pop-up and the backend's notices also come first (their time does not count against the 30 seconds).
- Never from the cached feed: without internet, or with a feed that does not read, the menu is exactly as before.
- Never in a scripted test run (`BF_DEV_NEWS=1` asks for it there).
- The NEWS row on the main menu opens it again at any time (this run's feed, else the cached one; without either,
  the menu says the news could not be reached). The row shows "3 NEW" while there are items not seen yet.

Nothing in it blocks the start: the feed and the pictures are fetched on a worker, the cached feed is read when the
main menu is reached, and the video's browser starts only when the window shows a video.

## Controls

| | Controller | Keyboard | Mouse |
|---|---|---|---|
| Move between the lead and the cards | stick, D-pad, LB/RB | arrows, Q/E | hover |
| Lead: open the link or watch on YouTube | A | Enter | the green button, the thumbnail |
| Card: put it in the lead (its video plays there) | A | Enter | a click |
| Mute or sound on | Y | R | MUTE |
| Close | B | Esc (also while the player has the keyboard) | CLOSE, or a click outside the window |
| Scroll the cards (more than fit) | LB/RB | Q/E | the wheel |

The Steam Deck uses the controller column. The choice of mute is kept (`news_muted` in the settings).

## The video

YouTube's own embedded player only (the IFrame Player API, `youtube-nocookie.com`): no stream is ever downloaded or
extracted, which YouTube's terms forbid, and nothing is drawn over the player.

**Windows:** a WebView2 view over the game's window at the lead's place (`news_video.cpp`). WebView2's runtime is part
of Windows 10 and 11. The game loads Microsoft's `WebView2Loader.dll` from its own folder at run time
(`bf/third_party/webview2`, NuGet Microsoft.Web.WebView2 1.0.3179.45). That DLL is **signed by Microsoft**, so it
needs no reputation submission of ours (the code signing note). It ships in the player zip only.

- Autoplay with sound: a browser plays sound only after a click in the page, so the view starts with
  `--autoplay-policy=no-user-gesture-required`. Tested: the player reports "state 1 sound on" with no click.
- The player page is the game's own (`cache/news/web/player.html`), served under
  `https://news.bf1943reborn.example/` so the embed has a page address (YouTube's error 153 without one).
- Links out of the player (its logo, its title) open in the browser; the page itself never navigates away.
- The menu music pauses while the video plays with its sound (not heard in a test yet: test runs have no music).
- The view takes the keyboard on a click; the game takes it back at once (the controller and the menu read the game's
  window), and Esc inside it still closes the window.
- No WebView2 runtime, no DLL, or a failure: the thumbnail instead (below), for the rest of the run.
- Not tested: the exclusive Fullscreen mode (tests never go fullscreen). Borderless and windowed are tested.

**Linux and the Steam Deck:** the thumbnail with YouTube's play mark; A or a click opens the video on YouTube in the
browser. An embedded browser in the AppImage (WebKitGTK) would add well over 100 MB, would need its own window over an
SDL window that Wayland and the Deck's Game Mode (gamescope) do not allow, so it is not done.

## Tests

- Feed: `runs/news_feed_test/news.json` (pictures beside it, an expired item, a past upcoming entry, a picture from
  a host not allowed, a second video as a card), `broken.json` (a missing comma).
- Switches: `--bf_news_feed=<file or address>`, `BF_DEV_NEWS=1` (open it in a scripted run), `BF_NEWS_SILENT=1` (the
  view's output silent while the page plays with its sound: autoplay with sound proved without a sound on the PC),
  `BF_NEWS_NO_VIDEO=1` (the thumbnail, as on Linux).
- Runs of 15 Nov 2026 (bf-dev, background windows, test backend 127.0.0.5): news3 (opens by itself, video plays with
  sound), news4 (cards, a card into the lead, the second video, Mute, Esc, reopened from the NEWS row, closed by a
  click outside), news5 (What's new first, then the news; the thumbnail), news6 (feed not reachable: the menu as
  before, the NEWS row says so), news7 (broken feed: logged, no news).

Settings it keeps: `news_seen` (the seen items, hashed, the newest 64), `news_muted`.
