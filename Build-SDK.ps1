[CmdletBinding()]
param(
    [ValidateSet('All','Launcher','LauncherAssets','SDKRuntime','LevelEditor','ActorEditor','ParticleEditor','ShaderEditor',
        'XrAPI','XrCore','XrCDB','XrCPU_Pipe','XrParticles','XrPhysics','XrSound','XrXMLParser',
        'XrECore','XrEProps','XrEUI','XrETools','XrDXT','XrSE_Factory','Luabind')]
    [string]$Module = 'All',
    [ValidateSet('Debug','Development','Release')][string]$Configuration = 'Release',
    [ValidateSet('Win32','x64')][string]$Platform = 'x64',
    [string]$Destination,
    [switch]$CopyOnly,
    [switch]$Plan
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'SDKRuntime\BuildSupport.psm1') -Force
$projects = @{
    All = 'Launcher\Launcher.vcxproj'; Launcher = 'Launcher\Launcher.vcxproj'
    LauncherAssets = 'LauncherAssets\LauncherAssets.vcxproj'; SDKRuntime = 'SDKRuntime\SDKRuntime.vcxproj'
    LevelEditor = 'Editors\LevelEditor\LevelEditor.vcxproj'; ActorEditor = 'Editors\ActorEditor\ActorEditor.vcxproj'
    ParticleEditor = 'Editors\ParticleEditor\ParticleEditor.vcxproj'; ShaderEditor = 'Editors\ShaderEditor\ShaderEditor.vcxproj'
}
# Shared implementation archives must also be relinked into SDKRuntime.dll.
foreach ($part in @('XrAPI','XrCore','XrCDB','XrCPU_Pipe','XrParticles','XrPhysics','XrSound','XrXMLParser',
    'XrECore','XrEProps','XrEUI','XrETools','XrDXT','XrSE_Factory','Luabind')) {
    $projects[$part] = 'SDKRuntime\SDKRuntime.vcxproj'
}
$output = Join-Path $PSScriptRoot "BuildedSDK\${Platform}_${Configuration}"
if ($CopyOnly -and !$Destination) { throw '-CopyOnly requires -Destination.' }
if (!$CopyOnly) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (!(Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer/vswhere.exe is missing.' }
    $msbuild = @(& $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe') | Select-Object -First 1
    if (!$msbuild) { throw 'MSBuild was not found.' }
    $arguments = @((Join-Path $PSScriptRoot $projects[$Module]), '/t:Build', '/m', '/nologo', '/v:minimal',
        "/p:Configuration=$Configuration", "/p:Platform=$Platform", "/p:SolutionDir=$PSScriptRoot\")
    if ($Plan) { Write-Host "$msbuild $($arguments -join ' ')" }
    else {
        & $msbuild @arguments
        if ($LASTEXITCODE -ne 0) { throw "SDK build failed ($LASTEXITCODE). Package was not copied." }
    }
}
if ($Destination) {
    $destinationPath = [IO.Path]::GetFullPath($Destination)
    if ($Plan) { Write-Host "Sync package: $output -> $destinationPath (changed files only)"; return }
    $required = @('Launcher.exe','LauncherAssets.dll','SDKRuntime.dll','LevelEditor.dll','ActorEditor.dll',
        'ParticleEditor.dll','ShaderEditor.dll','BugTrap.dll','OpenAL.dll','LuaJIT.dll','FreeImage.dll','nvtt.dll')
    if ($Platform -eq 'x64') { $required += 'discord-rpc.dll' }
    # Validate the complete package before modifying the destination.
    foreach ($name in $required) {
        if (!(Test-Path -LiteralPath (Join-Path $output $name) -PathType Leaf)) {
            throw "Missing package file: $name. Build All for $Platform/$Configuration first."
        }
    }
    $names = @($required)
    foreach ($name in $required) {
        $pdb = [IO.Path]::ChangeExtension($name, '.pdb')
        if (Test-Path -LiteralPath (Join-Path $output $pdb)) { $names += $pdb }
    }
    $names += @(Get-ChildItem -LiteralPath $output -File | Where-Object {
        $_.Name -match '^(msvcp|vcruntime|concrt|vcomp).*\.dll$'
    } | ForEach-Object { $_.Name })
    $changed = 0
    foreach ($name in ($names | Sort-Object -Unique)) {
        if (Copy-SdkFile -Source (Join-Path $output $name) -Destination (Join-Path $destinationPath $name)) {
            Write-Host "Updated: $name"; ++$changed
        }
    }
    Write-Host "Package synchronized: $changed file(s) updated."
}
