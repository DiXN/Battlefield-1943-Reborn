// The processor the game needs, checked first thing (15 Nov 2026, the unattended compatibility work).
//
// The runtime and renderer (rexruntime, rexgpu-bfnative) are built for x86-64-v2 (the SDK's presets: SSE3, SSSE3,
// SSE4.1, SSE4.2, POPCNT) and the recompiled game uses SSSE3 and SSE4.1 throughout. AVX, AVX2, FMA and LZCNT appear
// only in library routines that check the processor first (the C++ library's vector routines and the C runtime's), so
// the game does not need AVX: a Pentium, Celeron or Atom without it runs the game. A processor older than x86-64-v2
// (Intel's Core 2, AMD's Phenom II and Athlon II) stopped at an illegal instruction with no word. Now a message says
// why before anything else runs: this file's static initializer runs as the game's executable starts, before the SDK's
// main. (A routine in the runtime library's own start could still come first on such a processor; none was found in
// a disassembly of 0.4.7-indev4, where the newer instructions sit in hashing and text routines used later.)
//
// BF_DEV_FAKE_OLD_CPU=1 (developer builds, tests): as if the processor lacked SSE4.2, logged and printed but no box
// unless BF_DEV_START_BOX=1.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <intrin.h>
#include <windows.h>
#else
#include <cpuid.h>
#endif

namespace {

struct CpuFeatures {
  bool sse3 = false, ssse3 = false, sse41 = false, sse42 = false, popcnt = false, cx16 = false;
};

CpuFeatures ReadFeatures() {
  CpuFeatures f;
  unsigned int ecx = 0;
#if defined(_WIN32)
  int regs[4] = {};
  __cpuid(regs, 0);
  if (regs[0] < 1) return f;
  __cpuid(regs, 1);
  ecx = unsigned(regs[2]);
#else
  unsigned int eax = 0, ebx = 0, edx = 0;
  if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return f;
#endif
  f.sse3 = ecx & (1u << 0);
  f.ssse3 = ecx & (1u << 9);
  f.cx16 = ecx & (1u << 13);
  f.sse41 = ecx & (1u << 19);
  f.sse42 = ecx & (1u << 20);
  f.popcnt = ecx & (1u << 23);
  return f;
}

std::string Missing(const CpuFeatures& f) {
  std::string out;
  auto add = [&](bool have, const char* name) {
    if (have) return;
    if (!out.empty()) out += ", ";
    out += name;
  };
  add(f.sse3, "SSE3");
  add(f.ssse3, "SSSE3");
  add(f.sse41, "SSE4.1");
  add(f.sse42, "SSE4.2");
  add(f.popcnt, "POPCNT");
  add(f.cx16, "CMPXCHG16B");
  return out;
}

[[maybe_unused]] bool Set(const char* name) {
  const char* v = std::getenv(name);
  return v && *v == '1';
}

const bool g_checked = [] {
  std::string missing = Missing(ReadFeatures());
  bool fake = false;
#if defined(BF_DEV_TOOLS)
  fake = Set("BF_DEV_FAKE_OLD_CPU");
  if (fake && missing.empty()) missing = "SSE4.2 (BF_DEV_FAKE_OLD_CPU)";
#endif
  if (missing.empty()) return true;
  const std::string text =
      "Battlefield 1943 Reborn needs a processor with SSE4.2 and POPCNT (x86-64-v2): an Intel Core i3, i5 or i7 (2008 "
      "or newer), or most Intel and AMD processors from 2011 on (AMD FX, Ryzen and newer).\n\nThis processor lacks: " +
      missing + ".\n\nThe game cannot run on it and closes now.";
  std::fprintf(stderr, "BF cpu: %s\n", text.c_str());
  std::fflush(stderr);
  bool box = true;
#if defined(BF_DEV_TOOLS)
  if (fake && !Set("BF_DEV_START_BOX")) box = false;
#endif
#if defined(_WIN32)
  if (box) MessageBoxA(nullptr, text.c_str(), "Battlefield 1943 Reborn", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
#else
  (void)box;
#endif
  std::_Exit(3);
}();

}  // namespace
