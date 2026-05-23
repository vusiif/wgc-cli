wgccli 开发任务说明书
项目目标

开发一个 Windows 命令行截图工具 wgccli.exe，使用 Windows.Graphics.Capture API 按窗口句柄或窗口标题捕获指定窗口，并将截图保存为 PNG 文件。

这个工具主要服务于 AI/Agent 场景：Claude Code 或其他 CLI agent 可以调用它截取 Windows GUI 应用窗口，然后读取输出路径并分析截图。

核心目标：

wgccli.exe --title "Android Studio" --out "%USERPROFILE%\Pictures\ClaudeScreenshots"


输出：

Matched window: Android Studio - MyProject
Handle: 0x0000000000123456
Window size: 1920x1080
RESULT_SCREENSHOT_PATH=C:\Users\Alex\Pictures\ClaudeScreenshots\20260523-153022-android-studio.png

技术选型

必须使用：

Language: C++20 or C++17
Build system: CMake
IDE: CLion
Compiler: MSVC toolchain on Windows
Windows API: Win32 + Windows.Graphics.Capture
WinRT projection: C++/WinRT
Graphics backend: Direct3D 11
Image encoding: Windows Imaging Component, WIC
Output format: PNG


不要使用：

ShareX
Python
mss
GDI BitBlt as primary capture method
PowerShell screenshot implementation
GUI application framework
Electron
.NET


可以参考但不要复制依赖：

robmikh/Win32CaptureSample
Microsoft ScreenCaptureforHWND sample


微软的 Win32 WGC 示例也说明了 Win32 应用可以用 IGraphicsCaptureItemInterop 基于 HWND 获取 GraphicsCaptureItem，这正是本工具要走的路线。

命令行接口要求
基础命令
按窗口标题捕获
wgccli.exe --title "Android Studio" --out "%USERPROFILE%\Pictures\ClaudeScreenshots"


默认使用部分匹配，不区分大小写。

按精确窗口标题捕获
wgccli.exe --title "Android Studio - MyProject" --exact --out "C:\Screenshots"

按 HWND 捕获
wgccli.exe --hwnd 0x0000000000123456 --out "C:\Screenshots"

列出窗口
wgccli.exe --list


输出可读表格：

HandleHex           PID     Width Height State      Title
0x0000000000123456  12345   1920  1080   Normal     Android Studio - MyProject
0x0000000000ABCDEF  23456   1280  720    Minimized  IntelliJ IDEA

JSON 输出
wgccli.exe --title "Android Studio" --out "C:\Screenshots" --json


成功输出：

{
"ok": true,
"matchedWindow": {
"title": "Android Studio - MyProject",
"hwnd": "0x0000000000123456",
"pid": 12345,
"width": 1920,
"height": 1080,
"state": "normal"
},
"screenshotPath": "C:\\Screenshots\\20260523-153022-android-studio.png"
}


失败输出：

{
"ok": false,
"errorCode": "WINDOW_NOT_FOUND",
"message": "No visible top-level window matched title: Android Studio"
}

必须支持的参数
--title <text>             按窗口标题匹配
--exact                    精确标题匹配
--hwnd <hex-or-decimal>    按 HWND 捕获
--list                     列出候选窗口
--out <dir-or-file>        输出目录或完整 png 文件路径
--json                     JSON 输出
--timeout-ms <number>      等待首帧超时，默认 3000
--include-minimized        列表中包含最小化窗口，默认 true
--no-restore               不尝试恢复窗口
--restore                  捕获前尝试恢复窗口，默认 true
--help                     打印帮助
--version                  打印版本

可选增强参数
--monitor <index>          捕获显示器，MVP 可不做
--pid <pid>                按进程 ID 过滤窗口
--class-name <name>        按窗口类名过滤
--output-contract plain    输出 RESULT_SCREENSHOT_PATH=...
--output-contract json     输出 JSON
--delay-ms <number>        捕获前等待，例如 500ms

MVP 功能范围

第一版只需要完成：

1. --list
2. --title partial match
3. --title --exact
4. --hwnd
5. 单帧截图
6. PNG 保存
7. RESULT_SCREENSHOT_PATH=... 输出
8. 明确 exit code


不要一开始做：

录屏
多帧捕获
OCR
窗口选择 GUI
热键
系统托盘
区域截图
多显示器高级适配


MVP 的一句话验收标准：

能在 CLion 里构建出 wgccli.exe，运行 `wgccli.exe --title "Notepad" --out C:\Temp` 后生成一张指定窗口的 PNG，并打印 RESULT_SCREENSHOT_PATH。

核心行为要求
窗口枚举

使用 Win32 API 枚举顶层窗口：

EnumWindows
IsWindowVisible
GetWindowTextW
GetWindowTextLengthW
GetWindowRect
GetWindowThreadProcessId
IsIconic
GetClassNameW


过滤规则：

- 标题为空的窗口默认不显示
- 不可见窗口默认不显示
- tool window / cloaked window 可以后续优化
- 最小化窗口可以列出，但捕获时要提示可能失败


--list 输出中至少包含：

HWND hex
PID
Title
Width
Height
State: Normal / Minimized
ClassName

标题匹配规则

按以下优先级排序：

1. 完全相等，不区分大小写
2. startsWith，不区分大小写
3. contains，不区分大小写
4. 标题更短者优先


如果有多个匹配：

- 默认选择分数最高的一个
- 同时输出 Other matches
- JSON 模式下把 candidates 一并输出

捕获前窗口处理

默认尝试：

1. 如果窗口最小化，调用 ShowWindow(hwnd, SW_RESTORE)
2. 等待 200-500ms
3. 不强依赖 SetForegroundWindow


重点：WGC 按 HWND 捕获时，不应该依赖“当前活动窗口”。这正是它比 ShareX active window workflow 更适合 AI 的地方。

不要把“抢焦点”作为成功条件。最多作为辅助：

SetForegroundWindow 可以尝试，但失败不应导致截图失败。

WGC 捕获流程

使用 IGraphicsCaptureItemInterop::CreateForWindow 从 HWND 创建 GraphicsCaptureItem。微软文档明确这个方法用于针对单个窗口创建 graphics capture item，参数就是窗口 HWND。

捕获流程建议：

1. 初始化 COM / WinRT apartment
2. 创建 D3D11 device
3. 将 ID3D11Device 转成 WinRT IDirect3DDevice
4. 通过 IGraphicsCaptureItemInterop::CreateForWindow 创建 GraphicsCaptureItem
5. 读取 item.Size()
6. 创建 Direct3D11CaptureFramePool
7. CreateCaptureSession(item)
8. StartCapture()
9. 等待 FrameArrived 或循环 TryGetNextFrame
10. 获取 Direct3D11CaptureFrame.Surface()
11. 转成 ID3D11Texture2D
12. CopyResource 到 staging texture
13. Map staging texture
14. 使用 WIC 保存为 PNG
15. 释放资源


Direct3D11CaptureFramePool 的职责是存储捕获帧，并可通过 CreateCaptureSession(GraphicsCaptureItem) 创建捕获会话；它也有 FrameArrived 事件和 TryGetNextFrame 获取帧。

工程结构

建议文件结构：

wgccli/
CMakeLists.txt
README.md
src/
main.cpp
cli_args.h
cli_args.cpp
window_enum.h
window_enum.cpp
capture_wgc.h
capture_wgc.cpp
d3d_helpers.h
d3d_helpers.cpp
image_wic.h
image_wic.cpp
output.h
output.cpp

模块职责
cli_args

负责：

- 解析命令行参数
- 校验互斥参数
- 生成 Options 结构体
- 打印 help/version


约束：

不要引入大型 CLI 框架。
可以手写 parser。

window_enum

负责：

- 枚举顶层窗口
- 解析 HWND 字符串
- 标题匹配和排序
- 输出候选窗口


核心结构：

struct WindowInfo {
HWND hwnd;
DWORD pid;
std::wstring title;
std::wstring className;
RECT rect;
bool visible;
bool minimized;
};

capture_wgc

负责：

- 用 HWND 创建 GraphicsCaptureItem
- 启动 WGC
- 获取单帧
- 返回 D3D11 texture 或 BGRA buffer


建议返回：

struct CapturedImage {
uint32_t width;
uint32_t height;
uint32_t stride;
std::vector<uint8_t> bgra;
};

d3d_helpers

负责：

- 创建 ID3D11Device
- 创建 WinRT IDirect3DDevice
- 从 IDirect3DSurface 获取 ID3D11Texture2D
- texture copy / staging / map

image_wic

负责：

- BGRA buffer 保存 PNG
- 自动创建输出目录
- 文件名生成

output

负责：

- plain 输出
- JSON 输出
- 错误格式化
- exit code 映射

CMake 要求

必须能在 CLion 中直接打开并构建。

CMakeLists.txt 要求：

- cmake_minimum_required >= 3.24
- C++17 或 C++20
- MSVC only，非 MSVC 直接报错
- 链接 d3d11 dxgi windowsapp windowscodecs runtimeobject
- 使用 Unicode


大致目标：

cmake_minimum_required(VERSION 3.24)
project(wgccli LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT MSVC)
message(FATAL_ERROR "wgccli requires MSVC on Windows")
endif()

add_executable(wgccli
src/main.cpp
src/cli_args.cpp
src/window_enum.cpp
src/capture_wgc.cpp
src/d3d_helpers.cpp
src/image_wic.cpp
src/output.cpp
)

target_compile_definitions(wgccli PRIVATE
UNICODE
_UNICODE
NOMINMAX
WIN32_LEAN_AND_MEAN
)

target_link_libraries(wgccli PRIVATE
d3d11
dxgi
windowsapp
windowscodecs
runtimeobject
)


如果 C++/WinRT 头文件不可用，要给出清晰报错，而不是一堆模板爆炸。

错误码要求

进程 exit code：

0  成功
1  通用错误
2  参数错误
3  窗口未找到
4  WGC 不可用或系统版本不支持
5  捕获失败
6  保存 PNG 失败
7  超时
8  权限/会话/桌面限制


错误输出 plain 模式：

ERROR_CODE=WINDOW_NOT_FOUND
ERROR_MESSAGE=No visible top-level window matched title: Android Studio


成功输出 plain 模式：

Matched window: ...
Handle: ...
Window size: ...
RESULT_SCREENSHOT_PATH=...

关键约束
不依赖前台焦点

这是最重要的约束。

工具必须按 HWND 直接捕获目标窗口，而不是“把窗口拉到前台再截 active window”。

ShareX 方案的问题是 active-window workflow 容易截成终端；这个工具的价值就是消除这类依赖。

不扫描磁盘找结果

这个工具自己保存文件，所以不需要像 ShareX 脚本那样扫描截图目录。

成功后直接打印实际保存路径：

RESULT_SCREENSHOT_PATH=...

不使用剪贴板

不要依赖剪贴板传路径，也不要污染用户剪贴板。

不弹 UI

默认不弹 picker、不弹文件选择器、不弹窗口。

如果 WGC 系统权限需要用户授权，错误信息要明确说明，但 CLI 工具本身不要引导复杂 UI。

不保证捕获最小化窗口

文档和 README 要写清楚：

Minimized windows may be listed but may not be capturable.
Restore the window first if capture fails.


微软的 Win32 HWND capture 示例也提示：最小化窗口可以被枚举，但不会被捕获。

输出对 AI 友好

必须稳定输出机器可读路径：

RESULT_SCREENSHOT_PATH=C:\...


不要只输出自然语言。

推荐开发步骤
阶段 1：项目骨架

任务：

1. 创建 CMake 项目
2. 创建 main.cpp
3. 实现 --help / --version
4. 实现 Options 结构
5. 实现 exit code


验收：

wgccli.exe --help
wgccli.exe --version

阶段 2：窗口枚举

任务：

1. EnumWindows 枚举窗口
2. 输出 --list 表格
3. 实现 HWND hex 格式化
4. 实现标题匹配


验收：

wgccli.exe --list
wgccli.exe --title "Notepad"
wgccli.exe --hwnd 0x...


此阶段可以先不截图，只打印 matched window。

阶段 3：D3D11 初始化

任务：

1. 创建 D3D11 device
2. 开启 BGRA support
3. 实现 D3D device helper
4. 实现 HRESULT 错误格式化


要求：

D3D11_CREATE_DEVICE_BGRA_SUPPORT 必须启用。

阶段 4：WGC 单帧捕获

任务：

1. HWND -> GraphicsCaptureItem
2. FramePool + Session
3. 等待首帧
4. 拿到 ID3D11Texture2D
5. Copy 到 staging
6. Map 到 CPU


验收：

能打印 captured width/height/stride。

阶段 5：WIC PNG 保存

任务：

1. 创建 IWICImagingFactory
2. PNG encoder
3. BGRA 写入
4. 自动创建目录
5. 生成文件名


文件名格式：

yyyyMMdd-HHmmss-<sanitized-window-title>.png


标题清理规则：

- 替换 \ / : * ? " < > | 为 -
- 最长 80 字符
- 空标题用 window

阶段 6：输出契约和 JSON

任务：

1. 成功输出 RESULT_SCREENSHOT_PATH
2. 失败输出 ERROR_CODE / ERROR_MESSAGE
3. JSON 模式输出结构化信息

测试用例

至少测试这些窗口：

1. Notepad / 记事本
2. Windows Terminal
3. Chrome / Edge
4. Android Studio / IntelliJ IDEA
5. 被其他窗口遮挡的窗口
6. 后台窗口
7. 最小化窗口
8. 高 DPI 缩放环境
9. 多显示器环境
10. 管理员权限窗口和普通权限 CLI 的组合


重点验收：

- 被其他窗口遮挡时，WGC 仍应捕获目标窗口内容，而不是遮挡物。
- 不应该截到终端自身。
- 不应该依赖当前 active window。
- 输出路径必须真实存在。

README 必须说明的限制

README 里必须写：

wgccli uses Windows.Graphics.Capture and captures by HWND.
It is designed for AI/automation workflows.
It does not use active-window screenshots.
It does not use clipboard.
It does not scan output folders.
Minimized windows may not be capturable.
Some protected windows, elevated windows, secure desktop, DRM/video surfaces, or windows from another session/desktop may fail.

给 AI 的实现提醒

请不要一次性写巨大 main.cpp。
请按模块拆分，优先做可运行 MVP。

优先级：

Correctness > simple code > debuggability > performance > fancy features


不要过度抽象。
不要引入 vcpkg 依赖。
不要引入 OpenCV。
不要引入第三方 PNG 库。
用 WIC 保存 PNG。

最终交付物
1. 完整 CMake 项目
2. 可在 CLion + MSVC toolchain 下构建
3. README.md
4. 示例命令
5. 简单 smoke test 脚本，可选


最终命令必须可用：

wgccli.exe --list

wgccli.exe --title "Notepad" --out "$env:USERPROFILE\Pictures\ClaudeScreenshots"

wgccli.exe --hwnd 0x0000000000123456 --out "$env:USERPROFILE\Pictures\ClaudeScreensh