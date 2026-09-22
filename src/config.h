// comfytime.ini: the time of day to show, and the addresses it lives at.
#pragma once

#include <windows.h>

struct TimeSettings
{
    bool  enabled      = true;        // installing comfytime is the opt-in; this is the off switch
    float hour         = 13.0f;       // 0..24, fractions allowed (13.5 = 13:30)
    float step         = 0.0167f;     // hours per Ctrl+PageUp / PageDown, 0.0167 is about a minute
    DWORD addrMinutes  = 0x00CE9B60;  // int minutes since midnight   (found by the Ctrl+F12 search)
    DWORD addrFraction = 0x00CE9B64;  // float fraction of the day
    DWORD addrMinutesF = 0x00CE8574;  // float minutes since midnight; 0 = leave alone
};

struct Settings
{
    TimeSettings time;

    bool  logEnabled  = true;
    bool  hook        = true;         // 0: load, log, patch nothing (bisecting)
    int   reloadKey   = VK_F11;       // reload comfytime.ini (comfyfog reloads its own on the same key)
    int   scanKey     = VK_F12;       // with Ctrl: search memory for the game clock (read-only)
    int   saveKey     = VK_HOME;      // with Ctrl: write the time being shown back to the ini, so the
                                      // next start comes up with the same sun
    int   chainWaitMs = 10000;        // how long to wait for comfygrass / comfyfog to finish patching
};

extern Settings g_cfg;

void LoadSettings(const wchar_t* iniPath);
void ResolveIniPath(HMODULE self, wchar_t* out, size_t count);
