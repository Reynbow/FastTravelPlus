// Entry point. crloader (winmm.dll) loads every DLL in crmods\ at start-up; we do our setup on a worker
// thread so the loader lock is never held while we read and scan the game image.
#include "common.h"

namespace ftp {

HMODULE g_self = nullptr;
std::wstring g_modDir;
uintptr_t g_gameBase = 0;

static const char* kKnownBuildSha = "4f6596b08bb5bc7fe4150cf5b9f71d7bae87eea02d66627c03a5e05ffe84ea62";  // build 25673981

static void Setup() {
    LoadConfig();
    LogInit();
    Log("Fast Travel Plus " FTP_VERSION " folder=%s", Utf8(g_modDir).c_str());
    // First thing: Mod Settings Menu reads the saved values when the game's UI loads, and then shows these.
    if (CarryOverSliders()) LoadConfig();
    if (!g_cfg.enabled) {
        Log("Disabled by INI (Enabled=0)");
        return;
    }
    wchar_t name[64];
    swprintf_s(name, L"Local\\FastTravelPlus.Instance.%lu", GetCurrentProcessId());
    HANDLE instance = CreateMutexW(nullptr, TRUE, name);
    if (!instance || GetLastError() == ERROR_ALREADY_EXISTS) {
        Log("Another Fast Travel Plus copy is already active in this process; this copy stays idle");
        return;
    }
    // Intentionally never closed: marks this process as served for its lifetime.

    g_gameBase = (uintptr_t)GetModuleHandleW(nullptr);
    wchar_t exe[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, exe, (DWORD)(sizeof(exe) / sizeof(exe[0])));
    if (!n || n >= sizeof(exe) / sizeof(exe[0])) {
        Log("Cannot resolve the game executable path; not installing");
        return;
    }
    Image img;
    std::string sha;
    if (!LoadPristineImage(exe, img, sha)) {
        Log("Cannot read the game image; not installing");
        return;
    }
    Log("Game EXE SHA256=%s (%s)", sha.c_str(),
        sha == kKnownBuildSha ? "known build 25673981" : "other build; running on signatures");

    BuildKeyNames();
    if (g_cfg.hotkey >= 0 && !SetHotkeyVk(g_cfg.hotkey))
        Log("Saved hotkey %d isn't a keyboard or mouse key; using the default", g_cfg.hotkey);
    if ((g_cfg.padHold >= 0 || g_cfg.padPress >= 0) &&
        !SetPadCodes(g_cfg.padHold >= 0 ? g_cfg.padHold : kPadCodeBase + kPadDefaultHold,
                     g_cfg.padPress >= 0 ? g_cfg.padPress : kPadCodeBase + kPadDefaultPress))
        Log("Saved controller hotkey %d + %d isn't pad buttons; using the default", g_cfg.padHold, g_cfg.padPress);

    // Game hooks first, so the script's config already says whether they work when the UI asks for it.
    std::string err;
    if (!InstallTravelHooks(img, err))
        Log("Fast travel hooks off: %s. The map button stays hidden on this game version.", err.c_str());
    err.clear();
    if (!InstallResourceHook(img, err)) {
        Log("Resource interception failed (%s); Fast Travel Plus stays off for this game version", err.c_str());
        return;
    }
    if (TravelHooksInstalled()) {
        StartHotkey();
        StartPad();
    }
    Log("Setup done: hooks=%d diagnostics=%d", TravelHooksInstalled(), g_cfg.diagnostics);
}

static DWORD WINAPI SetupThread(void*) {
    try {
        Setup();
    } catch (...) {
        Log("Initialization exception; no further setup attempted");
    }
    return 0;
}

}  // namespace ftp

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        ftp::g_self = module;
        wchar_t path[MAX_PATH * 2];
        DWORD n = GetModuleFileNameW(module, path, (DWORD)(sizeof(path) / sizeof(path[0])));
        std::wstring p(path, n);
        size_t slash = p.find_last_of(L"\\/");
        ftp::g_modDir = slash == std::wstring::npos ? L".\\" : p.substr(0, slash + 1);
        HANDLE h = CreateThread(nullptr, 0, ftp::SetupThread, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
    }
    return TRUE;
}
