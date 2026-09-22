#include "config.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

Settings g_cfg;

namespace
{
    const wchar_t* kTime    = L"time";
    const wchar_t* kGeneral = L"general";

    float GetF(const wchar_t* sec, const wchar_t* key, float dflt, const wchar_t* ini)
    {
        wchar_t buf[64] = {};
        wchar_t def[64];
        swprintf(def, 64, L"%.6f", dflt);
        GetPrivateProfileStringW(sec, key, def, buf, 64, ini);
        return static_cast<float>(_wtof(buf));
    }

    int GetI(const wchar_t* sec, const wchar_t* key, int dflt, const wchar_t* ini)
    {
        return static_cast<int>(GetPrivateProfileIntW(sec, key, dflt, ini));
    }

    bool GetB(const wchar_t* sec, const wchar_t* key, bool dflt, const wchar_t* ini)
    {
        return GetPrivateProfileIntW(sec, key, dflt ? 1 : 0, ini) != 0;
    }

    // Reads a value that may be written in hex ("0x00CE9B60") or decimal.
    DWORD GetX(const wchar_t* sec, const wchar_t* key, DWORD dflt, const wchar_t* ini)
    {
        wchar_t buf[64] = {};
        GetPrivateProfileStringW(sec, key, L"", buf, 64, ini);
        if (!buf[0])
            return dflt;
        return static_cast<DWORD>(wcstoul(buf, nullptr, 0));
    }

    float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
}

void ResolveIniPath(HMODULE self, wchar_t* out, size_t count)
{
    GetModuleFileNameW(self, out, static_cast<DWORD>(count));
    wchar_t* slash = wcsrchr(out, L'\\');
    if (slash)
        wcscpy_s(slash + 1, count - (slash + 1 - out), L"comfytime.ini");
}

void LoadSettings(const wchar_t* ini)
{
    Settings s;

    s.time.enabled      = GetB(kTime, L"enabled", s.time.enabled, ini);
    s.time.hour         = Clamp(GetF(kTime, L"hour", s.time.hour, ini), 0.0f, 24.0f);
    s.time.step         = Clamp(GetF(kTime, L"step", s.time.step, ini), 0.001f, 6.0f);
    s.time.addrMinutes  = GetX(kTime, L"addrMinutes",  s.time.addrMinutes,  ini);
    s.time.addrFraction = GetX(kTime, L"addrFraction", s.time.addrFraction, ini);
    s.time.addrMinutesF = GetX(kTime, L"addrMinutesF", s.time.addrMinutesF, ini);

    s.logEnabled  = GetB(kGeneral, L"log",         s.logEnabled,  ini);
    s.hook        = GetB(kGeneral, L"hook",        s.hook,        ini);
    s.reloadKey   = GetI(kGeneral, L"reloadKey",   s.reloadKey,   ini);
    s.saveKey     = GetI(kGeneral, L"saveKey",     s.saveKey,     ini);
    s.scanKey     = GetI(kGeneral, L"scanKey",     s.scanKey,     ini);
    s.chainWaitMs = GetI(kGeneral, L"chainWaitMs", s.chainWaitMs, ini);

    g_cfg = s;
}
