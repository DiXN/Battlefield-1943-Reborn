// Discord Rich Presence (discord_presence.cpp; docs/DISCORD-PRESENCE.md): what the player is doing shows on their
// Discord profile. Spoken to the Discord client on this PC over its local IPC (the named pipe discord-ipc-N on
// Windows, the socket of that name on Linux), with no Discord library: nothing new ships.
#pragma once

#include <string>

namespace bf1943::discord {

// What the game is doing now, as the menus know it (game_menu.cpp NotePresence, a few times a second).
struct Presence {
  enum class Where { kNone, kStarting, kLauncher, kMenu, kBrowser, kJoining, kMatch, kTutorial };
  Where where = Where::kNone;
  std::string map;     // the map's title ("Wake Island"); empty: not known
  std::string server;  // the server's name as shown to others; empty: not shown (a player's own match, an address)
  bool private_match = false;  // a player hosted match: "Private match" in place of a name
  int team = 0;                // 1, 2; 0 not known
  int players = 0, max_players = 0;  // 0: not known
  bool operator==(const Presence&) const = default;
};

// Cheap: compared with the last and handed to the presence thread when it changed. Players' games only (never a
// dedicated server); the thread starts at the first call. Nothing happens without an application id
// (bf_discord_app_id), with the Settings toggle off (bf_discord_status), or without a Discord client.
void Note(const Presence& p);

// A server's name fit for a profile: censored (text_filter.h), and none when it holds an address.
std::string ShownServerName(const std::string& name);

}  // namespace bf1943::discord
