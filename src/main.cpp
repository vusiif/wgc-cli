#include "cli_args.h"
#include "output.h"
#include "window_enum.h"
#include "capture_common.h"
#include "server.h"
#include "client.h"
#include "diagnose.h"
#include "mcp_server.h"

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

    // MCP stdio server mode
    if (opts.mcp_stdio) {
        return run_mcp_server();
    }

    // Server mode
    if (opts.server) {
        return run_server(opts.pipe);
    }

    // Client mode
    if (!opts.client.empty()) {
        return run_client(opts.pipe, opts.client);
    }

    // Health check (ping server)
    if (opts.health) {
        return run_client(opts.pipe, L"{\"action\":\"ping\"}");
    }

    return run(opts);
}

static int run(const Options& opts) {
    // --doctor mode
    if (opts.doctor) {
        return run_doctor(opts);
    }

    // --list mode
    if (opts.list) {
        auto windows = enumerate_windows(opts.include_minimized);
        auto filtered = filter_windows(windows, opts.pid, opts.has_pid,
                                       opts.process, opts.has_process,
                                       opts.className, opts.has_className);
        if (opts.json) {
            output_window_list_json(filtered);
        } else {
            print_window_list(filtered);
        }
        return 0;
    }

    // Need --title, --hwnd, --pid, --process, or --class-name for capture
    if (!opts.has_title && !opts.has_hwnd && !opts.has_pid && !opts.has_process && !opts.has_className) {
        output_error(opts, ExitCode::BadArgs, L"MISSING_TARGET",
            L"Specify --title, --hwnd, --pid, --process, --class-name, or --list");
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
    std::vector<WindowInfo> match_candidates;
    WindowInfo target{};
    bool found = false;

    if (opts.has_hwnd) {
        target = find_window_by_hwnd(opts.hwnd);
        if (target.hwnd) {
            found = true;
        }
    } else {
        all_windows = enumerate_windows(opts.include_minimized);
        // Apply filters
        auto filtered = filter_windows(all_windows, opts.pid, opts.has_pid,
                                       opts.process, opts.has_process,
                                       opts.className, opts.has_className);

        if (opts.has_title) {
            // Match by title within filtered set
            auto match = find_best_match(filtered, opts.title, opts.exact, &match_candidates);
            if (match) {
                target = *match;
                found = true;
            }
        } else if (opts.has_pid || opts.has_process || opts.has_className) {
            // --pid/--process/--class-name without --title
            if (filtered.size() == 1) {
                target = filtered[0];
                match_candidates = filtered;
                found = true;
            } else if (filtered.size() > 1) {
                // Multiple matches: fail with AMBIGUOUS_MATCH
                match_candidates = filtered;
                output_error(opts, ExitCode::WindowNotFound, L"AMBIGUOUS_MATCH",
                    std::to_wstring(filtered.size()) + L" windows matched. Use --title or --hwnd to select one.");
                return static_cast<int>(ExitCode::WindowNotFound);
            }
        }
    }

    if (!found) {
        std::wstring desc;
        if (opts.has_hwnd) desc = L"hwnd " + std::to_wstring(opts.hwnd);
        else if (opts.has_title) desc = L"title: " + opts.title;
        else if (opts.has_pid) desc = L"pid " + std::to_wstring(opts.pid);
        else if (opts.has_process) desc = L"process: " + opts.process;
        else if (opts.has_className) desc = L"class: " + opts.className;
        output_error(opts, ExitCode::WindowNotFound, L"WINDOW_NOT_FOUND",
            L"No visible top-level window matched " + desc);
        return static_cast<int>(ExitCode::WindowNotFound);
    }

    // --require-unique: fail if multiple candidates
    if (opts.require_unique && match_candidates.size() > 1) {
        output_error(opts, ExitCode::WindowNotFound, L"AMBIGUOUS_MATCH",
            L"Multiple windows matched (" + std::to_wstring(match_candidates.size()) +
            L"). Use --hwnd to select a specific window.");
        return static_cast<int>(ExitCode::WindowNotFound);
    }

    int win_w = target.rect.right - target.rect.left;
    int win_h = target.rect.bottom - target.rect.top;
    if (!opts.json) {
        std::wcout << L"Matched window: " << target.title << L"\n";
        std::wcout << L"Handle: 0x" << std::hex << reinterpret_cast<uintptr_t>(target.hwnd) << std::dec << L"\n";
        std::wcout << L"Window size: " << win_w << L"x" << win_h << L"\n";
    }

    // Restore if minimized
    if (target.minimized && opts.restore) {
        if (!opts.json) std::wcout << L"Restoring minimized window...\n";
        ShowWindow(target.hwnd, SW_RESTORE);
        Sleep(500);
        for (int i = 0; i < 10 && IsIconic(target.hwnd); ++i) {
            Sleep(100);
        }
    }

    // --delay-ms
    if (opts.delay_ms > 0) {
        Sleep(opts.delay_ms);
    }

    // Capture via shared pipeline
    CaptureRequest req;
    req.hwnd = target.hwnd;
    req.title = target.title;
    req.out = opts.out;
    req.format = opts.format;
    req.timeout_ms = opts.timeout_ms;
    req.restore_minimized = false;  // already handled above
    req.has_crop = opts.has_crop;
    req.crop_x = opts.crop_x; req.crop_y = opts.crop_y;
    req.crop_w = opts.crop_w; req.crop_h = opts.crop_h;
    req.resize_w = opts.resize_w; req.resize_h = opts.resize_h;
    req.max_width = opts.max_width;

    auto cr = capture_to_file(req);
    if (!cr.ok) {
        ExitCode ec = (cr.error_code == L"TIMEOUT") ? ExitCode::Timeout
            : (cr.error_code == L"SAVE_FAILED") ? ExitCode::SaveFailed
            : (cr.error_code == L"D3D11_INIT_FAILED") ? ExitCode::WgcUnavailable
            : ExitCode::CaptureFailed;
        output_error_ex(opts, ec, cr.error_code, cr.message, cr.stage, cr.hresult, cr.suggestion);
        return static_cast<int>(ec);
    }
    std::wstring out_path = cr.out_path;
    uint32_t out_width = cr.image_width;
    uint32_t out_height = cr.image_height;

    // Output success
    MatchedWindow mw;
    mw.title = target.title;
    mw.className = target.className;
    mw.hwnd = reinterpret_cast<uint64_t>(target.hwnd);
    mw.pid = target.pid;
    mw.width = out_width;
    mw.height = out_height;
    mw.minimized = target.minimized;

    // Build candidates list
    std::vector<MatchedWindow> mwc;
    for (const auto& c : match_candidates) {
        MatchedWindow cmw;
        cmw.title = c.title;
        cmw.className = c.className;
        cmw.hwnd = reinterpret_cast<uint64_t>(c.hwnd);
        cmw.pid = c.pid;
        cmw.width = c.rect.right - c.rect.left;
        cmw.height = c.rect.bottom - c.rect.top;
        cmw.minimized = c.minimized;
        mwc.push_back(cmw);
    }

    output_success(opts, mw, out_path, mwc);
    return 0;
}
