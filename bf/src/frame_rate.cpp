// Frame rate above 30 (TekRantGaming, 5 Nov 2026: unlimited, with no speed, animation or physics problems).
//
// The game's main loop (sub_82397278) reads the setting Game.VsyncEnable (true on Xenon, Game.cfg). With it on,
// the game timer runs in mode 2 (sub_822BBCF8): frames locked to two vertical blanks, 30 per second whatever the
// guest refresh rate (runs fps1 and fps2: 30.0 at 60 and at 120 Hz). With it off the timer runs in mode 0, a
// variable frame time, optionally limited by Game.MaxFps (sub_822C4830).
//
// The game logic runs in ticks: the timer (sub_822C7388, called twice per frame) sets the tick count at timer+48
// to elapsed time times a rate, and the game runs the ticks between +52 and +48 each frame. With Game.MaxFps 0 the
// rate is the tick rate at +44 (30 per second, correct at any frame rate: run tbunl, 30.0 ticks per second at 75 to
// 115 FPS). With a limit it is Game.MaxFps itself: at 60 the game ran 61 ticks per second, twice as fast, and the
// server pulled the soldier back (TekRantGaming's play test; run tb60). So the game's own limit is never used: Game.MaxFps
// stays 0 and the limit is ours, a wait before each present (LimitFrame, from frame_stats.cpp's VdSwap).
//
// A dedicated server keeps the game's own timing: with the unlock on the server too, clients were kicked a few
// seconds after joining (runs fps3, fps4).
//
// Settings are read through the game's registry: sub_821F3248(registry, name) returns a pointer to a bool, and
// sub_82616750(registry, name, default) a pointer to a float (big endian in guest memory).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <fmt/format.h>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <timeapi.h>
#else
#include <linux/futex.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <ctime>
#endif

REXCVAR_DEFINE_BOOL(bf_unlock_fps, true, "BF1943", "Run above the original 30 frames per second");
namespace bf1943 {
bool LauncherShowing();  // game_menu.cpp: the launcher's own pages are up
int TickRate();  // tick_rate.cpp
}
REXCVAR_DECLARE(bool, server);  // server_mode.cpp: a dedicated server keeps the game's own timing
REXCVAR_DEFINE_DOUBLE(bf_max_fps, 60.0, "BF1943", "Frame rate limit with bf_unlock_fps (0: none)");

namespace {

bool Unlocked() { return REXCVAR_GET(bf_unlock_fps) && !REXCVAR_GET(server); }

std::string GuestString(const uint8_t* base, uint32_t address) {
  std::string s;
  for (uint32_t i = 0; address && i < 96 && base[address + i]; ++i) s += char(base[address + i]);
  return s;
}

void StoreFloat(uint8_t* base, uint32_t address, float value) {
  uint32_t bits;
  std::memcpy(&bits, &value, 4);
  base[address] = uint8_t(bits >> 24);
  base[address + 1] = uint8_t(bits >> 16);
  base[address + 2] = uint8_t(bits >> 8);
  base[address + 3] = uint8_t(bits);
}

uint32_t g_vsync = 0, g_max_fps = 0;  // guest addresses of the two settings, once the game has read them

#if defined(BF_DEV_TOOLS)
// Which settings the game reads: the shadow ones always, every name once with BF_CONFIG_PROBE=1.
bool LogSetting(const std::string& name) {
  if (name.find("hadow") != std::string::npos) return true;
  static const bool probe = [] {
    const char* v = std::getenv("BF_CONFIG_PROBE");
    return v && *v == '1';
  }();
  if (!probe) return false;
  static std::mutex mutex;
  static std::set<std::string> seen;
  std::lock_guard<std::mutex> lock(mutex);
  return seen.insert(name).second;
}
#endif

}  // namespace

// The game logic stays at 30 ticks per second. The main loop (sub_82396C48) creates its two timers
// (sub_822C7270(timer, rate, mode)) with a rate of 30; creating them at 60, on client and server alike, made the
// soldier move about twice as far per second (runs tt30, tt60): the step per tick is a fixed 1/30 s. Smooth motion
// above 30 needs the frames between ticks drawn from interpolated positions.

// Our menu's frame rate setting, live: 30 locks to the original timing, 0 is no limit.
void SetFrameRate(double fps) {
  rex::cvar::SetFlagByName("bf_unlock_fps", fps == 30 ? "false" : "true");
  rex::cvar::SetFlagByName("bf_max_fps", std::to_string(fps));
  if (REXCVAR_GET(server)) return;
  if (g_vsync) *REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(g_vsync) = fps == 30 ? 1 : 0;
  if (g_max_fps) StoreFloat(REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(g_max_fps), 0, 0.0f);
}

// Windows' timer at 1 ms for the whole process (7 Nov 2026). The runtime turns the game's Sleep(n) into ::Sleep(n)
// (xthread.cpp, Delay), and at the default 15.6 ms resolution a Sleep(1) in a job wait, or our limiter's sleep below,
// took up to 15.6 ms: run hitch1, at the players' default 60 FPS limit, had 422 frames over 20 ms in three minutes,
// most with the renderer idle. Windows 11 stops honouring a process's resolution while its window is hidden or
// covered unless it opts out of that power throttling, so it does (a background window here, Alt-Tab for a player).
namespace bf1943 {
void RaiseTimerResolution() {
#if defined(_WIN32)
#if defined(BF_DEV_TOOLS)
  if (const char* v = std::getenv("BF_TIMER_DEFAULT"); v && *v == '1') return;  // A/B tests: Windows' own timer
#endif
#ifndef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
#define PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION 0x4
#endif
  PROCESS_POWER_THROTTLING_STATE state{};
  state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
  state.ControlMask = PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
  state.StateMask = 0;  // the control bit on, the state off: timer requests are always honoured
  // A guest Sleep(1)'s length before and after, as the log's evidence (three each, the longest).
  auto sleep_ms = [] {
    double longest = 0;
    for (int i = 0; i < 3; ++i) {
      const auto start = std::chrono::steady_clock::now();
      ::Sleep(1);
      longest = std::max(longest, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
    return longest;
  };
  const double before = sleep_ms();
  const bool kept = SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
  const bool raised = timeBeginPeriod(1) == TIMERR_NOERROR;
  const double after = sleep_ms();
  REXLOG_INFO("BF timer: 1 ms resolution {}{}; Sleep(1) took up to {:.1f} ms before, {:.1f} ms after",
              raised ? "set" : "refused", kept ? ", kept while the window is hidden" : "", before, after);
#endif
}
}  // namespace bf1943

namespace bf1943 {
// The time our limit waited (microseconds) since the last call, for the FPS log line (frame_stats.cpp): the frame
// time less this is what the frames took, so a capped run still shows how much room there was.
std::atomic<uint64_t> g_limit_waited_us{0};
uint64_t TakeLimitWaitedUs() { return g_limit_waited_us.exchange(0); }
}

// Our frame limit, before each present: wait until 1/bf_max_fps after the last frame (a high resolution timer, then
// a spin through the last half millisecond for even pacing). A slow frame starts a new schedule instead of rushing
// to catch up.
// The launcher's own pages (Home, Settings from Home, the setup...: LauncherShowing) at bf_menu_max_fps at most
// (15 Nov 2026): with an Unlimited frame rate the idle Home page took 46% of an RTX 4070 Ti, 19% at 60 FPS, and a
// static menu gains nothing from more frames (a weaker GPU only runs hot). A match, and its pause menu, keep the
// chosen rate.
REXCVAR_DEFINE_INT32(bf_menu_max_fps, 60, "BF1943",
                     "The launcher's pages (not a match nor its pause menu) at this many frames a second at most; 0: as "
                     "the frame rate setting");
void LimitFrame() {
#if defined(BF_DEV_TOOLS) && defined(_WIN32)
  // BF_TIMER_PROBE=1: every 600 frames, how long a Sleep(1) takes now (the window in the background in tests).
  static const bool probe = [] {
    const char* v = std::getenv("BF_TIMER_PROBE");
    return v && *v == '1';
  }();
  static uint64_t probe_frames = 0;
  if (probe && ++probe_frames % 600 == 0) {
    double longest = 0;
    for (int i = 0; i < 3; ++i) {
      const auto start = std::chrono::steady_clock::now();
      ::Sleep(1);
      longest = std::max(longest, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
    REXLOG_INFO("BF timer probe: frame {}: Sleep(1) took up to {:.1f} ms", probe_frames, longest);
  }
#endif
  if (!Unlocked()) return;
#if defined(BF_DEV_TOOLS)
  // BF_DEV_NO_LIMIT=1 (tests): our limit never waits (does a load go faster without it?).
  static const bool no_limit = [] {
    const char* v = std::getenv("BF_DEV_NO_LIMIT");
    return v && *v == '1';
  }();
  if (no_limit) return;
#endif
  double fps = REXCVAR_GET(bf_max_fps);
  if (const int menu = REXCVAR_GET(bf_menu_max_fps); menu > 0 && bf1943::LauncherShowing() && (fps <= 0 || fps > menu))
    fps = menu;
  using Clock = std::chrono::steady_clock;
  static Clock::time_point next{};
#if defined(BF_DEV_TOOLS)
  // BF_DEV_SLOW_FRAMES=<min ms>,<max ms> (tests: a slow computer): each frame at least a random time in that range after
  // the last, whatever the chosen frame rate (our own limit is then left out).
  static const std::pair<double, double> slow = [] {
    const char* v = std::getenv("BF_DEV_SLOW_FRAMES");
    if (!v || !*v) return std::pair<double, double>{0.0, 0.0};
    const double lo = std::atof(v);
    const char* comma = std::strchr(v, ',');
    return std::pair<double, double>{lo, comma ? std::max(lo, std::atof(comma + 1)) : lo};
  }();
  if (slow.first > 0) {
    static Clock::time_point slow_last{};
    static uint32_t seed = 12345;
    seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5;
    const double ms = slow.first + (slow.second - slow.first) * double(seed & 0xFFFF) / 65535.0;
    const auto until = slow_last + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double, std::milli>(ms));
    while (Clock::now() < until) std::this_thread::sleep_for(std::chrono::microseconds(200));
    slow_last = Clock::now();
    return;
  }
#endif
  const auto now = Clock::now();
  if (fps <= 0) {
    next = now;
    return;
  }
  const auto period = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / fps));
  if (next.time_since_epoch().count() == 0 || now > next + period) next = now;
  if (now < next) {
    const auto sleep = next - now - std::chrono::microseconds(500);
    if (sleep > std::chrono::microseconds(200)) {
#if defined(_WIN32)
      // A high resolution waitable timer (Windows 10 1803 and later) wakes within about 0.1 ms whatever the timer
      // resolution; else the 1 ms resolution above.
      static thread_local HANDLE timer =
          CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
      LARGE_INTEGER due;
      due.QuadPart = -std::max<long long>(1, std::chrono::duration_cast<std::chrono::nanoseconds>(sleep).count() / 100);
      if (timer && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
        WaitForSingleObject(timer, INFINITE);
      } else {
        std::this_thread::sleep_for(sleep);
      }
#else
      std::this_thread::sleep_for(sleep);
#endif
    }
    while (Clock::now() < next) std::this_thread::yield();
    bf1943::g_limit_waited_us += uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - now).count());
  }
  next += period;
}

// The game's job scheduler, sub_82609000(scheduler, flags): runs the oldest job it can claim and returns 1, or 0 when
// there is none. Threads waiting for jobs call it in a loop (the main thread: about 78% of its time in this morning's
// profile, sub_823C2430), so with no work it spins a core. bf_job_wait_yield: after 64 empty calls in a row on a
// thread, it gives up the rest of its time slice (SwitchToThread), so the game's workers get the core on PCs with
// few cores. Measured (runs jobyield0, jobyield1, Wake Island at 90 FPS): 2.37 cores without, 2.27 with, the same
// frame times (11.1 ms average, 11.2 ms 99%): the wait costs little, so it stayed off then.
// On by default since 14 Nov 2026 (the performance brief): on a PC with few cores the waiting threads took the cores
// from the threads with the work. The client held to two cores of this PC (Wake Island's carrier, no frame limit but
// the 90 FPS the menu offers here): without it 78 FPS, 1% low 19 to 23 FPS, 204 to 210 frames over twice the average
// (runs pb_base_wake2c2, 2c3); with it 88 to 90 FPS, 1% low 77 FPS, no frame over 50 ms (pb_yield_wake2c, 2c2),
// on less CPU (1.20 cores against 1.37). On four cores the longest frame 11.5 ms instead of 14.2 (pb_yield_wake4c,
// pb_base_wake4c); on all sixteen nothing changes (pb_yield_wake16: 90.0 FPS, 0.1% low 86). A thread with no other
// thread ready on its core gets the core straight back, so a PC with cores to spare loses nothing.
REXCVAR_DEFINE_BOOL(bf_job_wait_yield, true, "BF1943",
                    "A thread waiting for the game's jobs yields its core after 64 empty polls (fewer cores busy)");
// bf_job_wait_sleep: the main thread's own wait only (its call from sub_823C2430's loop, return address 823C24C8;
// the workers already block on events, run loadstacks1): after 64 empty polls it sleeps a quarter millisecond on a
// high resolution timer instead of spinning, so a capped frame rate leaves that core idle (at the 90 FPS cap the
// frame's work is about 3.6 ms of 11.1, the rest of the main thread's time spent polling). Measured (Wake Island,
// the carrier): at the 90 FPS cap 1.49 cores instead of 2.21 with the same frame times (11.1 ms, 99% 11.2 ms; runs
// jobsleep1, jobsleep0); without our limit 110 FPS either way, 2.46 cores instead of 3.01 (jobsleep1u, 0u). On.
REXCVAR_DEFINE_BOOL(bf_job_wait_sleep, true, "BF1943",
                    "The game's main thread sleeps 0.25 ms at a time while it waits for its jobs, not spinning");
#if defined(BF_DEV_TOOLS)
// BF_DEV_JOBWAITS=1 (dev builds): the game's waits for its jobs by caller, logged every 10 s: how many waits (runs of
// empty polls from one call site on one thread), their total and longest time, and the empty polls. A wait ends at
// a poll that ran a job, or when the thread next polls from another call site.
namespace jobwake {
std::string Stats();
}
namespace jobwaits {
using Clock = std::chrono::steady_clock;
struct Site {
  uint64_t waits = 0, polls = 0, total_us = 0, max_us = 0;
};
std::mutex g_mutex;
std::vector<std::pair<uint32_t, Site>> g_sites;
Clock::time_point g_logged{};
bool On() {
  static const bool on = [] {
    const char* v = std::getenv("BF_DEV_JOBWAITS");
    return v && *v == '1';
  }();
  return on;
}
struct Run {
  uint32_t caller = 0, polls = 0;
  Clock::time_point first{}, last{};
};
void End(Run& run) {
  if (!run.polls) return;
  const uint64_t us = uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(run.last - run.first).count());
  std::lock_guard lock(g_mutex);
  auto it = std::find_if(g_sites.begin(), g_sites.end(), [&](const auto& s) { return s.first == run.caller; });
  if (it == g_sites.end()) it = g_sites.insert(g_sites.end(), {run.caller, Site{}});
  ++it->second.waits, it->second.polls += run.polls, it->second.total_us += us;
  it->second.max_us = std::max(it->second.max_us, us);
  run.polls = 0;
  const auto now = Clock::now();
  if (g_logged == Clock::time_point{}) g_logged = now;
  const double secs = std::chrono::duration<double>(now - g_logged).count();
  if (secs < 10) return;
  std::sort(g_sites.begin(), g_sites.end(), [](const auto& a, const auto& b) { return a.second.total_us > b.second.total_us; });
  std::string line;
  for (size_t i = 0; i < g_sites.size() && i < 10; ++i) {
    const Site& s = g_sites[i].second;
    line += fmt::format("; {:08X}: {:.0f} waits/s, {:.1f} ms/s, longest {:.2f} ms, {:.0f} polls/s", g_sites[i].first,
                        s.waits / secs, s.total_us / 1000.0 / secs, s.max_us / 1000.0, s.polls / secs);
  }
  REXLOG_INFO("BF job waits over {:.0f} s{}; {}", secs, line, jobwake::Stats());
  g_sites.clear();
  g_logged = now;
}
void Poll(uint32_t caller, bool ran) {
  thread_local Run run;
  if (ran || run.caller != caller) End(run);
  if (ran) return;
  const auto now = Clock::now();
  if (!run.polls) run.caller = caller, run.first = now;
  run.last = now;
  ++run.polls;
}
}  // namespace jobwaits
#endif

// bf_job_wait_wake (with bf_job_wait_sleep): the main thread's frame wait (sub_823C2430: while the frame's job group
// at client+3248 is not done, poll the scheduler) sleeps until another thread's poll has run a job, or a quarter
// millisecond, instead of a quarter millisecond after every 64 polls. The wait ends when the frame's jobs are done,
// and the game's jobs run in these polls (its JobManager threads), so a job run is when it can have ended. Measured
// before (BF_DEV_JOBWAITS, run pr_jw1, Wake Island at the 90 FPS cap): that wait took 817 ms a second of the main
// thread's time in 102,000 polls a second, each through the scheduler's lock: the main thread's 39% of a core was
// nearly all those polls. Every wait still starts with 64 polls, so a short one never sleeps.
REXCVAR_DEFINE_BOOL(bf_job_wait_wake, true, "BF1943",
                    "The game's main thread sleeps in its frame wait until another thread has run a job");
REXCVAR_DEFINE_INT32(bf_job_wait_wake_processors, 4, "BF1943",
                     "bf_job_wait_wake only with at least this many processors (fewer: the frame wait polls)");
// bf_job_wait_sleep_unlimited (15 Nov 2026): whether the frame wait sleeps (bf_job_wait_sleep, bf_job_wait_wake) with
// no frame rate limit too. Pull request 28 made it poll then, for the most frames a second; but its polls hold the
// main thread at 95% of a core (57% of it in the scheduler's polls, run o15_prof_low) and on a processor with few
// cores they take the core the game's jobs need. Measured with no limit (Wake Island, 50 s a run): all 16 threads
// 226.5 and 224.6 FPS sleeping against 226.7 and 226.2 polling, on 3.15 cores instead of 3.76; four cores 1% low 127.8
// FPS and 12 frames over twice the average sleeping, 97.8 and 75 polling; two cores 132.9 FPS sleeping, 50.3 polling
// (runs o15_sleep_*, o15_poll_*). 1 (the default): sleep; 0: poll (pull request 28); -1: sleep only with fewer than
// bf_job_wait_sleep_unlimited_processors processors.
REXCVAR_DEFINE_INT32(bf_job_wait_sleep_unlimited, 1, "BF1943",
                     "With no frame rate limit, the main thread's frame wait sleeps (1), polls (0), or sleeps only on "
                     "processors with fewer than bf_job_wait_sleep_unlimited_processors processors (-1)");
REXCVAR_DEFINE_INT32(bf_job_wait_sleep_unlimited_processors, 8, "BF1943",
                     "bf_job_wait_sleep_unlimited -1: the frame wait sleeps with no limit on fewer processors than this");
namespace jobwake {
std::atomic<uint32_t> g_runs{0};       // jobs run by any thread's poll (the futex word on Linux)
std::atomic<bool> g_sleeping{false};  // the main thread sleeps in its frame wait
#if defined(_WIN32)
HANDLE Event() {
  static const HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  return event;
}
#endif
#if defined(BF_DEV_TOOLS)
std::atomic<uint64_t> g_woken{0}, g_timed_out{0}, g_skipped{0};
#endif
void Ran() {
  g_runs.fetch_add(1);
  if (!g_sleeping.load()) return;
#if defined(_WIN32)
  SetEvent(Event());
#else
  syscall(SYS_futex, reinterpret_cast<uint32_t*>(&g_runs), FUTEX_WAKE_PRIVATE, 1, nullptr, nullptr, 0);
#endif
}
// With fewer processors (bf_job_wait_wake_processors) the main thread's polls in this wait are the ones that pick up
// the frame's jobs first, while the JobManager threads wait for a core: there, the wait polls as before. Waking it
// when a job is posted as well (the job manager's wake of its workers, sub_826070B8, about 19,000 a second) did not
// help: on two cores still 13 to 22 frames over twice the average against 1 to 6 polling (runs pr_post2c_*).
// The processors this process may run on (its affinity), read again at most once a second.
uint32_t Processors() {
  using Clock = std::chrono::steady_clock;
  static std::atomic<uint32_t> count{0};
  static std::atomic<int64_t> read_at{0};
  const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
  if (count.load(std::memory_order_relaxed) && now - read_at.load(std::memory_order_relaxed) < 1000)
    return count.load(std::memory_order_relaxed);
  read_at.store(now, std::memory_order_relaxed);
  uint32_t n = 0;
#if defined(_WIN32)
  DWORD_PTR process = 0, system = 0;
  if (GetProcessAffinityMask(GetCurrentProcess(), &process, &system))
    for (; process; process &= process - 1) ++n;
#else
  cpu_set_t set;
  if (sched_getaffinity(0, sizeof(set), &set) == 0) n = uint32_t(CPU_COUNT(&set));
#endif
  if (!n) n = std::max(1u, std::thread::hardware_concurrency());
  count.store(n, std::memory_order_relaxed);
  return n;
}
// Sleeps unless a job was run since `before` (read before this thread's poll); false when the wait could not be made.
bool Sleep(uint32_t before) {
  g_sleeping.store(true);
  if (g_runs.load() != before) {
    g_sleeping.store(false);
#if defined(BF_DEV_TOOLS)
    ++g_skipped;
#endif
    return true;
  }
  bool woken = false, ok = false;
#if defined(_WIN32)
  static thread_local HANDLE timer =
      CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  LARGE_INTEGER due;
  due.QuadPart = -2500;  // 0.25 ms, in 100 ns units
  if (timer && Event() && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
    const HANDLE handles[2] = {Event(), timer};
    const DWORD r = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
    woken = r == WAIT_OBJECT_0, ok = r == WAIT_OBJECT_0 || r == WAIT_OBJECT_0 + 1;
  }
#else
  const timespec timeout{0, 250000};
  const long r = syscall(SYS_futex, reinterpret_cast<uint32_t*>(&g_runs), FUTEX_WAIT_PRIVATE, before, &timeout,
                         nullptr, 0);
  woken = r == 0 || errno == EAGAIN, ok = r == 0 || errno == EAGAIN || errno == ETIMEDOUT || errno == EINTR;
#endif
  g_sleeping.store(false);
#if defined(BF_DEV_TOOLS)
  if (ok) ++(woken ? g_woken : g_timed_out);
#endif
  return ok;
}
#if defined(BF_DEV_TOOLS)
std::string Stats() {
  return fmt::format("the frame wait's sleeps: {} woken by a job, {} timed out, {} not made (a job had run)",
                     g_woken.exchange(0), g_timed_out.exchange(0), g_skipped.exchange(0));
}
#endif
}  // namespace jobwake

REX_EXTERN(__imp__sub_82609000);
REX_HOOK_RAW(sub_82609000) {
  const uint32_t caller = uint32_t(ctx.lr);
  const bool frame_wait = caller == 0x823C24C8u;
  const bool wake = REXCVAR_GET(bf_job_wait_wake) && REXCVAR_GET(bf_job_wait_sleep);
  const uint32_t runs_before = wake && frame_wait ? jobwake::g_runs.load() : 0;
  __imp__sub_82609000(ctx, base);
  const bool ran = ctx.r3.u32 & 0xFF;
  if (wake && ran) jobwake::Ran();
#if defined(BF_DEV_TOOLS)
  if (jobwaits::On()) jobwaits::Poll(caller, ran);
#endif
  // With a frame rate limit; with none, as bf_job_wait_sleep_unlimited says (DiXN, pull request 28: it polled then).
  const int unlimited = REXCVAR_GET(bf_job_wait_sleep_unlimited);
  const bool limited = !Unlocked() || REXCVAR_GET(bf_max_fps) > 0;
  const bool sleep = REXCVAR_GET(bf_job_wait_sleep) && frame_wait &&
                     (limited || unlimited > 0 ||
                      (unlimited < 0 && jobwake::Processors() <
                                            uint32_t(std::max(1, REXCVAR_GET(bf_job_wait_sleep_unlimited_processors)))));
  if (!sleep && !REXCVAR_GET(bf_job_wait_yield)) return;
  // `empty` counts a thread's empty polls from any call site, as it always has: counting them per call site instead
  // (reset when the site changed) made the waiting threads yield less, and on two cores the 1% low fell from 83 to
  // 40 to 74 FPS (runs pr_nowake2c_*, 14 Nov 2026). The frame wait's own count is kept apart.
  thread_local uint32_t empty = 0, frame_wait_polls = 0;
  if (ran) {
    empty = 0, frame_wait_polls = 0;
    return;
  }
  if (!frame_wait) frame_wait_polls = 0;
  if (sleep && wake && jobwake::Processors() >= uint32_t(std::max(1, REXCVAR_GET(bf_job_wait_wake_processors)))) {
    // After the wait's first 64 polls: one poll per sleep, each until a job was run or 0.25 ms.
    if (frame_wait_polls < 64) {
      ++frame_wait_polls;
      return;
    }
    if (jobwake::Sleep(runs_before)) return;
  }
  if (++empty < 64) return;
  empty = 0;
  if (sleep) {
#if defined(_WIN32)
    static thread_local HANDLE timer =
        CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    LARGE_INTEGER due;
    due.QuadPart = -2500;  // 0.25 ms, in 100 ns units
    if (timer && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
      WaitForSingleObject(timer, INFINITE);
      return;
    }
#endif
    std::this_thread::sleep_for(std::chrono::microseconds(250));
    return;
  }
  std::this_thread::yield();
}

REX_EXTERN(__imp__sub_821F3248);
REX_HOOK_RAW(sub_821F3248) {
  const std::string name = GuestString(base, ctx.r4.u32);
  __imp__sub_821F3248(ctx, base);
#if defined(BF_DEV_TOOLS)
  if (ctx.r3.u32 && LogSetting(name)) REXLOG_INFO("BF setting {} = {}", name, base[ctx.r3.u32]);
#endif
  if (ctx.r3.u32 && name == "Game.VsyncEnable") g_vsync = ctx.r3.u32;
  if (Unlocked() && ctx.r3.u32 && name == "Game.VsyncEnable") {
    base[ctx.r3.u32] = 0;
    REXLOG_INFO("BF frame rate: Game.VsyncEnable off (frame rate unlocked)");
  }
}

REX_EXTERN(__imp__sub_82616750);
REX_HOOK_RAW(sub_82616750) {
  const std::string name = GuestString(base, ctx.r4.u32);
  __imp__sub_82616750(ctx, base);
  // Every setting the game asks for by name, once each with its value (13 Nov 2026: which of them are the level of
  // detail's and the view distance's is not known yet; the log's list says). Dev builds only (600 lines).
#if defined(BF_DEV_TOOLS)
  if (ctx.r3.u32 && !name.empty()) {
    static std::mutex names_mutex;
    static std::set<std::string> names;
    std::lock_guard lock(names_mutex);
    if (names.size() < 600 && names.insert(name).second) {
      const uint32_t bits = uint32_t(base[ctx.r3.u32]) << 24 | base[ctx.r3.u32 + 1] << 16 | base[ctx.r3.u32 + 2] << 8 |
                            base[ctx.r3.u32 + 3];
      float value;
      std::memcpy(&value, &bits, 4);
      REXLOG_INFO("BF game setting: {} = {} (bits {:08X})", name, value, bits);
    }
  }
#endif
#if defined(BF_DEV_TOOLS)
  if (ctx.r3.u32 && LogSetting(name)) {
    uint32_t bits = uint32_t(base[ctx.r3.u32]) << 24 | base[ctx.r3.u32 + 1] << 16 | base[ctx.r3.u32 + 2] << 8 |
                    base[ctx.r3.u32 + 3];
    float value;
    std::memcpy(&value, &bits, 4);
    REXLOG_INFO("BF setting {} = {}", name, value);
  }
#endif
  if (ctx.r3.u32 && name == "Game.MaxFps") g_max_fps = ctx.r3.u32;
  // A dedicated server's main timer ticks once a frame, paced by Game.MaxFps (60 Hz research, tick_rate.cpp): at
  // another rate than 30 it is paced at that rate, so its ticks a second match its players'.
  if (REXCVAR_GET(server) && ctx.r3.u32 && name == "Game.MaxFps" && bf1943::TickRate() != 30) {
    uint32_t bits = uint32_t(base[ctx.r3.u32]) << 24 | base[ctx.r3.u32 + 1] << 16 | base[ctx.r3.u32 + 2] << 8 |
                    base[ctx.r3.u32 + 3];
    float was;
    std::memcpy(&was, &bits, 4);
    StoreFloat(base, ctx.r3.u32, float(bf1943::TickRate()));
    REXLOG_INFO("BF tick rate: the server's Game.MaxFps {} -> {}", was, bf1943::TickRate());
  }
  if (Unlocked() && ctx.r3.u32 && name == "Game.MaxFps") {
    // 0: the game's timer keeps its 30 ticks per second; the limit is LimitFrame's.
    StoreFloat(base, ctx.r3.u32, 0.0f);
    REXLOG_INFO("BF frame rate: Game.MaxFps 0, our limit {}", REXCVAR_GET(bf_max_fps));
  }
}

// The Flash UI (HUD, menus) above 30. The UI's update (sub_826B6698(ui, frame time)) advances the Flash player one
// movie frame per game frame: sub_82954388(player, 33, 1) into sub_82991B20, which counts one frame. The movies are
// made for 30 frames per second, so at 60 the HUD's animations ran twice as fast. Here the player advances on a
// 30 Hz clock instead: some frames skip the call, a slow frame makes up one step at most. The UI still draws every
// frame. With the frame rate locked to 30 every call goes through.
REXCVAR_DEFINE_BOOL(bf_ui_30hz, true, "BF1943", "Advance the Flash UI at 30 frames per second at any frame rate");

REX_EXTERN(__imp__sub_82954388);
REX_HOOK_RAW(sub_82954388) {
  if (!Unlocked() || !REXCVAR_GET(bf_ui_30hz) || ctx.r4.u32 != 33) return __imp__sub_82954388(ctx, base);
  using Clock = std::chrono::steady_clock;
  constexpr double kStep = 1.0 / 30;
  static Clock::time_point last = Clock::now();
  static double owed = kStep;  // seconds of UI time not advanced yet
  const Clock::time_point now = Clock::now();
  owed += std::min(std::chrono::duration<double>(now - last).count(), 0.25);
  last = now;
#if defined(BF_DEV_TOOLS)
  static int frames = 0, advances = 0;
  static Clock::time_point counted = now;
  ++frames;
  if (now - counted >= std::chrono::seconds(5)) {
    REXLOG_INFO("BF UI clock: {} game frames, {} Flash advances in 5 s", frames, advances);
    frames = advances = 0;
    counted = now;
  }
#endif
  if (owed < kStep) return;
  PPCContext saved = ctx;
  for (int steps = 0; owed >= kStep && steps < 2; ++steps) {
    if (steps) ctx = saved;
    __imp__sub_82954388(ctx, base);
    owed -= kStep;
#if defined(BF_DEV_TOOLS)
    ++advances;
#endif
  }
  owed = std::min(owed, kStep);
}

// Physics in time with the game's tick (TekRantGaming: "physics is in slow motion. When I broke a tree, it came down very
// slowly. When I blew up a building, the rubble moved slowly"). sub_8253EDF8(world, tick time f1, frame time f2) runs
// once a game tick (30 a second, more when it catches up): its own steps take the tick's 1/30 s, but a second system
// in it (sub_82537AC8, its time smoothed toward f2) took the frame's time, so above 30 FPS it advanced 9 ms a tick at
// 110 FPS instead of 33 (runs args110, args30). On the Xbox the two are equal: the tick's time goes in both.
REXCVAR_DEFINE_BOOL(bf_physics_tick_time, true, "BF1943",
                    "Physics effects (debris, falling trees) at the right speed above 30 FPS: the game tick's time for "
                    "the tick's second physics step, as at 30 FPS");
REX_EXTERN(__imp__sub_8253EDF8);
REX_HOOK_RAW(sub_8253EDF8) {
  if (REXCVAR_GET(bf_physics_tick_time) && ctx.f1.f64 > 0.0) ctx.f2.f64 = ctx.f1.f64;
  const uint32_t world = ctx.r3.u32;
  __imp__sub_8253EDF8(ctx, base);
#if defined(BF_DEV_TOOLS)
  // BF_PHYS_LOG=1: every 5 s, the smoothed time the second system stepped with (world + 0xA32C).
  static const bool log = std::getenv("BF_PHYS_LOG") != nullptr;
  if (log) {
    static auto last = std::chrono::steady_clock::now();
    if (std::chrono::steady_clock::now() - last > std::chrono::seconds(5)) {
      last = std::chrono::steady_clock::now();
      const uint8_t* p = base + world + 0xA32C;
      const uint32_t u = uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
      float v;
      std::memcpy(&v, &u, 4);
      REXLOG_INFO("BF physics: second step time {:.6f}", v);
    }
  }
#endif
}
