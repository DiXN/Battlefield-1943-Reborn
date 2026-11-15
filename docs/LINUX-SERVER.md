# The official Linux server package: optimisation of 14 Nov 2026

TekRantGaming, 14 Nov 2026: "optimize the official Linux server package. I want to make sure that thing's rock solid,
no problems, minimal resource usage as possible, so that it's optimized for that VPS" (the US Central server: AlmaLinux 10,
4 vCPU, 8 GB).

## Method

A private server on the VPS itself (scratchpad `us_vps_prof.sh`): a test backend on 127.0.0.5 and a test key in a
folder of its own, one 60 Hz server on Wake Island, nothing registered with the main backend, everything removed after
(a trap on exit). After it settles: per thread CPU time and context switches over 30 s from /proc, the blocking system
call of chosen threads sampled 200 times, memory, and the server's own tick lines ("BF server ticks").

## What an empty server spent (0.4.6 as released)

30% of one core and about 7,900 context switches a second, nearly all of it threads waking every millisecond to look
for work that was not there:

| Thread | CPU | Wakeups a second | Why |
|---|---|---|---|
| Four guest JobManager threads | 3.4% each | 815 each | Frostbite's job threads wait on their semaphore with a 1 ms timeout |
| GPU Commands | 3.4% | 1,470 | an alertable 5 ms wait that looked for queued callbacks every 1 ms |
| The host main thread | 4% | 796 | SDL's offscreen driver has no blocking wait: SDL_WaitEvent polls every 1 ms |
| The KeTimeStampBundle timer | 3.3% | 1,005 | the guest's millisecond tick count, kept current every 1 ms (kept) |
| GPU VSync | 2% | 814 | it slept 1 ms between looks at the clock, for a 60 Hz vblank |
| Main XThread (the game's own work) | 2.4% | 60 | |
| Audio Worker | 1.3% | 395 | an alertable wait on several handles, polled every 1 ms |

## Changes (SDK copy sdk/src-bf 397c7584; game 5b9226e, 4783215)

1. POSIX waits on several handles (WaitMultiple): the waiter is registered on each handle and woken by its signal
   (events, semaphores, mutants, timers and thread exits all notify through NotifyAll). It slept 1 ms and looked again.
   A 50 ms slice stays as a safety net.
2. Alertable waits (one handle, several, SignalAndWait) go through the same wait; a callback queued for the thread
   wakes its alertable waiter. They looked again every 1 ms.
3. The vblank thread sleeps until 2 ms before the next vblank, then 1 ms steps as before (its timing unchanged).
4. On a headless video driver (offscreen, dummy) the main loop waits on its own condition, woken by queued work, and
   looks at SDL's events at least every 100 ms.
5. `job_wait_floor_ms`: the guest JobManager threads' wait timeouts at least that long (a job posted wakes them at
   once whatever the timeout). Off in the game; the server manager passes `--job_wait_floor_ms=5` to every server.
6. An abort's stack: on Linux SIGABRT writes its stack (module+offset) to the error output, which the manager keeps as
   `game.out`, then the abort and its core go on. The US server's abort at 07:27 on 14 Nov had left no stack: its log
   was written over by the restart and its core was cut at 1 GB, all of it the console's memory.
7. The package carries its systemd unit (`bf1943-reborn-server.service`): Restart=always, whole cores
   (LimitCORE=infinity, with systemd-coredump's ProcessSizeMax raised), Nice -10, OOMScoreAdjust -500. The README says
   how to install it. `systemd-analyze verify` passes on the VPS.

## Results (the same private profile on the VPS)

| | 0.4.6 | after |
|---|---|---|
| CPU of an empty 60 Hz server | 30% of a core | 11% of a core |
| Context switches a second | 7,900 | 2,300 |
| Memory | 350 MB | 350 MB |
| Ticks | 60.0 a second | 60.0 a second |
| The tick's own work | 0.3 to 0.5 ms average | 0.26 to 0.44 ms average |

Under load (a live local Windows match, one player firing and throwing grenades every 3 s, the floor off then on): the
same tick figures (60.0 a second, 0.17 to 0.18 ms average work, the same longest gaps). The Windows client's frame
pacing is unchanged (the same Wake Island run before and after: maxima 16.9 to 18.5 ms against 17.0 to 18.4 ms).

A 15 minute soak of the final package on the VPS (the manager passing the floor): 30 tick reports, all 60.0 a second;
no restart and no crash; memory steady at 360 MB; 14% of a core at the end. Its worst moment was one 363 ms gap with
321 ms on a single tick (0.3 ms a tick otherwise): such long single ticks are still open (below).

Two servers on the VPS now take about a quarter of a core between them while empty, against 60% before.

## Crash checks

- The abort handler, tested on the VPS (SIGABRT sent to a private server): the stack in `game.out`, the manager
  started the server again within 2 s, systemd-coredump kept the whole core (66 MB compressed).
- Logs flush on every info line (the SDK's flush level), so the last lines before a crash are on disk.

## Not changed, and why

- Single ticks that run for 50 to 340 ms every so often: not solved by this work, and under investigation.
- The 1 ms KeTimeStampBundle timer (3.3%): it is the game's own millisecond clock; a coarser one would change what the
  game sees. It could be made 2 to 4 ms on servers only after testing what the game uses it for.
- The Linux client gets the same wait changes from the SDK. The servers exercised them for many minutes, but the ROG
  Ally was not reachable, so no Linux client was run on hardware: test it before a release ships them.
- Nothing is deployed: the live US package is 0.4.6 as released. The changes reach the VPS with the next release
  (a new server tar, the unit from the package).
