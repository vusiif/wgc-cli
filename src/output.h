#pragma once

#include "cli_args.h"
#include <string>
#include <vector>

// Write a wide string as UTF-8 to stdout via WriteFile, bypassing std::wcout.
// This avoids corruption from control characters in window titles.
void write_stdout_utf8(const std::wstring& s);

struct MatchedWindow {
    std::wstring title;
    std::wstring className;
    uint64_t hwnd = 0;
    uint32_t pid = 0;
    int width = 0;
    int height = 0;
    bool minimized = false;
};

void output_success(const Options& opts, const MatchedWindow& win,
                    const std::wstring& screenshot_path,
                    const std::vector<MatchedWindow>& candidates = {});
void output_error(const Options& opts, ExitCode code, const std::wstring& error_code, const std::wstring& message);
void output_error_ex(const Options& opts, ExitCode code, const std::wstring& error_code,
                     const std::wstring& message, const std::wstring& stage = L"",
                     const std::wstring& hresult = L"", const std::wstring& suggestion = L"");
