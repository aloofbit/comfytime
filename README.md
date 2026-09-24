# comfytime

**Bugs, questions and screenshots: [join our Discord](https://discord.gg/YSWzYk8xP).**

[![Discord](https://img.shields.io/badge/Discord-ComfyCraft-5865F2?logo=discord&logoColor=white&style=for-the-badge)](https://discord.gg/YSWzYk8xP)

> **Early alpha.** Tested on one computer, with one 1.12 client build (VanillaFixes + DXVK).
> comfytime writes into the client's memory. Back up your client folder first. To remove it, delete the
> `comfytime.dll` line from `dlls.txt`.

Set the time of day in the World of Warcraft 1.12 client. The sky, the sun and the world's lighting follow
the hour you choose. **Your screen only:** the server keeps its own time, and other players see no change.

comfytime works with [comfyatmosphere](https://github.com/aloofbit/comfyatmosphere) (fog, sun rays,
volumetric light). Its effects follow the sun in the sky, so they move with the time comfytime sets. Each
works without the other.

## Keys

| Key | |
| --- | --- |
| Ctrl+PageUp / PageDown | Later / earlier by `step` hours; hold to keep moving |
| Ctrl+Home | Save the time being shown into `comfytime.ini` |
| F11 | Reload `comfytime.ini` |
| Ctrl+F12 | Search memory for the game clock (read-only; for a different `WoW.exe`, see below) |

## Settings (`comfytime.ini`)

| | |
| --- | --- |
| `enabled` | `0` uses the game's own time |
| `hour` | The time to show, 0..24; fractions allowed (`13.5` is 13:30) |
| `addrMinutes`, `addrFraction`, `addrMinutesF` | Where this `WoW.exe` keeps the time (see below) |

## Install

Download the zip from [Releases](https://github.com/aloofbit/comfytime/releases), or build it (below).

1. Copy `comfytime.dll` and `comfytime.ini` to the client folder, next to `WoW.exe`.
2. Add the line `comfytime.dll` to `dlls.txt`. If you use comfygrass or comfyfog, put it **after** them.
   All three patch the same Direct3D device, and comfytime waits for the others to attach first.
3. Start the game with `VanillaFixes.exe`.

## How it works

The client keeps the time of day in three places. They were found by measurement in one `WoW.exe`, with the
Ctrl+F12 search: take a snapshot of the client's writable memory, wait, and keep the values that increased by
exactly the minutes that passed, in any of several encodings.

| Address | What |
| --- | --- |
| `0x00CE9B60` | minutes since midnight, integer |
| `0x00CE9B64` | fraction of the day, float (continuous: carries the seconds) |
| `0x00CE8574` | minutes since midnight, float |

In the world, the client writes these values every frame, between `BeginScene` and `Present`. comfytime
writes the chosen time at `Present`, and again at `BeginScene`, immediately before the sky is drawn. The
second write wins. comfytime gets DXVK's device vtable from a throwaway device and patches those two slots in
place, as comfygrass and comfyfog do.

Before its first write, comfytime checks that the three addresses hold a consistent time. If they do not, as
on another client build, it does not write, and it logs why. To find the new addresses: read the minimap
clock, press Ctrl+F12, and play for about six minutes. `comfytime.log` then lists the matches. Put them in
the ini.

## Build

Visual Studio 2022 and CMake. **32-bit only**: the 1.12 client is x86.

```
cmake -B build -A Win32
cmake --build build --config Release
```

`comfytime.dll` is written to the project root, next to `comfytime.ini`.

## Licence

GPL-3.0. See [LICENSE](LICENSE).
