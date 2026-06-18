#include "diagnose.h"
#include "d3d_helpers.h"
#include "window_enum.h"
#include "output.h"

#include <windows.h>
#include <iostream>
#include <sstream>
#include <iomanip>

#define _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>

static uint32_t get_os_build() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return 0;
    using RtlGetVersionFn = LONG(__stdcall*)(PRTL_OSVERSIONINFOW);
    auto fn = reinterpret_cast<RtlGetVersionFn>(
        GetProcAddress(ntdll, "RtlGetVersion"));
    if (!fn) return 0;
    RTL_OSVERSIONINFOW osvi{};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    fn(&osvi);
    return static_cast<uint32_t>(osvi.dwBuildNumber);
}

static bool is_interactive_session() {
    HWINSTA ws = GetProcessWindowStation();
    if (!ws) return false;
    USEROBJECTFLAGS flags{};
    DWORD needed = 0;
    if (!GetUserObjectInformationW(ws, UOI_FLAGS, &flags, sizeof(flags), &needed))
        return false;
    return (flags.dwFlags & WSF_VISIBLE) != 0;
}

static bool is_elevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION elev{};
    DWORD size = 0;
    BOOL ok = GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &size);
    CloseHandle(token);
    return ok && elev.TokenIsElevated;
}

static bool check_wgc_available() {
    try {
        auto factory = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>();
        return factory != nullptr;
    } catch (...) {
        return false;
    }
}

static std::wstring escape_doc(const std::wstring& s) {
    std::wostringstream oss;
    for (wchar_t c : s) {
        switch (c) {
            case L'"':  oss << L"\\\""; break;
            case L'\\': oss << L"\\\\"; break;
            case L'\n': oss << L"\\n";  break;
            case L'\r': oss << L"\\r";  break;
            default:    oss << c;       break;
        }
    }
    return oss.str();
}

int run_doctor(const Options& opts) {
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
    } catch (...) {}

    uint32_t os_build = get_os_build();
    bool d3d_ok = false;
    bool wgc_ok = false;
    try {
        auto d3d = create_d3d_device();
        d3d_ok = d3d.device != nullptr;
        if (d3d_ok) {
            wgc_ok = check_wgc_available();
        }
    } catch (...) {
        d3d_ok = false;
    }
    bool interactive = is_interactive_session();
    bool elevated = is_elevated();

    int win_count = 0;
    try {
        auto windows = enumerate_windows(true);
        for (const auto& w : windows) {
            if (IsWindowVisible(w.hwnd) && GetWindowTextLengthW(w.hwnd) > 0) {
                int ww = w.rect.right - w.rect.left;
                int wh = w.rect.bottom - w.rect.top;
                if (ww > 1 && wh > 1) win_count++;
            }
        }
    } catch (...) {}

    if (opts.json) {
        std::wostringstream oss;
        oss << L"{\"ok\":true"
            << L",\"osBuild\":" << os_build
            << L",\"minSupportedBuild\":18362"
            << L",\"compatible\":" << (os_build >= 18362 ? L"true" : L"false")
            << L",\"wgcAvailable\":" << (wgc_ok ? L"true" : L"false")
            << L",\"d3d11Available\":" << (d3d_ok ? L"true" : L"false")
            << L",\"interactiveSession\":" << (interactive ? L"true" : L"false")
            << L",\"elevated\":" << (elevated ? L"true" : L"false")
            << L",\"windowsCount\":" << win_count
            << L"}\n";
        write_stdout_utf8(oss.str());
    } else {
        std::wcout << L"OS Build:       " << os_build << L" (min: 18362)\n";
        std::wcout << L"Compatible:     " << (os_build >= 18362 ? L"Yes" : L"No") << L"\n";
        std::wcout << L"WGC Available:  " << (wgc_ok ? L"Yes" : L"No") << L"\n";
        std::wcout << L"D3D11 Available:" << (d3d_ok ? L"Yes" : L"No") << L"\n";
        std::wcout << L"Interactive:    " << (interactive ? L"Yes" : L"No") << L"\n";
        std::wcout << L"Elevated:       " << (elevated ? L"Yes" : L"No") << L"\n";
        std::wcout << L"Visible Windows:" << win_count << L"\n";
    }

    return 0;
}
