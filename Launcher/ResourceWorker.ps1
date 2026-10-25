param(
    [Parameter(Mandatory=$true)][string]$Target,
    [Parameter(Mandatory=$true)][string]$Work,
    [Parameter(Mandatory=$true)][string]$ArchiveDirectory,
    [string]$PreviousArchiveDirectory = '',
    [Parameter(Mandatory=$true)][int]$ParentId,
    [Parameter(Mandatory=$true)][long]$ParentStart
)
$ErrorActionPreference = 'Stop'
try {
    Add-Type -Path (Join-Path $Work 'ResourceWorker.cs') -ReferencedAssemblies System.dll,System.Core.dll,System.IO.Compression.dll,System.IO.Compression.FileSystem.dll
    exit [SDKResourceWorker]::Run($Target, $Work, $ArchiveDirectory, $PreviousArchiveDirectory, $ParentId, $ParentStart)
} catch {
    [IO.File]::WriteAllText((Join-Path $Work 'worker-error.txt'), $_.Exception.ToString())
    exit 1
}
