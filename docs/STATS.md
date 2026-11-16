# Stats, leaderboards and achievements

TekRantGaming's request of 16 Nov 2026: new Stats & Rewards and Leaderboards pages with more stats, such as kills and
K/D, and achievements that pop up in game with a sound and are listed as in King Kong Recompiled. He chose these three
of six mockups (`runs/ui-designs/build_progress.py`, pictures in `runs/ui-designs/out/`):

- **S1 Service Record** for Stats & Rewards;
- **L1 Roll of Honour** for Leaderboards;
- **M1 Medal Case** for Achievements, with its popup.

## Where the numbers come from

Before this work, each player's own game added up its score events (`stat.<label>` in the settings) and sent its
totals (`stats_put`): score, time and captures, as the player's game said. Those totals cannot be checked, and the game
does not see deaths, wins or losses this way.

Now the dedicated server counts every player from the game's own bookkeeping (`bf/src/server_stats.cpp`). The functions
it reads were found in the generated code on 16 Nov 2026:

| Function | When the server calls it | What it gives |
|---|---|---|
| `sub_823F8960` | Every score award. r4 is the player, f1 the points, r6 the label. | Score, melee kills, kill assists, flag captures, defends, repairs, heals |
| `sub_82409708` | The kill handler. r4 is the killer (0 for the world), r5 the victim, r9 & 0xFF a headshot. | Kills, deaths, team kills, suicides, headshots, kills made from a vehicle |
| `sub_823F7518` | Each player's round result. r5 & 0xFF is set when the player lost. | Rounds, wins, losses |
| `sub_824144A0` | The event dispatcher. The type is at r4+136, the player at r4+144, a name at r4+156. | Spawn with its kit (17), into a vehicle (15), out of one (16), a player gone (1): the time in each kit and vehicle |

Other details:

- A player the game marks at +532 gets no score record, so it is left out too.
- The best kill streak is counted here, because the game keeps none.
- The first time the server sees each kit name, vehicle name and award label, it writes it to its log as `BF server
  stats:`, so how each one counts can be checked.

### How the counts reach the backend

Every 5 seconds the server sends one `player_stats` message holding each player's new counts. It goes one of two ways:

- **Official servers (the server manager):** the game writes the message as one line into `stats.txt` in the server's
  folder (`--bf_stats_file`). The host, `server/gmhost.py --stats-file`, sends each new line on its backend link.
- **The built in host** (`gm_host.cpp`) sends it with its status.

The backend (`server/player_stats.py`) keeps the counts only from official servers, so a community server cannot make
up anyone's stats. A test backend can accept every server with `--player-stats-from all`.

For each name it keeps totals for all time, this month and this week, plus results by map and the best streak. They
are saved to `player_stats.json` in the server package's data folder (`--player-stats`).

When an account moves to another name, its stats move with it.

### Checked in a match

TekRantGaming's kill test of 16 Nov 2026 used a local Wake Island server and the target soldier TargetDummy from this
session. The server counted all of these, and they reached the backend and the boards:

- three kills, each a headshot;
- the game's own awards, "kill" (+15) and "headshot" (+5);
- a best streak of 3;
- the target's three deaths;
- the time in each kit.

Earlier test runs, on the same local setup, counted these as well:

- deaths and suicides;
- the time in the landing boat (LCVP counts as Boat);
- spawns, with the kit.

The kits go by the engine's names:

| Engine name | Kit |
|---|---|
| Support | Infantry |
| Assault | Rifleman |
| Recon | Scout |

Not seen in a match yet: flag captures, round wins and losses, kills from vehicles, heals and repairs.

### What is not counted

- **Revives:** Battlefield 1943 has none.
- **Shots fired and hit:** not counted, so there is no accuracy.
- **Heals and repairs:** counted as the game's award events. They may come one per amount healed or repaired rather
  than one per action. This has not been checked in a match yet.

### Backend requests

| Request | Reply |
|---|---|
| `{"op": "leaderboard", "sort", "period", "me"}` | The top 50 by `score`, `kills`, `kd` (50 kills or more), `captures`, `seconds`, `headshots`, `vk_plane`, `vk_tank` or `wins`, for `all`, `month` or `week`, with the asker's own row and place. Players the servers have not counted yet fill in from their own totals, all time only. Without these fields the reply is the old one. |
| `{"op": "player_card", "name"}` | One player's record: every count, by map, the best streak, and their place on each board. |
| `{"op": "achievement_share"}` | How many players with an account have each achievement, counts only. |

## The pages

All three are in `bf/src/game_menu_progress.inc`.

### Stats & Rewards (S1)

- The soldier, their rank, the next rank and the rank ladder.
- Eight numbers: kills, deaths, K/D, score, flags, rounds won, headshots and repairs.
- Time by kit and by vehicle, and results by map.
- The latest rewards, the achievement total, and the next achievements to earn.

Each number is the larger of the servers' count and this PC's own score events, because the servers began counting
later. Deaths, K/D and rounds won show a dash until the servers have counted the player.

### Leaderboards (L1)

- Folder tabs for Score, Kills, K/D, Flags and Time, switched with LB and RB.
- X switches between all time, this month and this week.
- Every column is shown. Your own row is pinned under the table, and the chosen soldier's card is on the right.

### Achievements (M1)

- The game's 12 achievements in two columns. Unlocked ones are first, with when they were unlocked.
- Locked ones are grey with a padlock. Where our counts can tell, they show how far along you are. These are our own
  counts: the game decides the unlock.
- Each one shows how many players have it.
- X filters all, unlocked or locked. Y shows a test popup.

## Achievements and the popup

The game unlocks an achievement through XGIUserWriteAchievements, which the SDK's AchievementManager handles: it saves
the unlock to `user/achievements/5841097E.toml` and tells the app. `bf/src/achievement_toast.cpp` replaces the SDK's
toast and copies King Kong's behaviour:

- A paper slip slides in at the top right in 0.35 s, holds for 5 s, then slides out.
- It shows the achievement's own picture, its name in the game's headline font, its words and its gamerscore.
- Several unlocks queue one after another.
- A short chime plays through SDL: King Kong's chime, made at run time. A player's own `user/sounds/achievement.wav`
  (16 bit PCM) plays in its place.

The pictures come from the game's title data and are written once to `user/cache/extracted/achievements/<id>.png`.

There are two settings under Settings > Audio: Achievement popups (`bf_achievement_popups`) and Achievement sound volume
(`bf_achievement_volume`).

A protected name already sends its unlocked ids with its signed stats, and a recovery code brings them back on a new PC.

## Tests

- `python -I tools/test_player_stats.py` checks the module and the backend's requests. It runs on 127.0.0.5.
- Dev switches:
  - `BF_DEV_ACHIEVEMENT_TEST="<s>:<id>,..."` shows a test popup at that time.
  - `BF_DEV_ACHIEVEMENT_WAV=<file>` writes the chime to that file instead of playing it, so tests stay silent.
