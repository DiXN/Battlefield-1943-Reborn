// Reborn's own achievements (TekRantGaming, 16 Nov 2026: "add some custom achievements into our achievements, assign
// gamer score to them and give them custom images"), beside the game's 12:
//   101 I am reborn      10 G  loading into your first multiplayer match (any server, a custom match too)
//   102 I am forever     20 G  loading into a match on a community server (not a Reborn one, not your own match)
//   103 I am inevitable  30 G  loading into a custom match you are hosting
// "Loading into" is the soldier in the level (on foot or in a seat), not the join's pick (game_menu.cpp
// NoteRebornAchievements).
//
// They sit in the SDK's own catalog (AchievementManager::RegisterAchievement, made for a recomp's own entries), so
// everything the game's 12 have, ours have: the unlock saved with theirs (<user>\achievements\5841097E.toml), the
// popup and chime (achievement_toast.cpp), the Achievements page and Stats & Rewards, and the account's awards that
// the backend counts for "how many players have it". The ids are 101 and up: the game's title data has 1 to 12, and
// the backend counts every id from 1 to 999 (plasma.py achievement_share), so no backend change is needed.
//
// The pictures are ours, drawn by tools/make_reborn_achievements.py into data/achievements/<id>.png next to the exe
// (64 x 64 RGBA PNGs, as the game's own pictures are).
#pragma once

#include <cstdint>
#include <filesystem>

namespace bf1943::reborn_achievements {

inline constexpr uint32_t kReborn = 101, kForever = 102, kInevitable = 103;

// One of ours (their image ids are their ids).
bool IsOurs(uint32_t id);

// Puts ours into the SDK's catalog once the game's own are loaded (it replaces the whole catalog when it loads the
// game's title data). Cheap: call it before anything that asks the catalog. True once ours are there.
bool Ensure();

// The picture for one of ours (data/achievements/<id>.png next to the exe); empty for any other.
std::filesystem::path IconFile(uint32_t image_id);

// Unlocks one of ours, with its popup and chime; `why` goes to the log. True when it was not unlocked before.
bool Unlock(uint32_t id, const char* why);

}  // namespace bf1943::reborn_achievements
