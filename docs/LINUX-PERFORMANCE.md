# Linux performance: investigation and changes

Branch `linux-perf` (from v0.4.0 plus the dedicated server's null page guard, be509fe). Started 13 Nov 2026 at
TekRantGaming's request ("Linux Performance Optimisation and Development Directive"), after
[issue 10](https://github.com/TekRantGaming/Battlefield-1943-Reborn/issues/10) and players' reports of low frame rates
on strong Linux PCs. Verified results are kept apart from hypotheses below.

## Test environment

| | |
|---|---|
| Device | ASUS ROG Ally, AMD Ryzen Z1 Extreme (8 cores, 16 threads, RDNA 3 iGPU "Phoenix"), 12 GB shared memory, 4 GB as VRAM |
| OS | Bazzite 44 (Kinoite), kernel 7.2.9-ogc3.1, KDE Plasma on Wayland, the game on XWayland (SDL video driver x11) |
| Graphics | Mesa 26.2.4, RADV, Vulkan 1.4.354 |
| Power | Platform profile balanced, on mains (battery full). Firmware limits SPL 30 W, SPPT 43 W, FPPT 53 W, but the chip settles at about 15 W (PPT read by `sensors`) a minute or two into a match, after a short boost. Temperatures 50 to 56 C (edge, Tctl). |
| Server | A LAN only dedicated server on the test PC (runs\mgr2, Wake Island, every US carrier spawn point moved onto one with BF_DEV_MOVE_SPAWNS so each run starts in the same place), one player |
| Build | Linux Release with the dev hooks (BF_DEV_TOOLS=ON: auto join and spawn), version 0.4.0, made in WSL by scratchpad bf_linux_bench.sh; the same compiler options as the release |
| Settings | The owner's own Ally settings, with no frame cap and no vsync (`frame_rate = unlimited`, `vsync = off`); MAILBOX present mode in the end (as on Windows); the game renders 1280x720 into a 1038x506 window |

Windows on the same hardware was not available, so there is no same machine Windows figure. The Windows build on the
test PC (Ryzen 7 7800X3D, RTX 4070 Ti, 60 FPS cap) is an **unverified reference only**: in a match its renderer
thread used 0.4 to 4.6 ms of CPU a frame where the Ally's used 7.7 to 9 ms, which is roughly what the slower, power
limited CPU explains on its own.

## Method

`bf_bench.sh` (scratchpad, copied to the Ally): the game joins the LAN server, spawns, stands still; after
`SETTLE` seconds (100 since the boost was found) it samples 60 s: the game's own frame lines (`BF FPS`, every 30 s),
NativeXRenderer's statistics (renderer CPU per frame, the command processor's waits), per thread CPU time from
/proc, GPU busy and clocks, power, temperatures and memory once a second.

Profiling: `perf` cannot open any event from a user's container on the Ally (perf_event_paranoid 2), and changing
that is a system setting; `eu-stack` froze the game for 10 s a snapshot. So the dev build has its own sampling
profiler, `bf/src/linux_profiler.cpp` (Linux, dev builds only): `BF_DEV_PROFILE=<start s>,<seconds>[,<hz>]`, a timer on
each thread's own CPU time clock (a blocked thread is never sampled or woken), optional call stacks
(`BF_DEV_PROFILE_STACKS=1`, the unwind tables), written with the process's mappings and named on the Ally with
eu-addr2line (scratchpad bf_prof_report.py, bf_prof_stacks.py).

**Noise.** The same build gave 70.3 to 81 FPS in different runs (heat over the session, the power boost). Single
runs prove nothing here; compare CPU time and repeated runs, in the settled part of each run.

## Baseline (v0.4.0 code, settled)

About 72 to 81 FPS on Wake Island's carrier deck, 99th percentile 19 to 22 ms. CPU: JobManager3 (the game's job
thread that also carries NativeXRenderer's state tracking) 80 to 87% of a core, the main game thread 57 to 64%,
NativeXRenderer's render thread 57 to 61%, the GPU command thread 31 to 34%, all threads about 3.5 cores. GPU 70 to
76% busy at 1.05 to 1.1 GHz. No thread is saturated: the frame is a chain of threads waiting on each other, on a CPU
held at about 2 GHz by the power limit.

## Findings

1. **The game's interrupt lock (verified, the largest Linux only cost).** The game disables and restores interrupts
   (`mfmsr`, `mtmsrd`) around its spinlocks thousands of times a frame. The recompiler turns each into a lock of the
   runtime's global critical region, a `std::recursive_mutex`, and every `mfmsr` also locked and unlocked it just to
   read a counter. glibc's recursive mutex sleeps in the kernel at the first contention and every unlock with a
   waiter is a futex wake; Windows' lock spins first. Profile prof3: 27.6% of all the game's CPU time in the C
   library, `__lll_lock_wake` 9.7% and `futex_wait` 7.5% of it, called from the recompiled game code
   (sub_825CA8C8, sub_82608E60, sub_82609000 and others).
2. **The renderer built with the large code model (verified as a fact, no measurable effect).** The SDK builds every
   target on Linux with `-mcmodel=large`; the game's own code was put back to the default before, the renderer
   (NativeXRenderer and its SDK glue) was not. Linux was the only platform whose renderer made every call and global
   access an absolute 64 bit load.
3. **Smaller Linux only costs, not changed yet:** page protection system calls for the renderer's memory write
   watching (`rex::memory::Protect` from PhysicalHeap::EnableAccessCallbacks and TriggerCallbacks, about 2% of the
   game's CPU), the kernel object table's lock (`ObjectTable::LookupObject` under `xeNtSetEvent`, about 1.5%), the
   runtime's event objects (robust pthread mutexes and condition variables; waits on several objects poll with 1 ms
   sleeps, `PosixConditionBase::WaitMultiple`), the GPU command thread's yields (`MaybeYield`, about 1% in a match).
4. **Not Linux specific:** the rest of JobManager3's time is the game's own code (sub_82609000, an unoptimised loop
   over a list, 5 to 17% of the main and job threads) and NativeXRenderer's state tracking on the game's draw calls
   (`StateTracker::Gather`, `RegisterRecord::Value`, `NoteRecordedDraw`), the same code on Windows.

## Changes

| Change | Files | Evidence | Status |
|---|---|---|---|
| The interrupt lock on Linux: `mfmsr` answers from the thread's own count without locking; taking the lock tries it (try_lock and the CPU's pause, 200 times) before sleeping. Windows kept the old macros until 0.4.1, below. | sdk/src-bf resources/templates/codegen/pch_h.inja, and bf/generated/default/bf1943_pch.h (generated, not in git: made again by codegen from the template) | All threads' CPU 357% of a core before, 338% and 342% after (two runs): about 5% less CPU. In the profile the C library fell from 27.6% to 21.8% of the game's CPU, and the futex waits gave way to try_lock. Frame rate: 72.5 before, 70.3 and 78.6 after: within the noise, no measured gain. Four matches played without a fault. | kept (less CPU for the same frames: battery and heat on handhelds, room for other threads) |
| The renderer back to the default code model on Linux | bf/CMakeLists.txt | Renderer CPU 7.9 ms a frame before, 7.7 ms after; frame rate within the noise | kept (no measured gain; matches the game's own targets, no risk) |
| The sampling profiler (dev builds only) | bf/src/linux_profiler.cpp, bf/CMakeLists.txt | Used for every profile above | kept (test tool; nothing in release builds) |

Windows: at first the template's Windows path was the old text unchanged (a Windows match, run winlock1, joined,
spawned and held 60 FPS with it). For 0.4.1 (13 Nov 2026, merged with this work) the half that does not depend on the
C library went to Windows too, SDK commit 43b1c995: `mfmsr` answers from the thread's own count there as well, and
taking the lock stays as it was (Windows' lock spins by itself). Measured on Windows, Wake Island's carrier spawn,
the builds the same but for this (the one before kept as bf/out/build/bf-dev-lockold), the client's CPU over 40 s:

| | before | after |
|---|---|---|
| 90 FPS (this PC's cap), three matches each, the last four in turn | 1.523, 1.529, 1.512 cores (1.521) | 1.478, 1.450, 1.474 cores (1.467) |
| the main thread's busy time a frame at 90 FPS | 3.8 to 3.85 ms | 3.6 to 3.65 ms |
| 60 FPS, two matches each | 1.168, 1.074 cores | 1.071, 1.118 cores |

About 3.5 percent less CPU and the main thread about 5 percent less busy at 90 FPS, the same way in every pair; at 60
FPS about 2 percent, inside that pair's noise (the PC's other load differed). Runs lockb1 to 3, locka1 to 3, lockc1
and 2, lockd1 and 2.

The generated header bf1943_pch.h is not in git: it was edited to match the template, and the next codegen run must
use an SDK built with the template of SDK commit 43b1c995 (sdk/src-bf master), or the change is lost.

## Not verified

- No change improved the Ally's frame rate beyond the run to run noise. The Ally runs at about 2 GHz on its CPU
  under a 15 W limit in the balanced profile; what limits a frame there is the chain of game, job and render
  threads, mostly code that costs the same on Windows.
- Issue 10's player (RTX 2080 Super, NVIDIA 620.70.06, 60 Hz, 60 FPS cap) held 60 FPS most of the time; their
  renderer CPU rose to 8 to 12 ms a frame in matches. Their CPU is not in their log, so whether that is a Linux
  cost or a slow CPU cannot be told from it.
- Audio distortion (issue 10, and two players in its chat): the logs show nothing wrong (6 channels sent to a
  stereo device, mixed down by the sound server). Not checked by ear: the tests run muted.
- No NVIDIA or Intel Linux machine was tested; nothing here claims Steam Deck results.

## Next steps, by expected value

1. Profile a desktop Linux machine with a fast CPU and an NVIDIA GPU (the reports are from such PCs): the Ally's
   power limit hides CPU costs that a desktop would show as frame rate.
2. Spin before sleeping in the runtime's other hot locks on Linux (the kernel object table, the global critical
   region where the runtime itself takes it, NativeXRenderer's recorded draws lock).
3. Fewer page protection changes for the write watching (NativeXRenderer D97 already leaves pages rewritten every
   frame unwatched; a lower threshold on Linux, where each fault is a signal).
4. NativeXRenderer's state tracking on the game's draw thread (all platforms, so only with Windows tests).
5. Event waits on several objects without 1 ms polling (a futex based wait).
