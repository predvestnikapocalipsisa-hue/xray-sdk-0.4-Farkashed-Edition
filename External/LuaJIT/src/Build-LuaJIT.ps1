[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SolutionRoot,
    [ValidateSet('Win32','x64')][string]$Platform = 'x64',
    [ValidateSet('Debug','Development','Release')][string]$Configuration = 'Release',
    [switch]$Force,
    [switch]$Clean,
    [switch]$SyncOnly
)
$ErrorActionPreference = 'Stop'
$SolutionRoot = [IO.Path]::GetFullPath($SolutionRoot)
Import-Module (Join-Path $SolutionRoot 'SDKRuntime\BuildSupport.psm1') -Force
$architecture = if ($Platform -eq 'Win32') { 'x86' } else { 'x64' }
$binaryDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\bin\$architecture"))
$cache = Join-Path $binaryDirectory 'sdk-inputs.txt'
$required = @('LuaJIT.dll','LuaJIT.lib')
if ($Clean) {
    # Only this architecture's known build products; do not delete the shared bin tree.
    foreach ($name in @('LuaJIT.dll','LuaJIT.lib','LuaJIT.exp','LuaJIT.pdb','Lua_JIT.exe','sdk-inputs.txt')) {
        $path = Join-Path $binaryDirectory $name
        if (Test-Path -LiteralPath $path -PathType Leaf) { Remove-Item -LiteralPath $path -Force }
    }
    return
}
if (!$SyncOnly) {
    $inputs = @(Get-ChildItem -LiteralPath $PSScriptRoot -Recurse -File | Where-Object {
        $_.Extension -in @('.c','.h','.dasc','.lua','.bat','.ps1','.vcxproj') -and
        $_.Name -notin @('buildvm_arch.h','lj_bcdef.h','lj_ffdef.h','lj_libdef.h','lj_recdef.h','lj_folddef.h','vmdef.lua')
    })
    $inputs += @(Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot '..\dynasm') -Recurse -File)
    $inputs += Get-Item -LiteralPath (Join-Path $SolutionRoot 'SDKRuntime\BuildSupport.psm1')
    $signature = ($inputs | Sort-Object FullName | ForEach-Object {
        "$($_.FullName)|$((Get-FileHash -LiteralPath $_.FullName).Hash)"
    }) -join "`n"
    $signature = "$architecture|$env:VCToolsVersion|$env:VCToolsInstallDir`n$signature"
    $ready = @($required | Where-Object { !(Test-Path -LiteralPath (Join-Path $binaryDirectory $_)) }).Count -eq 0
    $cached = if (Test-Path -LiteralPath $cache) { [IO.File]::ReadAllText($cache) } else { '' }
    if ($Force -or !$ready -or $cached -ne $signature) {
        Write-Host "LuaJIT: building changed inputs ($architecture)."
        $started = [DateTime]::UtcNow
        Push-Location $PSScriptRoot
        try {
            & .\msvcbuild.bat $architecture
            if ($LASTEXITCODE -ne 0) { throw "LuaJIT build failed ($LASTEXITCODE)." }
        } finally { Pop-Location }
        foreach ($name in $required) {
            $file = Get-Item -LiteralPath (Join-Path $binaryDirectory $name) -ErrorAction Stop
            if ($file.Length -eq 0) { throw "LuaJIT produced an empty $name." }
            # LINK may preserve an unchanged import library even after successfully relinking the DLL.
            # Require a refreshed DLL, but only existence and nonzero size for the import library.
            if ($name -eq 'LuaJIT.dll' -and $file.LastWriteTimeUtc -lt $started.AddSeconds(-2)) {
                throw "LuaJIT did not refresh $name."
            }
        }
        [IO.File]::WriteAllText($cache, $signature)
    } else { Write-Host 'LuaJIT: inputs unchanged, reusing cached DLL and import library.' }
}
foreach ($name in $required) {
    if (!(Test-Path -LiteralPath (Join-Path $binaryDirectory $name))) { throw "Missing LuaJIT output: $name" }
}
$output = Join-Path $SolutionRoot "BuildedSDK\${Platform}_${Configuration}"
$libraries = Join-Path $SolutionRoot "output\libraries_${Platform}_${Configuration}"
foreach ($name in @('LuaJIT.dll','LuaJIT.pdb','LuaJIT.lib','LuaJIT.exp')) {
    $source = Join-Path $binaryDirectory $name
    if (!(Test-Path -LiteralPath $source)) { continue }
    $folder = if ([IO.Path]::GetExtension($name) -in @('.lib','.exp')) { $libraries } else { $output }
    if (Copy-SdkFile -Source $source -Destination (Join-Path $folder $name)) { Write-Host "LuaJIT: updated $name" }
}
