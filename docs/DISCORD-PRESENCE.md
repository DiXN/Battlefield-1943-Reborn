# Discord Rich Presence

A player's Discord profile shows what they are doing in Battlefield 1943 Reborn (TekRantGaming, 15 Nov 2026).

| Where the player is | Discord shows |
|---|---|
| The game starting | Starting the game |
| The launcher (before the game runs) | In the launcher |
| The server browser | Browsing servers |
| The main menu | In the main menu |
| Joining, loading a map | Joining Wake Island, then the server's name |
| In a match | Wake Island · Conquest, then US Marines · Reborn All Maps 1 (12 of 24), time elapsed |
| The tutorial | Playing the tutorial |

The large picture is the Reborn logo in the menus, and the map in a match with the logo as the small picture. The
elapsed time counts from the game's start in the menus, and from the map's start in a match (a server's map rotation
starts it again).

Never shown: a server's address (a name holding one is left out), another player's name (a match a player hosts is
"Private match", never "Name's match"), the player's own name. Server names pass the profanity filter.

## The setting

Settings, Gameplay, Player: **Discord status**, On by default (the cvar `bf_discord_status`). Off clears the presence
at once and closes the connection.

The application id is the cvar `bf_discord_app_id` (in `bf/src/discord_presence.cpp`; also settable in
`bf1943.toml` or on the command line). Its default is TekRantGaming's application "BF 1943 Reborn",
1571686541817020506 (16 Nov 2026). Empty means no presence at all: the thread is never started.

## Design

`bf/src/discord_presence.cpp` talks to the Discord app on the same PC over its local IPC, the same protocol the old
discord-rpc library and the Social SDK use underneath:

- Windows: the named pipe `\\.\pipe\discord-ipc-0` to `-9`.
- Linux: the Unix socket `discord-ipc-0` to `-9` in `$XDG_RUNTIME_DIR`, `$TMPDIR`, `$TMP`, `$TEMP` or `/tmp`, and in the
  folders Flatpak Discord (`app/com.discordapp.Discord/`), Snap Discord (`snap.discord/`) and Vesktop use under them.
- A frame is the opcode and the JSON length (4 bytes each, little endian), then the JSON. Handshake
  `{"v":1,"client_id":...}`, answered with a READY dispatch; then `SET_ACTIVITY` commands. Pings are answered.

Why not a library: the Discord Social SDK and the old Game SDK are DLLs (a new DLL needs a Microsoft reputation
submission, and antivirus has flagged our extra libraries before), the Social SDK wants an account link flow we do not
need, and discord-rpc is archived and unmaintained. The IPC is about 500 lines of our own code and ships nothing new.

It never touches the game:

- One detached thread, asleep on a condition variable; the menus hand it the state a few times a second
  (`GameMenu::NotePresence`) and only a change wakes it.
- No Discord: one log line (per outage), then a quiet look every 5 to 15 seconds. A Discord restart reconnects the same
  way. A handshake Discord refuses (an unknown application id) is logged and tried again after 5 minutes.
- Rate limit (Discord allows 5 updates in 20 s): a change must hold 1 s, then goes at least 4 s after the last update;
  a change of only the player count waits 15 s.
- Every pipe or socket call has a 2 s timeout (overlapped I/O on Windows, `SO_SNDTIMEO`, `poll` and `MSG_NOSIGNAL` on
  Linux), so a hung Discord cannot hold the thread. The pipe closes with the process, and Discord clears the presence.
- Dedicated servers never start it.

Where the state comes from: the menu's page and the menu driver's stage (`auto_join.cpp`), the map from the level's
capture point names (`capture_points.cpp`, so rotations show), the team from `comms::Team()` (team 1 owns the
level's ALLIED carrier: US Marines; team 2 the AXIS one: Imperial Japanese Navy), the player count from the game's
player list (`MatchPlayerCount`, `map_players.cpp`) and the server's limit from the server list.

## Testing

`tools/fake_discord_ipc.py` stands in for Discord and records every frame. A dev build looks only at
`BF_DEV_DISCORD_IPC=<pipe name or socket path>` when it is set (a running Discord holds `discord-ipc-0`):

    python tools/fake_discord_ipc.py --name bf-fake-discord --log runs/discord1/frames.jsonl
    set BF_DEV_DISCORD_IPC=bf-fake-discord
    bf1943.exe --bf_discord_app_id=123456789012345678 ...

`--restart-after S --away A` drops the connection after each handshake and stays away A seconds (a Discord
restart); `--refuse` answers like Discord does for an unknown application id. Dev builds log every update they send
("BF discord: sent ...").

## The Developer Portal (TekRantGaming's steps)

1. Open https://discord.com/developers/applications and sign in with the account that should own the game's entry.
2. New Application. Name it **Battlefield 1943 Reborn** (Discord shows "Playing" and this name). Accept the terms.
3. General Information: copy the **Application ID** (a long number). Optionally set the App Icon to `logo.png`.
4. Rich Presence, Art Assets: upload these five pictures, keeping their names as the asset keys:
   `logo`, `map_wake`, `map_guadalcanal`, `map_iwo`, `map_coral`, from `runs\discord-art` on the dev PC (not in
   git). Discord can take a few minutes to show new art. `logo` is the stencil plate (a big stenciled 43 and a red
   REBORN stamp), drawn from scratch by `tools/make_discord_icons.py` with the free Oswald font, so it holds no EA
   art; it is also the application's icon. The map pictures are crops of the game's own menu art
   (`tools/make_discord_art.py`).
5. Send the Application ID to the developers; it goes in as the default of `bf_discord_app_id`. Until then the
   presence stays off.
