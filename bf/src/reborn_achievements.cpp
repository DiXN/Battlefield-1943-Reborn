// Reborn's own achievements (reborn_achievements.h).

#include "reborn_achievements.h"

#include <atomic>
#include <chrono>

#include <rex/logging.h>
#include <rex/system/kernel_state.h>

namespace bf1943 {
std::filesystem::path ExecutableDir();  // shader_prep.cpp: the folder of bf1943.exe
}

namespace bf1943::reborn_achievements {
namespace {

struct Ours {
  uint32_t id, gamerscore;
  const char* label;
  const char* description;
};
// Our own words. The game's locked text is its unlocked text, and so is ours.
constexpr Ours kOurs[] = {
    {kReborn, 10, "I am reborn", "Load into your first multiplayer match."},
    {kForever, 20, "I am forever", "Load into a match on a community server."},
    {kInevitable, 30, "I am inevitable", "Load into a custom match that you are hosting."},
};

std::atomic<bool> g_ready{false};
std::atomic<int64_t> g_checked_ms{0};

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

bool IsOurs(uint32_t id) {
  for (const Ours& o : kOurs)
    if (o.id == id) return true;
  return false;
}

bool Ensure() {
  // Checked again every 2 s once there (the SDK's catalog is replaced only when it loads the game's title data).
  const int64_t now = NowMs();
  if (g_ready.load() && now - g_checked_ms.load() < 2000) return true;
  g_checked_ms = now;
  auto* k = REX_KERNEL_STATE();
  if (!k) return g_ready = false;
  auto& m = k->achievements();
  bool missing = false;
  for (const Ours& o : kOurs) missing |= !m.FindAchievement(o.id).has_value();
  if (!missing) return g_ready = true;
  // Only after the game's own are in: until then a register would be wiped by the SDK's load of them.
  if (m.ListAchievements().empty()) return g_ready = false;
  for (const Ours& o : kOurs) {
    rex::system::AchievementInfo info;
    info.id = o.id;
    info.label = o.label;
    info.description = o.description;
    info.unachieved_description = o.description;
    info.image_id = o.id;
    info.icon_path = IconFile(o.id).string();
    info.gamerscore = o.gamerscore;
    m.RegisterAchievement(std::move(info));
  }
  REXLOG_INFO("BF achievements: Reborn's own 3 added beside the game's ({} in all; unlocked: {}, {}, {})",
              m.ListAchievements().size(), m.IsUnlocked(kReborn), m.IsUnlocked(kForever), m.IsUnlocked(kInevitable));
  return g_ready = true;
}

std::filesystem::path IconFile(uint32_t image_id) {
  if (!IsOurs(image_id)) return {};
  return ExecutableDir() / "data" / "achievements" / (std::to_string(image_id) + ".png");
}

bool Unlock(uint32_t id, const char* why) {
  if (!IsOurs(id) || !Ensure()) return false;
  auto* k = REX_KERNEL_STATE();
  if (!k || k->achievements().IsUnlocked(id)) return false;
  const auto result = k->achievements().UnlockAchievement(id, rex::system::AchievementNotification::kShow);
  REXLOG_INFO("BF achievements: Reborn's {} {} ({})", id,
              result == rex::system::AchievementUnlockResult::kUnlocked ? "unlocked" : "not unlocked", why);
  return result == rex::system::AchievementUnlockResult::kUnlocked;
}

}  // namespace bf1943::reborn_achievements
