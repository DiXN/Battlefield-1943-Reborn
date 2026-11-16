# Clan tags (TekRantGaming, 16 Nov 2026)

The brief: "can be left blank or can have custom text, shows up next to a username in launcher, in game and in
scoreboard like this [clan]Username. Ban the use of Devs and Dev clan tags and profanity like NOG etc etc. Developers
TekRantGaming and ZALIAS are devs of this project and should have the clan tag of Dev." Then: "The Dev clan tag adds an
effect to the username in launcher and in game and on the scoreboard ... like World at War's cycl clan tag."

## Setting it

The launcher's name page (Settings > Account, or the setup's step 2) has two boxes in a row, as the name is shown:
CLAN TAG, then SOLDIER NAME. RB (F4 on a keyboard, or a click on a box) moves the typing between them; the keyboard,
the on screen keys (A, X to delete) and Start to save work in either. Blank is the default and means no tag.

**Limits: 2 to 4 letters or digits.**
- Four is World at War's own clan tag length, the look he asked for.
- Short keeps "[TAG]Name" readable where the game has little room: the kill feed and the name tags over soldiers.
  The longest is "[ABCD]" plus a 15 letter name, 21 characters.
- Letters and digits only: no spaces, dots, dashes or brackets, so "D.E.V", "D-V" or "[Dev]" cannot be typed, and the
  brackets are always ours.
- Case is kept as typed ("Dev", "ABC", "xYz").

## Who decides

The backend (server/plasma.py, "Clan tags"). The game only asks:
- `{"op": "tag", "name", "id", "tag"}` after each start's name check and whenever the player saves a tag. Only the
  player holding the name may set its tag: the same player id, from the address of the name check that gave the name,
  and for a protected name the game signed in for it. The answer: the tag worn, or `why` and the player's words.
- `{"op": "tags", "gid", "names", "me"}` every 10 s in a match: the tags of that server's players, and this player's
  own tag sent again (so a backend restart loses nothing for long). The scoreboard, players and leaderboard replies
  carry `tag` in their rows.

Each game draws everyone's tag from what the backend says, never from what another game claims, so a changed game can
only change what it shows itself. Tags live in memory on the backend (each game sends its own again), nothing on disk.

## Banned tags

server/text_filter.py and bf/src/text_filter.h (CheckTag) hold the same rules; tools/test_clan_tags.py `rules` checks
that the lists match and that both answer the same over about 4,800 tags. A tag is read several ways: lower case with
digits as letters (0 o, 1 i or l, 2 z, 3 e, 4 a, 5 s, 6 g, 7 t, 8 b, 9 g), with the digits dropped, with held letters
as one (DDEEV), and with padding at both ends dropped (xSSx).
- **reserved:** Dev inside any reading (Dev, DEVS, D3V, Devz, xDvx, Dev1, Deev) and dv, dvs, dvz; staff lookalikes
  (adm inside, mod, mods, staf, stff) and the original developers' names (EA, DICE).
- **profanity:** our name filter (text_filter.h NameAllowed), plus short slurs and swears a 4 letter tag can hold:
  his example NOG and its readings (N0G, NOGG), nig, ngr, nga, fag, fgt, kkk, fu, fuk, fck, stfu, ss, 88, 1488 and
  others (the lists in text_filter.h).
- **with_name:** the tag and the name read together ("[Shi]tHead").

The player's words: "That tag is kept for the game's developers. Choose another one.", "That clan tag is not
allowed. Choose another one.", "That clan tag and your name together are not allowed.", "A clan tag needs 2 to 4
letters or numbers, or leave it blank."

## The Dev tag

The team names of server/plasma.py TEAM_OWNERS (TekRantGaming and ZALIAS, with every variation, kept for their two
recovery code keys) always wear Dev, whatever their game sends: only the owner's signed game can hold one of those
names, so only they can show it. Nobody else can type Dev or a lookalike.

## Where it shows

- **Our pages** draw "[TAG]Name" themselves (clan_tags.cpp DrawTagged): Home's name box, the pause's scoreboard, the
  leaderboards, the friends list, and the chat and the names of who is talking (comms_ui.cpp). The tag is gold; the Dev
  tag's letters cycle through the colours.
- **In a match** (the kill feed, the name tags over soldiers, the game's own scoreboard): the game's name stays the 15
  character gamertag it signed in with (the server's roster finds each player by it), and each player's name in this
  game's own player list (the player manager at 0x82AF3AE4, +0x98 to +0x9C; a player's name at +8, a string the game
  grows itself) is written as "[TAG]Name" with the game's own string assign (sub_821D51C8), four times a second
  (clan_tags.cpp GameNamesTick). Nothing is cut: the string grows. The plain names are put back as the level goes.
  bf_clan_tags_in_game=false turns it off.
- **The Dev tag's colours in a match** (World at War's "cycl"): the game draws all its Flash text (menus, HUD, kill
  feed, scoreboard) through one text renderer; a run's colour is one word at renderer + 192 (red in the low byte,
  alpha in the high one), its glyphs are added one by one (sub_826AB240) and drawn by a flush (sub_826A5650).
  clan_tag_colours.cpp hooks the run (sub_826AB588: its lines are a circular list whose head, r5, sits in the text
  command) and, in a run holding "[Dev]", draws the glyphs so far and changes that colour before each letter of the
  whole "[Dev]Name" (the tag and the name after it, up to 15 letters, digits or underscores; his word: "the full
  clan tag and user name not just the clan tag"), then puts the run's own colour back: the letters cycle through the
  colours, a step apart, with the run's own alpha (fades). Our pages and the chat draw the whole "[Dev]Name" the same
  way (clan_tags.cpp DrawDevText). Runs without "[Dev]" are not touched (and nothing is allocated for them). bf_dev_tag_colours=false
  turns it off. Found with a read only search of the recompiled code (16 Nov 2026); BF_DEV_TEXTLOG=1 logs the first
  60 texts drawn.

Why not raise the 15 character limit or put the tag in the gamertag: the gamertag is the name the server's roster
matches (a game that finds another name than the backend's crashes joining, live_layers.cpp), the Xbox name buffers
are 16 bytes, and the tag would then come from the player's own game, not the backend.

## Tests

- tools/test_clan_tags.py rules <text_filter_tests.exe>; tools/test_clan_tags.py backend (127.0.0.5: set, change,
  clear, refused with why, another player cannot set it, Dev only for the team owner's signed game).
- bf/tests/text_filter_tests.cpp (the C++ rules, 136 checks).
- In game runs (16 Nov 2026, scratchpad scripts tagpage.ps1 and tagmatch.ps1: a backend on 127.0.0.8 with a test
  team name DevTester, two clients): tagpage2, tagnog1, tagd3v1 (the name page); tagmatch12 (the game's scoreboard
  shows [Dev]DevTester and [IWO]Kestrel); cycmatch2 (the kill feed: "[Dev]DevTester † [Dev]DevTester");
  cycmatch3 and cycmatch4 (the Dev letters' colours cycling in the scoreboard and the kill feed).
