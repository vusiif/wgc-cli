#include "output.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <vector>

static std::wstring hwnd_to_hex(uint64_t hwnd) {
    std::wostringstream oss;
    oss << L"0x" << std::uppercase << std::setfill(L'0') << std::setw(16) << std::hex << hwnd;
    return oss.str();
}

static std::wstring escape_json_string(const std::wstring& s) {
    std::wostringstream oss;
    for (wchar_t c : s) {
        switch (c) {
            case L'"':  oss << L"\\\""; break;
            case L'\\': oss << L"\\\\"; break;
            case L'\n': oss << L"\\n";  break;
            case L'\r': oss << L"\\r";  break;
            case L'\t': oss << L"\\t";  break;
            default:    oss << c;       break;
        }
    }
    return oss.str();
}

static std::wstring state_str(bool minimized) {
    return minimized ? L"minimized" : L"normal";
}

void output_success(const Options& opts, const MatchedWindow& win,
                    const std::wstring& screenshot_path,
                    const std::vector<MatchedWindow>& candidates) {
    if (opts.json) {
        std::wostringstream oss;
        oss << L"{\n"
            << L"  \"ok\": true,\n"
            << L"  \"matchedWindow\": {\n"
            << L"    \"title\": \"" << escape_json_string(win.title) << L"\",\n"
            << L"    \"hwnd\": \"" << hwnd_to_hex(win.hwnd) << L"\",\n"
            << L"    \"pid\": " << win.pid << L",\n"
            << L"    \"width\": " << win.width << L",\n"
            << L"    \"height\": " << win.height << L",\n"
            << L"    \"className\": \"" << escape_json_string(win.className) << L"\",\n"
            << L"    \"state\": \"" << state_str(win.minimized) << L"\"\n"
            << L"  },\n";

        if (candidates.size() > 1) {
            oss << L"  \"candidates\": [\n";
            for (size_t i = 0; i < candidates.size(); ++i) {
                const auto& c = candidates[i];
                oss << L"    {\"hwnd\":\"" << hwnd_to_hex(c.hwnd)
                    << L"\",\"pid\":" << c.pid
                    << L",\"title\":\"" << escape_json_string(c.title)
                    << L"\",\"className\":\"" << escape_json_string(c.className)
                    << L"\",\"width\":" << c.width
                    << L",\"height\":" << c.height
                    << L",\"state\":\"" << state_str(c.minimized) << L"\"}";
                if (i + 1 < candidates.size()) oss << L",";
                oss << L"\n";
            }
            oss << L"  ],\n";
        }

        oss << L"  \"screenshotPath\": \"" << escape_json_string(screenshot_path) << L"\"\n"
            << L"}\n";
        std::wcout << oss.str();
    } else {
        std::wcout << L"Matched window: " << win.title << L"\n";
        std::wcout << L"Handle: " << hwnd_to_hex(win.hwnd) << L"\n";
        std::wcout << L"Window size: " << win.width << L"x" << win.height << L"\n";
        std::wcout << L"RESULT_SCREENSHOT_PATH=" << screenshot_path << L"\n";
    }
}

void output_error(const Options& opts, ExitCode code, const std::wstring& error_code, const std::wstring& message) {
    output_error_ex(opts, code, error_code, message);
}

void output_error_ex(const Options& opts, ExitCode code, const std::wstring& error_code,
                     const std::wstring& message, const std::wstring& stage,
                     const std::wstring& hresult, const std::wstring& suggestion) {
    if (opts.json) {
        std::wostringstream oss;
        oss << L"{\n"
            << L"  \"ok\": false,\n"
            << L"  \"errorCode\": \"" << escape_json_string(error_code) << L"\",\n"
            << L"  \"exitCode\": " << static_cast<int>(code) << L",\n";
        if (!stage.empty())
            oss << L"  \"stage\": \"" << escape_json_string(stage) << L"\",\n";
        if (!hresult.empty())
            oss << L"  \"hresult\": \"" << escape_json_string(hresult) << L"\",\n";
        oss << L"  \"message\": \"" << escape_json_string(message) << L"\"";
        if (!suggestion.empty())
            oss << L",\n  \"suggestion\": \"" << escape_json_string(suggestion) << L"\"";
        oss << L"\n}\n";
        std::wcout << oss.str();
    } else {
        std::wcerr << L"ERROR_CODE=" << error_code << L"\n";
        std::wcerr << L"ERROR_MESSAGE=" << message << L"\n";
        if (!hresult.empty()) std::wcerr << L"HRESULT=" << hresult << L"\n";
        if (!stage.empty()) std::wcerr << L"STAGE=" << stage << L"\n";
        if (!suggestion.empty()) std::wcerr << L"SUGGESTION=" << suggestion << L"\n";
    }
}
