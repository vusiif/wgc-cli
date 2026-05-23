#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <optional>

struct WindowInfo {
    HWND hwnd = nullptr;
    DWORD pid = 0;
    std::wstring title;
    std::wstring className;
    RECT rect{};
    bool visible = false;
    bool minimized = false;
};

std::vector<WindowInfo> enumerate_windows(bool include_minimized);
WindowInfo find_window_by_hwnd(uint64_t hwnd_value);
std::optional<WindowInfo> find_best_match(const std::vector<WindowInfo>& windows,
                                           const std::wstring& query, bool exact);
void print_window_list(const std::vector<WindowInfo>& windows);
