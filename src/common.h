// Fast Travel Plus - open the game's fast travel menu from the map screen in CONTROL Resonant.
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdint.h>
#include <algorithm>
#include <string>
#include <vector>

#define FTP_VERSION "1.2.1"

namespace ftp {

// ---- globals (dllmain.cpp) ----
extern HMODULE g_self;
extern std::wstring g_modDir;   // folder holding fasttravelplus.dll, trailing backslash
extern uintptr_t g_gameBase;    // live base of CONTROLResonant.exe

// ---- logging (util.cpp) ----
void LogInit();
void Log(const char* fmt, ...);

// ---- small helpers (util.cpp) ----
std::string Utf8(const std::wstring& w);
std::wstring Wide(const std::string& s);
bool ReadWholeFile(const std::wstring& path, std::string& out, size_t maxBytes);
bool WriteWholeFile(const std::wstring& path, const void* data, size_t size);  // temp file + replace
bool EnsureDirectoryFor(const std::wstring& filePath);
std::string UrlDecode(const char* s, size_t n);
// Value of key in a query string ("a=1&b=2"), URL-decoded. Returns false when absent.
bool QueryParam(const char* query, const char* key, std::string& out);
std::string JsonEscape(const std::string& s);

// ---- configuration (config.cpp) ----
struct Config {
    bool enabled = true;
    bool diagnostics = false;  // extra log lines from the DLL and the script
    int hotkey = -1;           // slider position saved by Mod Settings Menu (ModMenuConfig), -1 = not saved
    int padHold = -1, padPress = -1;  // the controller section's saved slider positions, -1 = not saved
};
extern Config g_cfg;
void LoadConfig();

// ---- hotkey (keys.cpp) ----
struct HotkeyKey {
    int vk;            // virtual-key code, 0 = off
    std::string name;  // as shown on the MODS page, in the player's keyboard layout
};
const int kDefaultHotkey = 1;  // the key left of 1: ` ~ on US keyboards
void BuildHotkeyList();        // the MODS page slider's keys, by position
const std::vector<HotkeyKey>& HotkeyList();
int HotkeyIndex();
bool SetHotkeyIndex(int index);
uint32_t HotkeyPresses();      // counts presses while the game has focus
uint32_t HotkeyHeldMs();       // how long the current press has lasted, 0 while the key is up
void StartHotkey();

// ---- controller hotkey (pad.cpp) ----
const int kPadDefaultHold = 11;   // LS (L3)
const int kPadDefaultPress = 12;  // RS (R3)
int PadButtonCount();             // slider positions: 0 = None (Hold) / Off (Press), then the 16 buttons
std::string PadButtonName(int index);
int PadHold();
int PadPress();
bool SetPadCombo(int hold, int press);
uint32_t PadPresses();            // counts combo presses while the game has focus
uint32_t PadHeldMs();             // how long the combo has been held, 0 while it's off
void StartPad();
// One DualSense / DualShock 4 input report to the button bits (1 << slider position); for the tests too.
bool ParseSonyReport(uint16_t pid, size_t reportLength, const uint8_t* d, size_t n, uint32_t& buttons);

// ---- pristine game image (image.cpp) ----
struct Section {
    char name[9];
    uint32_t rva, size;
    bool exec, write;
};
struct Image {
    std::vector<uint8_t> mem;  // sections copied to their RVAs
    uint64_t prefBase = 0;
    uint32_t sizeOfImage = 0;
    std::vector<Section> secs;
    const Section* SectionOf(uint32_t rva) const;
    bool Contains(uint32_t rva, uint32_t n) const { return (uint64_t)rva + n <= mem.size(); }
    uint32_t U32(uint32_t rva) const { uint32_t v; memcpy(&v, &mem[rva], 4); return v; }
    int32_t I32(uint32_t rva) const { int32_t v; memcpy(&v, &mem[rva], 4); return v; }
    uint64_t U64(uint32_t rva) const { uint64_t v; memcpy(&v, &mem[rva], 8); return v; }
};
bool LoadPristineImage(const std::wstring& exePath, Image& img, std::string& sha256Hex);
struct Pattern {
    std::vector<int> bytes;  // -1 = wildcard
    bool Parse(const char* text);
};
// All matches of pattern in executable sections (up to max).
std::vector<uint32_t> FindPattern(const Image& img, const Pattern& p, size_t max = 16);
bool MatchAt(const Image& img, uint32_t rva, const Pattern& p);
// Unique match or 0 with an error message.
uint32_t FindUnique(const Image& img, const char* what, const char* pattern, std::string& err);

// ---- inline hooks (hook.cpp) ----
bool PatchWithThreadsSuspended(uint8_t* target, const uint8_t* patch, size_t n);
// Replaces the first len (14..32, whole position-independent instructions) bytes of target with a jump to
// hook; *original gets a trampoline that runs them and continues at target+len.
bool InstallJmpHook(uint8_t* target, const uint8_t* prologue, size_t len, void* hook, void* volatile* original,
                    const char* what, std::string& err);

// ---- UI resource interception (resources.cpp) ----
bool FindResourceSlot(const Image& img, uint32_t& slotRva, std::string& err);
bool InstallResourceHook(const Image& img, std::string& err);
bool BuildInjectedScript(const std::vector<uint8_t>& original, std::string& out);
std::string BuildConfigJs();

// ---- fast travel and mission hooks (travel.cpp) ----
struct TravelTargets {
    uint32_t menuEvents;    // heron::ui_system_menu::process_events (closes the menus like Resume)
    uint32_t travelEvents;  // heron::ui_fast_travel::process_events (opens fast travel like a door)
    uint32_t request;       // heron::fast_travel::handle_request (mission state, Abandon)
    uint32_t abandon;       // the pause menu's Abandon: loads the mission's "return-" save
};
bool FindTravelTargets(const Image& img, TravelTargets& out, std::string& err);
bool InstallTravelHooks(const Image& img, std::string& err);
// The same with explicit addresses and their original bytes (used by the offline tests with stand-in functions).
bool InstallTravelHooksAt(uint8_t* menu, const uint8_t* menuBytes, uint8_t* travel, const uint8_t* travelBytes,
                          uint8_t* request, const uint8_t* requestBytes, void* abandon, std::string& err);
bool TravelHooksInstalled();
std::string TravelAction(const std::string& action, const char* query = nullptr);  // the __fasttravelplus__.json endpoint

}  // namespace ftp
