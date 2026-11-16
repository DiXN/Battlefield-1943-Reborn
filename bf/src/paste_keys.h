#pragma once
// Pasting in our menus (TekRantGaming, 16 Nov 2026: "ctrl v did not paste"). The SDK hands ImGui the Ctrl key as
// ImGuiKey_LeftCtrl and writes io.KeyCtrl the old way, which ImGui overwrites every frame from ImGuiMod_Ctrl, a key it
// is never sent: io.KeyCtrl was always false, so no Ctrl+V anywhere (the name page, Direct connect, chat) ever fired.
// Ctrl and Shift are read from the keys themselves and from SDL's own modifier state; the text from SDL's clipboard
// (the system's, Windows and Linux), ImGui's as a fallback.

#include <string>

#include <SDL3/SDL.h>
#include <imgui.h>

namespace paste {

// Tests (BF_MENU_STEPS clip/ and paste/): the clipboard's text in place of the system's, so a test never touches the
// clipboard of the PC it runs on. Taken by the next Text().
inline std::string& TestText() {
  static std::string text;
  return text;
}

inline bool CtrlDown() {
  return ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl) || ImGui::GetIO().KeyCtrl ||
         (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
}
inline bool ShiftDown() {
  return ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift) || ImGui::GetIO().KeyShift ||
         (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
}

// Ctrl+V or Shift+Insert pressed this frame.
inline bool Pressed() {
  return (ImGui::IsKeyPressed(ImGuiKey_V, false) && CtrlDown()) ||
         (ImGui::IsKeyPressed(ImGuiKey_Insert, false) && ShiftDown());
}

// The clipboard's text ("" when it holds none).
inline std::string Text() {
  if (!TestText().empty()) {
    std::string text;
    text.swap(TestText());
    return text;
  }
  std::string out;
  if (char* clip = SDL_GetClipboardText()) {
    out = clip;
    SDL_free(clip);
  }
  if (out.empty())
    if (const char* clip = ImGui::GetClipboardText()) out = clip;
  return out;
}

}  // namespace paste
