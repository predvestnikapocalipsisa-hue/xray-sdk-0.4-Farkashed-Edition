$ErrorActionPreference = 'Stop'
$worker = Join-Path $PSScriptRoot '..\..\..\Launcher\UpdateWorker.ps1'
$parseErrors = $null
$tokens = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($worker, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw ($parseErrors | Out-String) }
# Load only pure package-validation helpers; never run the network/update flow.
foreach ($name in @('MachineOf','InstallFiles')) {
    $function = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name }, $false)
    if (!$function) { throw "Missing function $name" }
    . ([scriptblock]::Create($function.Extent.Text))
}
function Assert($condition, $message) { if (!$condition) { throw $message } }
function Put($path, [byte[]]$contents) {
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($path)) -Force | Out-Null
    [IO.File]::WriteAllBytes($path, $contents)
}
function FakePE($path, [int]$machine) {
    $bytes = New-Object byte[] 134
    $bytes[0]=0x4d; $bytes[1]=0x5a; $bytes[0x3c]=0x80
    $bytes[128]=0x50; $bytes[129]=0x45
    $bytes[132]=$machine -band 255; $bytes[133]=($machine -shr 8) -band 255
    Put $path $bytes
}
$root = Join-Path ([IO.Path]::GetTempPath()) ('SDK update package tests ' + [guid]::NewGuid())
FakePE "$root\Launcher.exe" 0x8664
$legacy = @(InstallFiles $root 0x8664)
Assert ($legacy.Count -eq 1) 'Legacy SDK package was rejected'
foreach ($name in @('SDKSceneHost.exe','xrEngine.exe','OpenAL32.dll','nvngx_dlss.dll','dbghelp.dll')) {
    FakePE "$root\InGame\bin\$name" 0x8664
}
foreach ($name in @('fsgame.ltx','preview.ltx','gamedata\config\external.ltx')) {
    Put "$root\InGame\defaults\$name" ([Text.Encoding]::ASCII.GetBytes('default'))
}
Put "$root\InGame\gamedata\custom.ltx" ([byte[]]@(1))
Put "$root\InGame\_appdata_\user.ltx" ([byte[]]@(1))
Put "$root\InGame\fsgame.runtime.ltx" ([byte[]]@(1))
$files = @(InstallFiles $root 0x8664)
Assert ($files.Count -eq 9) 'Wrong package file count'
Assert ($files.Name -contains 'InGame\bin\SDKSceneHost.exe') 'Runtime binary omitted'
Assert ($files.Name -contains 'InGame\defaults\gamedata\config\external.ltx') 'Nested defaults omitted'
Assert (!($files.Name -match 'custom|user|runtime\.ltx')) 'User files included in update'
$rejected = $false
FakePE "$root\InGame\bin\xrEngine.exe" 0x14c
try { $null = @(InstallFiles $root 0x8664) } catch { $rejected = $true }
Assert $rejected 'Wrong runtime architecture accepted'
$rejected = $false
try { $null = @(InstallFiles $root 0x14c) } catch { $rejected = $true }
Assert $rejected 'Wrong SDK architecture accepted'
Write-Output 'PASS: legacy package, runtime binaries/defaults, user-file exclusion, architecture checks'
Write-Output "Fixtures: $root"
