// What a dedicated server's tick is doing when it runs long (Linux; BF_TICK_PROBE=<ms>, off unless set). 15 Nov 2026,
// the US Central server's rubber banding: its log showed ticks of 50 to 500 ms in nearly every 30 s window while the
// Windows servers' longest tick under the same load was about 8 ms. The tick's own timing says only that it was long.
//
// A watchdog thread looks at the tick every 2 ms. Once a tick has run that many milliseconds it records, for the game's
// main thread: its scheduler state and the system call it is in (/proc/self/task/<tid>/stat and syscall), then its
// stack (a signal to that thread; the handler walks its own stack). Again at 2x, 4x and 8x the threshold in the same
// tick, so a long tick shows whether it stays in one place. Between ticks, a wait that lasts a tick and the threshold
// longer is sampled once the same way (what the thread waits on). At most 40 lines every 30 s. Each frame is module+offset
// for addr2line on the build's own binary (a recompiled guest function's frame names its guest address: sub_8xxxxxxx).
#if defined(__linux__)

#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <unwind.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include <rex/logging.h>

namespace {
using Clock = std::chrono::steady_clock;
constexpr int kDepth = 24;

std::atomic<int64_t> g_start_ns{0};  // the running tick's start (steady clock ns), 0 between ticks
std::atomic<int64_t> g_tick_id{0};
std::atomic<int64_t> g_end_ns{0};    // the last tick's end
pthread_t g_main{};
pid_t g_main_tid = 0;
double g_threshold_ms = 0;

// The handler's result: filled on the main thread, read by the watchdog after it is set done.
uint64_t g_frames[kDepth];
std::atomic<int> g_frame_count{0};
std::atomic<bool> g_done{false};

struct Walk {
  uint64_t* out;
  int n, cap;
};
_Unwind_Reason_Code Step(_Unwind_Context* c, void* arg) {
  auto* w = static_cast<Walk*>(arg);
  const uint64_t ip = _Unwind_GetIP(c);
  if (!ip) return _URC_END_OF_STACK;
  w->out[w->n++] = ip;
  return w->n < w->cap ? _URC_NO_REASON : _URC_END_OF_STACK;
}

void OnProbe(int) {
  const int saved = errno;
  Walk w{g_frames, 0, kDepth};
  _Unwind_Backtrace(Step, &w);
  g_frame_count.store(w.n, std::memory_order_relaxed);
  g_done.store(true, std::memory_order_release);
  errno = saved;
}

int ProbeSignal() { return SIGRTMIN + 8; }  // the SDK uses SIGRTMIN and +1 (threading_posix.cpp)

std::string ReadSmall(const char* path) {
  FILE* f = std::fopen(path, "r");
  if (!f) return {};
  char buf[512];
  const size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
  std::fclose(f);
  buf[n] = 0;
  std::string s(buf);
  while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
  return s;
}

// The state letter (R running, S sleeping, D disk...) from /proc/.../stat, after the command name's ')'.
char ThreadState() {
  char path[64];
  std::snprintf(path, sizeof(path), "/proc/self/task/%d/stat", g_main_tid);
  const std::string s = ReadSmall(path);
  const size_t close = s.rfind(')');
  return close != std::string::npos && close + 2 < s.size() ? s[close + 2] : '?';
}

std::string ThreadSyscall() {
  char path[64];
  std::snprintf(path, sizeof(path), "/proc/self/task/%d/syscall", g_main_tid);
  std::string s = ReadSmall(path);
  // "running" or "<nr> <args...> <sp> <pc>": the number and the first two arguments are enough.
  size_t cut = 0;
  for (int fields = 0; cut < s.size() && fields < 3; ++cut)
    if (s[cut] == ' ') ++fields;
  return cut && cut < s.size() ? s.substr(0, cut - 1) : s;
}

std::string Frame(uint64_t ip) {
  Dl_info info{};
  if (dladdr(reinterpret_cast<void*>(ip), &info) && info.dli_fname) {
    const char* name = std::strrchr(info.dli_fname, '/');
    name = name ? name + 1 : info.dli_fname;
    const uint64_t off = ip - reinterpret_cast<uint64_t>(info.dli_fbase);
    if (info.dli_sname) return fmt::format("{}+{:x}({})", name, off, info.dli_sname);
    return fmt::format("{}+{:x}", name, off);
  }
  return fmt::format("{:x}", ip);
}

std::string Sample() {
  const char state = ThreadState();
  const std::string call = ThreadSyscall();
  g_done.store(false);
  g_frame_count.store(0);
  std::string stack;
  if (pthread_kill(g_main, ProbeSignal()) == 0) {
    const auto until = Clock::now() + std::chrono::milliseconds(20);
    while (!g_done.load(std::memory_order_acquire) && Clock::now() < until) std::this_thread::yield();
    if (g_done.load(std::memory_order_acquire)) {
      const int n = g_frame_count.load();
      for (int i = 1; i < n; ++i) stack += " " + Frame(g_frames[i]);  // frame 0 is the handler
    } else {
      stack = " (no answer to the signal in 20 ms)";
    }
  }
  return fmt::format("state {}, syscall {}; stack{}", state, call.empty() ? "?" : call, stack);
}

void Watch() {
  pthread_setname_np(pthread_self(), "BF tick probe");
  int64_t sampled_tick = -1, sampled_wait = 0;
  int level = 0;  // samples taken in this tick: at 1x, 2x, 4x, 8x the threshold
  auto window = Clock::now();
  int lines = 0;
  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto now = Clock::now();
    if (now - window > std::chrono::seconds(30)) window = now, lines = 0;
    const int64_t start = g_start_ns.load(std::memory_order_acquire);
    if (!start) {
      // Between ticks: a wait that has lasted a tick (16.7 ms at 60) plus the threshold, sampled once.
      const int64_t end = g_end_ns.load(std::memory_order_acquire);
      const double waited = end ? double(now.time_since_epoch().count() - end) / 1e6 : 0;
      if (end && end != sampled_wait && waited >= 16.7 + g_threshold_ms && lines < 40) {
        sampled_wait = end;
        ++lines;
        const std::string what = Sample();
        const bool same = g_start_ns.load() == 0 && g_end_ns.load() == end;
        REXLOG_INFO("BF tick probe: waiting {:.0f} ms after tick {}{}: {}", waited, g_tick_id.load(),
                    same ? "" : " (the next tick began meanwhile)", what);
      }
      continue;
    }
    const int64_t id = g_tick_id.load(std::memory_order_relaxed);
    if (id != sampled_tick) sampled_tick = id, level = 0;
    if (level >= 4) continue;
    const double ms = double(now.time_since_epoch().count() - start) / 1e6;
    if (ms < g_threshold_ms * double(1 << level)) continue;
    ++level;
    if (lines >= 40) continue;
    ++lines;
    const std::string what = Sample();
    // Still the same tick? (It may have ended while the stack was taken.)
    const bool same = g_tick_id.load() == id && g_start_ns.load() == start;
    REXLOG_INFO("BF tick probe: tick {} at {:.0f} ms{}: {}", id, ms, same ? "" : " (it ended meanwhile)", what);
  }
}
}  // namespace

namespace bf1943 {
// Called on the game's main thread at its tick's start and end (server_rotation.cpp's tick hook).
void TickProbeBegin() {
  static const bool on = [] {
    const char* v = std::getenv("BF_TICK_PROBE");
    g_threshold_ms = v && *v ? std::atof(v) : 0.0;
    if (g_threshold_ms <= 0) return false;
    g_main = pthread_self();
    g_main_tid = static_cast<pid_t>(syscall(SYS_gettid));
    struct sigaction sa {};
    sa.sa_handler = OnProbe;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(ProbeSignal(), &sa, nullptr);
    Walk warm{g_frames, 0, 2};
    _Unwind_Backtrace(Step, &warm);  // libgcc loaded now, not in the handler
    std::thread(Watch).detach();
    REXLOG_INFO("BF tick probe: on, ticks of {:.0f} ms or more sampled (thread {})", g_threshold_ms, g_main_tid);
    return true;
  }();
  if (!on) return;
  g_tick_id.fetch_add(1, std::memory_order_relaxed);
  g_start_ns.store(Clock::now().time_since_epoch().count(), std::memory_order_release);
}
void TickProbeEnd() {
  g_end_ns.store(Clock::now().time_since_epoch().count(), std::memory_order_release);
  g_start_ns.store(0, std::memory_order_release);
}
}  // namespace bf1943

#else
namespace bf1943 {
void TickProbeBegin() {}
void TickProbeEnd() {}
}  // namespace bf1943
#endif
