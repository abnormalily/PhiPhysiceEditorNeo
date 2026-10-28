#include "app/SingleInstance.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <string>

namespace ppn {
namespace {
#ifdef _WIN32
HANDLE g_instanceMutex = nullptr;

std::wstring instanceMutexName() {
    wchar_t path[32768] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, 32768);
    uint64_t hash = 1469598103934665603ull;
    for (DWORD i = 0; i < length; ++i) {
        hash ^= (uint64_t)(uint16_t)path[i];
        hash *= 1099511628211ull;
    }
    wchar_t name[96] = {};
    std::swprintf(name, 96, L"Local\\PhiPhysiceEditorNeo.SingleInstance.%016llx",
                  (unsigned long long)hash);
    return name;
}
#endif
} // namespace

bool ensureSingleInstance() {
#ifdef _WIN32
    const std::wstring name = instanceMutexName();
    g_instanceMutex = CreateMutexW(nullptr, TRUE, name.c_str());
    if (g_instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        // A second copy of the same executable should reveal the existing
        // window instead of silently doing nothing.
        if (HWND existing = FindWindowW(nullptr, L"PhiPhysiceEditorNeo")) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        } else if (GetEnvironmentVariableW(L"PHIPHYSICE_SINGLE_INSTANCE_SILENT",
                                           nullptr, 0) == 0 &&
                   GetEnvironmentVariableW(L"POLYNOTE_SINGLE_INSTANCE_SILENT",
                                           nullptr, 0) == 0) {
            MessageBoxW(nullptr,
                        L"PhiPhysiceEditorNeo 已在运行。\n请先关闭已有窗口，再重新启动。",
                        L"PhiPhysiceEditorNeo", MB_OK | MB_ICONINFORMATION);
        }
        CloseHandle(g_instanceMutex);
        g_instanceMutex = nullptr;
        return false;
    }
#endif
    return true;
}

} // namespace ppn
