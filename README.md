# wgccli

[中文](README_zh.md)

Windows command-line screenshot tool using **Windows.Graphics.Capture** API. Designed for AI/Agent workflows — capture any window by title, HWND, process name, or PID, without relying on active window focus or clipboard.

## Features

- Capture windows by title (partial/exact match, case-insensitive)
- Capture windows by HWND, PID, process name, or window class
- List all visible windows as JSON (`--list --json`)
- PNG/JPEG/BMP output with auto-generated filenames
- Image resize, crop, and format conversion
- Minimized window auto-restore before capture
- **Server/Client mode** — named pipe IPC for AI agents running in different sessions
- **Environment diagnostics** (`--doctor`)
- No GUI, no clipboard, no disk scanning

## Download

Grab `wgccli.exe` from [Releases](https://github.com/vusiif/wgc-cli/releases).

## AI Agent Skill

This repo includes a [SKILL.md](SKILL.md) for Claude Code and other AI agents. It provides step-by-step instructions for capturing Windows GUI screenshots, including server/client mode for session isolation, troubleshooting, and a ready-to-use PowerShell helper.

## Quick Start

```bash
# Capture by window title
wgccli.exe --title "Notepad" --out C:\Screenshots

# Capture by PID
wgccli.exe --pid 12345 --out C:\Screenshots --json

# Capture by process name
wgccli.exe --process chrome.exe --title "ChatGPT" --out C:\Screenshots --json

# List windows as JSON
wgccli.exe --list --json

# Scale down to 1280px wide (saves tokens for VLMs)
wgccli.exe --title "Chrome" --out C:\Screenshots --max-width 1280 --format jpg --json
```

## Server/Client Mode (for AI Agents)

When an AI agent runs in a different session (e.g., as a service or SSH), it cannot directly enumerate or capture GUI windows due to session isolation. The server/client mode solves this by using named pipes for IPC.

```bash
# 1. Start the server in the user's interactive session
wgccli.exe --server

# 2. Agent sends requests via the named pipe
wgccli.exe --client "{\"action\":\"list\"}"
wgccli.exe --client "{\"action\":\"capture\",\"title\":\"Notepad\",\"out\":\"C:\\Temp\"}"
wgccli.exe --client "{\"action\":\"capture\",\"process\":\"chrome.exe\",\"out\":\"C:\\Temp\"}"
```

Use `--pipe <name>` to customize the pipe name (default: `wgccli`).

## Parameters

### Selectors (at least one required for capture)

| Parameter | Description |
|---|---|
| `--title <text>` | Match window by title (partial, case-insensitive) |
| `--hwnd <hex>` | Capture by window handle |
| `--pid <pid>` | Filter/capture by process ID |
| `--process <exe>` | Filter/capture by process name (e.g. `chrome.exe`) |
| `--class-name <name>` | Filter/capture by window class name |
| `--list` | List candidate windows |
| `--exact` | Exact title match |

### Output Options

| Parameter | Description |
|---|---|
| `--out <dir-or-file>` | Output directory or full file path |
| `--json` | JSON output |
| `--format <png\|jpg\|jpeg\|bmp>` | Output format (default: png) |

### Image Processing

| Parameter | Description |
|---|---|
| `--max-width <n>` | Scale down to max width (proportional) |
| `--resize <WxH>` | Resize to exact dimensions (e.g. `1024x768`) |
| `--crop <x,y,w,h>` | Crop region after capture |

### Behavior Options

| Parameter | Description |
|---|---|
| `--timeout-ms <n>` | Wait for first frame (default: 3000) |
| `--delay-ms <n>` | Wait before capture |
| `--no-restore` | Don't restore minimized windows |
| `--require-unique` | Fail if multiple windows match |

### Server/Client & Diagnostics

| Parameter | Description |
|---|---|
| `--server` | Start named pipe server |
| `--client <json>` | Send request to server via pipe |
| `--pipe <name>` | Custom pipe name (default: `wgccli`) |
| `--doctor` | Run environment diagnostics |
| `--help` | Print help |
| `--version` | Print version |

## JSON Output Examples

### Screenshot success

```json
{
  "ok": true,
  "matchedWindow": {
    "title": "Android Studio",
    "hwnd": "0x0000000000123456",
    "pid": 12345,
    "width": 1920,
    "height": 1080,
    "className": "SunAwtFrame",
    "state": "normal"
  },
  "candidates": [...],
  "screenshotPath": "C:\\Screenshots\\20260619-153022-android-studio.png"
}
```

### Error with diagnostics

```json
{
  "ok": false,
  "errorCode": "CAPTURE_FAILED",
  "exitCode": 5,
  "stage": "CreateForWindow",
  "hresult": "0x80070005",
  "message": "Access denied",
  "suggestion": "Try matching elevation or use server/client mode."
}
```

### Environment diagnostics

```json
{
  "ok": true,
  "osBuild": 26100,
  "wgcAvailable": true,
  "d3d11Available": true,
  "interactiveSession": true,
  "elevated": false,
  "windowsCount": 12
}
```

## Title Matching Priority

1. Exact match (case-insensitive)
2. Starts with (case-insensitive)
3. Contains (case-insensitive)
4. Shorter title preferred

## Exit Codes

| Code | Meaning |
|---|---|
| 0 | Success |
| 1 | General error |
| 2 | Bad arguments |
| 3 | Window not found / ambiguous match |
| 4 | WGC not available |
| 5 | Capture failed |
| 6 | Save failed |
| 7 | Timeout |
| 8 | Permission denied |

## Requirements

- Windows 10 version 1903 (build 18362) or later
- D3D11 capable GPU
- Run `wgccli.exe --doctor` to check compatibility

## Build from Source

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Or open in CLion with MSVC toolchain configured.

## Limitations

- Minimized windows may not be capturable (auto-restore is attempted)
- Some protected windows, elevated windows, secure desktop, or DRM surfaces may fail
- Does not capture the cursor
- Windows from another session/desktop require server/client mode
