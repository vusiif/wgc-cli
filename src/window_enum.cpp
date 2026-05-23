#include "window_enum.h"
#include <windows.h>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <sstream>

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
                                           const std::wstring& query, bool exact) {
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

    return *candidates[0].info;
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
