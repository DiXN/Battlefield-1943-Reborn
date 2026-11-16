// Discord Rich Presence (TekRantGaming, 15 Nov 2026: a player's Discord profile shows what they are doing in the
// game: the menus, the launcher or the server browser; a map on a named server with their team and the player count;
// the time in the match; the Reborn logo). docs/DISCORD-PRESENCE.md has the design and the Developer Portal steps.
//
// No Discord library: the Discord client on this PC is spoken to directly over its local IPC, as the old discord-rpc
// library did (and as the Social SDK still does underneath): a named pipe \\.\pipe\discord-ipc-0 to -9 on Windows, a
// Unix socket of that name on Linux (in $XDG_RUNTIME_DIR, or the temporary folder; Flatpak and Snap Discord put theirs
// in a folder under it). A frame there is the opcode (4 bytes, little endian), the length of the JSON that follows
// (the same), then the JSON: 0 the handshake ({"v":1,"client_id":...}, answered with a READY dispatch), 1 a command or
// its answer, 2 a close (with Discord's code and reason), 3 a ping (answered with 4, a pong with the same data).
// Nothing new ships, so no new DLL needs a Microsoft reputation submission and no antivirus sees a new library.
//
// It never touches the game: everything runs on its own thread, which sleeps on a condition variable. The menus hand
// it what the game is doing (Note, a few times a second; only a change wakes it). Without a Discord client it looks
// again every 5 to 15 seconds, quietly (one log line each time it goes); a Discord restart is reconnected the same
// way. Updates stay well under Discord's limit of 5 SET_ACTIVITY calls in 20 s: a change waits until it has held for
// a second,
// then at least 4 s after the last update (15 s when only the player count changed). Every pipe or socket call has a
// timeout, so a Discord that hangs cannot hold the thread for long, and the thread is detached: the game never waits
// for it to end (the pipe closes with the process, and Discord clears the presence then).
//
// Never shown: a server's address (a name that holds one is left out), another player's name (a player hosted match
// is "Private match", never "<host>'s match"), the player's own name. Server names pass the profanity filter.

#include "discord_presence.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <fmt/format.h>
#include <rex/cvar.h>
#include <rex/logging.h>

#include "text_filter.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#endif

REXCVAR_DEFINE_BOOL(bf_discord_status, true, "BF1943/Discord",
                    "Discord Rich Presence: your Discord profile shows what you are doing in the game");
// TekRantGaming's application "BF 1943 Reborn" on the Discord Developer Portal (its assets:
// docs/DISCORD-PRESENCE.md). Empty: no presence at all (nothing is tried).
REXCVAR_DEFINE_STRING(bf_discord_app_id, "1571686541817020506", "BF1943/Discord",
                      "The Discord application id the presence is shown under (empty: off)");
REXCVAR_DECLARE(bool, server);

namespace bf1943::discord {
namespace {

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;
using std::chrono::seconds;

constexpr uint32_t kOpHandshake = 0, kOpFrame = 1, kOpClose = 2, kOpPing = 3, kOpPong = 4;
constexpr uint32_t kMaxFrame = 64 * 1024;  // Discord's answers are a few KB (READY carries the user and the config)
constexpr int kIoTimeoutMs = 2000;

// The asset keys on the Developer Portal (Rich Presence, Art Assets): docs/DISCORD-PRESENCE.md.
constexpr const char* kLogoKey = "logo";
constexpr const char* kGameTitle = "Battlefield 1943 Reborn";

// ---- the connection: one pipe (Windows) or socket (Linux)

class Conn {
 public:
  ~Conn() { Close(); }
  bool Open() const {
#if defined(_WIN32)
    return pipe_ != INVALID_HANDLE_VALUE;
#else
    return fd_ >= 0;
#endif
  }
  // The first Discord IPC endpoint that answers; false when none does (no Discord client running).
  bool Connect(std::string* where) {
    Close();
    // BF_DEV_DISCORD_IPC=<pipe name or socket path> (dev builds, tests): only that one (tools/fake_discord_ipc.py),
    // since a running Discord holds discord-ipc-0.
    std::string only;
#if defined(BF_DEV_TOOLS)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    if (const char* v = std::getenv("BF_DEV_DISCORD_IPC"); v && *v) only = v;
#pragma clang diagnostic pop
#endif
#if defined(_WIN32)
    for (int i = 0; i < 10; ++i) {
      std::wstring name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
      if (!only.empty()) {
        if (i > 0) break;
        name = L"\\\\.\\pipe\\" + std::wstring(only.begin(), only.end());
      }
      HANDLE h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED,
                             nullptr);
      if (h == INVALID_HANDLE_VALUE) continue;  // not there, or busy (another game holds that one)
      pipe_ = h;
      event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
      if (!event_) {
        Close();
        return false;
      }
      *where = only.empty() ? fmt::format("discord-ipc-{}", i) : only;
      return true;
    }
    return false;
#else
    std::vector<std::string> bases;
    for (const char* var : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"})
      if (const char* v = std::getenv(var); v && *v) bases.emplace_back(v);
    bases.emplace_back("/tmp");
    if (!only.empty()) bases = {only};
    // Discord's own package, then Flatpak's and Snap's (each keeps its socket in a folder of its own), and Vesktop's.
    static constexpr const char* kSubs[] = {"",
                                            "app/com.discordapp.Discord/",
                                            "app/com.discordapp.DiscordCanary/",
                                            "app/com.discordapp.DiscordPTB/",
                                            "snap.discord/",
                                            "snap.discord-canary/",
                                            ".flatpak/dev.vencord.Vesktop/xdg-run/",
                                            "app/dev.vencord.Vesktop/"};
    for (const std::string& base : bases)
      for (const char* sub : kSubs)
        for (int i = 0; i < 10; ++i) {
          if (!only.empty() && (i > 0 || *sub)) continue;
          const std::string path = only.empty() ? base + "/" + sub + "discord-ipc-" + std::to_string(i) : only;
          sockaddr_un addr{};
          addr.sun_family = AF_UNIX;
          if (path.size() >= sizeof(addr.sun_path)) continue;
          std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
          const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
          if (fd < 0) return false;
          if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
            timeval tv{kIoTimeoutMs / 1000, (kIoTimeoutMs % 1000) * 1000};
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            fd_ = fd;
            *where = path;
            return true;
          }
          close(fd);
        }
    return false;
#endif
  }
  void Close() {
#if defined(_WIN32)
    if (pipe_ != INVALID_HANDLE_VALUE) CancelIoEx(pipe_, nullptr), CloseHandle(pipe_);
    if (event_) CloseHandle(event_);
    pipe_ = INVALID_HANDLE_VALUE, event_ = nullptr;
#else
    if (fd_ >= 0) close(fd_);
    fd_ = -1;
#endif
    in_.clear();
  }
  bool Send(uint32_t op, const std::string& json) {
    if (!Open()) return false;
    std::string frame(8, '\0');
    const uint32_t len = uint32_t(json.size());
    for (int i = 0; i < 4; ++i) frame[size_t(i)] = char(op >> (8 * i)), frame[size_t(4 + i)] = char(len >> (8 * i));
    frame += json;
    if (Write(frame.data(), frame.size())) return true;
    Close();
    return false;
  }
  // The next whole frame, waiting up to `wait_ms` for it (0: only what is there now). False with none; the
  // connection is closed when it broke.
  bool Receive(uint32_t* op, std::string* json, int wait_ms) {
    const auto until = Clock::now() + milliseconds(wait_ms);
    for (;;) {
      if (in_.size() >= 8) {
        uint32_t o = 0, len = 0;
        for (int i = 0; i < 4; ++i)
          o |= uint32_t(uint8_t(in_[size_t(i)])) << (8 * i), len |= uint32_t(uint8_t(in_[size_t(4 + i)])) << (8 * i);
        if (len > kMaxFrame) {
          Close();  // not Discord's IPC as we know it
          return false;
        }
        if (in_.size() >= 8 + size_t(len)) {
          *op = o;
          json->assign(in_, 8, len);
          in_.erase(0, 8 + size_t(len));
          return true;
        }
      }
      const int left = int(std::chrono::duration_cast<milliseconds>(until - Clock::now()).count());
      if (!ReadSome(std::max(left, 0))) return false;  // broken (closed), or nothing within the wait
    }
  }

 private:
#if defined(_WIN32)
  // Overlapped, so a Discord that stops reading cannot hold the thread longer than the timeout.
  bool Finish(BOOL ok, DWORD* done) {
    if (!ok && GetLastError() != ERROR_IO_PENDING) return false;
    if (WaitForSingleObject(event_, kIoTimeoutMs) != WAIT_OBJECT_0) {
      CancelIoEx(pipe_, nullptr);
      GetOverlappedResult(pipe_, &ov_, done, TRUE);
      return false;
    }
    return GetOverlappedResult(pipe_, &ov_, done, FALSE) != 0;
  }
  bool Write(const char* p, size_t n) {
    while (n > 0) {
      ov_ = {}, ov_.hEvent = event_, ResetEvent(event_);
      DWORD done = 0;
      if (!Finish(WriteFile(pipe_, p, DWORD(n), nullptr, &ov_), &done) || done == 0) return false;
      p += done, n -= done;
    }
    return true;
  }
  bool ReadSome(int wait_ms) {
    const auto until = Clock::now() + milliseconds(wait_ms);
    for (;;) {
      DWORD avail = 0;
      if (!PeekNamedPipe(pipe_, nullptr, 0, nullptr, &avail, nullptr)) {
        Close();
        return false;
      }
      if (avail > 0) {
        char buf[4096];
        ov_ = {}, ov_.hEvent = event_, ResetEvent(event_);
        DWORD done = 0;
        if (!Finish(ReadFile(pipe_, buf, std::min<DWORD>(avail, sizeof(buf)), nullptr, &ov_), &done) || done == 0) {
          Close();
          return false;
        }
        in_.append(buf, done);
        return true;
      }
      if (Clock::now() >= until) return false;
      Sleep(20);
    }
  }
  HANDLE pipe_ = INVALID_HANDLE_VALUE;
  HANDLE event_ = nullptr;
  OVERLAPPED ov_{};
#else
  bool Write(const char* p, size_t n) {
    while (n > 0) {
      const ssize_t done = send(fd_, p, n, MSG_NOSIGNAL);  // never SIGPIPE when Discord went away
      if (done <= 0) return false;
      p += done, n -= size_t(done);
    }
    return true;
  }
  bool ReadSome(int wait_ms) {
    pollfd pfd{fd_, POLLIN, 0};
    const int r = poll(&pfd, 1, wait_ms);
    if (r == 0) return false;
    if (r < 0 || (pfd.revents & (POLLERR | POLLNVAL))) {
      Close();
      return false;
    }
    char buf[4096];
    const ssize_t got = recv(fd_, buf, sizeof(buf), MSG_DONTWAIT);
    if (got <= 0) {  // closed (POLLHUP with nothing left), or broken
      Close();
      return false;
    }
    in_.append(buf, size_t(got));
    return true;
  }
  int fd_ = -1;
#endif
  std::string in_;
};

// ---- JSON (written by hand: a handful of fields; Discord's answers are only looked into for a few words)

std::string Quote(const std::string& s) {
  std::string out = "\"";
  for (const char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (uint8_t(c) < 0x20) out += fmt::format("\\u{:04x}", int(uint8_t(c)));
        else out += c;
    }
  }
  return out + "\"";
}

// Discord takes 2 to 128 characters in a text field: cut on a UTF-8 character's start.
std::string Fit(std::string s) {
  if (s.size() > 128) {
    size_t n = 125;
    while (n > 0 && (uint8_t(s[n]) & 0xC0) == 0x80) --n;
    s = s.substr(0, n) + "...";
  }
  if (!s.empty() && s.size() < 2) s += ' ';
  return s;
}

// The text of a string field in Discord's answer ("evt":"READY"), or empty.
std::string Field(const std::string& json, const char* key) {
  const std::string k = std::string("\"") + key + "\":";
  size_t p = json.find(k);
  if (p == std::string::npos) return "";
  p += k.size();
  while (p < json.size() && json[p] == ' ') ++p;
  if (p >= json.size()) return "";
  if (json[p] != '"') {  // a number (a close's code)
    size_t e = p;
    while (e < json.size() && (std::isdigit(uint8_t(json[e])) || json[e] == '-')) ++e;
    return json.substr(p, e - p);
  }
  std::string out;
  for (size_t i = p + 1; i < json.size() && json[i] != '"'; ++i) {
    if (json[i] == '\\' && i + 1 < json.size()) ++i;
    out += json[i];
  }
  return out;
}

bool ValidAppId(const std::string& id) {
  return id.size() >= 15 && id.size() <= 21 && std::all_of(id.begin(), id.end(), [](char c) { return c >= '0' && c <= '9'; });
}

int64_t UnixNow() {
  return std::chrono::duration_cast<seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

uint32_t Pid() {
#if defined(_WIN32)
  return uint32_t(GetCurrentProcessId());
#else
  return uint32_t(getpid());
#endif
}

// ---- what is shown

struct MapArt {
  const char* key;    // the portal asset
  const char* title;  // as the game's menus name it
  const char* mode;
};
const MapArt* ArtFor(const std::string& map) {
  static constexpr MapArt kMaps[] = {{"map_wake", "Wake Island", "Conquest"},
                                     {"map_guadalcanal", "Guadalcanal", "Conquest"},
                                     {"map_iwo", "Iwo Jima", "Conquest"},
                                     {"map_coral", "Coral Sea", "Air Superiority"}};
  for (const MapArt& m : kMaps)
    if (map == m.title) return &m;
  return nullptr;
}

const char* TeamName(int team) {
  // The game's two sides: team 1 owns the level's CQ_MP_BASE_ALLIED_CARRIER, team 2 its AXIS_CARRIER (run v41team2,
  // 13 Nov 2026, capture_points.cpp's list).
  return team == 1 ? "US Marines" : team == 2 ? "Imperial Japanese Navy" : "";
}

std::string Join(const std::string& a, const std::string& b) {
  return a.empty() ? b : b.empty() ? a : a + " \xC2\xB7 " + b;  // a middle dot
}

struct Built {
  std::string json;   // the activity object, or empty (none)
  std::string major;  // the same without the player count: a change of only the count waits longer
};

// `start` is the elapsed clock's start (Unix seconds).
Built Build(const Presence& p, int64_t start, const std::string& party_id) {
  using W = Presence::Where;
  if (p.where == W::kNone) return {};
  std::string details, state;
  const MapArt* art = ArtFor(p.map);
  const std::string server = p.private_match ? std::string("Private match") : p.server;
  bool party = false;
  switch (p.where) {
    case W::kNone: break;
    case W::kStarting: details = "Starting the game"; break;
    case W::kLauncher: details = "In the launcher"; break;
    case W::kMenu: details = "In the main menu"; break;
    case W::kBrowser: details = "Browsing servers"; break;
    case W::kTutorial: details = "Playing the tutorial"; break;
    case W::kJoining:
      details = p.map.empty() ? std::string("Joining a match") : "Joining " + p.map;
      state = server;
      break;
    case W::kMatch:
      details = Join(p.map.empty() ? std::string("In a match") : p.map, art ? art->mode : "");
      state = Join(TeamName(p.team), server);
      if (state.empty()) state = "In a match";
      party = p.players > 0 && p.max_players >= p.players;
      break;
  }
  std::string a = "{\"details\":" + Quote(Fit(details));
  if (!state.empty()) a += ",\"state\":" + Quote(Fit(state));
  a += fmt::format(",\"timestamps\":{{\"start\":{}}}", start);
  const bool map_art = art && (p.where == W::kMatch || p.where == W::kJoining);
  if (map_art)
    a += fmt::format(",\"assets\":{{\"large_image\":\"{}\",\"large_text\":{},\"small_image\":\"{}\",\"small_text\":{}}}",
                     art->key, Quote(art->title), kLogoKey, Quote(kGameTitle));
  else
    a += fmt::format(",\"assets\":{{\"large_image\":\"{}\",\"large_text\":{}}}", kLogoKey, Quote(kGameTitle));
  Built b;
  b.major = a + "}";
  if (party) a += fmt::format(",\"party\":{{\"id\":{},\"size\":[{},{}]}}", Quote(party_id), p.players, p.max_players);
  b.json = a + "}";
  return b;
}

// ---- the thread

struct Shared {
  std::mutex mutex;
  std::condition_variable wake;
  Presence wanted;
  Clock::time_point wanted_at = Clock::now();  // when it last changed
  bool have = false;
  std::atomic<bool> started{false};
};
Shared& S() {
  static Shared* s = new Shared;  // never freed: the detached thread may still run at exit
  return *s;
}

void Run() {
  Shared& s = S();
  Conn conn;
  const int64_t game_start = UnixNow();
  auto next_try = Clock::now();
  seconds backoff(5);
  bool told_absent = false, told_off = false, told_bad_id = false;
  int told_lost = 0;
  std::string last_error;
  uint64_t nonce = 0;
  std::string sent_json, sent_major;
  bool sent_any = false;  // an activity is up on Discord (cleared when the toggle goes off)
  auto sent_at = Clock::now() - seconds(60);
  // The elapsed clock: from the game's start in the menus, from the join, from the map's start in a match (a new
  // map on the same server starts it again).
  Presence::Where clock_where = Presence::Where::kNone;
  std::string clock_map;
  int64_t clock_start = game_start;

  for (;;) {
    Presence want;
    Clock::time_point want_at;
    {
      std::unique_lock lock(s.mutex);
      s.wake.wait_for(lock, milliseconds(500));
      want = s.wanted, want_at = s.wanted_at;
    }
    const auto now = Clock::now();
    const std::string app_id = REXCVAR_GET(bf_discord_app_id);
    const bool enabled = REXCVAR_GET(bf_discord_status) && !app_id.empty();
    if (!app_id.empty() && !ValidAppId(app_id)) {
      if (!told_bad_id) REXLOG_WARN("BF discord: bf_discord_app_id is not a Discord application id: no presence");
      told_bad_id = true;
      conn.Close();
      continue;
    }
    if (!enabled) {
      if (conn.Open()) {
        if (sent_any) conn.Send(kOpFrame, fmt::format("{{\"cmd\":\"SET_ACTIVITY\",\"args\":{{\"pid\":{}}},\"nonce\":\"{}\"}}", Pid(), ++nonce));
        conn.Close();
        REXLOG_INFO("BF discord: presence turned off: cleared and disconnected");
      } else if (!told_off && !app_id.empty()) {
        REXLOG_INFO("BF discord: presence is off (Settings)");
      }
      told_off = true;
      sent_json.clear(), sent_major.clear(), sent_any = false;
      continue;
    }
    told_off = false;

    if (!conn.Open()) {
      if (now < next_try) continue;
      std::string where;
      if (!conn.Connect(&where)) {
        if (!told_absent) REXLOG_INFO("BF discord: no Discord client on this PC; looking again now and then, quietly");
        told_absent = true;
        next_try = now + backoff, backoff = std::min(backoff * 2, seconds(15));
        continue;
      }
      // The handshake, and Discord's READY (or its refusal: a close with its code).
      bool ready = false;
      std::string why;
      if (conn.Send(kOpHandshake, "{\"v\":1,\"client_id\":" + Quote(app_id) + "}")) {
        uint32_t op = 0;
        std::string json;
        const auto until = Clock::now() + seconds(5);
        while (!ready && conn.Open() && Clock::now() < until && conn.Receive(&op, &json, 500)) {
          if (op == kOpFrame && Field(json, "evt") == "READY") ready = true;
          else if (op == kOpClose) why = fmt::format("code {}, {}", Field(json, "code"), Field(json, "message")), conn.Close();
          else if (op == kOpPing) conn.Send(kOpPong, json);
        }
      }
      if (!ready) {
        conn.Close();
        // A refusal (an unknown application id: code 4000) is not tried again soon.
        if (!why.empty()) {
          REXLOG_WARN("BF discord: Discord ({}) refused the handshake ({}); trying again in 5 minutes", where, why);
          next_try = now + std::chrono::minutes(5);
        } else {
          if (told_lost < 5) REXLOG_INFO("BF discord: Discord ({}) did not finish the handshake; trying again later", where);
          ++told_lost;
          next_try = now + backoff, backoff = std::min(backoff * 2, seconds(15));
        }
        continue;
      }
      REXLOG_INFO("BF discord: connected to Discord ({})", where);
      told_absent = false, backoff = seconds(5);
      sent_json.clear(), sent_major.clear();
      sent_at = Clock::now() - seconds(60);
    }

    // Discord's answers: pings, a close, an error to a command (logged once per kind).
    {
      uint32_t op = 0;
      std::string json;
      while (conn.Open() && conn.Receive(&op, &json, 0)) {
        if (op == kOpPing) conn.Send(kOpPong, json);
        else if (op == kOpClose) conn.Close();
        else if (op == kOpFrame && Field(json, "evt") == "ERROR") {
          const std::string message = Field(json, "message");
          if (message != last_error) REXLOG_WARN("BF discord: Discord answered an update with an error: {}", message);
          last_error = message;
        }
      }
    }
    if (!conn.Open()) {
      if (told_lost < 5) REXLOG_INFO("BF discord: the connection to Discord closed; reconnecting quietly when it is back");
      ++told_lost;
      next_try = now + seconds(2), backoff = seconds(5);
      continue;
    }

    if (want.where != clock_where || want.map != clock_map) {
      const bool menus = [](Presence::Where w) {
        return w == Presence::Where::kStarting || w == Presence::Where::kLauncher || w == Presence::Where::kMenu ||
               w == Presence::Where::kBrowser;
      }(want.where);
      clock_start = menus ? game_start : UnixNow();
      clock_where = want.where, clock_map = want.map;
    }
    const Built b = Build(want, clock_start, fmt::format("bf1943-{:08x}", uint32_t(std::hash<std::string>{}(want.server + want.map))));
    if (b.json == sent_json) continue;
    // Held a second (a page passing through is never sent), then spaced out (Discord's limit: 5 in 20 s).
    if (now - want_at < seconds(1)) continue;
    const auto gap = b.major != sent_major ? seconds(4) : seconds(15);
    if (now - sent_at < gap) continue;
    std::string cmd = fmt::format("{{\"cmd\":\"SET_ACTIVITY\",\"args\":{{\"pid\":{}", Pid());
    if (!b.json.empty()) cmd += ",\"activity\":" + b.json;
    cmd += fmt::format("}},\"nonce\":\"{}\"}}", ++nonce);
    if (!conn.Send(kOpFrame, cmd)) continue;  // closed: reconnected at the next turn
    sent_json = b.json, sent_major = b.major, sent_at = now, sent_any = !b.json.empty();
#if defined(BF_DEV_TOOLS)
    REXLOG_INFO("BF discord: sent {}", cmd);
#endif
  }
}

}  // namespace

void Note(const Presence& p) {
  if (REXCVAR_GET(server)) return;
  if (REXCVAR_GET(bf_discord_app_id).empty()) {  // no application: nothing at all, not even the thread
#if defined(BF_DEV_TOOLS)
    static bool said = false;
    if (!said) REXLOG_INFO("BF discord: no application id (bf_discord_app_id): no Rich Presence");
    said = true;
#endif
    return;
  }
  Shared& s = S();
  {
    std::lock_guard lock(s.mutex);
    if (s.have && p == s.wanted) return;
    s.wanted = p, s.have = true, s.wanted_at = Clock::now();
  }
  s.wake.notify_one();
  if (!s.started.exchange(true)) {
    REXLOG_INFO("BF discord: Rich Presence on (Settings: {})", REXCVAR_GET(bf_discord_status) ? "on" : "off");
    std::thread(Run).detach();
  }
}

std::string ShownServerName(const std::string& name) {
  // An address in the name (a dotted quad, or a host and port): left out.
  int digits = 0, dots = 0;
  for (size_t i = 0; i < name.size(); ++i) {
    const char c = name[i];
    if (c >= '0' && c <= '9') ++digits;
    else if (c == '.' && digits > 0) ++dots, digits = 0;
    else {
      if (dots >= 3 && digits > 0) return "";
      if (c == ':' && i + 1 < name.size() && std::isdigit(uint8_t(name[i + 1])) && dots >= 1) return "";
      digits = 0, dots = 0;
    }
  }
  if (dots >= 3 && digits > 0) return "";
  return text_filter::Censor(name);
}

}  // namespace bf1943::discord
