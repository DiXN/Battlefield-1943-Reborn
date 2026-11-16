// The achievement popup (TekRantGaming, 16 Nov 2026: "make it work like the king kong game had achievements with a
// popup and a sound"; his pick M1 "Medal Case", runs/ui-designs/out/m1_popup.png): when the game unlocks one of its 12
// achievements (the SDK's XGIUserWriteAchievements, AchievementManager), or one of Reborn's own is unlocked
// (reborn_achievements.h), a paper dispatch slip slides in at the top right with the achievement's own picture, its name in the game's headline font, its words and its gamerscore, and a
// short chime plays. Several at once queue, one after the other. As King Kong Recompiled's toast (kk/src/toast.cpp),
// in the Field Dispatch look.
//
// Settings > Audio: bf_achievement_popups (on or off) and bf_achievement_volume (0 to 100, the chime's volume; 0 is
// silent). A WAV of the player's own, <user>\sounds\achievement.wav (16 bit PCM), plays in place of the chime.
#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>

#include <rex/ui/overlay/achievement_notification.h>

namespace bf1943 {

class AchievementToast final : public rex::ui::AchievementNotificationDialog {
 public:
  explicit AchievementToast(rex::ui::ImGuiDrawer* drawer);
  void Push(const rex::system::AchievementEvent& event) override;  // any thread

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  struct Toast {
    std::string title, description;
    uint32_t gamerscore = 0, image_id = 0;
    double started = -1;
  };
  std::mutex mutex_;
  std::deque<Toast> queue_;
};

// Shows a popup as if the game had unlocked achievement `id` (the Achievements page's Y, "Test the popup"; and
// BF_DEV_ACHIEVEMENT_TEST in dev builds). Nothing is unlocked or saved.
void TestAchievementPopup(uint32_t id);

// The achievement's own picture (the game's title data, its image `image_id`), written once into the user's cache as
// a PNG for the menus (kit::DrawImageFile). Empty while there is none.
std::filesystem::path AchievementIconFile(uint32_t image_id);

// The chime (or the player's achievement.wav), at the achievement_sound volume.
void PlayAchievementSound();

// Where the user folder is (bf1943_app.h, at setup): the settings file and sounds\achievement.wav.
void SetAchievementUserDir(const std::filesystem::path& user_dir);

}  // namespace bf1943
