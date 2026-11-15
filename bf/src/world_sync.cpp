// The match's world followed by this game, and the admin's panel (14 Nov 2026: "cloud coverage, rate, weather
// percentage, day/night adjustment and time scale settings and sliders for servers and in the create match menu ...
// in-game admin controls for whoever creates a custom match ... giving player IDs admin panel access in-game").
//
// The match's host keeps the world (gm_host.cpp's World in a match a player's game hosts, server/gmhost.py's in the
// server package). This game asks the host for it every two seconds on a UDP socket of its own to the host's port
// (the port players already reach), and hands the answer to the renderer (native_x_renderer::SetWorldState), which
// then draws the host's hour, clouds and weather instead of the player's own sky settings. A host that keeps none
// does not answer: the player's own settings stay.
//
//   ask     "BF43WRLD", version 1, flags (1: a change follows), the player's name and a key (a length byte and text
//           each); for a change: day and night (a byte), then floats: hour, a day's minutes, cloud cover, cloud
//           rate, weather, distance fog, ground fog, wind, and (15 Nov 2026) the cloud base's height in metres
//   answer  "BF43WRLR", version, flags (1 day and night, 2 the asker is an admin), the same eight floats as they are
//           now, the clouds' two clocks as doubles (drift seconds, forming), and the cloud base's height (a float)
// The cloud height came later and stands last in both: a host from before it sends and reads none (450 m then).
// Big endian. An admin is the game that shows the host's key (the game that started a private match has it), or a
// joined player whose ID is in the host's list (bf_world_admins; a dedicated server's --admins).
//
// The panel (F9, for an admin only): the same settings as sliders, sent as they are changed.

#include "world_sync.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <imgui.h>
#include <native_x_renderer/native_x_renderer.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>

#include "launcher_kit.h"

namespace bf1943 {
const std::string& PlayerName();  // live_layers.cpp: the name played under online
uint32_t OnlinePlayerId();        // live_layers.cpp: the player's ID as a match's host sees it
bool InMatch();                   // auto_join.cpp
bool LauncherShowing();           // game_menu.cpp
}  // namespace bf1943

namespace bf1943::world {
namespace {

#if defined(_WIN32)
using Socket = SOCKET;
constexpr Socket kNoSocket = INVALID_SOCKET;
void CloseSocket(Socket s) { closesocket(s); }
#else
using Socket = int;
constexpr Socket kNoSocket = -1;
void CloseSocket(Socket s) { close(s); }
#endif

std::mutex g_mutex;
std::string g_joined, g_key;
Status g_status;
bool g_have_request = false;
Settings g_request;
int g_request_sends = 0;
std::atomic<bool> g_started{false};

double Now() {
  static const auto start = std::chrono::steady_clock::now();
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

void PutF32(std::string& b, float v) {
  uint32_t bits;
  std::memcpy(&bits, &v, 4);
  for (int s = 24; s >= 0; s -= 8) b += char((bits >> s) & 0xFF);
}
void PutText(std::string& b, const std::string& s) {
  const size_t n = std::min<size_t>(s.size(), 64);
  b += char(n);
  b.append(s, 0, n);
}
float GetF32(const uint8_t* p) {
  const uint32_t bits = uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
  float v;
  std::memcpy(&v, &bits, 4);
  return std::isfinite(v) ? v : 0.0f;
}
double GetF64(const uint8_t* p) {
  uint64_t bits = 0;
  for (int i = 0; i < 8; ++i) bits = bits << 8 | p[i];
  double v;
  std::memcpy(&v, &bits, 8);
  return std::isfinite(v) ? v : 0.0;
}

void Run() {
#if defined(_WIN32)
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
  Socket sock = kNoSocket;
  sockaddr_in to{};
  std::string target;
  double next_ask = 0, last_answer = -100;
  bool valid = false;
  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::string joined, key;
    Settings request;
    bool have_request = false;
    {
      std::lock_guard lock(g_mutex);
      joined = g_joined, key = g_key;
      if (g_have_request && g_request_sends > 0) have_request = true, request = g_request;
    }
    const double now = Now();
    if (joined != target) {
      target = joined;
      if (sock != kNoSocket) CloseSocket(sock), sock = kNoSocket;
      last_answer = -100, next_ask = 0;
      if (!target.empty()) {
        const size_t colon = target.rfind(':');
        addrinfo hints{}, *found = nullptr;
        hints.ai_family = AF_INET, hints.ai_socktype = SOCK_DGRAM;
        if (colon != std::string::npos &&
            getaddrinfo(target.substr(0, colon).c_str(), target.substr(colon + 1).c_str(), &hints, &found) == 0 && found) {
          std::memcpy(&to, found->ai_addr, sizeof(to));
          freeaddrinfo(found);
          sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
          if (sock != kNoSocket) {
#if defined(_WIN32)
            u_long on = 1;
            ioctlsocket(sock, FIONBIO, &on);
#else
            fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
#endif
          }
        }
      }
    }
    // Lost for eight seconds (or no match): the player's own settings again.
    if (valid && (sock == kNoSocket || now - last_answer > 8.0)) {
      valid = false;
      native_x_renderer::SetWorldState(native_x_renderer::WorldState{});
      std::lock_guard lock(g_mutex);
      g_status = Status{};
      REXLOG_INFO("BF world: the match's host no longer answers: the player's own sky settings again");
    }
    if (sock == kNoSocket) continue;
    if (now >= next_ask || have_request) {
      next_ask = now + (have_request ? 0.3 : 2.0);
      std::string ask = "BF43WRLD";
      ask += char(1);
      ask += char(have_request ? 1 : 0);
      PutText(ask, PlayerName());
      PutText(ask, key);
      if (have_request) {
        ask += char(request.day_night ? 1 : 0);
        for (float v : {request.hour, request.day_minutes, request.cloud_cover, request.cloud_rate, request.weather,
                        request.fog, request.ground_fog, request.wind})
          PutF32(ask, v);
        PutF32(ask, request.cloud_height);
        std::lock_guard lock(g_mutex);
        if (--g_request_sends <= 0) g_have_request = false;
      }
      sendto(sock, ask.data(), int(ask.size()), 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
      if (have_request) std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    for (;;) {
      uint8_t buf[128];
      sockaddr_in from{};
#if defined(_WIN32)
      int from_size = sizeof(from);
#else
      socklen_t from_size = sizeof(from);
#endif
      const int n = int(recvfrom(sock, reinterpret_cast<char*>(buf), sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &from_size));
      if (n <= 0) break;
      if (from.sin_addr.s_addr != to.sin_addr.s_addr || from.sin_port != to.sin_port) continue;
      if (n < 8 + 2 + 32 + 16 || std::memcmp(buf, "BF43WRLR", 8) != 0 || buf[8] != 1) continue;
      Status s;
      s.answered = true;
      s.now.day_night = (buf[9] & 1) != 0;
      s.admin = (buf[9] & 2) != 0;
      s.now.hour = GetF32(buf + 10), s.now.day_minutes = GetF32(buf + 14), s.now.cloud_cover = GetF32(buf + 18);
      s.now.cloud_rate = GetF32(buf + 22), s.now.weather = GetF32(buf + 26);
      s.now.fog = GetF32(buf + 30), s.now.ground_fog = GetF32(buf + 34), s.now.wind = GetF32(buf + 38);
      if (n >= 62) s.now.cloud_height = std::clamp(GetF32(buf + 58), 100.0f, 3000.0f);
      native_x_renderer::WorldState w;
      w.valid = true;
      w.cloud_height = s.now.cloud_height;
      w.day_night = s.now.day_night;
      w.hour = s.now.hour, w.day_minutes = s.now.day_minutes;
      w.cloud_cover = s.now.cloud_cover, w.cloud_rate = s.now.cloud_rate, w.weather = s.now.weather;
      w.fog = s.now.fog, w.ground_fog = s.now.ground_fog, w.wind = s.now.wind;
      w.cloud_seconds = GetF64(buf + 42), w.cloud_evolve = GetF64(buf + 50);
      native_x_renderer::SetWorldState(w);
      bool was_admin;
      {
        std::lock_guard lock(g_mutex);
        was_admin = g_status.admin;
        g_status = s;
      }
      if (!valid)
        REXLOG_INFO("BF world: the match's host keeps its world: day and night {}, {:.1f} h, a day in {:.0f} min, clouds "
                    "{:.0f}% at rate {:.0f}%, weather {:.0f}%, fog {:.0f}%, ground fog {:.0f}%, wind {:.0f}%, cloud height {:.0f} m{}",
                    s.now.day_night ? "on" : "off", s.now.hour, s.now.day_minutes, s.now.cloud_cover * 100,
                    s.now.cloud_rate * 100, s.now.weather * 100, s.now.fog * 100, s.now.ground_fog * 100, s.now.wind * 100,
                    s.now.cloud_height, s.admin ? "; this game is an admin (F9)" : "");
      else if (s.admin && !was_admin)
        REXLOG_INFO("BF world: this game is an admin of the match's world (F9)");
      valid = true, last_answer = now;
    }
  }
}

void StartOnce() {
  if (g_started.exchange(true)) return;
  std::thread(Run).detach();
}

// ---------------------------------------------------------------------------------------------------------------
// The admin's panel (F9): the f8 panel's look, mouse and keys.

class AdminPanel final : public rex::ui::ImGuiDialog {
 public:
  explicit AdminPanel(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}
  std::atomic<bool> open{false};

 protected:
  void OnDraw(ImGuiIO& io) override {
    const Status status = Current();
#if defined(BF_DEV_TOOLS)
    // BF_DEV_WORLD_PANEL=<seconds> (a test run has no keyboard): that long after this game became an admin the
    // panel opens by itself, and five seconds later the weather is set to 60% through it.
    if (const char* v = std::getenv("BF_DEV_WORLD_PANEL"); v && *v && status.admin) {
      static const double since = ImGui::GetTime();
      static int stage = 0;
      const double after = ImGui::GetTime() - since - std::atof(v);
      if (stage == 0 && after > 0) stage = 1, open = true, edit_ = status.now, edited_at_ = -100;
      if (stage == 1 && after > 5) {
        stage = 2;
        edit_ = status.now, edit_.weather = 0.6f, edited_at_ = ImGui::GetTime();
        Request(edit_);
      }
    }
#endif
    if (ImGui::IsKeyPressed(ImGuiKey_F9, false) && !LauncherShowing()) {
      if (open) open = false;
      else if (status.answered && status.admin) open = true, edit_ = status.now, edited_at_ = -100;
      else told_at_ = ImGui::GetTime();
    }
    const float u = io.DisplaySize.y / 720.0f;
    const kit::Fonts& f = kit::GetFonts();
    ImDrawList* d = ImGui::GetForegroundDrawList();
    if (!open) {
      // F9 without the right: said for four seconds, with the ID a server's owner would add.
      if (ImGui::GetTime() - told_at_ < 4 && told_at_ > 0) {
        char line[160];
        if (!status.answered) std::snprintf(line, sizeof(line), "This match has no world settings to change.");
        else std::snprintf(line, sizeof(line), "Admins only. Your player ID is %08X: the server's owner can add it.", OnlinePlayerId());
        const float w = kit::TextWidth(f.ui, 15 * u, line) + 24 * u;
        const ImVec2 a(io.DisplaySize.x * 0.5f - w * 0.5f, 60 * u), b(a.x + w, a.y + 30 * u);
        d->AddRectFilled(a, b, IM_COL32(18, 22, 14, 225));
        d->AddText(f.ui, 15 * u, ImVec2(a.x + 12 * u, a.y + 6 * u), kit::kCream, line);
      }
      return;
    }
    if (LauncherShowing() || !status.answered || !status.admin || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
      open = false;
      return;
    }
    // The host's values, except for a second after a change of ours (its answer is on the way).
    if (ImGui::GetTime() - edited_at_ > 1.5 && drag_ < 0) edit_ = status.now;

    struct Row {
      const char* label;
      float* value;
      float lo, hi, step;
      const char* help;
    };
    float day_night = edit_.day_night ? 1.0f : 0.0f;
    Row rows[] = {
        {"Day and night", &day_night, 0, 1, 1, "The light, the shadows and the sky follow the match's clock."},
        {"Time of day", &edit_.hour, 0, 24, 0.25f, "12 is noon, 18 sunset, 0 midnight."},
        {"A day lasts", &edit_.day_minutes, 0, 240, 5, "Minutes a whole day and night take. 0: the time stands still."},
        {"Cloud cover", &edit_.cloud_cover, 0, 1, 0.01f, "How much of the sky the volumetric clouds take."},
        {"Cloud change rate", &edit_.cloud_rate, 0, 1, 0.01f, "How fast the clouds form and dissolve."},
        {"Weather", &edit_.weather, 0, 1, 0.01f, "From fair to a storm: overcast, mist, rain, lightning."},
        {"Distance fog", &edit_.fog, 0, 1, 0.01f, "A haze that hides what is far."},
        {"Ground fog", &edit_.ground_fog, 0, 1, 0.01f, "A low layer over the sea and the shore."},
        {"Wind", &edit_.wind, 0, 1, 0.01f, "From calm to a gale: how far the rain leans."},
        {"Cloud height", &edit_.cloud_height, 200, 1500, 50, "The cloud base over the sea. Low, the planes fly through the clouds."},
    };
    const int count = int(std::size(rows));
    const float w = 420 * u, x0 = 16 * u, x1 = x0 + w, y0 = 120 * u, rh = 44 * u;
    const float y1 = y0 + 48 * u + rh * count + 64 * u;
    d->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(18, 22, 14, 225));
    d->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(201, 183, 122, 102), 0, 0, u);
    d->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y0 + 40 * u), IM_COL32(93, 138, 50, 90));
    d->AddText(f.ui_bold, 20 * u, ImVec2(x0 + 12 * u, y0 + 6 * u), kit::kCream, "MATCH ADMIN (F9)");
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) row_ = (row_ + count - 1) % count;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) row_ = (row_ + 1) % count;
    const int key_step = (ImGui::IsKeyPressed(ImGuiKey_RightArrow) ? 1 : 0) - (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) ? 1 : 0);
    bool changed = false;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) drag_ = -1;
    for (int i = 0; i < count; ++i) {
      Row& r = rows[i];
      const float yy = y0 + 48 * u + rh * i;
      const ImVec2 a(x0 + 4 * u, yy), b(x1 - 4 * u, yy + rh - 2 * u);
      const bool hover = ImGui::IsMouseHoveringRect(a, b);
      if (hover && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) row_ = i;
      if (i == row_) d->AddRectFilled(a, b, IM_COL32(93, 138, 50, 80));
      d->AddText(f.ui, 15 * u, ImVec2(a.x + 10 * u, yy + 2 * u), kit::kCream, r.label);
      char value[32];
      if (i == 0) std::snprintf(value, sizeof(value), "%s", *r.value > 0.5f ? "On" : "Off");
      else if (i == 1) std::snprintf(value, sizeof(value), "%02d:%02d", int(*r.value) % 24, int(*r.value * 60.0f) % 60);
      else if (i == 2) std::snprintf(value, sizeof(value), *r.value <= 0 ? "Stands still" : "%.0f min", *r.value);
      else if (i == 9) std::snprintf(value, sizeof(value), "%.0f m", *r.value);
      else std::snprintf(value, sizeof(value), "%.0f%%", *r.value * 100.0f);
      d->AddText(f.ui, 15 * u, ImVec2(b.x - 10 * u - kit::TextWidth(f.ui, 15 * u, value), yy + 2 * u), kit::kGreenBright, value);
      // The track: dragged with the mouse, stepped with left and right.
      const float tx0 = a.x + 10 * u, tx1 = b.x - 10 * u, ty = yy + 30 * u;
      d->AddRectFilled(ImVec2(tx0, ty - u), ImVec2(tx1, ty + u), IM_COL32(232, 226, 204, 60));
      const float at = tx0 + (tx1 - tx0) * (*r.value - r.lo) / (r.hi - r.lo);
      d->AddRectFilled(ImVec2(at - 3 * u, ty - 7 * u), ImVec2(at + 3 * u, ty + 7 * u), kit::kGold);
      float v = *r.value;
      if (ImGui::IsMouseHoveringRect(ImVec2(tx0 - 6 * u, ty - 10 * u), ImVec2(tx1 + 6 * u, ty + 10 * u)) &&
          ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        drag_ = i;
      }
      if (drag_ == i) {
        v = r.lo + (r.hi - r.lo) * std::clamp((io.MousePos.x - tx0) / (tx1 - tx0), 0.0f, 1.0f);
        v = r.lo + std::round((v - r.lo) / r.step) * r.step;
      } else if (i == row_ && key_step) {
        v = std::clamp(v + key_step * (i == 1 ? 0.5f : i == 2 || i == 9 ? r.step : i == 0 ? 1.0f : 0.05f), r.lo, r.hi);
      }
      if (i == 1 && v >= 24.0f) v = 23.75f;
      if (v != *r.value) *r.value = v, changed = true;
    }
    edit_.day_night = day_night > 0.5f;
    if (changed) {
      edited_at_ = ImGui::GetTime();
      Request(edit_);
    }
    const float fy = y0 + 48 * u + rh * count + 6 * u;
    d->AddLine(ImVec2(x0 + 12 * u, fy), ImVec2(x1 - 12 * u, fy), IM_COL32(201, 183, 122, 64));
    d->AddText(f.ui, 12 * u, ImVec2(x0 + 12 * u, fy + 6 * u), IM_COL32(207, 198, 168, 255), rows[row_].help);
    d->AddText(f.ui, 12 * u, ImVec2(x0 + 12 * u, fy + 24 * u), IM_COL32(207, 198, 168, 255),
               "Every player of the match sees a change within two seconds.");
    d->AddText(f.ui, 12 * u, ImVec2(x0 + 12 * u, fy + 40 * u), IM_COL32(207, 198, 168, 255),
               "Drag, or arrows: pick and change \xC2\xB7 F9 or Esc: close");
  }

 private:
  Settings edit_;
  double edited_at_ = -100, told_at_ = 0;
  int row_ = 0, drag_ = -1;
};

AdminPanel* g_panel = nullptr;

}  // namespace

void NoteJoined(const std::string& address) {
  {
    std::lock_guard lock(g_mutex);
    g_joined = address;
  }
  if (!address.empty()) StartOnce();
}

void SetAdminKey(const std::string& key) {
  std::lock_guard lock(g_mutex);
  g_key = key;
}

Status Current() {
  std::lock_guard lock(g_mutex);
  return g_status;
}

void Request(const Settings& settings) {
  std::lock_guard lock(g_mutex);
  g_request = settings, g_have_request = true, g_request_sends = 3;  // UDP may lose one
}

bool AdminPanelOpen() { return g_panel && g_panel->open.load(); }

}  // namespace bf1943::world

namespace bf1943 {
void CreateWorldAdminPanel(rex::ui::ImGuiDrawer* drawer) {
  if (!world::g_panel) world::g_panel = new world::AdminPanel(drawer);  // lives for the session
}
}  // namespace bf1943
