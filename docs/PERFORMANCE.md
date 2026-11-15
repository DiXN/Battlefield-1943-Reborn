# Windows and Linux performance work of 14 November 2026

TekRantGaming's brief (14 Nov 2026, "Comprehensive Windows and Linux Game Performance Optimisation", run unattended):
investigate the whole game on Windows and Linux, measure first, fix real bottlenecks, build and test both platforms,
compare against a baseline, and keep confirmed results apart from estimates. No ROG Ally today: Linux only through
WSL, and WSL figures are never presented as native Linux ones. The Linux server work of the same morning is in
[LINUX-SERVER.md](LINUX-SERVER.md); earlier Linux work in [LINUX-PERFORMANCE.md](LINUX-PERFORMANCE.md).

Status labels below: **confirmed** (measured before and after on this PC, repeated), **estimated** (follows from a
measurement but not measured as such), **unverified** (built and run, not measured where it matters).

## How it was measured

- **The PC:** Ryzen 7 7800X3D (8 cores, 16 threads), RTX 4070 Ti, Windows 11. Other load checked before each batch
  (only the three local Night Ops servers ran, about 0.1 core each).
- **The scene:** a match on Wake Island on a local dedicated server with its own test backend (127.0.0.5), the
  soldier at the carrier spawn, sound off, the dev build with NativeXRenderer (`--native_x_renderer=true
  --bf_engine_d3d_path=true`), measured over 50 s after it settles (scratchpad `pbench.ps1`).
- **Every frame time:** `BF_FRAME_TIMES=<file>` (new, frame_stats.cpp) writes each frame's time; `pb_lows.py` gives
  the average, the 1% and 0.1% lows (the average of the slowest 1% and 0.1% of frames, as FPS), p99, p99.9, the
  longest frame and the frames over twice the average.
- **The frame rate ceiling here is 90 FPS:** NVIDIA's global Max Frame Rate on this PC is 110, and the game offers
  rates in steps of 30 below it. This CPU and GPU hold 90 FPS with most of each frame idle, so on all sixteen threads
  the gains show as CPU time, not FPS.
- **A slower PC's CPU, imitated:** the client held to two cores (affinity 0x5, one thread each) or four (0x55). The
  runtime sets the process back to every CPU when it starts (EnableAffinityConfiguration), so the affinity is set
  again every 2 s for the first 70 s. Two cores of this CPU is a stand in for a weak quad core or a power limited
  handheld, not a model of any one PC (**estimated** relevance).
- **Profiles:** the dev build's sampling profiler (`BF_DEV_PROFILE`), now also from a join's loading screen
  (`BF_DEV_PROFILE=load,<s>`) and with a chosen threshold (`BF_DEV_PROFILE_MIN=<percent>`), and the long frames'
  samples by call chain.
- **Linux:** WSL (Ubuntu 24.04) on the same CPU. It has no GPU Vulkan driver here (only Mesa's software lavapipe),
  so the Linux client's frame rate in WSL means nothing; the Linux dedicated server (headless, no GPU) is compared
  with the Windows one on the same CPU instead.

## Results

All Wake Island, 90 FPS ceiling. Before: the dev build at the start of the work (runs `pb_base_*`). After: master with
every change below (runs `pb_fin_*`, three runs on two cores).

| | Before | After | Status |
|---|---|---|---|
| All 16 threads: CPU | 1.54 cores | 1.45 to 1.47 cores (about 5% less) | confirmed |
| All 16 threads: pacing | 0.1% low 86.5 FPS, longest 11.7 ms | 0.1% low 85.5 to 85.9 FPS, longest 12.1 to 12.8 ms (the same) | confirmed |
| 4 cores: pacing | 0.1% low 78.5 FPS, longest 14.2 ms | 0.1% low 89.1 FPS, longest 11.3 ms | confirmed (one run each) |
| 4 cores: CPU | 1.41 cores | 1.31 cores | confirmed (one run each) |
| 2 cores: average | 77.9 to 78.5 FPS | 89.9 to 90.0 FPS | confirmed |
| 2 cores: 1% low | 19.5 to 23.0 FPS | 82.5 to 83.4 FPS | confirmed |
| 2 cores: frames over twice the average | 204 to 210 | 1 | confirmed |
| 2 cores: CPU | 1.36 to 1.37 cores | 1.12 to 1.13 cores | confirmed |
| Idle 60 Hz Windows dedicated server | 10.5% of a core | 1.3% of a core, ticks 60.0 | confirmed |
| Idle 60 Hz Linux dedicated server (WSL, same CPU) | 7.7% of a core (job floor off) | 3.3% of a core | confirmed in WSL; ticks 56.6 there (WSL timers; 60.0 on the US VPS) |
| Memory (client) | 2.8 GB working set, 4.5 GB private | the same | confirmed: unchanged |
| Level load (loading screen to the game's Loading complete) | 3.1 s | 3.1 s | confirmed: not a bottleneck here |

On a PC whose CPU limits the frame rate the 2 core figures are the ones that matter: the frame rate went from 78 to
the ceiling and the stutter (frames of 35 to 110 ms) almost entirely went. How much a given weak PC gains is
**estimated**, not measured: no such PC was tested.

## What was found and changed

### 1. Threads waiting for the game's jobs took the cores from the threads doing them (both platforms)

The game waits for its jobs by polling its scheduler (sub_82609000) in loops: the main thread inside its frame
(sub_823961E8, several waits), the job threads (sub_824177E0) and others, 19 call sites. With cores to spare that
costs only CPU; with few cores the pollers ran while the threads with the work waited for a core. The profile of the
two core run's long frames: the main thread 26% and JobManager3 19% of their samples polling.

**Change:** `bf_job_wait_yield` on by default (bf/src/frame_rate.cpp): after 64 empty polls in a row a thread gives up
the rest of its time slice (SwitchToThread on Windows, sched_yield on Linux). A thread with no other thread ready on
its core gets the core straight back, so a PC with cores to spare loses nothing. The main thread's own frame wait
already slept (bf_job_wait_sleep, 10 Nov). **Confirmed** (the 2 and 4 core rows; the first measurement alone, runs
pb_yield_wake2c: 1% low 23 to 77 FPS).

### 2. The runtime's timer thread spun on Windows (Windows)

The runtime keeps the guest's millisecond clock and its timers on one thread (rex::thread::TimerQueue). On Windows it
waited with a spin wait strategy: yields in a loop, and every poll of its queue ran thousands of pause instructions
and asked the system for the processor count (std::thread::hardware_concurrency, a system call). The Linux build
already slept on a condition variable. On an idle Windows server nearly all the process's CPU was this thread.

**Change** (SDK copy, src/core/timer_queue.cpp): on Windows the thread waits on an event (set when a timer is queued)
and a high resolution waitable timer set for the next due timer, and polls its queue only when the event woke it.
**Confirmed:** an idle 60 Hz Windows server from 10.5% to 1.3% of a core (runs wsrv_rel_before, wsrv_tq2a), ticks
60.0; the client carries the same thread (part of the 16 thread CPU row).

### 3. The game's job threads woke every millisecond (both platforms)

Frostbite's JobManager threads wait on their semaphore with a 1 ms timeout and look again: about 15,000 waits a
second in a match (BF_DEV_IOSTATS). A posted job wakes them at once whatever the timeout. The servers already
lengthened these waits to 5 ms (the morning's Linux work); the game now does by default (`job_wait_floor_ms` 5, SDK
src/system/xobject.cpp). **Confirmed** on the Linux server in WSL (7.7% to 3.3% of a core) and the Windows server
(9.4% to 7.6% before change 2); on the client about 1% less CPU on sixteen threads, and on two cores 1% low 80 FPS
with it against 67 without (two runs against one).

### 4. One repeated command line option dropped every option (both platforms)

Found while measuring: a run given `--bf_voice_volume=0` twice drew without NativeXRenderer. The runtime's command
line parser (CLI11) refused a second occurrence, and because it applies options only after the whole parse, that one
error dropped every option, with only a line on the error output. A player whose launch options repeat a setting
(Steam's, a shortcut's) would lose them all the same way.

**Change** (SDK, src/core/cvar.cpp): an option given twice takes its last value. **Confirmed** (run pb_dupflag1: the
renderer kept). Some of this work's own comparison runs had a repeated option and were set aside (pb_nofl_*,
pb_final_*, pb_new_*); the results above use none of them.

### 5. Level loads: measured, nothing to fix

The test driver's "level loaded, spawning" came 7.1 s after the loading screen whatever the CPU, which looked like a
fixed wait. The game's own Loading complete (its gameIsReady, now logged) comes 3.1 s after the loading screen; the
rest was the driver's own 4 s wait before spawning. A load capped at 30 FPS took about as long as at 90, so loads are
not paced by the frame rate. During the load the decompression thread is busy at first, then waits; file reads take
milliseconds (98 MB in 20 ms). **Confirmed:** no bottleneck on this PC; the new log line gives players' logs the real
figure.

### 6. Linux (the same day's morning, LINUX-SERVER.md)

POSIX waits on several handles and alertable waits woken by signals instead of 1 ms polls, the vblank thread's
sleep, a headless video driver's loop that waits: an idle Linux server from 30% to 11% of a core on the US VPS. The
Linux client gets the same code.

### 7. The main thread's frame wait polled 100,000 times a second (both platforms; evening of 14 Nov)

A new dev counter (`BF_DEV_JOBWAITS=1`: the game's job waits by call site, logged every 10 s) showed where the main
thread's time went. Not its waits inside the frame (about 10 ms a second in all) but its wait for the frame's jobs
(sub_823C2430: while the job group at client+3248 is not done, poll the scheduler): 817 ms a second, in 102,000 polls a
second through the scheduler's lock. bf_job_wait_sleep (10 Nov) slept a quarter millisecond after every 64 polls,
and 64 polls take about half a millisecond, so the main thread spun two thirds of that wait: nearly all of its 39%
of a core.

**Change** (bf/src/frame_rate.cpp, `bf_job_wait_wake`, on): after a wait's first 64 polls the main thread sleeps until
another thread's poll has run a job, or a quarter millisecond, then polls once. Windows: an event set by the thread
that ran the job, with a high resolution waitable timer; Linux: a futex on the count of jobs run. Polls fell to 8,000
a second. **Only with four processors or more** (the process's affinity, read once a second): on two cores those
polls are the ones that pick up the frame's jobs first while the JobManager threads wait for a core, and sleeping
there gave more long frames (two core runs pr_wake2c_* against pr_nowake2c_*: 15 to 49 frames over twice the
average against 5 to 15).

| | Change off | Change on | Status |
|---|---|---|---|
| All 16 threads: CPU | 1.39 to 1.49 cores | 1.24 to 1.34 cores (0.05 to 0.21 less, 4 to 13%) | confirmed (six pairs, alternating; the later ones with other sessions' 0.7 core on the PC) |
| All 16 threads: pacing | 0.1% low 84.7 to 87.7 FPS, no long frames | 0.1% low 85.2 to 87.7 FPS, no long frames | confirmed: the same |
| 4 cores: CPU | 1.32 to 1.39 cores | 1.17 to 1.22 cores | confirmed (five pairs) |
| 4 cores: pacing | 0.1% low 20 to 88 FPS | 0.1% low 17 to 85 FPS | the same within the runs' spread (both had disturbed runs) |
| 2 cores | as before | as before (the gate) | the code path is the old one; runs disturbed (below) |
| Idle Linux server (WSL) | 3.6% of a core | 3.5% | confirmed: no change (its main thread does not spin there) |

Tried and dropped: waking the frame wait also when a job is posted (a hook on the job manager's wake of its workers,
sub_826070B8: about 19,000 a second; its wake of one worker, sub_826083D8, never came), so that on two cores it could
sleep too and still take the jobs. It did not help: three back to back pairs on two cores (runs pr_post2c_*, a quiet
PC) gave 1% lows of 41 to 53 FPS and 13 to 22 frames over twice the average sleeping, against 62 to 85 FPS and 1 to 6
polling. The gate stays at four processors, now a setting (`bf_job_wait_wake_processors`) for testing.

A fault of this change's first version, found and fixed before it was kept: it counted a thread's empty polls per
call site, so the waiting threads yielded less, and on two cores the 1% low fell to 40 to 74 FPS with the change
off too. The count is the old one again.

The runs of this evening shared the PC with a browser playing video (about one core, "others" 1.0 to 1.5 cores
against 0.6 in the afternoon), so the two core runs varied too much to compare with the afternoon's; every result
above is from pairs run back to back. **Estimated:** on a laptop or handheld the main thread's saved fifth of a core
is battery and heat, not frame rate (the frame rate here is held at the 90 FPS cap).

The Linux client in WSL with it: joined, loaded, spawned and played (runs p4, p6on), the futex sleeps working
(about 2,000 a second there). WSL's software renderer ran at about 3.7 FPS this evening against 29 in the afternoon,
with this change off, on and with the afternoon's own build (runs p5off, p6on, p7perf2): WSL's, not the game's. A run without sound in WSL (ALSA found no card) stopped at the game's start with a guest
access violation repeated every millisecond (a read of guest 0x3C on the main thread). The same read is in logs of
10 and 11 Nov on Windows (after the game's audio failed to start, and after the suicide command), so it is older than
this change; with the change off it did not come up, and with it on it came up once in three runs. Left for a
follow up (below).

## Linux compared with Windows

On the same CPU the idle Linux server (WSL) now costs less than the Windows one did before change 2, and about the
same order as Windows after it (3.3% against 1.3% of a core; WSL's own timer overhead included). The remaining Linux
server cost is the guest's 1 ms clock (about 1,000 wakeups a second, as on Windows) and the job threads at 5 ms.
Earlier Linux findings (the guest interrupt lock, 13 Nov) are in LINUX-PERFORMANCE.md.

## Builds and tests

- Windows: the dev build (bf-dev) and the SDK copy built after every change; the final build is master 948331d with
  the SDK 13212f4 (runs pb_fin_*). Matches played on Wake Island (above); earlier in the work Guadalcanal and Iwo
  Jima ran normally.
- Linux: the WSL Release build with the test hooks built from the same sources (runs\linux\bench-perf1, then perf2);
  the Linux dedicated server ran in WSL (60 Hz, no errors) and on the US VPS.
- The Linux client in WSL (scratchpad wsl_client_test.sh: a test backend, a Linux dedicated server and its host, the
  client drawing with Mesa's software Vulkan into a hidden Xvfb screen): it signed in, joined, loaded Wake Island
  (16.8 s on the software renderer), spawned and played to the end of the 7 minute run, about 29 FPS (the software
  renderer's figure, not a Linux hardware one), no crash; its only errors are the three Xbox Live messages Windows
  logs too. **Confirmed** that the Linux build works with these changes; its speed is **unverified**. (A first run
  with the test host on port 1000 never loaded: the Linux build moves ports below 1024 up by 30,000, and on one
  machine the replies came from the moved port, which the game's packet filter rightly dropped. Real servers use
  ports from 18300; the test now does too.)
- The released v0.4.6.3 (the same day) carries none of this work: it is the 0.4.6.2 release plus two menu changes.

## What is left

- **The main thread's waits inside its frame still poll** (yielding), but they are small: about 10 ms a second in all
  (BF_DEV_JOBWAITS). Its big wait, the one for the frame's jobs, now sleeps (change 7).
- **On two or three processors the frame wait still polls** (change 7's gate, `bf_job_wait_wake_processors`, 4):
  there those polls pick up the frame's jobs first. Waking it when a job is posted as well was tried and dropped
  (change 7).
- **Renderer memory:** NativeXRenderer holds about 1.7 GB of video memory on Wake Island at 3200x1800, 512 MB of it a
  mirror of the console's memory. For 3 GB cards this deserves a look; not changed.
- **Renderer CPU:** about 2.9 ms a frame on its own thread (about 3.5 microseconds a draw for 836 draws). Off the
  critical path on this PC; on a weak CPU it competes with the game's threads.
- **Two cores still show one frame of 30 to 40 ms in 50 s.** Its samples are in the game's own code on the job thread.
- **A guest access violation (a read of guest 0x3C on the main thread)** after the game's audio fails to start or a
  suicide command: logged once on Windows (10 and 11 Nov), repeated every millisecond in one WSL run without sound.
- **Older NVIDIA cards:** a player's game crashed unless the driver's "Vulkan/OpenGL present method" was set to
  "Prefer layered on DXGI swapchain". Not reproduced here (needs their log and crash report).
- **Long single ticks on the US Linux server** (100 to 290 ms every so often): under investigation.

## Needs native Linux hardware

- The Linux client's frame rate and pacing with these changes (no GPU Vulkan in WSL here).
- The two core imitation stands in for weak CPUs; a real quad core PC or a handheld would confirm the gain.
- Sound on Linux (issue 10's distortion reports) was not part of this work.
