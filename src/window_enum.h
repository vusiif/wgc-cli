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
    std::wstring exeName;
    RECT rect{};
    bool visible = false;
    bool minimized = false;
};

std::vector<WindowInfo> enumerate_windows(bool include_minimized);
WindowInfo find_window_by_hwnd(uint64_t hwnd_value);
std::optional<WindowInfo> find_best_match(const std::vector<WindowInfo>& windows,
                                           const std::wstring& query, bool exact,
                                           std::vector<WindowInfo>* out_candidates = nullptr);
std::vector<WindowInfo> find_by_pid(const std::vector<WindowInfo>& windows, uint32_t pid);
std::vector<WindowInfo> filter_windows(const std::vector<WindowInfo>& windows,
                                       uint32_t pid, bool has_pid,
                                       const std::wstring& process, bool has_process,
                                       const std::wstring& className, bool has_className);
void print_window_list(const std::vector<WindowInfo>& windows);
void output_window_list_json(const std::vector<WindowInfo>& windows);
