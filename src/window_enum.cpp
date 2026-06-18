#include "window_enum.h"
#include "output.h"
#include <windows.h>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstdint>

static std::wstring to_lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    return s;
}

static bool is_noise_window(const WindowInfo& w) {
    // Skip invisible windows
    if (!w.visible) return true;

    // Skip zero-size windows (hidden utility windows)
    int width = w.rect.right - w.rect.left;
    int height = w.rect.bottom - w.rect.top;
    if (width <= 1 && height <= 1 && !w.minimized) return true;

    // Skip known noise class names
    static const wchar_t* noise_classes[] = {
        L"IME", L"MSCTFIME UI", L"SoPY_Hint", L"Sogou_TSF_UI",
        L"CiceroUIWndFrame", L"TF_FloatingLangBar_WndTitle",
        L".NET-BroadcastEventWindow", L"GDI+ Hook Window Class",
        L"OleDdeWndClass", L"COMTASKSWINDOWCLASS",
        L"DDE Server Window", L"Default IME",
        L"Progman", L"Shell_TrayWnd",
        L"Windows.UI.Core.CoreWindow",
        L"FilterGraphWindow", L"ActiveMovie Window",
        L"MediaContextNotificationWindow",
        L"SystemResourceNotifyWindow",
        L"BroadcastListenerWindow",
    };
    for (const auto& nc : noise_classes) {
        if (w.className == nc) return true;
    }

    // Skip hidden OMEN overlay windows
    if (w.className.find(L"HwndWrapper[OMEN") != std::wstring::npos &&
        w.title.find(L"Hidden") != std::wstring::npos) {
        return true;
    }

    // Skip known noise titles
    static const wchar_t* noise_titles[] = {
        L"Microsoft Text Input Application",
        L"Program Manager",
        L"Default IME",
        L"Hidden Window",
    };
    for (const auto& nt : noise_titles) {
        if (w.title == nt) return true;
    }

    // Skip windows with WS_EX_TOOLWINDOW style (not main app windows)
    LONG ex_style = GetWindowLongW(w.hwnd, GWL_EXSTYLE);
    if (ex_style & WS_EX_TOOLWINDOW) return true;

    return false;
}

static std::wstring get_exe_name(DWORD pid) {
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return L"";
    wchar_t buf[MAX_PATH]{};
    DWORD size = MAX_PATH;
    BOOL ok = QueryFullProcessImageNameW(hProc, 0, buf, &size);
    CloseHandle(hProc);
    if (!ok) return L"";
    std::wstring path(buf, size);
    auto pos = path.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? path.substr(pos + 1) : path;
}

struct EnumCtx {
    std::vector<WindowInfo>* result;
    int total;
    int titled;
};

static BOOL CALLBACK enum_proc(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<EnumCtx*>(lParam);
    ctx->total++;

    int len = GetWindowTextLengthW(hwnd);
    if (len == 0) return TRUE;
    ctx->titled++;

    WindowInfo info{};
    info.hwnd = hwnd;
    info.visible = IsWindowVisible(hwnd) != 0;

    std::wstring title(len + 1, L'\0');
    GetWindowTextW(hwnd, title.data(), len + 1);
    title.resize(len);
    info.title = title;

    wchar_t cls[256]{};
    GetClassNameW(hwnd, cls, 256);
    info.className = cls;

    GetWindowThreadProcessId(hwnd, &info.pid);
    info.exeName = get_exe_name(info.pid);
    GetWindowRect(hwnd, &info.rect);
    info.minimized = IsIconic(hwnd) != 0;

    ctx->result->push_back(info);
    return TRUE;
}

std::vector<WindowInfo> enumerate_windows(bool include_minimized) {
    std::vector<WindowInfo> result;
    EnumCtx ctx{ &result, 0, 0 };

    EnumWindows(enum_proc, reinterpret_cast<LPARAM>(&ctx));

    // For title matching: keep all visible windows with titles
    // Noise filtering happens at display time, not here
    return result;
}

WindowInfo find_window_by_hwnd(uint64_t hwnd_value) {
    HWND hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(hwnd_value));

    if (!IsWindow(hwnd)) {
        return {};
    }

    WindowInfo info{};
    info.hwnd = hwnd;
    info.visible = IsWindowVisible(hwnd) != 0;
    info.minimized = IsIconic(hwnd) != 0;

    int len = GetWindowTextLengthW(hwnd);
    if (len > 0) {
        std::wstring title(len + 1, L'\0');
        GetWindowTextW(hwnd, title.data(), len + 1);
        title.resize(len);
        info.title = title;
    }

    wchar_t cls[256]{};
    GetClassNameW(hwnd, cls, 256);
    info.className = cls;

    GetWindowThreadProcessId(hwnd, &info.pid);
    GetWindowRect(hwnd, &info.rect);

    return info;
}

std::optional<WindowInfo> find_best_match(const std::vector<WindowInfo>& windows,
                                           const std::wstring& query, bool exact,
                                           std::vector<WindowInfo>* out_candidates) {
    std::wstring q_lower = to_lower(query);

    struct ScoredWindow {
        const WindowInfo* info;
        int score;
        size_t title_len;
    };

    std::vector<ScoredWindow> candidates;

    for (const auto& w : windows) {
        if (is_noise_window(w)) continue;
        std::wstring t_lower = to_lower(w.title);

        if (exact) {
            if (t_lower == q_lower) {
                candidates.push_back({ &w, 100, w.title.size() });
            }
        } else {
            if (t_lower == q_lower) {
                candidates.push_back({ &w, 100, w.title.size() });
            } else if (t_lower.find(q_lower) == 0) {
                candidates.push_back({ &w, 80, w.title.size() });
            } else if (t_lower.find(q_lower) != std::wstring::npos) {
                candidates.push_back({ &w, 60, w.title.size() });
            }
        }
    }

    if (candidates.empty()) {
        return std::nullopt;
    }

    // Sort by score desc, then by title length asc
    std::sort(candidates.begin(), candidates.end(),
        [](const ScoredWindow& a, const ScoredWindow& b) {
            if (a.score != b.score) return a.score > b.score;
            return a.title_len < b.title_len;
        });

    // Populate all candidates if requested
    if (out_candidates) {
        out_candidates->clear();
        for (const auto& sw : candidates) {
            out_candidates->push_back(*sw.info);
        }
    }

    return *candidates[0].info;
}

std::vector<WindowInfo> find_by_pid(const std::vector<WindowInfo>& windows, uint32_t pid) {
    std::vector<WindowInfo> result;
    for (const auto& w : windows) {
        if (w.pid == pid && !is_noise_window(w)) {
            result.push_back(w);
        }
    }
    return result;
}

std::vector<WindowInfo> filter_windows(const std::vector<WindowInfo>& windows,
                                       uint32_t pid, bool has_pid,
                                       const std::wstring& process, bool has_process,
                                       const std::wstring& className, bool has_className) {
    std::vector<WindowInfo> result;
    std::wstring process_lower = to_lower(process);
    for (const auto& w : windows) {
        if (is_noise_window(w)) continue;
        if (has_pid && w.pid != pid) continue;
        if (has_process && to_lower(w.exeName).find(process_lower) == std::wstring::npos) continue;
        if (has_className && w.className != className) continue;
        result.push_back(w);
    }
    return result;
}

static std::wstring hwnd_hex(HWND hwnd) {
    std::wostringstream oss;
    oss << L"0x" << std::uppercase << std::setfill(L'0')
        << std::setw(16) << std::hex
        << reinterpret_cast<uintptr_t>(hwnd);
    return oss.str();
}

void print_window_list(const std::vector<WindowInfo>& windows) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

    auto write_line = [&](const std::wstring& s) {
        WriteConsoleW(hOut, s.c_str(), static_cast<DWORD>(s.size()), nullptr, nullptr);
        WriteConsoleW(hOut, L"\n", 1, nullptr, nullptr);
    };

    // Header
    std::wostringstream hdr;
    hdr << std::left
        << std::setw(20) << L"HandleHex"
        << std::setw(8)  << L"PID"
        << std::setw(7)  << L"Width"
        << std::setw(7)  << L"Height"
        << std::setw(12) << L"State"
        << std::setw(24) << L"ClassName"
        << L"Title";
    write_line(hdr.str());
    write_line(std::wstring(90, L'-'));

    int shown = 0;
    for (const auto& w : windows) {
        if (is_noise_window(w)) continue;

        int w_width = w.rect.right - w.rect.left;
        int w_height = w.rect.bottom - w.rect.top;
        std::wstring state = w.minimized ? L"Minimized" : L"Normal";

        // Truncate long class names
        std::wstring cls = w.className;
        if (cls.size() > 22) cls = cls.substr(0, 22);

        std::wostringstream line;
        line << std::left
             << std::setw(20) << hwnd_hex(w.hwnd)
             << std::setw(8)  << w.pid
             << std::setw(7)  << w_width
             << std::setw(7)  << w_height
             << std::setw(12) << state
             << std::setw(24) << cls
             << w.title;
        write_line(line.str());
        shown++;
    }

    // Summary
    std::wostringstream summary;
    summary << L"\nShowing " << shown << L" of " << windows.size() << L" windows";
    write_line(summary.str());
}

static std::wstring json_escape(const std::wstring& s) {
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

static std::wstring hwnd_hex_json(HWND hwnd) {
    std::wostringstream oss;
    oss << L"0x" << std::uppercase << std::setfill(L'0')
        << std::setw(16) << std::hex << reinterpret_cast<uintptr_t>(hwnd);
    return oss.str();
}

void output_window_list_json(const std::vector<WindowInfo>& windows) {
    std::wostringstream oss;
    oss << L"{\"ok\":true,\"windows\":[";
    bool first = true;
    for (const auto& w : windows) {
        if (is_noise_window(w)) continue;
        int ww = w.rect.right - w.rect.left;
        int wh = w.rect.bottom - w.rect.top;
        if (ww <= 1 && wh <= 1 && !w.minimized) continue;

        if (!first) oss << L",";
        first = false;
        oss << L"{\"hwnd\":\"" << hwnd_hex_json(w.hwnd)
            << L"\",\"pid\":" << w.pid
            << L",\"title\":\"" << json_escape(w.title)
            << L"\",\"className\":\"" << json_escape(w.className)
            << L"\",\"width\":" << ww
            << L",\"height\":" << wh
            << L",\"state\":\"" << (w.minimized ? L"minimized" : L"normal")
            << L"\"}";
    }
    oss << L"]}\n";
    write_stdout_utf8(oss.str());
}
