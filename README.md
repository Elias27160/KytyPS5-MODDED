# KytyPS5 Lite (modded build)

> **I am not the creator of KytyPS5.** This repository is a **modded version** of the KytyPS5
> project. All credit for the emulator goes to its authors.
>
> - Original project: https://github.com/KytyPS5/KytyPS5
> - Find the creator: https://discord.gg/Y5TvKkacAt
>
> Please do not report issues with this modded build on the official repository or Discord:
> they do not maintain it.

KytyPS5 Lite starts from KytyPS5 (upstream commit `b3295beb`) and adds optimizations for
mid-range PCs, a console-style launcher and a few fixes. Everything else is KytyPS5's code.
The original KytyPS5 README is kept in [README-KytyPS5.md](README-KytyPS5.md).

## What the modded build adds

### Engine performance

- **Background pipeline compilation** (experimental, off by default). Enable "Async pipeline
  compilation (experimental)" in the game settings, or pass `--async-pipelines`. When a
  graphics pipeline is missing, the driver compiles it on another thread instead of stalling
  the game. The game waits at most 4 ms; past that, the affected object is not drawn for a few
  frames until the compile finishes. Draws that write memory or count an occlusion query always
  wait. The overlay shows how many draws were deferred.
- **Faster shader cache loading**: when a game starts, cache entries are checked and
  decompressed on every core, 256 at a time to keep memory use bounded.
- **1080p video output**: the game believes it runs on a 1080p display instead of 4K, so it
  renders far fewer pixels and uses less VRAM. The environment variable `KYTY_LITE_4K=1`
  restores 4K.
- **VRAM management tuned for cards with 8 GB or less**: the texture and buffer garbage
  collector no longer runs every frame, frees images that need no GPU readback first, and
  avoids overcommitting video memory.
- **Presentation on a separate Vulkan queue** when the GPU supports it, so presenting a frame
  does not block GPU submissions.
- **Lighter texture filtering**: anisotropic filtering on material textures drops to bilinear,
  with a mip bias based on VRAM (less bandwidth on small cards).
- **Windows priority**: the process runs at above-normal priority, opts out of Windows 11
  EcoQoS power throttling and uses a 1 ms timer for steadier frame pacing.
- **Profiler zones removed** from the hottest code paths.

### Fixes

- **Slow motion in some games**: the vblank counter fell behind whenever presenting a frame
  took longer than one period, and games timed by that counter ran in slow motion. It now
  follows real time.
- **Cropped picture**: a 1920×1080 window plus its title bar did not fit a 1080p screen (even
  less with Windows display scaling) and cut off the picture and the HUD. The game window now
  opens maximized, sized to the usable screen area, without distorting the picture.
- **GPU device losses**: render areas and copies between images of different sizes now stay
  within image bounds (crash seen in UFC).
- **TLS slot reserved before NVIDIA drivers load**, since they use most of the available slots.
- **`kyty_crash.txt`** is written next to the emulator on a fatal error, even with logging off.

### PS5-style launcher

- **Console-inspired home screen**: a row of game covers, the selected game's artwork, a Play
  button and an options menu. No PlayStation logos.
- **Keyboard, mouse and controller navigation** (Cross or A to confirm, Circle or B to go
  back, Triangle or Y for the menu).
- **Smooth transitions**: the selected tile grows while the row slides, then the title and
  background fade in.
- **Cards with real data**:
  - trophies earned in the selected game (platinum, gold, silver, bronze);
  - total trophies across all your games;
  - connected controller and its battery level;
  - free space on the game's drive;
  - shortcut to the game settings.
- **Faster startup**: the window appears before the games are read, and covers are decoded in
  the background.
- **"Async pipeline compilation" option** in the game settings.

### Diagnostic tools (environment variables, for developers)

| Variable | Effect |
| --- | --- |
| `KYTY_LITE_4K=1` | Restores the original 4K output |
| `KYTY_LITE_STOCK_GC=1` | Restores the original VRAM garbage collector |
| `KYTY_LITE_BENCH=<file>` | Writes the overlay's measurements to a CSV file |
| `KYTY_LITE_DEVICE_FAULT=1` | Vulkan details after a GPU device loss |
| `KYTY_LITE_PREFETCH=1` | GPU readback prefetch (slower in UFC) |
| `KYTY_LITE_CORE_RESERVE=1` | Reserves a CPU core for the GPU thread |

## Requirements

- Windows 10 or 11, 64-bit.
- **CPU with AVX2**: this build is compiled with `/arch:AVX2`. CPUs without AVX2 cannot run it.
- Vulkan-capable graphics card. Only tested on a GTX 1660 SUPER (6 GB) with a Ryzen 5 4500 and
  16 GB of RAM.
- As with KytyPS5, you need your own legally obtained PS5 games and system files.

## Known limitations

- **Nothing has been thoroughly validated in real gameplay.** Performance gains are not
  measured unless stated otherwise.
- Heavy games are still slow on modest hardware: UFC 5 runs at about 15 to 28 FPS in fights on
  the test machine.
- With background pipeline compilation, objects can be missing for a few frames the first time
  they appear.
- The shader cache is rebuilt for every new emulator build: the first launch of each game
  stutters until it fills up.
- Only Windows has been built and tested.

## Building

Same toolchain as KytyPS5 on Windows: Visual Studio 2022 or newer, LLVM (`clang-cl`), CMake,
Ninja and Qt 6. The third-party libraries are Git submodules, so clone recursively:

```bat
git clone --recursive https://github.com/<your-account>/KytyPS5-Lite.git
```

Then, from a Visual Studio x64 developer prompt, the commands used for this build:

```bat
cmake -S . -B _Build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON ^
  -DCMAKE_C_FLAGS_INIT=/arch:AVX2 -DCMAKE_CXX_FLAGS_INIT=/arch:AVX2 ^
  -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/msvc2022_64
cmake --build _Build/windows --target launcher
cmake --install _Build/windows --prefix _Build/windows/install
```

The `_Build/windows/install` folder then contains `kyty_launcher.exe`, `kyty_emulator.exe` and
the Qt DLLs.

## License

KytyPS5 is distributed under the **GNU GPL v2** (see `LICENSE` and the `LICENSES` folder). This
modded build keeps the same license: its complete source code is published in this repository,
and every change is visible in the Git history.
