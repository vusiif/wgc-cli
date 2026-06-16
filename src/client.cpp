#include "client.h"

#include <windows.h>
#include <iostream>
#include <string>

int run_client(const std::wstring& pipe_name, const std::wstring& request_json) {
    std::wstring full_pipe = L"\\\\.\\pipe\\" + pipe_name;

    HANDLE hPipe = CreateFileW(
        full_pipe.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, 0, nullptr);

    if (hPipe == INVALID_HANDLE_VALUE) {
        std::wcerr << L"[error] Cannot connect to pipe " << full_pipe
                   << L" (error " << GetLastError() << L"). Is wgccli --server running?\n";
        return 1;
    }

    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(hPipe, &mode, nullptr, nullptr);

    // Send request
    DWORD bytes_written = 0;
    if (!WriteFile(hPipe, request_json.c_str(),
                   static_cast<DWORD>(request_json.size() * sizeof(wchar_t)),
                   &bytes_written, nullptr)) {
        std::wcerr << L"[error] WriteFile failed: " << GetLastError() << L"\n";
        CloseHandle(hPipe);
        return 1;
    }

    FlushFileBuffers(hPipe);

    // Read response
    wchar_t buf[65536]{};
    DWORD bytes_read = 0;
    if (!ReadFile(hPipe, buf, sizeof(buf) - sizeof(wchar_t), &bytes_read, nullptr) || bytes_read == 0) {
        std::wcerr << L"[error] ReadFile failed: " << GetLastError() << L"\n";
        CloseHandle(hPipe);
        return 1;
    }

    std::wstring response(buf, bytes_read / sizeof(wchar_t));
    // Write response to stdout
    // Note: WriteConsoleW doesn't work when stdout is redirected,
    // so we use wcout. The server escapes all control characters in JSON.
    std::wcout << response << L"\n";

    CloseHandle(hPipe);
    return 0;
}
