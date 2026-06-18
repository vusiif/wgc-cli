#pragma once

#include <string>
#include <cstdint>

enum class ExitCode : int {
    Ok = 0,
    GeneralError = 1,
    BadArgs = 2,
    WindowNotFound = 3,
    WgcUnavailable = 4,
    CaptureFailed = 5,
    SaveFailed = 6,
    Timeout = 7,
    PermissionDenied = 8,
};

struct Options {
    std::wstring title;
    bool exact = false;
    uint64_t hwnd = 0;
    bool has_hwnd = false;
    bool list = false;
    std::wstring out;
    bool json = false;
    uint32_t timeout_ms = 3000;
    bool include_minimized = true;
    bool restore = true;
    bool help = false;
    bool version = false;
    bool has_title = false;
    bool server = false;
    std::wstring client;   // JSON request to send to server
    std::wstring pipe = L"wgccli";
    bool require_unique = false;
    bool doctor = false;
    uint32_t pid = 0;
    bool has_pid = false;
    std::wstring process;
    bool has_process = false;
    std::wstring className;
    bool has_className = false;
    uint32_t delay_ms = 0;
    uint32_t max_width = 0;
    uint32_t resize_w = 0;
    uint32_t resize_h = 0;
    std::wstring format = L"png";
    uint32_t crop_x = 0, crop_y = 0, crop_w = 0, crop_h = 0;
    bool has_crop = false;
};

// Returns false if parsing fails (caller should exit with BadArgs).
bool parse_args(int argc, wchar_t* argv[], Options& opts);

void print_help();
void print_version();
