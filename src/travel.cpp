// Fast travel from the map, and the mission check. Everything goes through the game's own events, in the
// order the game itself uses them:
//  * Open: close the gameplay menus the way the pause menu's Resume does (GameplayMenuState event 1), then
//    queue the FastTravelState "Open" event that a fast travel door queues (event 0). The game then pushes
//    its fast travel screen, builds the list and travels exactly as it does at a door.
//  * Missions: the game writes a "return-" save when a mission (a "sequence") starts. SavegameCache+0xa0
//    counts them, and the pause menu offers Abandon while it's non-zero. Our Abandon calls the function the
//    pause menu's Abandon calls, which loads that save.
// The hooks run inside the game's own systems, so every write happens where the game itself makes it.
#include "common.h"
#include <atomic>

namespace ftp {

// ---- signatures (build 25472515, updated for 25600401); each one pins the structure offsets used below ----
// heron::ui_system_menu::process_events body. It keeps rdx (GameplayMenuState&) as capture[0] (49 89 53 88).
static const char* kSigMenuEvents =
    "4C 8B DC 53 48 81 EC 90 00 00 00 80 79 0C 00 48 8B D9 0F 84 80 00 00 00 48 8B 84 24 D0 00 00 00 49 89 43 90 "
    "48 8B 84 24 C8 00 00 00 49 89 43 A0 48 8B 84 24 D8 00 00 00 49 89 43 B8 49 89 43 C8 48 8B 84 24 C0 00 00 00 "
    "49 89 43 D0 48 8B 84 24 E8 00 00 00 49 89 53 88";
static const size_t kMenuEventsPrologue = 15;
// Its Resume branch: GameplayMenuState (capture[0]) event 1, variant at +0xc, pending flag at +0x10.
static const char* kSigResume = "48 8B 03 80 78 10 00 74 21 80 78 0C 01 0F 84 ?? ?? ?? ?? C6 40 0C 01";
// heron::ui_fast_travel::process_events body: FastTravelState& in rcx, pending flag at +8.
static const char* kSigTravelEvents =
    "48 89 5C 24 08 55 48 8D 6C 24 B9 48 81 EC F0 00 00 00 80 79 08 00 48 8B D9 74 7F";
static const size_t kTravelEventsPrologue = 18;
// heron::fast_travel::check_interaction (a door): FastTravelRequest+0x10 = door id, then FastTravelState
// event 0 (variant at +4, pending at +8).
static const char* kSigDoor = "48 89 4A 10 41 80 78 08 00 74 14 41 80 78 04 00 74 17 41 C6 40 04 00";
// heron::fast_travel::handle_request body: FastTravelRequest& in rcx. The 2026-10-01 game update clears a new
// FastTravelRequest+0x18 ("a trip started this frame", set after it starts one) on entry; the registers the
// arguments are moved to are wildcards.
static const char* kSigRequest =
    "48 89 5C 24 10 48 89 74 24 18 55 57 41 56 48 8D 6C 24 F0 48 81 EC 10 01 00 00 4D 8B F1 49 8B ?? 48 8B ?? "
    "C6 41 18 00 80 79 08 00 0F 84";
static const size_t kRequestPrologue = 14;
// Abandon(SavegameCache<HeaderChunk>&, SavegameCache& (return saves at +0xa0), SavegameState& (slot at +0x90),
//         Ptr<TransitionRequest>&, int 0) -> bool
static const char* kSigAbandon =
    "48 89 5C 24 08 57 48 83 EC 60 49 8B D9 48 8B F9 83 BA A0 00 00 00 00 77 0D 32 C0 48 8B 5C 24 70 48 83 C4 60 "
    "5F C3 41 0F B6 90 90 00 00 00";

// ---- structure offsets (from the signatures) ----
static const size_t kMenuVariant = 0x0c, kMenuPending = 0x10;
static const uint8_t kMenuClose = 1;  // Resume: back to plain gameplay
static const size_t kTravelVariant = 0x04, kTravelPending = 0x08;
static const uint8_t kTravelOpen = 0;
static const size_t kRequestDoor = 0x10;  // the door the menu was opened at ("current location")
static const size_t kCacheReturnSaves = 0xa0;

using MenuEventsFn = void (*)(void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, void*);
using TravelEventsFn = void (*)(void*, void*, void*, void*, void*, void*);
using RequestFn = void (*)(void*, void*, void*, void*, void*, void*, void*, void*, void*, void*);
using AbandonFn = bool (*)(const void*, const void*, const void*, void*, int);

static void* volatile g_origMenu = nullptr;
static void* volatile g_origTravel = nullptr;
static void* volatile g_origRequest = nullptr;
static AbandonFn g_abandon = nullptr;
static std::atomic<bool> g_installed{false};
static std::atomic<bool> g_faulted{false};

// ---- state shared with the endpoint (UI resource thread) ----
enum Want { kWantNone = 0, kWantOpen = 1, kWantAbandon = 2 };
enum Phase { kIdle = 0, kClosingMenus = 1 };
enum Result { kResNone = 0, kResOpened, kResAbandoning, kResAbandonFailed, kResNotInMission };
static const char* kResultNames[] = {"none", "opened", "abandoning", "abandon-failed", "not-in-mission"};

static std::atomic<int> g_want{kWantNone};  // set by the script, claimed by a hook
static std::atomic<ULONGLONG> g_wantAt{0};
static std::atomic<int> g_phase{kIdle};
static std::atomic<ULONGLONG> g_phaseAt{0};
static std::atomic<uint32_t> g_phaseFrame{0};
static std::atomic<uint32_t> g_menuFrames{0};
static std::atomic<ULONGLONG> g_menuSeen{0}, g_travelSeen{0}, g_requestSeen{0};
static std::atomic<int> g_returnSaves{-1};
static std::atomic<ULONGLONG> g_abandonedAt{0};
static std::atomic<uint32_t> g_serial{0};  // bumps when a request finishes
static std::atomic<int> g_result{kResNone};

static void Finish(Result r) {
    g_result = r;
    ++g_serial;
    Log("Result: %s", kResultNames[r]);
}

// Diagnostics: every 5 s, which of the hooked systems ran and how often.
static std::atomic<uint32_t> g_travelFrames{0}, g_requestFrames{0};
static std::atomic<ULONGLONG> g_lastBeat{0};
static void Heartbeat(ULONGLONG now) {
    if (!g_cfg.diagnostics) return;
    ULONGLONG last = g_lastBeat.load();
    if (now - last < 5000 || !g_lastBeat.compare_exchange_strong(last, now)) return;
    static uint32_t menu0, travel0, request0;
    const uint32_t menu = g_menuFrames.load(), travel = g_travelFrames.load(), request = g_requestFrames.load();
    Log("Frames in 5 s: pause menu %u, fast travel %u, request %u; return saves %d, want %d, phase %d", menu - menu0,
        travel - travel0, request - request0, g_returnSaves.load(), g_want.load(), g_phase.load());
    menu0 = menu, travel0 = travel, request0 = request;
}

// ---- the three game hooks ----
// Same writes as the pause menu's Resume.
static void QueueMenuClose(uint8_t* menu) {
    if (!menu[kMenuPending]) {
        menu[kMenuVariant] = kMenuClose;
        menu[kMenuPending] = 1;
    } else if (menu[kMenuVariant] != kMenuClose) {
        menu[kMenuVariant] = kMenuClose;
    }
}

// Same writes as a fast travel door, except the door id: 0 means no list entry is "current location".
static void QueueTravelOpen(uint8_t* state, uint8_t* request) {
    *(uint64_t*)(request + kRequestDoor) = 0;
    if (state[kTravelPending]) {
        if (state[kTravelVariant] != kTravelOpen) state[kTravelVariant] = kTravelOpen;
    } else {
        state[kTravelPending] = 1;
        state[kTravelVariant] = kTravelOpen;
    }
}

// Mission state and Abandon. Both the pause menu's system (where the game itself calls Abandon) and the fast
// travel request system have the savegame state, so whichever runs first serves the request: the pause menu's
// systems run while a gameplay menu is up, the request system during play.
static void ServeMission(const uint8_t* cacheHC, const uint8_t* saveState, const uint8_t* cache, void* transition,
                         const char* where) {
    const uint32_t saves = *(const uint32_t*)(cache + kCacheReturnSaves);
    const int clamped = (int)std::min<uint32_t>(saves, 1000000u);
    const int previous = g_returnSaves.exchange(clamped);
    if (g_cfg.diagnostics && previous != clamped) Log("Mission return saves: %d -> %d (%s)", previous, clamped, where);
    int want = kWantAbandon;
    if (g_want.load() == kWantAbandon && g_want.compare_exchange_strong(want, kWantNone)) {
        if (!saves) {
            Finish(kResNotInMission);
            return;
        }
        const bool ok = g_abandon(cacheHC, cache, saveState, transition, 0);
        Log("Abandon (%s): %s", where, ok ? "the game is loading the mission's return save" : "the game refused");
        if (ok) g_abandonedAt = GetTickCount64();
        Finish(ok ? kResAbandoning : kResAbandonFailed);
    }
}

static void MenuFrame(uint8_t* gameplayMenu, void* transition, const uint8_t* saveState, const uint8_t* cacheHC,
                      const uint8_t* cache) {
    const ULONGLONG now = GetTickCount64();
    g_menuSeen = now;
    const uint32_t frame = ++g_menuFrames;
    Heartbeat(now);
    ServeMission(cacheHC, saveState, cache, transition, "pause menu");
    int want = kWantOpen;
    if (g_phase.load() == kIdle && g_want.load() == kWantOpen && g_want.compare_exchange_strong(want, kWantNone)) {
        QueueMenuClose(gameplayMenu);
        g_phaseFrame = frame;
        g_phaseAt = now;
        g_phase = kClosingMenus;
        Log("Open: closing the gameplay menus (Resume)");
    }
}

static void TravelFrame(uint8_t* state, uint8_t* request) {
    const ULONGLONG now = GetTickCount64();
    g_travelSeen = now;
    ++g_travelFrames;
    Heartbeat(now);
    // The pause menu's systems only run while a gameplay menu is up. If they aren't running, there's nothing
    // to close: open right away.
    int want = kWantOpen;
    if (g_phase.load() == kIdle && g_want.load() == kWantOpen &&
        (now - g_menuSeen.load() > 250 || now - g_wantAt.load() > 500) && g_want.compare_exchange_strong(want, kWantNone)) {
        Log("Open: no gameplay menu is running (menu seen %llu ms ago)", now - g_menuSeen.load());
        g_phaseFrame = g_menuFrames.load() - 2;
        g_phaseAt = now;
        g_phase = kClosingMenus;
    }
    if (g_phase.load() != kClosingMenus) return;
    // Give the gameplay menu at least one full frame to process Resume before fast travel pushes its screen.
    if (g_menuFrames.load() - g_phaseFrame.load() >= 2 || now - g_phaseAt.load() >= 150) {
        QueueTravelOpen(state, request);
        g_phase = kIdle;
        Finish(kResOpened);
    }
}

static void RequestFrame(const uint8_t* cacheHC, const uint8_t* saveState, const uint8_t* cache, void* transition) {
    const ULONGLONG now = GetTickCount64();
    g_requestSeen = now;
    ++g_requestFrames;
    Heartbeat(now);
    ServeMission(cacheHC, saveState, cache, transition, "fast travel request");
}

static void Fault(const char* where) {
    if (!g_faulted.exchange(true)) Log("Exception in the %s hook; Fast Travel Plus stops touching the game", where);
}

// process_events(SystemMenuState&, GameplayMenuState& [2], ErrorModalState&, ExitModalState&,
//                Ptr<TransitionRequest>& [5], UIStateStacks&, SubSaveGameState&, AudioEventRequests&,
//                SavegameState [9], SavegameCache<HeaderChunk> [10], SavegameCache [11])
static void HookMenuEvents(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6, void* a7, void* a8, void* a9,
                           void* a10, void* a11) {
    if (!g_faulted.load() && a2 && a5 && a9 && a10 && a11) {
        __try {
            MenuFrame((uint8_t*)a2, a5, (const uint8_t*)a9, (const uint8_t*)a10, (const uint8_t*)a11);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Fault("pause menu");
        }
    }
    ((MenuEventsFn)g_origMenu)(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
}

static void HookTravelEvents(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) {
    if (!g_faulted.load() && a1 && a4) {
        __try {
            TravelFrame((uint8_t*)a1, (uint8_t*)a4);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Fault("fast travel menu");
        }
    }
    ((TravelEventsFn)g_origTravel)(a1, a2, a3, a4, a5, a6);
}

// handle_request(FastTravelRequest&, DistrictMarkerDatabase, ZonesMetadata, SharedBundleState,
//                SavegameCache<HeaderChunk> [5], SavegameState [6], SavegameCache [7], TransitionRequest [8], ...)
static void HookRequest(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6, void* a7, void* a8, void* a9,
                        void* a10) {
    if (!g_faulted.load() && a5 && a6 && a7 && a8) {
        __try {
            RequestFrame((const uint8_t*)a5, (const uint8_t*)a6, (const uint8_t*)a7, a8);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Fault("fast travel request");
        }
    }
    ((RequestFn)g_origRequest)(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
}

// ---- setup ----
bool FindTravelTargets(const Image& img, TravelTargets& out, std::string& err) {
    out = TravelTargets{};
    if (!(out.menuEvents = FindUnique(img, "pause menu events", kSigMenuEvents, err))) return false;
    if (!FindUnique(img, "pause menu Resume", kSigResume, err)) return false;
    if (!(out.travelEvents = FindUnique(img, "fast travel events", kSigTravelEvents, err))) return false;
    if (!FindUnique(img, "fast travel door", kSigDoor, err)) return false;
    if (!(out.request = FindUnique(img, "fast travel request", kSigRequest, err))) return false;
    if (!(out.abandon = FindUnique(img, "Abandon", kSigAbandon, err))) return false;
    return true;
}

bool InstallTravelHooksAt(uint8_t* menu, const uint8_t* menuBytes, uint8_t* travel, const uint8_t* travelBytes,
                          uint8_t* request, const uint8_t* requestBytes, void* abandon, std::string& err) {
    g_abandon = (AbandonFn)abandon;
    // The request hook first: it only reads until the script asks for something.
    if (!InstallJmpHook(request, requestBytes, kRequestPrologue, (void*)&HookRequest, &g_origRequest,
                        "fast travel request", err))
        return false;
    if (!InstallJmpHook(travel, travelBytes, kTravelEventsPrologue, (void*)&HookTravelEvents, &g_origTravel,
                        "fast travel events", err))
        return false;
    if (!InstallJmpHook(menu, menuBytes, kMenuEventsPrologue, (void*)&HookMenuEvents, &g_origMenu, "pause menu events",
                        err))
        return false;
    g_installed = true;
    return true;
}

bool InstallTravelHooks(const Image& img, std::string& err) {
    TravelTargets t;
    if (!FindTravelTargets(img, t, err)) return false;
    uint8_t* base = (uint8_t*)g_gameBase;
    if (!InstallTravelHooksAt(base + t.menuEvents, &img.mem[t.menuEvents], base + t.travelEvents,
                              &img.mem[t.travelEvents], base + t.request, &img.mem[t.request], base + t.abandon, err))
        return false;
    Log("Game hooks active: pause menu +0x%x, fast travel +0x%x, request +0x%x, abandon +0x%x", t.menuEvents,
        t.travelEvents, t.request, t.abandon);
    return true;
}

bool TravelHooksInstalled() { return g_installed.load(); }

// ---- endpoint ----
static long long Age(ULONGLONG t, ULONGLONG now) { return t ? (long long)(now - t) : -1; }

static std::string StatusJson(bool accepted) {
    const ULONGLONG now = GetTickCount64();
    const int saves = g_returnSaves.load();
    char buf[640];
    sprintf_s(buf,
              "{\"ok\":true,\"version\":\"" FTP_VERSION "\",\"installed\":%s,\"accepted\":%s,\"menuAge\":%lld,"
              "\"travelAge\":%lld,\"requestAge\":%lld,\"returnSaves\":%d,\"inMission\":%s,\"busy\":%s,\"serial\":%u,"
              "\"result\":\"%s\",\"abandonAge\":%lld,\"hotkey\":%d,\"presses\":%u,\"keyHeld\":%u,\"padHold\":%d,"
              "\"padPress\":%d,\"padPresses\":%u,\"padHeld\":%u}",
              g_installed.load() && !g_faulted.load() ? "true" : "false", accepted ? "true" : "false",
              Age(g_menuSeen.load(), now), Age(g_travelSeen.load(), now), Age(g_requestSeen.load(), now), saves,
              saves > 0 ? "true" : "false", (g_want.load() != kWantNone || g_phase.load() != kIdle) ? "true" : "false",
              g_serial.load(), kResultNames[g_result.load()], Age(g_abandonedAt.load(), now), HotkeyVk(),
              HotkeyPresses(), HotkeyHeldMs(), PadHoldCode(), PadPressCode(), PadPresses(), PadHeldMs());
    return buf;
}

std::string TravelAction(const std::string& action, const char* query) {
    const bool usable = g_installed.load() && !g_faulted.load();
    bool accepted = false;
    if (action == "open" || action == "abandon") {
        const int want = action == "open" ? kWantOpen : kWantAbandon;
        int none = kWantNone;
        // A request nobody claimed for 3 s (the game was paused or loading) is replaced.
        if (g_want.load() != kWantNone && GetTickCount64() - g_wantAt.load() > 3000) g_want = kWantNone;
        if (usable && g_phase.load() == kIdle) {
            g_wantAt = GetTickCount64();
            accepted = g_want.compare_exchange_strong(none, want);
        }
        Log("Script asks: %s (%s)", action.c_str(), accepted ? "accepted" : usable ? "busy" : "hooks unavailable");
        if (accepted && want == kWantOpen) g_abandonedAt = 0;  // the after-abandon step is done
    } else if (action == "clear") {
        g_abandonedAt = 0;
        accepted = true;
    } else if (action == "hotkey") {
        // The keyboard key option changed on the MODS page: its key code, so the key works without a restart.
        std::string k;
        bool digits = QueryParam(query, "k", k) && !k.empty() && k.size() <= 4;
        for (char c : k) digits = digits && c >= '0' && c <= '9';
        accepted = digits && SetHotkeyVk(atoi(k.c_str()));
    } else if (action == "pad") {
        // A controller key option changed: the Hold and Press button codes.
        std::string h, p;
        bool digits = QueryParam(query, "h", h) && QueryParam(query, "p", p) && !h.empty() && !p.empty() &&
                      h.size() <= 3 && p.size() <= 3;
        for (char c : h + p) digits = digits && c >= '0' && c <= '9';
        accepted = digits && SetPadCodes(atoi(h.c_str()), atoi(p.c_str()));
    }
    return StatusJson(accepted);
}

std::string BuildConfigJs() {
    std::string js = "window.__FastTravelPlusConfig={version:\"" FTP_VERSION "\",diagnostics:";
    js += g_cfg.diagnostics ? "1" : "0";
    js += ",hooks:";
    js += g_installed.load() ? "1" : "0";
    // The current key codes, and every keyboard and mouse key's name on the player's layout, for the prompts.
    js += ",hotkey:" + std::to_string(HotkeyVk()) + ",keyNames:{";
    bool first = true;
    for (int vk = 3; vk <= 254; ++vk) {
        const std::string name = KeyName(vk);
        if (name.empty()) continue;
        if (!first) js += ',';
        first = false;
        js += std::to_string(vk) + ":\"" + JsonEscape(name) + "\"";
    }
    js += "},padHold:" + std::to_string(PadHoldCode()) + ",padPress:" + std::to_string(PadPressCode()) + "};";
    return js;
}

}  // namespace ftp
