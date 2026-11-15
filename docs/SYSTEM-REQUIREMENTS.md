# System requirements

What Battlefield 1943 Reborn needs, from what its programs ask of a PC (measured 15 November 2026). Only one PC
was used for the measurements (an AMD Ryzen 7 7800X3D with an NVIDIA RTX 4070 Ti); weaker processors were imitated by
holding the game to two or four of its cores. You also need your own copy of the Xbox 360 game's files.

## Processor

- **64-bit with SSE4.2 and POPCNT (x86-64-v2):** an Intel Core i3, i5 or i7 from 2008 on (Nehalem and newer), or an
  AMD processor from 2011 on (FX, Ryzen and newer). Most Intel Pentium, Celeron and Atom models of the last decade
  have it too.
- **AVX is not needed.** The game uses AVX only where it first checks that the processor has it.
- **Older processors (Intel Core 2, AMD Phenom II and Athlon II)** cannot run it: from version 0.4.7 the game says so
  when it starts, instead of closing without a word.
- **Cores:** the game plays on two cores (on two cores of a fast processor: 90 FPS held with a 90 FPS limit, about 200
  FPS without one). Four or more are better: the renderer then uses a thread of its own for the graphics driver.

## Graphics

- **Vulkan.** The one feature the renderer requires is independent blending, which every desktop graphics card with
  Vulkan has; everything else it uses is optional.
- **Graphics memory:** about 1 GB at the Potato and Low presets (the game drawn at 1600x900 for a 3200x1800 screen),
  about 1.5 to 1.7 GB with every setting up at 3200x1800.
- **Presets:** Potato and Low turn off ambient occlusion, global illumination and shadows, shorten the grass to 45 m
  and draw fewer pixels (AMD FSR 1 brings the picture up to the screen's size).

## Memory

- About 3 GB in use during a match (2.9 GB working set; 4.5 GB committed, most of it the Xbox 360 game's own memory).
  **8 GB** of system memory is the sensible minimum. Memory stays flat over a long match (21 minutes measured).

## Operating system

- **Windows 10 (version 1607 or newer) or Windows 11, 64-bit.** The Visual C++ runtime the game needs comes with it
  (from version 0.4.7); nothing else to install.
- **Linux, 64-bit, glibc 2.35 or newer** (Ubuntu 22.04, Linux Mint 21, Pop!_OS 22.04, Zorin 17, Debian 12, Fedora,
  Arch, SteamOS and newer), from version 0.4.7: the AppImage is built on Ubuntu 22.04
  ([tools/linux/build_in_ubuntu2204.sh](../tools/linux/build_in_ubuntu2204.sh)). Versions up to 0.4.6 need glibc 2.38
  (Ubuntu 24.04 and newer).

## Not tested

Weaker or older graphics cards, AMD and Intel graphics, Windows on ARM, and Intel processors with performance and
efficiency cores have not been measured; what is above follows from the programs themselves and the one PC.
