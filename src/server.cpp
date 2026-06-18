#include "server.h"
#include "window_enum.h"
#include "capture_wgc.h"
#include "d3d_helpers.h"
#include "image_wic.h"

#include <windows.h>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <iomanip>

static std::wstring escape_json(const std::wstring& s) {
    std::wostringstream oss;
    for (wchar_t c : s) {
        switch (c) {
            case L'"':  oss << L"\\\""; break;
            case L'\\': oss << L"\\\\"; break;
            case L'\n': oss << L"\\n";  break;
            case L'\r': oss << L"\\r";  break;
            case L'\t': oss << L"\\t";  break;
            case L'\0': oss << L"\\u0000"; break;
            default:
                if (c < 0x20) {
                    // Escape all control characters
                    std::wostringstream hex;
                    hex << L"\\u" << std::setfill(L'0') << std::setw(4)
                        << std::hex << static_cast<int>(c);
                    oss << hex.str();
                } else {
                    oss << c;
                }
                break;
        }
    }
    return oss.str();
}

static std::wstring hwnd_hex(HWND hwnd) {
    std::wostringstream oss;
    oss << L"0x" << std::uppercase << std::setfill(L'0')
        << std::setw(16) << std::hex << reinterpret_cast<uintptr_t>(hwnd);
    return oss.str();
}

static std::wstring handle_list() {
    auto windows = enumerate_windows(true);
    std::wostringstream oss;
    oss << L"{\"ok\":true,\"windows\":[";
    bool first = true;
    for (const auto& w : windows) {
        if (!w.visible) continue;
        int ww = w.rect.right - w.rect.left;
        int wh = w.rect.bottom - w.rect.top;
        if (ww <= 1 && wh <= 1 && !w.minimized) continue;
        if (!first) oss << L",";
        first = false;
        oss << L"{\"hwnd\":\"" << hwnd_hex(w.hwnd)
            << L"\",\"pid\":" << w.pid
            << L",\"title\":\"" << escape_json(w.title)
            << L"\",\"className\":\"" << escape_json(w.className)
            << L"\",\"width\":" << ww
            << L",\"height\":" << wh
            << L",\"minimized\":" << (w.minimized ? L"true" : L"false")
            << L"}";
    }
    oss << L"]}";
    return oss.str();
}

static std::wstring handle_capture(const std::wstring& title, uint64_t hwnd_val,
                                    const std::wstring& out, bool exact,
                                    uint32_t pid, const std::wstring& process,
                                    const std::wstring& className) {
    WindowInfo target{};
    bool found = false;

    if (hwnd_val != 0) {
        target = find_window_by_hwnd(hwnd_val);
        found = target.hwnd != nullptr;
    } else {
        auto windows = enumerate_windows(true);
        auto filtered = filter_windows(windows, pid, pid != 0,
                                       process, !process.empty(),
                                       className, !className.empty());
        if (!title.empty()) {
            auto match = find_best_match(filtered, title, exact);
            if (match) { target = *match; found = true; }
        } else if (!filtered.empty()) {
            target = filtered[0];
            found = true;
        }
    }

    if (!found) {
        return L"{\"ok\":false,\"errorCode\":\"WINDOW_NOT_FOUND\",\"message\":\"No window matched\"}";
    }

    // Restore if minimized
    if (target.minimized) {
        ShowWindow(target.hwnd, SW_RESTORE);
        Sleep(500);
        for (int i = 0; i < 10 && IsIconic(target.hwnd); ++i) Sleep(100);
    }

    auto d3d = create_d3d_device();
    if (!d3d.device) {
        return L"{\"ok\":false,\"errorCode\":\"D3D11_INIT_FAILED\",\"message\":\"D3D11 device creation failed\"}";
    }

    auto capture_result = capture_window(d3d.device.Get(), target.hwnd, 3000);
    if (!capture_result.ok) {
        std::wostringstream oss;
        oss << L"{\"ok\":false,\"errorCode\":\"" << escape_json(capture_result.error_code)
            << L"\",\"message\":\"" << escape_json(capture_result.message) << L"\"";
        if (!capture_result.stage.empty())
            oss << L",\"stage\":\"" << escape_json(capture_result.stage) << L"\"";
        if (!capture_result.hresult.empty())
            oss << L",\"hresult\":\"" << escape_json(capture_result.hresult) << L"\"";
        if (!capture_result.suggestion.empty())
            oss << L",\"suggestion\":\"" << escape_json(capture_result.suggestion) << L"\"";
        oss << L"}";
        return oss.str();
    }

    // Generate output path
    std::wstring out_path;
    if (out.size() >= 4 && (out.substr(out.size() - 4) == L".png" || out.substr(out.size() - 4) == L".PNG"
                          || out.substr(out.size() - 4) == L".jpg" || out.substr(out.size() - 4) == L".JPG"
                          || out.substr(out.size() - 4) == L".bmp" || out.substr(out.size() - 4) == L".BMP")) {
        out_path = out;
        auto parent = std::filesystem::path(out_path).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent);
    } else {
        std::filesystem::create_directories(out);
        out_path = generate_output_path(out, target.title, L"png");
    }

    if (!save_image(*capture_result.image, out_path, L"png")) {
        return L"{\"ok\":false,\"errorCode\":\"SAVE_FAILED\",\"message\":\"PNG save failed\",\"suggestion\":\"Check output directory exists and is writable.\"}";
    }

    int ww = target.rect.right - target.rect.left;
    int wh = target.rect.bottom - target.rect.top;

    std::wostringstream oss;
    oss << L"{\"ok\":true,\"matchedWindow\":{\"title\":\"" << escape_json(target.title)
        << L"\",\"hwnd\":\"" << hwnd_hex(target.hwnd)
        << L"\",\"pid\":" << target.pid
        << L",\"width\":" << ww << L",\"height\":" << wh
        << L",\"minimized\":" << (target.minimized ? L"true" : L"false")
        << L"},\"screenshotPath\":\"" << escape_json(out_path) << L"\"}";
    return oss.str();
}

// Minimal JSON value extraction (no external dependency)
static std::wstring json_get_string(const std::wstring& json, const std::wstring& key) {
    std::wstring needle = L"\"" + key + L"\":";
    auto pos = json.find(needle);
    if (pos == std::wstring::npos) return L"";
    pos += needle.size();
    while (pos < json.size() && json[pos] == L' ') pos++;
    if (pos >= json.size() || json[pos] != L'"') return L"";
    pos++; // skip opening quote
    auto end = json.find(L'"', pos);
    if (end == std::wstring::npos) return L"";
    return json.substr(pos, end - pos);
}

static uint64_t json_get_uint64(const std::wstring& json, const std::wstring& key) {
    std::wstring needle = L"\"" + key + L"\":";
    auto pos = json.find(needle);
    if (pos == std::wstring::npos) return 0;
    pos += needle.size();
    while (pos < json.size() && json[pos] == L' ') pos++;
    return std::wcstoull(json.c_str() + pos, nullptr, 10);
}

static bool json_get_bool(const std::wstring& json, const std::wstring& key) {
    std::wstring needle = L"\"" + key + L"\":";
    auto pos = json.find(needle);
    if (pos == std::wstring::npos) return false;
    pos += needle.size();
    while (pos < json.size() && json[pos] == L' ') pos++;
    return json.substr(pos, 4) == L"true";
}

static std::wstring process_request(const std::wstring& request) {
    auto action = json_get_string(request, L"action");
    if (action == L"list") {
        return handle_list();
    } else if (action == L"capture") {
        auto title = json_get_string(request, L"title");
        auto out = json_get_string(request, L"out");
        auto hwnd_val = json_get_uint64(request, L"hwnd");
        bool exact = json_get_bool(request, L"exact");
        uint32_t pid = static_cast<uint32_t>(json_get_uint64(request, L"pid"));
        auto process = json_get_string(request, L"process");
        auto className = json_get_string(request, L"className");
        if (out.empty()) out = L".";
        return handle_capture(title, hwnd_val, out, exact, pid, process, className);
    }
    return L"{\"ok\":false,\"errorCode\":\"BAD_ACTION\",\"message\":\"Unknown action\"}";
}

int run_server(const std::wstring& pipe_name) {
    std::wstring full_pipe = L"\\\\.\\pipe\\" + pipe_name;
    std::wcerr << L"[server] Listening on " << full_pipe << L"\n";
    std::wcerr << L"[server] Agent connects via: wgccli.exe --client <json>\n";
    std::wcerr << L"[server] Press Ctrl+C to stop.\n";

    while (true) {
        HANDLE hPipe = CreateNamedPipeW(
            full_pipe.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            PIPE_UNLIMITED_INSTANCES,
            65536, 65536, 0, nullptr);

        if (hPipe == INVALID_HANDLE_VALUE) {
            std::wcerr << L"[server] CreateNamedPipe failed: " << GetLastError() << L"\n";
            return 1;
        }

        if (!ConnectNamedPipe(hPipe, nullptr)) {
            if (GetLastError() != ERROR_PIPE_CONNECTED) {
                CloseHandle(hPipe);
                continue;
            }
        }

        // Read request
        wchar_t buf[4096]{};
        DWORD bytes_read = 0;
        if (!ReadFile(hPipe, buf, sizeof(buf) - sizeof(wchar_t), &bytes_read, nullptr) || bytes_read == 0) {
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
            continue;
        }

        std::wstring request(buf, bytes_read / sizeof(wchar_t));
        std::wstring response = process_request(request);

        // Write response
        DWORD bytes_written = 0;
        WriteFile(hPipe, response.c_str(),
                  static_cast<DWORD>(response.size() * sizeof(wchar_t)),
                  &bytes_written, nullptr);

        FlushFileBuffers(hPipe);
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }

    return 0;
}
