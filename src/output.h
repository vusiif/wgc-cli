#pragma once

#include "cli_args.h"
#include <string>

struct MatchedWindow {
    std::wstring title;
    std::wstring className;
    uint64_t hwnd = 0;
    uint32_t pid = 0;
    int width = 0;
    int height = 0;
    bool minimized = false;
};

void output_success(const Options& opts, const MatchedWindow& win, const std::wstring& screenshot_path);
void output_error(const Options& opts, ExitCode code, const std::wstring& error_code, const std::wstring& message);
