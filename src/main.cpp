#include "cli_args.h"
#include "output.h"
#include "window_enum.h"
#include "capture_wgc.h"
#include "image_wic.h"
#include "d3d_helpers.h"
#include "server.h"
#include "client.h"

#include <windows.h>
#include <iostream>
#include <filesystem>

static int run(const Options& opts);

int wmain(int argc, wchar_t* argv[]) {
    Options opts;
    if (!parse_args(argc, argv, opts)) {
        return static_cast<int>(ExitCode::BadArgs);
    }

    if (opts.help) {
        print_help();
        return 0;
    }

    if (opts.version) {
        print_version();
        return 0;
    }

    // Server mode
    if (opts.server) {
        return run_server(opts.pipe);
    }

    // Client mode
    if (!opts.client.empty()) {
        return run_client(opts.pipe, opts.client);
    }

    return run(opts);
}

static int run(const Options& opts) {
    // --list mode
    if (opts.list) {
        auto windows = enumerate_windows(opts.include_minimized);
        print_window_list(windows);
        return 0;
    }

    // Need --title or --hwnd for capture
    if (!opts.has_title && !opts.has_hwnd) {
        output_error(opts, ExitCode::BadArgs, L"MISSING_TARGET",
            L"Specify --title, --hwnd, or --list");
        return static_cast<int>(ExitCode::BadArgs);
    }

    // Need --out for capture
    if (opts.out.empty()) {
        output_error(opts, ExitCode::BadArgs, L"MISSING_OUTPUT",
            L"Specify --out <dir-or-file>");
        return static_cast<int>(ExitCode::BadArgs);
    }

    // Find target window
    std::vector<WindowInfo> all_windows;
    WindowInfo target{};
    bool found = false;

    if (opts.has_hwnd) {
        target = find_window_by_hwnd(opts.hwnd);
        if (target.hwnd) {
            found = true;
        }
    } else {
        all_windows = enumerate_windows(opts.include_minimized);
        auto match = find_best_match(all_windows, opts.title, opts.exact);
        if (match) {
            target = *match;
            found = true;
        }
    }

    if (!found) {
        std::wstring desc = opts.has_hwnd
            ? (L"hwnd " + std::to_wstring(opts.hwnd))
            : (L"title: " + opts.title);
        output_error(opts, ExitCode::WindowNotFound, L"WINDOW_NOT_FOUND",
            L"No visible top-level window matched " + desc);
        return static_cast<int>(ExitCode::WindowNotFound);
    }

    int win_w = target.rect.right - target.rect.left;
    int win_h = target.rect.bottom - target.rect.top;
    std::wcout << L"Matched window: " << target.title << L"\n";
    std::wcout << L"Handle: 0x" << std::hex << reinterpret_cast<uintptr_t>(target.hwnd) << std::dec << L"\n";
    std::wcout << L"Window size: " << win_w << L"x" << win_h << L"\n";

    // Restore if minimized
    if (target.minimized && opts.restore) {
        std::wcout << L"Restoring minimized window...\n";
        ShowWindow(target.hwnd, SW_RESTORE);
        Sleep(500);
        // Wait until window is no longer iconic
        for (int i = 0; i < 10 && IsIconic(target.hwnd); ++i) {
            Sleep(100);
        }
    }

    // Initialize D3D11
    auto d3d = create_d3d_device();
    if (!d3d.device) {
        output_error(opts, ExitCode::WgcUnavailable, L"D3D11_INIT_FAILED",
            L"Failed to create D3D11 device");
        return static_cast<int>(ExitCode::WgcUnavailable);
    }

    // Capture
    auto captured = capture_window(d3d.device.Get(), target.hwnd, opts.timeout_ms);
    if (!captured) {
        output_error(opts, ExitCode::CaptureFailed, L"CAPTURE_FAILED",
            L"Failed to capture window");
        return static_cast<int>(ExitCode::CaptureFailed);
    }

    // Generate output path
    std::wstring out_path;
    if (opts.out.size() >= 4 &&
        (opts.out.substr(opts.out.size() - 4) == L".png" || opts.out.substr(opts.out.size() - 4) == L".PNG")) {
        out_path = opts.out;
        auto parent = std::filesystem::path(out_path).parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }
    } else {
        std::filesystem::create_directories(opts.out);
        out_path = generate_png_path(opts.out, target.title);
    }

    // Save PNG
    if (!save_png(*captured, out_path)) {
        output_error(opts, ExitCode::SaveFailed, L"SAVE_FAILED",
            L"Failed to save PNG to " + out_path);
        return static_cast<int>(ExitCode::SaveFailed);
    }

    // Output success
    MatchedWindow mw;
    mw.title = target.title;
    mw.className = target.className;
    mw.hwnd = reinterpret_cast<uint64_t>(target.hwnd);
    mw.pid = target.pid;
    mw.width = captured->width;
    mw.height = captured->height;
    mw.minimized = target.minimized;

    output_success(opts, mw, out_path);
    return 0;
}
