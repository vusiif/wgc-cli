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
};

// Returns false if parsing fails (caller should exit with BadArgs).
bool parse_args(int argc, wchar_t* argv[], Options& opts);

void print_help();
void print_version();
