param([Parameter(Mandatory)][string]$Executable)
$ErrorActionPreference = 'Stop'

function New-CliProcess([string[]]$CliArgs) {
    $info = [System.Diagnostics.ProcessStartInfo]::new($Executable)
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    # The server speaks UTF-8; .NET would otherwise decode with the console
    # code page and mangle non-ASCII responses.
    $info.StandardOutputEncoding = [System.Text.Encoding]::UTF8
    $info.StandardErrorEncoding = [System.Text.Encoding]::UTF8
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

function Get-McpMessages([string]$Raw) {
    return @($Raw -split '\r?\n' | Where-Object { $_ } | ForEach-Object { $_ | ConvertFrom-Json })
}

function Invoke-McpRaw([byte[]]$InputBytes) {
    $process = New-CliProcess @('--mcp-stdio')
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $process.StandardInput.BaseStream.Write($InputBytes, 0, $InputBytes.Length)
        $process.StandardInput.BaseStream.Flush()
        $process.StandardInput.Close()
        if (!$process.WaitForExit(10000)) { throw 'CLI timed out' }
        if ($process.ExitCode -ne 0) { throw $stderr.GetAwaiter().GetResult() }
        return $stdout.GetAwaiter().GetResult()
    } finally {
        if (!$process.HasExited) { $process.Kill() }
        $process.Dispose()
    }
}

function Assert-McpError($msg, [int]$code, [string]$label) {
    $names = @($msg.PSObject.Properties.Name)
    if ($names -notcontains 'error' -or $names -contains 'result') {
        throw "$label : expected error-only envelope, got: $msg"
    }
    if ([int]$msg.error.code -ne $code) {
        throw "$label : expected code $code, got $($msg.error.code)"
    }
    if ($names -notcontains 'id' -or $null -ne $msg.id) {
        throw "$label : expected id null, got $($msg.id)"
    }
}

function Assert-McpResult($msg, [string]$label) {
    $names = @($msg.PSObject.Properties.Name)
    if ($names -notcontains 'result' -or $names -contains 'error') {
        throw "$label : expected result-only envelope, got: $msg"
    }
}

$responses = Invoke-Cli @('--mcp-stdio') @(
    '{"jsonrpc":"2.0","id":1,"method" : "initialize","params":{}}',
    '{"jsonrpc":"2.0","id":2,"method" : "tools/list","params":{}}',
    '{"jsonrpc":"2.0","id":3,"method" : "tools/call","params" : {"name" : "capture_window","arguments" : {"out":"unused","pid":4294967296}}}'
)
$messages = Get-McpMessages $responses
if ($messages.Count -ne 3 -or !$messages[0].result.serverInfo -or
    $messages[1].result.tools.Count -ne 2 -or !$messages[2].result.isError) {
    throw 'MCP protocol regression'
}

$rawTabId = '{"jsonrpc":"2.0","id":"a' + "`t" + 'b","method":"ping"}'
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

# A raw control character inside the id string makes the payload malformed JSON,
# so it must be reported as a parse error with id null instead of a reply id.
$rawTabMessages = Get-McpMessages (Invoke-Cli @('--mcp-stdio') @($rawTabId))
if ($rawTabMessages.Count -ne 1) { throw 'raw control id: expected exactly one response' }
Assert-McpError $rawTabMessages[0] -32700 'raw control id'

# Malformed JSON -> -32700 id:null; valid-JSON invalid envelopes -> -32600 id:null.
$parseCases = @(
    '{',
    'not json at all',
    '{"jsonrpc":"2.0","method":"ping"',
    '{"jsonrpc":"2.0",}',
    '{"jsonrpc":"2.0","method":ping}',
    '{"jsonrpc":"2.0","id":1,"method":"ping","extra"}'
)
$invalidCases = @(
    '[1,2,3]',
    '"just a string"',
    '{"id":1,"method":"ping"}',
    '{"jsonrpc":"1.0","id":1,"method":"ping"}',
    '{"jsonrpc":2.0,"id":1,"method":"ping"}',
    '{"jsonrpc":"2.0","id":1}',
    '{"jsonrpc":"2.0","id":1,"method":9}',
    '{"jsonrpc":"2.0","id":true,"method":"ping"}',
    '{"jsonrpc":"2.0","id":[1,2],"method":"ping"}',
    '{"jsonrpc":"2.0","id":{"a":1},"method":"ping"}',
    '{"jsonrpc":"2.0","id":1,"method":"ping","params":"x"}',
    '{"jsonrpc":"2.0","id":1,"method":"ping","params":42}',
    '{"jsonrpc":"2.0","id":1,"method":"ping","params":[1]}',
    '{"jsonrpc":"2.0","id":1,"method":"ping","params":true}',
    '{"jsonrpc":"2.0","id":1,"method":"ping","params":null}'
)
$badLines = $parseCases + $invalidCases
$badMessages = Get-McpMessages (Invoke-Cli @('--mcp-stdio') $badLines)
if ($badMessages.Count -ne $badLines.Count) {
    throw "malformed/invalid batch: expected $($badLines.Count) responses, got $($badMessages.Count)"
}
for ($i = 0; $i -lt $parseCases.Count; $i++) {
    Assert-McpError $badMessages[$i] -32700 "parse[$i]"
}
for ($i = $parseCases.Count; $i -lt $badMessages.Count; $i++) {
    Assert-McpError $badMessages[$i] -32600 "invalid[$($i - $parseCases.Count)]"
}

# Valid notifications (missing id) must produce no output and no tool execution.
$ghostBase = ([System.IO.Path]::GetTempPath().Replace('\', '/') +
    'wgccli-ghost-' + [guid]::NewGuid().ToString('N'))
$ghostTitle = 'wgccli-ghost-' + [guid]::NewGuid().ToString('N')
$notifyLines = @(
    '{"jsonrpc":"2.0","method":"totally/unknown"}',
    '{"jsonrpc":"2.0","method":"notifications/initialized"}',
    '{"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":7}}',
    '{"jsonrpc":"2.0","method":"tools/list"}',
    ('{"jsonrpc":"2.0","method":"tools/call","params":{"name":"capture_window","arguments":' +
        '{"out":"' + $ghostBase + '","title":"' + $ghostTitle + '"}}}')
)
$notifyRaw = Invoke-Cli @('--mcp-stdio') $notifyLines
if ("$notifyRaw".Trim().Length -ne 0) {
    throw "notifications must produce no output, got: $notifyRaw"
}
if (Test-Path $ghostBase) { throw 'notification tools/call must not execute capture' }

# Invalid payloads must never dispatch capture/list: only error envelopes come back.
$ghostInvalid = ('{"id":11,"method":"tools/call","params":{"name":"capture_window","arguments":' +
    '{"out":"' + $ghostBase + '2","title":"' + $ghostTitle + '"}}}')
$ghostTruncated = '{"jsonrpc":"2.0","id":12,"method":"tools/call","params":{"name":"capture_wi'
$ghostMessages = Get-McpMessages (Invoke-Cli @('--mcp-stdio') @($ghostInvalid, $ghostTruncated))
if ($ghostMessages.Count -ne 2) {
    throw "invalid tools/call: expected 2 responses, got $($ghostMessages.Count)"
}
Assert-McpError $ghostMessages[0] -32600 'invalid tools/call'
Assert-McpError $ghostMessages[1] -32700 'truncated tools/call'
if ((Test-Path ($ghostBase + '2')) -or (Test-Path ($ghostBase + '3'))) {
    throw 'invalid tools/call payloads must not execute capture'
}

# Sequence: malformed, invalid, notification, then valid requests still work.
$seqMessages = Get-McpMessages (Invoke-Cli @('--mcp-stdio') @(
    '{"bad',
    '{"jsonrpc":"1.0","id":9,"method":"ping"}',
    '{"jsonrpc":"2.0","method":"noise"}',
    '{"jsonrpc":"2.0","id":null,"method":"ping"}',
    '{"jsonrpc":"2.0","id":"end","method":"ping"}'
))
if ($seqMessages.Count -ne 4) { throw "sequence: expected 4 responses, got $($seqMessages.Count)" }
Assert-McpError $seqMessages[0] -32700 'sequence parse'
Assert-McpError $seqMessages[1] -32600 'sequence invalid'
Assert-McpResult $seqMessages[2] 'sequence null id'
$nullIdNames = @($seqMessages[2].PSObject.Properties.Name)
if ($nullIdNames -notcontains 'id' -or $null -ne $seqMessages[2].id) {
    throw 'request with id null must get a response carrying id null'
}
Assert-McpResult $seqMessages[3] 'sequence final'
if ($seqMessages[3].id -cne 'end') { throw 'sequence final: id not preserved' }

# Unknown method keeps -32601 with the original id; bad tool params keep -32602.
$unknownMessages = Get-McpMessages (Invoke-Cli @('--mcp-stdio') @(
    '{"jsonrpc":"2.0","id":"u1","method":"no/such/method"}'
))
if ($unknownMessages.Count -ne 1) { throw 'unknown method: expected exactly one response' }
$unknownNames = @($unknownMessages[0].PSObject.Properties.Name)
if ($unknownNames -notcontains 'error' -or [int]$unknownMessages[0].error.code -ne -32601 -or
    $unknownMessages[0].id -cne 'u1') {
    throw 'unknown method must return -32601 with the original id'
}

# Notification status depends only on a missing id: a request whose method
# merely looks like a notification still gets -32601 preserving its id.
$withIdNotify = Get-McpMessages (Invoke-Cli @('--mcp-stdio') @(
    '{"jsonrpc":"2.0","id":5,"method":"notifications/unknown"}'
))
if ($withIdNotify.Count -ne 1) {
    throw "with-id notifications/unknown: expected 1 response, got $($withIdNotify.Count)"
}
$withIdNames = @($withIdNotify[0].PSObject.Properties.Name)
if ($withIdNames -notcontains 'error' -or $withIdNames -contains 'result' -or
    [int]$withIdNotify[0].error.code -ne -32601 -or $withIdNotify[0].id -ne 5) {
    throw 'with-id notifications/unknown must return -32601 with id 5'
}

$badParamLines = @(
    '{"jsonrpc":"2.0","id":55,"method":"tools/call","params":{}}',
    '{"jsonrpc":"2.0","id":56,"method":"tools/call"}',
    '{"jsonrpc":"2.0","id":57,"method":"tools/call","params":{"name":123,"arguments":{}}}',
    '{"jsonrpc":"2.0","id":58,"method":"tools/call","params":{"name":"list_windows","arguments":"str"}}'
)
$badParamMessages = Get-McpMessages (Invoke-Cli @('--mcp-stdio') $badParamLines)
if ($badParamMessages.Count -ne $badParamLines.Count) {
    throw "invalid params: expected $($badParamLines.Count) responses, got $($badParamMessages.Count)"
}
$expectedParamIds = @(55, 56, 57, 58)
for ($i = 0; $i -lt $badParamMessages.Count; $i++) {
    $names = @($badParamMessages[$i].PSObject.Properties.Name)
    if ($names -notcontains 'error' -or $names -contains 'result') {
        throw "invalid params[$i]: expected error-only envelope"
    }
    if ([int]$badParamMessages[$i].error.code -ne -32602) {
        throw "invalid params[$i]: expected -32602, got $($badParamMessages[$i].error.code)"
    }
    if ($badParamMessages[$i].id -ne $expectedParamIds[$i]) {
        throw "invalid params[$i]: id not preserved"
    }
}

# Nesting boundary: deep-but-bounded payloads are answered, deeper ones are
# rejected as parse errors without crashing; oversized lines are bounded too.
$deepOk = '{"jsonrpc":"2.0","id":7,"method":"ping","params":' +
    ('{"a":' * 50) + '1' + ('}' * 50) + '}'
$deepBad = '{"jsonrpc":"2.0","id":8,"method":"ping","params":' +
    ('[' * 500) + (']' * 500) + '}'
$oversized = '{"jsonrpc":"2.0","id":99,"method":"ping","pad":"' + ('x' * 1048700) + '"}'
$deepMessages = Get-McpMessages (Invoke-Cli @('--mcp-stdio') @($deepOk, $deepBad, $oversized))
if ($deepMessages.Count -ne 3) { throw "nesting boundary: expected 3 responses, got $($deepMessages.Count)" }
Assert-McpResult $deepMessages[0] 'deep-but-bounded nesting'
Assert-McpError $deepMessages[1] -32700 'deep nesting overflow'
Assert-McpError $deepMessages[2] -32700 'oversized line'

# Strict UTF-8 at the read boundary: invalid byte sequences anywhere in the
# payload (even irrelevant fields) yield -32700 before dispatch, the stream
# recovers afterwards, and valid non-ASCII input is preserved faithfully.
$ascii = [System.Text.Encoding]::ASCII
$utf8Encoding = [System.Text.Encoding]::UTF8
$rawBytes = [System.Collections.Generic.List[byte]]::new()
function Add-RawLine([byte[]]$payload) {
    $script:rawBytes.AddRange($payload)
    $script:rawBytes.Add(0x0A)
}
function Add-JsonWithRawBytes([string]$prefix, [byte[]]$bad, [string]$suffix) {
    $buf = [System.Collections.Generic.List[byte]]::new()
    $buf.AddRange($ascii.GetBytes($prefix))
    foreach ($b in $bad) { $buf.Add($b) }
    $buf.AddRange($ascii.GetBytes($suffix))
    Add-RawLine $buf.ToArray()
}
$wrapPrefix = '{"jsonrpc":"2.0","id":'
$padPrefix = ',"method":"ping","pad":"'
foreach ($badCase in @(
    @(30, [byte[]](0xFF)),          # invalid single byte
    @(31, [byte[]](0xC0, 0x80)),    # overlong encoding of NUL
    @(32, [byte[]](0x80)),          # stray continuation byte
    @(33, [byte[]](0xE2, 0x82)))) { # truncated multi-byte sequence
    Add-JsonWithRawBytes ($wrapPrefix + $badCase[0] + $padPrefix) $badCase[1] '"}'
}
$nonAsciiId = 'h' + [char]0x00E9 + 'llo-' + (-join [char[]](0xD83D, 0xDE00))
$nonAsciiMethod = 'no/' + [char]0x00FC + 'nicode'
Add-RawLine $utf8Encoding.GetBytes('{"jsonrpc":"2.0","id":"' + $nonAsciiId + '","method":"ping"}')
Add-RawLine $utf8Encoding.GetBytes('{"jsonrpc":"2.0","id":34,"method":"' + $nonAsciiMethod + '"}')
$rawMessages = Get-McpMessages (Invoke-McpRaw $rawBytes.ToArray())
if ($rawMessages.Count -ne 6) {
    throw "utf8 boundary: expected 6 responses, got $($rawMessages.Count)"
}
for ($i = 0; $i -lt 4; $i++) { Assert-McpError $rawMessages[$i] -32700 "utf8[$i]" }
Assert-McpResult $rawMessages[4] 'utf8 non-ascii id'
if ($rawMessages[4].id -cne $nonAsciiId) { throw 'utf8 non-ascii id: id not preserved' }
$nonAsciiNames = @($rawMessages[5].PSObject.Properties.Name)
if ($nonAsciiNames -notcontains 'error' -or $nonAsciiNames -contains 'result' -or
    [int]$rawMessages[5].error.code -ne -32601 -or $rawMessages[5].id -ne 34 -or
    $rawMessages[5].error.message -cne ('Method not found: ' + $nonAsciiMethod)) {
    throw 'utf8 non-ascii method: -32601 must preserve id and message'
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
