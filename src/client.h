#pragma once

#include <string>

// Send a JSON request to the wgccli server via named pipe and print the response.
// Returns 0 on success, non-zero on error.
int run_client(const std::wstring& pipe_name, const std::wstring& request_json);
