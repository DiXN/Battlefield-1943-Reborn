// Each player's stats on our dedicated server (TekRantGaming, 16 Nov 2026: "expand upon what stats we collect like
// kills and K/D"; server_stats.cpp): counted from the game's own bookkeeping on the server, so no player's game can
// make up its own, and sent to the backend (server/player_stats.py) with the server's status every 5 seconds.
#pragma once

#include <string>

namespace bf1943 {

// The counts since the last call, as the backend's "player_stats" message ({"op": "player_stats", "rows": [...]}),
// or "" when nothing changed. `map` is the map now running (its title, server_rotation.cpp). gm_host.cpp sends it.
std::string TakeServerStatsMessage(const std::string& map);

// From the server's tick: every 5 s, the counts as one line into bf_stats_file for our host (server/gmhost.py
// --stats-file sends each line to the backend). Nothing without the file (our built in host, gm_host.cpp, sends them).
void ServerStatsTick();

namespace server_stats {
// `n` more of `what` (a player_stats.py count: "kills", "deaths", "captures", ...) for the player `name`.
void Add(const std::string& name, const char* what, long long n = 1);
}  // namespace server_stats

}  // namespace bf1943
