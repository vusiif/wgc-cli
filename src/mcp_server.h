#pragma once

#include <string>

// Run MCP stdio server (JSON-RPC 2.0 over stdin/stdout).
// Blocks until stdin closes or a fatal error occurs.
int run_mcp_server();
