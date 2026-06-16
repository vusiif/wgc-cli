#pragma once

#include <string>

// Start a named pipe server in the user's interactive session.
// The agent connects via: wgccli.exe --client <request-json>
// Returns 0 on clean shutdown.
int run_server(const std::wstring& pipe_name);
