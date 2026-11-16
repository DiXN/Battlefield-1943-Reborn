# Team balance on our dedicated servers

TekRantGaming, 15 Nov 2026: matches ended 7 Marines against 3 Japanese. Built in bf/src/team_balance.cpp (the game
server), server/gmhost.py (telling the players) and server/manager.py (server.json).

## What the game did on its own

Every level has the game's AutoTeamEntity (from 6 players, a gap of more than 2), but it only picks the team of a
player who arrives: the smaller one (sub_8239A210). Nothing moved a player already in the match, so teams drifted
apart as players left the losing side, friends joined each other and players used Change Team to join the winners
(the game refused a switch only to a team at its cap of 12).

## What the server does now

- **When:** all through a round. The teams count as uneven when one has `balance_gap` (2) or more players more, with
  `balance_min_players` (4) or more on the server. After `balance_delay` (20) seconds of that, the bigger team gets
  one chat warning, and from then on a player of it who has died moves, one at a time (2 s apart), until the gap is
  under `balance_gap`. Nothing moves between a round's end and the next level.
- **Who:** only a player who is down (at the deploy screen): nobody is killed to balance. First a player who used
  Change Team to join the bigger side this round, then one in no squad, then the newest arrival. A player moved is
  left alone for 15 minutes.
- **Cost:** none. The move is the game's own (as its join with a friend: no team, no squad, then sub_82399AE8 seats
  the player on the other team with a squad there). No death, no ticket, no score event.
- **Change Team:** with `balance_block_stacking` a player's own switch that would leave a gap of `balance_gap` or more
  is refused (the server's handling of selectTeam, sub_8239DE08). The pause's Change Team then does not kill the
  soldier either (it only does when the team really changed).
- **Players are told** (gmhost.py, from the server's `balance.txt`): a chat line from "Server" to everyone ("X was moved
  to Japan to even the teams"), a message box for the player moved ("You were moved to Japan to even the teams. No
  points lost."), a message box for a refused switch, and the warning as a team chat line. Chat lines show in every
  game with chat; the box in every game since v0.4.0.

## The scoreboard's team names (players' games)

The level's HUD movie (hud_root_cq) makes `_root.factionSIDs`, the scoreboard's [friendly, enemy] headings (USMC,
IJN), once as it loads, from the game's `extern.teamFactions`; the in-match scoreboard (igm_mp_cq, updateBackground)
sets its flags from `extern.playerTeam` every time but its headings from that array. So after any change of team
(Change Team, or a move by the balance) the flags were right and the names the old ones. bf/src/auto_join.cpp
TeamNamesTick: when the player's team (comms ReadTeam) differs from the one the names were made for, the game's own
`reverse()` runs on `_root.factionSIDs` and on the end of round screen's `factionSIDsWIN` and `factionSIDsLOSE`
(the `reverse:` Flash call, arrays only). Runs sb_names1 (names right after one switch and after three).
Run sb_round3 and sb_round4 (a switch to the US, the round ended by BF_DEV_TICKETS=95,84,1 and a suicide, then
Guadalcanal by the rotation): the end of round screen's scoreboard right (USMC over the US flag, "your team lost");
on Guadalcanal the game had put the player on Japan for the new round and the scoreboard showed IJN over the Japanese
flag (the level's HUD makes its names again; the count of swaps starts again at each Loading complete). The WIN and
LOSE pair is written to the victory view's outcomeText, which the end of round menu covers in the same frame (X only
moves between the scoreboard and the top players screen), so it is not seen; it is kept right all the same.

## Checked after a move (16 Nov 2026, runs sc_move1 and sc_move2, two players)

The moved player keeps their score: -10 before the move stayed -10 after it, and was -20 after a death on the new
team. The scoreboard held during play (Back on a controller; BF_MATCH_SCRIPT back/4) shows the moved player in the
new team's column on both players' screens. The end of round screen shows them on the new team's side, with that
score, on both screens ("your team lost" for the moved player, "won" for the other).

## server.json (each server)

| key | default | range |
| --- | --- | --- |
| `team_balance` | true | the Server Manager's "Team balance" checkbox |
| `balance_gap` | 2 | 2 to 6 |
| `balance_min_players` | 4 | 2 to 24 |
| `balance_delay` | 20 (seconds) | 5 to 300 |
| `balance_block_stacking` | true | |

The manager passes them as `--bf_team_balance`, `--bf_balance_gap`, `--bf_balance_min_players`, `--bf_balance_delay`,
`--bf_balance_block_stacking` and `--bf_balance_events=<server folder>\balance.txt` (gmhost.py `--balance-file`, the
same file). A dedicated server started any other way (a private or LAN match) has balance off (`bf_team_balance`
false by default).

## Testing

Local only: tools/run_dedicated_test.ps1 -BackendHost 127.0.0.5 with -PeerPad driver, and the server's switches in
-ServerArgs (a small bench: `--bf_balance_min_players=2 --bf_balance_delay=5`). BF_DEV_FOLLOW_TEAM=1 on the main client
(BF_PEER_CLEAR=BF_DEV_FOLLOW_TEAM) makes it join the peer's team, which makes the teams 2 against 0. BF_DEV_BALANCE_DUMP=1
logs each server player's object once. The host's part: python -I server/test_comms.py.
