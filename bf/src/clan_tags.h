// Clan tags (TekRantGaming, 16 Nov 2026: "can be left blank or can have custom text, shows up next to a username in
// launcher, in game and in scoreboard like this [clan]Username"). clan_tags.cpp; the backend's side is server/plasma.py
// (put_tag, tag_for), the rules text_filter.h CheckTag.
//
// The backend decides every tag: this game sends its player's own (settings clan_tag) and shows everyone's as the
// backend says, never as another game claims. The game's own name stays the 15 character name it signed in with (the
// gamertag the server's roster knows); the tag is put in front of it only where names are shown: our pages draw
// "[TAG]Name" themselves (DrawTagged), and in a match each player's name in this game's own player list is written as
// "[TAG]Name" (GameNamesTick), so the game's kill feed, name tags and scoreboard show it. That copy is a string the
// game grows as needed (begin, end, capacity), so a long name with its tag is never cut.
//
// The developers' tag, Dev (the team names of server/plasma.py TEAM_OWNERS: TekRantGaming and ZALIAS): the whole
// "[Dev]Name" is one colour at a time, each new colour sweeping in from left to right, on our pages (DrawDevText) and
// in the game's own text (clan_tag_colours.cpp) (TekRantGaming: like World at War's "cycl" clan tag, "the full clan
// tag and user name"; 16 Nov 2026 it moved from a rainbow along the letters to this).
#pragma once

#include <cstdint>
#include <string>

struct ImDrawList;
struct ImFont;
struct ImVec2;

namespace bf1943::clan {

// The tag in the settings file (the name page's field; "" none): sent with the next request.
void SetSavedTag(const std::string& tag);
// Sent to the backend for this name (the one the name check gave) and the XUID's low word, on a thread of its own.
// After each start's name check, and when the player saves another tag.
void SendOwnTag(const std::string& name, uint32_t online_id);
// The tag this player wears online as the backend said ("Dev" for the developers); until it answers, the saved one.
std::string OwnTag();
bool OwnTagConfirmed();
// The backend's words when it refused the saved tag (once; "" none).
std::string TakeRefusal();

// Everyone's tags as the backend said: from the scoreboard, players and leaderboard rows (Note) and the match's own
// requests (MatchTick, every 10 s while in a match: this server's players, and this player's own tag again).
void Note(const std::string& name, const std::string& tag);
std::string TagOf(const std::string& name);    // "" none
std::string Tagged(const std::string& name);   // "[TAG]Name", or the name
std::string Strip(const std::string& shown);   // "[TAG]Name" back to the name (a name the game shows, tagged by us)
void MatchTick(const std::string& gid, const std::string& own_name, uint32_t online_id);

// The pad read (auto_join.cpp, the game's thread): each player's name in the game's player list as "[TAG]Name".
// `ctx` for the game's own string assign. Restored to the plain names when the level goes (level_loaded false).
void GameNamesTick(void* ctx, uint8_t* base, bool level_loaded);

// Our pages: "[TAG]Name" at `at`, the tag in `tag_ink` (the Dev tag cycling its colours), the name in `ink`. Returns
// the width drawn. `tag` "" draws the name alone.
float DrawTagged(ImDrawList* d, ImFont* font, float size, const ImVec2& at, unsigned ink, const std::string& name,
                 const std::string& tag, unsigned tag_ink, float alpha = 1.0f);
float TaggedWidth(ImFont* font, float size, const std::string& name, const std::string& tag);
// A developer's whole tagged name ("[Dev]Name") in the Dev colours of the moment, letter by letter (with a drop
// shadow for the chat over the match). Returns the width drawn.
float DrawDevText(ImDrawList* d, ImFont* font, float size, const ImVec2& at, const std::string& text, float alpha,
                  bool shadow);
// The colour of letter `i` of a developer's tagged name now (World at War's "cycl": the whole name one colour, the
// next one travelling over it from left to right, a new one every second).
unsigned DevColour(int i, double seconds, float alpha = 1.0f);
bool IsDevTag(const std::string& tag);

}  // namespace bf1943::clan
