# comfytime

> **Very early alpha.** Tested only on one computer -- mine -- with one 1.12 client build (VanillaFixes +
> DXVK). It writes into the client's memory. Back up your client folder first, and if anything goes wrong
> just remove the `comfytime.dll` line from `dlls.txt`.

Set the time of day in the World of Warcraft 1.12 client. The sky, the sun and the world's lighting follow
the hour you choose -- noon in a forest, dusk in a city, whenever you like. **Your screen only:** the server
keeps its own time and nobody else sees a difference.

A sibling of [comfyatmosphere](https://github.com/aloofbit/comfyatmosphere) (fog, sun rays, volumetric light),
whose effects follow the sun in the sky and so move with the time comfytime sets. Each works without the
other.

## Keys

| Key | |
| --- | --- |
| Ctrl+PageUp / PageDown | An hour later / earlier |
| F11 | Reload `comfytime.ini` |
| Ctrl+F12 | Search memory for the game clock (read-only; for a different `WoW.exe`, see below) |

## Settings (`comfytime.ini`)

| | |
| --- | --- |
| `enabled` | `0` leaves the game's own time alone |
| `hour` | The time to show, 0..24; fractions allowed (`13.5` is 13:30) |
| `addrMinutes`, `addrFraction`, `addrMinutesF` | Where this `WoW.exe` keeps the time (see below) |

## Install

Download the zip from [Releases](https://github.com/aloofbit/comfytime/releases), or build it (below).

1. Copy `comfytime.dll` and `comfytime.ini` into the client folder, next to `WoW.exe`.
2. Add a line `comfytime.dll` to `dlls.txt`. If you use comfygrass or comfyfog, put it **after** them:
   all three patch the same Direct3D device, and comfytime waits for the others to finish first.
3. Start the game through `VanillaFixes.exe`.

## How it works

The client keeps the time of day in three places, found by measurement in one `WoW.exe` (the Ctrl+F12
search: snapshot the client's writable memory, wait, and keep the values that advanced by exactly the minutes
that passed, in any of several encodings):

| Address | What |
| --- | --- |
| `0x00CE9B60` | minutes since midnight, integer |
| `0x00CE9B64` | fraction of the day, float (continuous: carries the seconds) |
| `0x00CE8574` | minutes since midnight, float |

In the world the client rewrites them every frame, between `BeginScene` and `Present`. So comfytime writes
the chosen time at `Present` and again at `BeginScene` -- just before the sky is drawn -- and the second write
wins. Both are Direct3D calls: comfytime finds DXVK's device vtable through a throwaway device and patches
those two slots in place, the same way comfygrass and comfyfog attach.

Before its first write it checks that the three addresses hold a consistent time, and refuses (and logs why)
if they do not -- which is what another client build would do. There, Ctrl+F12 finds the new addresses:
read the minimap clock, press Ctrl+F12, keep playing for about six minutes, and `comfytime.log` lists what
matched. Put those into the ini.

## Build

Visual Studio 2022 and CMake. **32-bit only**: the 1.12 client is x86.

```
cmake -B build -A Win32
cmake --build build --config Release
```

`comfytime.dll` lands in the project root, next to `comfytime.ini`.

## Licence

GPL-3.0 -- see [LICENSE](LICENSE).
