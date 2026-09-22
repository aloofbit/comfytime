// comfytime: set the time of day in the 1.12 client, on your screen only.
//
// The server sets the game clock and 1.12 gives no command to change it. The client keeps the time in
// memory (timeofday.cpp has the addresses and how they were found) and builds the sky, the sun and the
// world's lighting from it, so writing a chosen time there moves all of them.
//
// Once in the world the client rewrites those values every frame, somewhere between BeginScene and
// Present. So the chosen time is written at Present and again at BeginScene, immediately before the sky is
// drawn from it: the second write is the one that wins. Both are Direct3D calls, so comfytime attaches the
// way its siblings do: a throwaway device names DXVK's shared IDirect3DDevice9 vtable, and two slots in it
// are patched in place (see comfygrass's README for why in place, and why a throwaway device).
//
// Taking turns: comfygrass, comfyfog and comfytime all patch that one vtable, on background threads, at
// start-up. Two installers flipping the same page's protection at once can leave it read-only under the
// other's write, which crashes the game. comfyfog already waits for comfygrass to finish; comfytime waits
// for both, then patches with a compare-exchange so it chains on top of whatever is there.

#define CINTERFACE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d9.h>

#include "common.h"
#include "config.h"
#include "timeofday.h"

#include <cstdarg>
#include <cstdio>

namespace
{
    wchar_t g_iniPath[MAX_PATH] = {};
    wchar_t g_logPath[MAX_PATH] = {};

    // The attach thread and the render thread both log; one lock keeps lines whole.
    CRITICAL_SECTION g_lock;
    bool             g_lockReady = false;

    struct Guard
    {
        Guard()  { if (g_lockReady) EnterCriticalSection(&g_lock); }
        ~Guard() { if (g_lockReady) LeaveCriticalSection(&g_lock); }
    };
}

void Log(const char* fmt, ...)
{
    if (!g_cfg.logEnabled)
        return;
    Guard g;
    FILE* f = nullptr;
    if (_wfopen_s(&f, g_logPath, L"a") != 0 || !f)
        return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

double Now()
{
    static double inv = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return 1.0 / static_cast<double>(f.QuadPart);
    }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart) * inv;
}

namespace
{
    // In place, never a copy, and as a compare-exchange: other mods patch these same slots.
    bool HookSlot(void** slot, void* hook, void** origOut)
    {
        if (*slot == hook)
            return true;
        DWORD prot = 0;
        if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &prot))
            return false;
        void* cur = *slot;
        while (cur != hook)
        {
            *origOut = cur;   // set before the swap, so the hook never runs with a null original
            void* prev = InterlockedCompareExchangePointer(slot, hook, cur);
            if (prev == cur)
                break;
            cur = prev;
        }
        VirtualProtect(slot, sizeof(void*), prot, &prot);
        return true;
    }

    using PresentFn    = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
    using BeginSceneFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*);
    PresentFn    g_oPresent    = nullptr;
    BeginSceneFn g_oBeginScene = nullptr;

    bool g_reloadDown = false, g_scanDown = false, g_upDown = false, g_dnDown = false;

    // Keys only count while the client has focus, so typing into another window does nothing here.
    bool ClientFocused()
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        return pid == GetCurrentProcessId();
    }

    void PollKeys()
    {
        const bool focused = ClientFocused();
        auto down = [focused](int vk) { return focused && (GetAsyncKeyState(vk) & 0x8000) != 0; };
        const bool ctrl = down(VK_CONTROL);

        const bool reload = down(g_cfg.reloadKey);
        if (reload && !g_reloadDown && !ctrl && !down(VK_SHIFT) && !down(VK_MENU))
        {
            LoadSettings(g_iniPath);
            TimeReload();
            Log("--- reloaded: time %s, hour %.2f ---", g_cfg.time.enabled ? "ON" : "OFF", g_cfg.time.hour);
        }
        g_reloadDown = reload;

        const bool scan = down(g_cfg.scanKey);
        if (scan && !g_scanDown && ctrl)
            TimeScanStart();
        g_scanDown = scan;

        // A press moves the time one step; holding the key keeps it moving, after a short wait so a
        // single tap stays a single step.
        const bool up = ctrl && down(VK_PRIOR);
        const bool dn = ctrl && down(VK_NEXT);
        const double now = Now();
        static double heldSince = 0.0, lastRepeat = 0.0;
        constexpr double kHoldWait   = 0.35;   // seconds before a held key starts repeating
        constexpr double kRepeatEvery = 0.03;  // seconds between steps while held
        if ((up && !g_upDown) || (dn && !g_dnDown))
        {
            TimeStep(up ? +g_cfg.time.step : -g_cfg.time.step);
            heldSince = now;
            lastRepeat = now;
        }
        else if ((up || dn) && now - heldSince > kHoldWait && now - lastRepeat >= kRepeatEvery)
        {
            TimeStep(up ? +g_cfg.time.step : -g_cfg.time.step);
            lastRepeat = now;
        }
        g_upDown = up; g_dnDown = dn;
    }

    HRESULT STDMETHODCALLTYPE hkPresent(IDirect3DDevice9* dev, const RECT* src, const RECT* dst, HWND wnd,
                                        const RGNDATA* dirty)
    {
        PollKeys();
        TimeScanTick();
        TimeApply("Present");
        return g_oPresent(dev, src, dst, wnd, dirty);
    }

    // The first call of a frame's rendering: the last chance to put the chosen time in place before the
    // sky is drawn from it.
    HRESULT STDMETHODCALLTYPE hkBeginScene(IDirect3DDevice9* dev)
    {
        TimeApply("BeginScene");
        return g_oBeginScene(dev);
    }

    // ---------------------------------------------------------------------------------------------
    // attaching

    HMODULE OwnerOf(const void* fn)
    {
        HMODULE m = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(fn), &m);
        return m;
    }

    // Until comfygrass and comfyfog, if loaded, have patched the last slot each of them installs:
    // comfyfog's is DrawIndexedPrimitiveUP; comfygrass's is DrawIndexedPrimitive (and comfyfog waits for
    // comfygrass itself, so with both loaded, comfyfog finishing means both have).
    void WaitForSiblings(IDirect3DDevice9Vtbl* v)
    {
        HMODULE grass = GetModuleHandleA("comfygrass.dll");
        HMODULE fog   = GetModuleHandleA("comfyfog.dll");
        if (!grass && !fog)
        {
            Log("neither comfygrass nor comfyfog is loaded, patching straight away");
            return;
        }
        const double t0 = Now();
        for (;;)
        {
            const bool fogDone   = !fog || OwnerOf(v->DrawIndexedPrimitiveUP) == fog;
            bool       grassDone = true;
            if (grass)   // with comfyfog loaded, comfyfog finishing implies comfygrass did
                grassDone = fog ? fogDone : OwnerOf(v->DrawIndexedPrimitive) == grass;
            if (fogDone && grassDone)
            {
                Log("%s%s%s patched first (waited %.0f ms), chaining on top", grass ? "comfygrass" : "",
                    grass && fog ? " and " : "", fog ? "comfyfog" : "", 1000.0 * (Now() - t0));
                return;
            }
            if ((Now() - t0) * 1000.0 > g_cfg.chainWaitMs)
            {
                Log("comfygrass/comfyfog loaded but not finished within %d ms; patching anyway", g_cfg.chainWaitMs);
                return;
            }
            Sleep(20);
        }
    }

    using Direct3DCreate9Fn = IDirect3D9*(WINAPI*)(UINT);

    bool AttachToDxvk()
    {
        HMODULE d3d9 = GetModuleHandleA("d3d9.dll");
        if (!d3d9)
            d3d9 = LoadLibraryA("d3d9.dll");
        if (!d3d9)
        {
            Log("FATAL: no d3d9.dll in this process");
            return false;
        }
        HMODULE pin = nullptr;   // our hooks live in its vtable: it must never unload under us
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, L"d3d9.dll", &pin);

        auto create = reinterpret_cast<Direct3DCreate9Fn>(GetProcAddress(d3d9, "Direct3DCreate9"));
        IDirect3D9* d3d = create ? create(D3D_SDK_VERSION) : nullptr;
        if (!d3d)
        {
            Log("FATAL: Direct3DCreate9 unavailable");
            return false;
        }

        WNDCLASSEXA wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = DefWindowProcA;
        wc.hInstance     = GetModuleHandleA(nullptr);
        wc.lpszClassName = "comfytime_probe";
        RegisterClassExA(&wc);
        HWND wnd = CreateWindowExA(0, wc.lpszClassName, "", WS_OVERLAPPED, 0, 0, 1, 1,
                                   nullptr, nullptr, wc.hInstance, nullptr);

        D3DPRESENT_PARAMETERS pp = {};
        pp.Windowed         = TRUE;
        pp.SwapEffect       = D3DSWAPEFFECT_DISCARD;
        pp.BackBufferFormat = D3DFMT_UNKNOWN;
        pp.BackBufferWidth  = 1;
        pp.BackBufferHeight = 1;
        pp.hDeviceWindow    = wnd;

        IDirect3DDevice9* probe = nullptr;
        const HRESULT hr = d3d->lpVtbl->CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, wnd,
                                                     D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_NOWINDOWCHANGES,
                                                     &pp, &probe);
        if (FAILED(hr) || !probe)
        {
            Log("FATAL: probe CreateDevice failed hr=0x%08X", hr);
            d3d->lpVtbl->Release(d3d);
            if (wnd) DestroyWindow(wnd);
            return false;
        }
        auto* v = const_cast<IDirect3DDevice9Vtbl*>(probe->lpVtbl);   // static data in the pinned d3d9.dll
        probe->lpVtbl->Release(probe);
        d3d->lpVtbl->Release(d3d);
        if (wnd) DestroyWindow(wnd);
        UnregisterClassA(wc.lpszClassName, wc.hInstance);

        WaitForSiblings(v);
        const bool ok =
            HookSlot(reinterpret_cast<void**>(&v->Present),    &hkPresent,    reinterpret_cast<void**>(&g_oPresent)) &&
            HookSlot(reinterpret_cast<void**>(&v->BeginScene), &hkBeginScene, reinterpret_cast<void**>(&g_oBeginScene));
        Log("device %s (vtable %p)", ok ? "hooked" : "HOOK FAILED", v);
        return ok;
    }

    // Off the loader lock: DllMain must not load libraries or create devices.
    DWORD WINAPI AttachThread(LPVOID)
    {
        const double t0 = Now();
        const bool ok = AttachToDxvk();
        Log("attach %s in %.0f ms", ok ? "succeeded" : "FAILED", 1000.0 * (Now() - t0));
        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        InitializeCriticalSection(&g_lock);
        g_lockReady = true;
        DisableThreadLibraryCalls(self);

        // Our hooks are function pointers in DXVK's vtable; unloading this image would leave them dangling.
        HMODULE pin = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           reinterpret_cast<LPCWSTR>(&g_lock), &pin);
        ResolveIniPath(self, g_iniPath, MAX_PATH);
        wcscpy_s(g_logPath, g_iniPath);
        wcscpy_s(wcsrchr(g_logPath, L'\\') + 1, 16, L"comfytime.log");
        DeleteFileW(g_logPath);
        LoadSettings(g_iniPath);
        Log("comfytime loaded (module=%p, time %s, hour %.2f)", self, g_cfg.time.enabled ? "ON" : "OFF", g_cfg.time.hour);

        if (g_cfg.hook)
        {
            HANDLE t = CreateThread(nullptr, 0, AttachThread, nullptr, 0, nullptr);
            if (t)
                CloseHandle(t);
            else
                Log("FATAL: could not start the attach thread");
        }
        else
        {
            Log("hook = 0, so nothing is patched: comfytime is inert this run");
        }
    }
    return TRUE;
}
