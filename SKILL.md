---
name: wgc-screenshot
description: Capture screenshots of Windows GUI application windows using wgccli. Use this when an agent needs to inspect a Windows app, browser, IDE, emulator, installer, dialog, or any visible desktop window without relying on active-window focus, the clipboard, or manual screenshot tools.
---

# WGC Screenshot Skill

Use this skill when you need to visually inspect a Windows GUI window from an agent workflow.

`wgccli.exe` captures Windows application windows by title, HWND, PID, process name, or window class using Windows.Graphics.Capture. It is especially useful when an agent needs screenshots of apps such as Android Studio, Chrome, Notepad, emulators, installers, or desktop tools.

## When to use

Use this skill when:

* You need a screenshot of a specific Windows GUI window.
* You need to inspect the current visual state of an app.
* You need to capture a window without activating it.
* You need a deterministic screenshot path for later image analysis.
* The agent is running in a Windows environment and `wgccli.exe` is available.

Do not use this skill when:

* The task does not require visual inspection.
* The target is a web page that can be inspected more directly through browser automation or DOM access.
* The target window is known to be protected, DRM-rendered, on the secure desktop, or inaccessible from the current Windows session.
* The operating system is not Windows.

## Prerequisites

Before using the tool, check that `wgccli.exe` is available.

Preferred checks:

```powershell
where.exe wgccli.exe
```

or:

```powershell
Get-Command wgccli.exe -ErrorAction SilentlyContinue
```

If it is not in `PATH`, look for a known local path such as:

```powershell
$env:WGC_CLI_PATH
```

or ask the user where `wgccli.exe` is installed.

Create a screenshot output directory before capture:

```powershell
$OutDir = Join-Path $env:TEMP "agent-screenshots"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
```

## Basic workflow

### 1. List visible windows

When the target title is uncertain, first list visible windows:

```powershell
wgccli.exe --list --json
```

Use the output to identify a stable title, HWND, PID, or process name.

Prefer HWND capture when multiple windows have similar titles.

### 2. Capture by partial title

Use partial title matching for common cases:

```powershell
wgccli.exe --title "Chrome" --out "$env:TEMP\agent-screenshots" --json
```

### 3. Capture by exact title

Use exact title matching when ambiguity matters:

```powershell
wgccli.exe --title "Android Studio - MyProject" --exact --out "$env:TEMP\agent-screenshots" --json
```

### 4. Capture by HWND

Use HWND when available from `--list` or previous output:

```powershell
wgccli.exe --hwnd 0x0000000000123456 --out "$env:TEMP\agent-screenshots" --json
```

### 5. Capture by PID or process name

Use `--pid` or `--process` to filter by process. Combine with `--title` for precision:

```powershell
# By PID
wgccli.exe --pid 12345 --out "$env:TEMP\agent-screenshots" --json

# By process name + title
wgccli.exe --process chrome.exe --title "ChatGPT" --out "$env:TEMP\agent-screenshots" --json

# By window class
wgccli.exe --class-name "SunAwtFrame" --out "$env:TEMP\agent-screenshots" --json
```

When `--pid`/`--process`/`--class-name` matches multiple windows without `--title`, the tool fails with `AMBIGUOUS_MATCH`. Use `--list --json` to find the exact HWND first.

### 6. Parse JSON output

Always prefer `--json` for agent workflows.

Expected successful shape:

```json
{
  "ok": true,
  "matchedWindow": {
    "title": "Android Studio - MyProject",
    "hwnd": "0x0000000000123456",
    "pid": 12345,
    "width": 1920,
    "height": 1080,
    "className": "SunAwtFrame",
    "state": "normal"
  },
  "candidates": [...],
  "screenshotPath": "C:\\Screenshots\\20260523-153022-android-studio.png"
}
```

After capture:

1. Confirm `ok` is true.
2. Read `screenshotPath`.
3. Verify the file exists.
4. Use the agent's image-viewing capability to inspect the PNG.

PowerShell example:

```powershell
$json = wgccli.exe --title "Chrome" --out "$env:TEMP\agent-screenshots" --json | ConvertFrom-Json

if (-not $json.ok) {
    throw "wgccli capture failed: $($json.errorCode) - $($json.message)"
}

if (-not (Test-Path $json.screenshotPath)) {
    throw "Screenshot file was not created: $($json.screenshotPath)"
}

$json.screenshotPath
```

## Image processing options

### Scale down to save tokens

Use `--max-width` to scale down large screenshots, saving VLM tokens:

```powershell
wgccli.exe --title "Chrome" --out "$env:TEMP\agent-screenshots" --max-width 1280 --format jpg --json
```

### Resize to exact dimensions

```powershell
wgccli.exe --title "Chrome" --out "$env:TEMP\agent-screenshots" --resize 1024x768 --json
```

### Crop a region

```powershell
wgccli.exe --title "Chrome" --out "$env:TEMP\agent-screenshots" --crop 100,100,800,600 --json
```

### Output format

```powershell
# JPEG (smaller files, good for screenshots)
wgccli.exe --title "Chrome" --out "$env:TEMP\agent-screenshots" --format jpg --json

# BMP (lossless, large files)
wgccli.exe --title "Chrome" --out "$env:TEMP\agent-screenshots" --format bmp --json
```

Supported formats: `png` (default), `jpg`, `jpeg`, `bmp`.

### Delay before capture

Wait for animations or loading to finish:

```powershell
wgccli.exe --title "Chrome" --out "$env:TEMP\agent-screenshots" --delay-ms 2000 --json
```

## Server/client mode for agent session isolation

Use server/client mode when the agent runs in a different Windows session from the interactive desktop, such as SSH, service execution, scheduled jobs, or remote automation.

### Start server in the user's interactive desktop session

This must be started where the GUI windows are accessible:

```powershell
wgccli.exe --server
```

Optionally use a custom pipe name:

```powershell
wgccli.exe --server --pipe my-agent-wgc
```

### Check server health

```powershell
wgccli.exe --health
```

Returns `{"ok":true,"server":{"version":"1.7.1","userInteractive":true}}` if the server is running.

### Send requests from the agent session

List windows:

```powershell
wgccli.exe --client '{"action":"list"}'
```

Capture by title:

```powershell
wgccli.exe --client '{"action":"capture","title":"Notepad","out":"C:\\Temp\\agent-screenshots"}'
```

Capture by PID or process:

```powershell
wgccli.exe --client '{"action":"capture","process":"chrome.exe","out":"C:\\Temp\\agent-screenshots"}'
```

Use a custom pipe name if the server was started with one:

```powershell
wgccli.exe --pipe my-agent-wgc --client '{"action":"list"}'
```

## MCP stdio server for IDE-integrated agents

Use MCP mode when the agent runs inside Claude Code, Cursor, Codex, or any MCP-compatible client.

### Start MCP server

```powershell
wgccli.exe --mcp-stdio
```

The server speaks JSON-RPC 2.0 over stdin/stdout.

### Registered MCP tools

| Tool | Description |
|---|---|
| `list_windows` | List all visible top-level windows. Optional: `include_minimized` (boolean, default true) |
| `capture_window` | Capture a screenshot. Required: `out`. Optional: `title`, `hwnd`, `pid`, `process`, `class_name`, `format`, `max_width`, `resize`, `crop`, `timeout_ms`, `exact` |

### MCP tool call examples

List windows:

```json
{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"list_windows","arguments":{}}}
```

Capture by title:

```json
{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"capture_window","arguments":{"title":"Notepad","out":"C:\\Temp"}}}
```

Capture with options:

```json
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"capture_window","arguments":{"title":"Chrome","out":"C:\\Temp","format":"jpg","max_width":1280,"exact":true}}}
```

## Environment diagnostics

Run `--doctor` to check if the environment supports window capture:

```powershell
wgccli.exe --doctor --json
```

Output:

```json
{
  "ok": true,
  "osBuild": 26100,
  "minSupportedBuild": 18362,
  "compatible": true,
  "wgcAvailable": true,
  "d3d11Available": true,
  "interactiveSession": true,
  "elevated": false,
  "windowsCount": 12
}
```

## Recommended agent procedure

Follow this order:

1. Check that the environment is Windows.
2. Check that `wgccli.exe` exists.
3. Create an output directory.
4. If the target window is ambiguous, run `--list --json`.
5. Prefer exact title or HWND when possible.
6. Run capture with `--json`.
7. Parse `screenshotPath`.
8. Verify the PNG exists.
9. Inspect the image.
10. If capture fails, use the troubleshooting table below.

## Title matching guidance

When matching by title:

1. Prefer exact match for known full titles.
2. Use partial match for broad app names.
3. If multiple windows match, run `--list --json` and capture by HWND.
4. Avoid overly generic titles such as `"Chrome"`, `"Settings"`, or `"Untitled"` when precision matters.
5. If a minimized window fails, retry after restoring it manually or without relying on minimized capture.

## Troubleshooting

### Window not found

Symptoms:

* Exit code `3`
* No matching title
* Empty or unexpected `--list`

Actions:

```powershell
wgccli.exe --list --json
```

Then retry with:

* A shorter title substring.
* `--exact` removed.
* HWND capture.
* Server/client mode if the agent may be in a different session.

### WGC unavailable

Symptoms:

* Exit code `4`

Actions:

* Confirm Windows 10 version 1903 (build 18362) or later.
* Confirm a D3D11-capable GPU is available.
* Confirm the app is running in the interactive desktop session.
* Run `wgccli.exe --doctor --json` for details.

### Capture failed

Symptoms:

* Exit code `5`
* Error codes: `CAPTURE_FAILED`, `D3D11_INIT_FAILED`
* JSON includes `stage`, `hresult`, `suggestion` fields

Actions:

* Check the `stage` field: `CreateForWindow`, `GetTextureFromSurface`, `CreateTexture2D`, `Map`, `FrameArrived`
* Check `hresult` for specific D3D/WGC error codes.
* Follow the `suggestion` in the JSON output.
* Check whether the window is protected, elevated, DRM-rendered, or on a secure desktop.
* Try running the agent or server with matching privileges.
* Try server/client mode from the interactive user session.

### Timeout

Symptoms:

* Exit code `7`
* `errorCode: "TIMEOUT"`

Actions:

* Increase timeout:

```powershell
wgccli.exe --title "Chrome" --out "$env:TEMP\agent-screenshots" --json --timeout-ms 8000
```

* Confirm the target window is visible and responsive.
* Try `--restore` if the window may be minimized.

### Save failed

Symptoms:

* Exit code `6`
* `errorCode: "SAVE_FAILED"`

Actions:

* Confirm the output directory exists.
* Confirm write permissions.
* Try a simple path such as `C:\Temp\agent-screenshots`.

### Ambiguous match

Symptoms:

* `errorCode: "AMBIGUOUS_MATCH"`
* Multiple windows match `--pid`/`--process`/`--class-name`

Actions:

* Add `--title` to narrow the match.
* Use `--hwnd` from `--list --json` output.
* Use `--require-unique` to fail early on ambiguity.

### Permission denied

Symptoms:

* Exit code `8`

Actions:

* Check whether the target window is elevated.
* Run the capture process with matching privileges.
* Avoid protected system windows and secure desktop prompts.

## Exit codes

| Code | Meaning                                |
| ---: | -------------------------------------- |
|    0 | Success                                |
|    1 | General error                          |
|    2 | Bad arguments                          |
|    3 | Window not found / ambiguous match     |
|    4 | Windows.Graphics.Capture not available |
|    5 | Capture failed                         |
|    6 | Save failed                            |
|    7 | Timeout                                |
|    8 | Permission denied                      |

## Command reference

List visible windows:

```powershell
wgccli.exe --list --json
```

Capture by title:

```powershell
wgccli.exe --title "Notepad" --out "$env:TEMP\agent-screenshots" --json
```

Capture by exact title:

```powershell
wgccli.exe --title "Android Studio - MyProject" --exact --out "$env:TEMP\agent-screenshots" --json
```

Capture by HWND:

```powershell
wgccli.exe --hwnd 0x0000000000123456 --out "$env:TEMP\agent-screenshots" --json
```

Capture by PID:

```powershell
wgccli.exe --pid 12345 --out "$env:TEMP\agent-screenshots" --json
```

Capture by process + title:

```powershell
wgccli.exe --process chrome.exe --title "ChatGPT" --out "$env:TEMP\agent-screenshots" --json
```

Capture with image processing:

```powershell
wgccli.exe --title "Chrome" --out "$env:TEMP\agent-screenshots" --max-width 1280 --format jpg --json
```

Capture with longer timeout:

```powershell
wgccli.exe --title "Chrome" --out "$env:TEMP\agent-screenshots" --json --timeout-ms 8000
```

Environment diagnostics:

```powershell
wgccli.exe --doctor --json
```

Start server:

```powershell
wgccli.exe --server
```

Check server health:

```powershell
wgccli.exe --health
```

Client list request:

```powershell
wgccli.exe --client '{"action":"list"}'
```

Client capture request:

```powershell
wgccli.exe --client '{"action":"capture","title":"Notepad","out":"C:\\Temp\\agent-screenshots"}'
```

Start MCP server:

```powershell
wgccli.exe --mcp-stdio
```

## Safety and reliability rules

* Prefer `--json` so the agent can parse results reliably.
* Never assume the active window is the target window.
* Never rely on the clipboard.
* Do not scan arbitrary screenshot folders looking for the newest file; use the returned `screenshotPath`.
* Do not repeatedly capture in a tight loop unless the task explicitly requires it.
* Be careful with screenshots that may contain secrets, credentials, personal data, or private messages.
* If the screenshot may contain sensitive information, mention that visual inspection can expose on-screen data.
* If capture fails because the window is protected or inaccessible, explain the limitation instead of bypassing platform security.

## Minimal robust PowerShell helper

Use this helper when you need a repeatable capture routine:

```powershell
param(
    [Parameter(Mandatory=$true)]
    [string]$Title,

    [string]$OutDir = "$env:TEMP\agent-screenshots",

    [switch]$Exact,

    [string]$Format = "png",

    [int]$MaxWidth = 0
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$cmd = @("wgccli.exe", "--title", $Title, "--out", $OutDir, "--json", "--format", $Format)

if ($Exact) {
    $cmd += "--exact"
}

if ($MaxWidth -gt 0) {
    $cmd += "--max-width"
    $cmd += "$MaxWidth"
}

$output = & $cmd[0] $cmd[1..($cmd.Length - 1)]

if ($LASTEXITCODE -ne 0) {
    throw "wgccli failed with exit code $LASTEXITCODE. Output: $output"
}

$json = $output | ConvertFrom-Json

if (-not $json.ok) {
    throw "wgccli returned ok=false: $($json.errorCode) - $($json.message)"
}

if (-not (Test-Path $json.screenshotPath)) {
    throw "Screenshot path does not exist: $($json.screenshotPath)"
}

Write-Output $json.screenshotPath
```

## Limitations

* Minimized windows may not be capturable (auto-restore is attempted).
* Some protected, elevated, DRM-rendered, secure desktop, or cross-session windows may fail.
* Cursor capture is not included.
* Windows from another session or desktop require server/client mode or may be inaccessible.
* Windows 10 version 1903 (build 18362) or later is required.
