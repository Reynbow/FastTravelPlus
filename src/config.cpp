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

// A slider position as Mod Settings Menu saved it in ModMenuConfig\<descriptor id>.ini, -1 if not saved.
static int ReadMenuValue(const wchar_t* file, const wchar_t* key) {
    wchar_t buf[64];
    GetPrivateProfileStringW(L"Settings", key, L"", buf, 64, (g_modDir + L"ModMenuConfig\\" + file).c_str());
    wchar_t* end = nullptr;
    double v = wcstod(buf, &end);
    if (end == buf || v != v || v < 0 || v > 10000) return -1;
    return (int)(v + 0.5);
}

void LoadConfig() {
    Config c;
    c.enabled = ReadBool(L"Enabled", true);
    c.diagnostics = ReadBool(L"Diagnostics", false);
    c.hotkey = ReadMenuValue(L"fasttravelplus.ini", L"hotkey");
    c.padHold = ReadMenuValue(L"fasttravelplus_controller.ini", L"pad_hold");
    c.padPress = ReadMenuValue(L"fasttravelplus_controller.ini", L"pad_press");
    g_cfg = c;
}

}  // namespace ftp
