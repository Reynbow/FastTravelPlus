// Inline hooks. The first len bytes of a hooked function (whole, position-independent instructions) are
// replaced by
//     jmp [rip+0] ; dq hook ; nop padding
// and a trampoline runs the original bytes, then jumps back to target+len.
#include "common.h"
#include <tlhelp32.h>

namespace ftp {

// Suspends every other thread while the bytes are rewritten, retrying if a thread happens to be
// executing inside them.
bool PatchWithThreadsSuspended(uint8_t* target, const uint8_t* patch, size_t n) {
    std::vector<DWORD> ids;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    THREADENTRY32 te = {sizeof(te)};
    const DWORD pid = GetCurrentProcessId(), self = GetCurrentThreadId();
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID == pid && te.th32ThreadID != self) ids.push_back(te.th32ThreadID);
            te.dwSize = sizeof(te);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    std::vector<HANDLE> handles;
    handles.reserve(ids.size());
    for (DWORD id : ids) {
        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, id);
        if (h) handles.push_back(h);
    }
    DWORD old = 0;
    if (!VirtualProtect(target, n, PAGE_EXECUTE_READWRITE, &old)) {
        for (HANDLE h : handles) CloseHandle(h);
        return false;
    }
    bool done = false;
    for (int attempt = 0; attempt < 20 && !done; ++attempt) {
        std::vector<HANDLE> suspended;
        suspended.reserve(handles.size());
        bool busy = false;
        for (HANDLE h : handles) {
            if (SuspendThread(h) == (DWORD)-1) continue;
            suspended.push_back(h);
            CONTEXT ctx = {};
            ctx.ContextFlags = CONTEXT_CONTROL;
            if (GetThreadContext(h, &ctx) && ctx.Rip >= (DWORD64)target && ctx.Rip < (DWORD64)(target + n)) busy = true;
        }
        if (!busy) {
            memcpy(target, patch, n);
            FlushInstructionCache(GetCurrentProcess(), target, n);
            done = true;
        }
        for (HANDLE h : suspended) ResumeThread(h);
        if (!done) Sleep(5);
    }
    DWORD tmp = 0;
    VirtualProtect(target, n, old, &tmp);
    for (HANDLE h : handles) CloseHandle(h);
    return done;
}

// *original is set before the jump is written: the hook can run the moment the patch lands.
bool InstallJmpHook(uint8_t* target, const uint8_t* prologue, size_t len, void* hook, void* volatile* original,
                    const char* what, std::string& err) {
    if (len < 14 || len > 32) {
        err = std::string(what) + ": bad prologue length";
        return false;
    }
    if (memcmp(target, prologue, len) != 0) {
        err = std::string(what) + " is already patched in memory (another mod hooks it)";
        return false;
    }
    uint8_t* tramp = (uint8_t*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!tramp) {
        err = "trampoline allocation failed";
        return false;
    }
    // Trampoline: the original bytes, then jmp [rip] -> target+len.
    const uint8_t jmp[6] = {0xFF, 0x25, 0, 0, 0, 0};
    memcpy(tramp, prologue, len);
    memcpy(tramp + len, jmp, 6);
    uint64_t back = (uint64_t)(target + len);
    memcpy(tramp + len + 6, &back, 8);
    DWORD old = 0;
    VirtualProtect(tramp, 64, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), tramp, 64);
    *original = tramp;
    MemoryBarrier();

    uint8_t patch[32];
    memset(patch, 0x90, sizeof(patch));
    memcpy(patch, jmp, 6);
    uint64_t dest = (uint64_t)hook;
    memcpy(patch + 6, &dest, 8);
    if (!PatchWithThreadsSuspended(target, patch, len)) {
        err = std::string("could not patch ") + what;
        return false;  // the hook never ran; the trampoline is simply left unused
    }
    return true;
}

}  // namespace ftp
