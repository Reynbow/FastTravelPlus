// The keyboard hotkey: the key code of the Mod Settings Menu key option (0 = off), every key's name on the player's
// keyboard for the script's prompts, and a thread that counts presses of the key. The script reads the count and
// decides what a press does, because it knows which screen is up.
#include "common.h"
#include <atomic>

namespace ftp {

static std::atomic<int> g_hotkeyVk{kDefaultHotkeyVk};
static std::atomic<uint32_t> g_presses{0};
static std::atomic<ULONGLONG> g_downSince{0};  // when the current press started, 0 while the key is up
static std::vector<std::string> g_names;        // by key code, from BuildKeyNames

// The characters a key types on the current layout, unshifted and shifted: "` ~" on a US keyboard.
static std::wstring LayoutName(int vk) {
    HKL layout = GetKeyboardLayout(0);
    UINT sc = MapVirtualKeyExW((UINT)vk, MAPVK_VK_TO_VSC, layout);
    BYTE state[256] = {};
    wchar_t plain[8] = {}, shifted[8] = {};
    // Flag 4: leave the keyboard's dead-key state alone. A dead key returns -1 with its spacing character.
    int n1 = ToUnicodeEx((UINT)vk, sc, state, plain, 8, 4, layout);
    state[VK_SHIFT] = 0x80;
    int n2 = ToUnicodeEx((UINT)vk, sc, state, shifted, 8, 4, layout);
    auto usable = [](int n, const wchar_t* s) { return (n == 1 || n == -1) && s[0] > L' ' && s[0] != 0x7f; };
    std::wstring name;
    if (usable(n1, plain)) name = std::wstring(1, plain[0]);
    if (usable(n2, shifted) && shifted[0] != plain[0]) name += (name.empty() ? L"" : L" ") + std::wstring(1, shifted[0]);
    if (!name.empty()) return name;
    wchar_t text[64] = {};
    if (sc && GetKeyNameTextW((LONG)(sc << 16), text, 64) > 0) return text;
    wchar_t hex[16];
    swprintf_s(hex, L"Key 0x%02X", vk);
    return hex;
}

static std::string NameNow(int vk) {
    if (vk <= 0 || vk > 255) return "";
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) return std::string(1, (char)vk);
    if (vk >= VK_F1 && vk <= VK_F24) return "F" + std::to_string(vk - VK_F1 + 1);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return "Numpad " + std::to_string(vk - VK_NUMPAD0);
    switch (vk) {
        case VK_CANCEL: return "Break";
        case VK_MBUTTON: return "Middle mouse";
        case VK_XBUTTON1: return "Mouse 4";
        case VK_XBUTTON2: return "Mouse 5";
        case VK_BACK: return "Backspace";
        case VK_TAB: return "Tab";
        case VK_CLEAR: return "Clear";
        case VK_RETURN: return "Enter";
        case VK_SHIFT: return "Shift";
        case VK_CONTROL: return "Ctrl";
        case VK_MENU: return "Alt";
        case VK_PAUSE: return "Pause";
        case VK_CAPITAL: return "Caps Lock";
        case VK_ESCAPE: return "Esc";
        case VK_SPACE: return "Space";
        case VK_PRIOR: return "Page Up";
        case VK_NEXT: return "Page Down";
        case VK_END: return "End";
        case VK_HOME: return "Home";
        case VK_LEFT: return "Left";
        case VK_UP: return "Up";
        case VK_RIGHT: return "Right";
        case VK_DOWN: return "Down";
        case VK_SNAPSHOT: return "Print Screen";
        case VK_INSERT: return "Insert";
        case VK_DELETE: return "Delete";
        case VK_LWIN: return "Left Windows";
        case VK_RWIN: return "Right Windows";
        case VK_APPS: return "Menu key";
        case VK_MULTIPLY: return "Numpad *";
        case VK_ADD: return "Numpad +";
        case VK_SUBTRACT: return "Numpad -";
        case VK_DECIMAL: return "Numpad .";
        case VK_DIVIDE: return "Numpad /";
        case VK_NUMLOCK: return "Num Lock";
        case VK_SCROLL: return "Scroll Lock";
        case VK_LSHIFT: return "Left Shift";
        case VK_RSHIFT: return "Right Shift";
        case VK_LCONTROL: return "Left Ctrl";
        case VK_RCONTROL: return "Right Ctrl";
        case VK_LMENU: return "Left Alt";
        case VK_RMENU: return "Right Alt";
    }
    return Utf8(LayoutName(vk));  // symbol keys: what they type on this layout; others: Windows' name for the key
}

void BuildKeyNames() {
    std::vector<std::string> names(256);
    for (int vk = 1; vk < 256; ++vk) names[vk] = NameNow(vk);
    g_names.swap(names);
}

std::string KeyName(int vk) { return vk > 0 && vk < (int)g_names.size() ? g_names[vk] : NameNow(vk); }

// 1.2.x's hotkey slider saved a position in this list (it only ever grew at the end). CarryOverSliders turns a saved
// position into the key option's code once.
int HotkeyFromSliderPosition(int position) {
    static const std::vector<int> keys = [] {
        std::vector<int> k = {0, VK_OEM_3};
        for (int i = 1; i <= 12; ++i) k.push_back(VK_F1 + i - 1);
        for (char c : std::string("1234567890")) k.push_back(c);
        for (char c = 'A'; c <= 'Z'; ++c) k.push_back(c);
        for (int vk : {VK_OEM_MINUS, VK_OEM_PLUS, VK_OEM_4, VK_OEM_6, VK_OEM_5, VK_OEM_1, VK_OEM_7, VK_OEM_COMMA,
                       VK_OEM_PERIOD, VK_OEM_2, VK_OEM_102})
            k.push_back(vk);
        for (int vk : {VK_TAB, VK_CAPITAL, VK_BACK, VK_INSERT, VK_DELETE, VK_HOME, VK_END, VK_PRIOR, VK_NEXT, VK_UP,
                       VK_DOWN, VK_LEFT, VK_RIGHT})
            k.push_back(vk);
        for (int i = 0; i <= 9; ++i) k.push_back(VK_NUMPAD0 + i);
        for (int vk : {VK_DIVIDE, VK_MULTIPLY, VK_SUBTRACT, VK_ADD, VK_DECIMAL, VK_SCROLL, VK_PAUSE, VK_MBUTTON,
                       VK_XBUTTON1, VK_XBUTTON2})
            k.push_back(vk);
        for (int i = 13; i <= 24; ++i) k.push_back(VK_F1 + i - 1);
        return k;
    }();
    return position >= 0 && position < (int)keys.size() ? keys[position] : -1;
}

int HotkeyVk() { return g_hotkeyVk.load(); }

bool SetHotkeyVk(int code) {
    if (code != 0 && (code < 3 || code > 254)) return false;
    if (g_hotkeyVk.exchange(code) != code) Log("Hotkey: %s", code ? KeyName(code).c_str() : "off");
    return true;
}

uint32_t HotkeyPresses() { return g_presses.load(); }

uint32_t HotkeyHeldMs() {
    const ULONGLONG since = g_downSince.load();
    return since ? (uint32_t)std::min<ULONGLONG>(GetTickCount64() - since, 600000) : 0;
}

static bool GameHasFocus() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

static bool Down(int vk) { return vk && (GetAsyncKeyState(vk) & 0x8000) != 0; }
static bool IsCtrl(int vk) { return vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL; }
static bool IsAlt(int vk) { return vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU; }
static bool IsWin(int vk) { return vk == VK_LWIN || vk == VK_RWIN; }

static DWORD WINAPI HotkeyThread(void*) {
    bool was = false;
    for (;;) {
        Sleep(15);
        const int vk = g_hotkeyVk.load();
        // Ctrl/Alt/Windows combinations belong to the game or Windows, not to us, unless the hotkey is that key.
        const bool chord = (!IsCtrl(vk) && Down(VK_CONTROL)) || (!IsAlt(vk) && Down(VK_MENU)) ||
                           (!IsWin(vk) && (Down(VK_LWIN) || Down(VK_RWIN)));
        const bool down = vk && GameHasFocus() && !chord && Down(vk);
        if (down && !was) {
            g_downSince = GetTickCount64();
            ++g_presses;
        } else if (!down) {
            g_downSince = 0;
        }
        was = down;
    }
}

void StartHotkey() {
    HANDLE h = CreateThread(nullptr, 0, HotkeyThread, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
    const int vk = g_hotkeyVk.load();
    Log("Hotkey: %s (key code %d)", vk ? KeyName(vk).c_str() : "off", vk);
}

}  // namespace ftp
