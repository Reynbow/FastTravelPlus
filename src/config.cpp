#include "common.h"
#include <wchar.h>

namespace ftp {

Config g_cfg;

static std::wstring IniPath() { return g_modDir + L"FastTravelPlus.ini"; }

static bool ReadBool(const wchar_t* key, bool def) {
    wchar_t buf[64];
    GetPrivateProfileStringW(L"FastTravelPlus", key, def ? L"1" : L"0", buf, 64, IniPath().c_str());
    std::wstring s(buf);
    size_t semi = s.find(L';');
    if (semi != std::wstring::npos) s.resize(semi);
    while (!s.empty() && iswspace(s.back())) s.pop_back();
    size_t start = 0;
    while (start < s.size() && iswspace(s[start])) ++start;
    s = s.substr(start);
    if (s.empty()) return def;
    if (_wcsicmp(s.c_str(), L"true") == 0 || _wcsicmp(s.c_str(), L"on") == 0 || _wcsicmp(s.c_str(), L"yes") == 0)
        return true;
    if (_wcsicmp(s.c_str(), L"false") == 0 || _wcsicmp(s.c_str(), L"off") == 0 || _wcsicmp(s.c_str(), L"no") == 0)
        return false;
    return _wtoi(s.c_str()) != 0;
}

static std::wstring MenuFile(const wchar_t* file) { return g_modDir + L"ModMenuConfig\\" + file; }

// A value as Mod Settings Menu saved it in ModMenuConfig\<descriptor id>.ini, -1 if not saved.
static int ReadMenuValue(const wchar_t* file, const wchar_t* key) {
    wchar_t buf[64];
    GetPrivateProfileStringW(L"Settings", key, L"", buf, 64, MenuFile(file).c_str());
    wchar_t* end = nullptr;
    double v = wcstod(buf, &end);
    if (end == buf || v != v || v < 0 || v > 10000) return -1;
    return (int)(v + 0.5);
}

void LoadConfig() {
    Config c;
    c.enabled = ReadBool(L"Enabled", true);
    c.diagnostics = ReadBool(L"Diagnostics", false);
    c.hotkey = ReadMenuValue(L"fasttravelplus.ini", L"travel_key");
    c.padHold = ReadMenuValue(L"fasttravelplus_controller.ini", L"hold_button");
    c.padPress = ReadMenuValue(L"fasttravelplus_controller.ini", L"press_button");
    g_cfg = c;
}

// 1.2.x's sliders saved a position in a list; 1.3.0's key options save a key code, so they have new ids (an old
// position read as a key code would be the wrong key: the backtick's position 1 is the left mouse button). A saved
// position is written as its key option's code, unless that's already saved, and then removed, so a later reset of
// the key option to its default isn't undone by it.
static bool CarryOver(const wchar_t* file, const wchar_t* oldKey, const wchar_t* newKey, int (*code)(int)) {
    const int position = ReadMenuValue(file, oldKey);
    wchar_t raw[8];
    if (position < 0 && !GetPrivateProfileStringW(L"Settings", oldKey, L"", raw, 8, MenuFile(file).c_str())) return false;
    const int c = position >= 0 ? code(position) : -1;
    if (ReadMenuValue(file, newKey) >= 0) {
        Log("%s: %ls is already set; dropped the old slider's %ls=%d", Utf8(file).c_str(), newKey, oldKey, position);
    } else if (c >= 0) {
        WritePrivateProfileStringW(L"Settings", newKey, std::to_wstring(c).c_str(), MenuFile(file).c_str());
        Log("%s: carried the old slider's %ls=%d over as %ls=%d", Utf8(file).c_str(), oldKey, position, newKey, c);
    } else {
        Log("%s: dropped the old slider's %ls=%d (not a position)", Utf8(file).c_str(), oldKey, position);
    }
    WritePrivateProfileStringW(L"Settings", oldKey, nullptr, MenuFile(file).c_str());  // removes it
    return true;
}

bool CarryOverSliders() {
    bool moved = CarryOver(L"fasttravelplus.ini", L"hotkey", L"travel_key", HotkeyFromSliderPosition);
    moved = CarryOver(L"fasttravelplus_controller.ini", L"pad_hold", L"hold_button", PadCodeFromSliderPosition) || moved;
    moved = CarryOver(L"fasttravelplus_controller.ini", L"pad_press", L"press_button", PadCodeFromSliderPosition) || moved;
    return moved;
}

}  // namespace ftp
