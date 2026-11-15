// NativeXRenderer - see cpu_cores.h. (Battlefield 1943 Reborn, 15 Nov 2026: weak processors' frame pacing.)

#include "cpu_cores.h"

#include <rex/cvar.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <set>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sched.h>

#include <fstream>
#endif

REXCVAR_DEFINE_INT32(native_x_renderer_spin_cores, 4, "NativeXRenderer",
                     "The renderer's waits spin briefly before they sleep only with at least this many processor cores "
                     "(fewer: they sleep at once, leaving the core to the game's threads)");

namespace native_x_renderer {
namespace {

uint32_t CountCores() {
#if defined(_WIN32)
  DWORD_PTR process = 0, system = 0;
  if (!GetProcessAffinityMask(GetCurrentProcess(), &process, &system) || !process) return 0;
  DWORD size = 0;
  GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &size);
  if (!size) return 0;
  std::vector<uint8_t> buffer(size);
  auto* info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data());
  if (!GetLogicalProcessorInformationEx(RelationProcessorCore, info, &size)) return 0;
  // The process's affinity is in its own processor group (the first, for up to 64 logical processors).
  WORD group = 0;
  {
    USHORT groups[4] = {};
    USHORT count = 4;
    if (GetProcessGroupAffinity(GetCurrentProcess(), &count, groups) && count) group = groups[0];
  }
  uint32_t cores = 0;
  for (DWORD at = 0; at < size;) {
    auto* entry = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + at);
    if (entry->Relationship == RelationProcessorCore) {
      for (WORD g = 0; g < entry->Processor.GroupCount; ++g) {
        const GROUP_AFFINITY& a = entry->Processor.GroupMask[g];
        if (a.Group == group && (a.Mask & process)) {
          ++cores;
          break;
        }
      }
    }
    at += entry->Size;
  }
  return cores;
#else
  cpu_set_t set;
  if (sched_getaffinity(0, sizeof(set), &set) != 0) return 0;
  std::set<std::string> cores;
  for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
    if (!CPU_ISSET(cpu, &set)) continue;
    // The processors sharing this one's core; without the file, each processor counts as a core.
    std::ifstream in("/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/thread_siblings_list");
    std::string siblings;
    if (!(in >> siblings)) siblings = std::to_string(cpu);
    cores.insert(siblings);
  }
  return uint32_t(cores.size());
#endif
}

}  // namespace

uint32_t AvailableCores() {
  using Clock = std::chrono::steady_clock;
  static std::atomic<uint32_t> count{0};
  static std::atomic<int64_t> read_at{0};
  const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
  if (count.load(std::memory_order_relaxed) && now - read_at.load(std::memory_order_relaxed) < 1000)
    return count.load(std::memory_order_relaxed);
  read_at.store(now, std::memory_order_relaxed);
  uint32_t n = CountCores();
  if (!n) n = std::max(1u, std::thread::hardware_concurrency());
  count.store(n, std::memory_order_relaxed);
  return n;
}

bool SpinWaitsAllowed() { return AvailableCores() >= uint32_t(std::max(0, REXCVAR_GET(native_x_renderer_spin_cores))); }

}  // namespace native_x_renderer
