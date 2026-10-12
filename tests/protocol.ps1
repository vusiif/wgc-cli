param([Parameter(Mandatory)][string]$Executable)
$ErrorActionPreference = 'Stop'

function New-CliProcess([string[]]$CliArgs) {
    $info = [System.Diagnostics.ProcessStartInfo]::new($Executable)
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($arg in $CliArgs) { $info.ArgumentList.Add($arg) }
    return [System.Diagnostics.Process]::Start($info)
}

function Invoke-Cli([string[]]$CliArgs, [string[]]$InputLines = @()) {
    $process = New-CliProcess $CliArgs
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        foreach ($line in $InputLines) { $process.StandardInput.WriteLine($line) }
        $process.StandardInput.Close()
        if (!$process.WaitForExit(10000)) { throw 'CLI timed out' }
        if ($process.ExitCode -ne 0) { throw $stderr.GetAwaiter().GetResult() }
        return $stdout.GetAwaiter().GetResult()
    } finally {
        if (!$process.HasExited) { $process.Kill() }
        $process.Dispose()
    }
}

$responses = Invoke-Cli @('--mcp-stdio') @(
    '{"jsonrpc":"2.0","id":1,"method" : "initialize","params":{}}',
    '{"jsonrpc":"2.0","id":2,"method" : "tools/list","params":{}}',
    '{"jsonrpc":"2.0","id":3,"method" : "tools/call","params" : {"name" : "capture_window","arguments" : {"out":"unused","pid":4294967296}}}'
)
$messages = @($responses -split '\r?\n' | Where-Object { $_ } | ForEach-Object { $_ | ConvertFrom-Json })
if ($messages.Count -ne 3 -or !$messages[0].result.serverInfo -or
    $messages[1].result.tools.Count -ne 2 -or !$messages[2].result.isError) {
    throw 'MCP protocol regression'
}

$idResponses = Invoke-Cli @('--mcp-stdio') @(
    '{"jsonrpc":"2.0","id":"str-1","method":"ping"}',
    '{"jsonrpc":"2.0","id":1e2,"method":"ping"}',
    '{"jsonrpc":"2.0","id":"a\"b","method":"ping"}',
    '{"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"AB\u0043","arguments":{}}}'
)
$idLines = @($idResponses -split '\r?\n' | Where-Object { $_ })
$idLines | ForEach-Object { Write-Output ('idprobe ' + $_) }
$idMessages = @($idLines | ForEach-Object { $_ | ConvertFrom-Json })
if ($idMessages.Count -ne 4 -or $idMessages[0].id -cne 'str-1' -or
    [double]($idMessages[1].id) -ne 100 -or $idMessages[2].id -cne 'a"b' -or
    $idMessages[3].error.message -ne 'Unknown tool: ABC') {
    throw 'JSON-RPC id or unicode escape regression'
}

$pipeName = 'wgccli-regression-' + [guid]::NewGuid().ToString('N')
$server = New-CliProcess @('--server', '--pipe', $pipeName)
try {
    # Connect once to wait for readiness, without requesting desktop data.
    $probe = [System.IO.Pipes.NamedPipeClientStream]::new('.', $pipeName, [System.IO.Pipes.PipeDirection]::InOut)
    try { $probe.Connect(5000) } finally { $probe.Dispose() }
    $ping = Invoke-Cli @('--pipe', $pipeName, '--client', '{"action" : "ping"}') | ConvertFrom-Json
    if (!$ping.ok -or !$ping.server.version) { throw 'Pipe whitespace regression' }
    $invalid = Invoke-Cli @('--pipe', $pipeName, '--client', '{"action":"capture","pid":4294967296,"out":"unused"}') | ConvertFrom-Json
    if ($invalid.ok -or $invalid.errorCode -ne 'BAD_ARGS') { throw 'Pipe overflow regression' }
} finally {
    if (!$server.HasExited) { $server.Kill() }
    $server.Dispose()
}
Write-Output 'MCP and named pipe protocol PASS (no screenshots)'
