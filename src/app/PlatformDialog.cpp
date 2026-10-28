#include "app/PlatformDialog.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>

#include <vector>
#endif

namespace ppn {

#ifdef _WIN32
namespace {

std::string wideToUtf8(const wchar_t* text) {
    if (!text) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr,
                                           nullptr);
    if (needed <= 1) return {};
    std::string result((size_t)needed - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), needed, nullptr, nullptr);
    return result;
}

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (needed <= 1) return {};
    std::wstring result((size_t)needed - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), needed);
    return result;
}

int CALLBACK browseCallback(HWND hwnd, UINT message, LPARAM, LPARAM data) {
    if (message == BFFM_INITIALIZED && data) {
        SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, data);
    }
    return 0;
}

} // namespace
#endif

bool browseFolderDialog(std::string& path) {
#ifdef _WIN32
    std::wstring initial = utf8ToWide(path);
    BROWSEINFOW info{};
    info.lpszTitle = L"选择预制事件存放目录";
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    info.lpfn = browseCallback;
    info.lParam = initial.empty() ? 0 : (LPARAM)initial.c_str();
    LPITEMIDLIST item = SHBrowseForFolderW(&info);
    if (!item) return false;
    wchar_t buffer[MAX_PATH * 4]{};
    const bool ok = SHGetPathFromIDListW(item, buffer) != FALSE;
    CoTaskMemFree(item);
    if (!ok) return false;
    path = wideToUtf8(buffer);
    return !path.empty();
#else
    (void)path;
    return false;
#endif
}

} // namespace ppn
