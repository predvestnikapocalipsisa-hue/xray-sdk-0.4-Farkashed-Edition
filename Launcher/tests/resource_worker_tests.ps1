# Offline checks only. Does not call Run(), contact Google, or touch SDK resources.
# Run with Windows PowerShell 5.1 (the same runtime used by the launcher).
$ErrorActionPreference = 'Stop'
Add-Type -Path (Join-Path $PSScriptRoot '../ResourceWorker.cs') -ReferencedAssemblies System.dll,System.Core.dll,System.IO.Compression.dll,System.IO.Compression.FileSystem.dll
function Assert($condition, [string]$message) { if (-not $condition) { throw $message } }
function Reject([scriptblock]$action, [string]$message) {
    $rejected = $false
    try { & $action | Out-Null } catch { $rejected = $true }
    Assert $rejected $message
}
function Fixture([string[]]$names) {
    $memory = New-Object IO.MemoryStream
    $zip = New-Object IO.Compression.ZipArchive($memory, [IO.Compression.ZipArchiveMode]::Create, $true)
    try {
        foreach ($name in $names) {
            $entry = $zip.CreateEntry($name)
            if (-not $name.EndsWith('/')) {
                $stream = $entry.Open()
                try { $bytes = [Text.Encoding]::UTF8.GetBytes('123456789'); $stream.Write($bytes, 0, $bytes.Length) }
                finally { $stream.Dispose() }
            }
        }
    } finally { $zip.Dispose() }
    $memory.Position = 0
    return ,$memory
}
function Inspect([IO.Stream]$stream) {
    $crcs = [SDKResourceWorker]::ReadCrcs($stream)
    $stream.Position = 0
    $zip = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Read, $true)
    try { return ,([SDKResourceWorker]::Plan($zip, $crcs)) }
    finally { $zip.Dispose() }
}

Assert ([SDKResourceWorker]::ValidateResume(206, 'bytes 100-999/1000', 900, 100, 1000)) 'valid range'
Assert (-not [SDKResourceWorker]::ValidateResume(200, '', 1000, 100, 1000)) 'full response must restart'
Reject { [SDKResourceWorker]::ValidateResume(206, 'bytes 0-999/1000', 1000, 100, 1000) } 'wrong offset accepted'
Reject { [SDKResourceWorker]::ValidateResume(206, 'bytes 100-999/2000', 900, 100, 1000) } 'wrong total accepted'
Reject { [SDKResourceWorker]::ValidateResume(206, 'bytes 100-999/1000', 899, 100, 1000) } 'wrong body size accepted'
Reject { [SDKResourceWorker]::ValidateResume(403, '', 0, 100, 1000) } 'error body accepted'
foreach ($unsafe in @('../evil', '/evil', 'C:/evil', 'gamedata/../evil', 'gamedata/file:stream', 'settings/CON.ini', 'settings/name.', 'settings//file')) {
    Reject { [SDKResourceWorker]::SafeName($unsafe) } "unsafe path accepted: $unsafe"
}

$fixture = Fixture @('xray sdk 4.8/settings/system.ltx', 'xray sdk 4.8/gamedata/textures/test.dds', 'xray sdk 4.8/rawdata/object', 'xray sdk 4.8/bins/Launcher.exe', 'xray sdk 4.8/.git/config')
try {
    $plan = Inspect $fixture
    Assert ($plan.Count -eq 3) 'binary or Git files selected'
    Assert ($plan[0].Path -eq 'settings/system.ltx') 'wrapper directory was not stripped'
    Assert ($plan[0].Crc -eq [uint32]3421780262) 'CRC directory parsing failed'
} finally { $fixture.Dispose() }
$fixture = Fixture @('settings/system.ltx', 'gamedata/file')
try { Assert ((Inspect $fixture).Count -eq 2) 'root-level resources unsupported' } finally { $fixture.Dispose() }
$fixture = Fixture @('settings/system.ltx', 'gamedata/file', 'GAMEDATA/FILE')
try { Reject { Inspect $fixture } 'case-insensitive duplicate accepted' } finally { $fixture.Dispose() }
$fixture = Fixture @('settings/system.ltx', 'bins/Launcher.exe')
try { Reject { Inspect $fixture } 'missing gamedata accepted' } finally { $fixture.Dispose() }

# Minimal ZIP64 footer: exercises 64-bit central directory lookup without large files.
$zip64 = New-Object IO.MemoryStream
$writer = New-Object IO.BinaryWriter($zip64)
try {
    $writer.Write([uint32]0x06064b50); $writer.Write([uint64]44)
    $writer.Write([uint16]45); $writer.Write([uint16]45)
    $writer.Write([uint32]0); $writer.Write([uint32]0)
    $writer.Write([uint64]0); $writer.Write([uint64]0); $writer.Write([uint64]0); $writer.Write([uint64]0)
    $writer.Write([uint32]0x07064b50); $writer.Write([uint32]0); $writer.Write([uint64]0); $writer.Write([uint32]1)
    $writer.Write([uint32]0x06054b50); $writer.Write([uint16]0); $writer.Write([uint16]0)
    $writer.Write([uint16]65535); $writer.Write([uint16]65535)
    $writer.Write([uint32]::MaxValue); $writer.Write([uint32]::MaxValue); $writer.Write([uint16]0)
    $writer.Flush(); $zip64.Position = 0
    Assert (([SDKResourceWorker]::ReadCrcs($zip64)).Count -eq 0) 'ZIP64 directory lookup failed'
} finally { $writer.Dispose() }
Write-Output 'PASS: ranges, safe paths, resource selection, wrapper roots, duplicates, CRC and ZIP64'
