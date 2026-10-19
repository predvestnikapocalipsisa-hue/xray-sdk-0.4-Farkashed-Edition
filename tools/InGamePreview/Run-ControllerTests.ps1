[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$testOutput = Join-Path $repo 'output\ingame_tests'
New-Item -ItemType Directory -Path $testOutput -Force | Out-Null
$source = [IO.File]::ReadAllText((Join-Path $repo 'Editors\LevelEditor\Kernel\InGameMode.cpp'))
$start = $source.IndexOf('class CInGameMode')
$end = $source.IndexOf('void CLevelMain::StartInGame()')
if ($start -lt 0 -or $end -le $start) { throw 'Cannot locate the production controller.' }
[IO.File]::WriteAllText((Join-Path $testOutput 'controller-under-test.h'), $source.Substring($start, $end - $start))
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'controller_tests.cpp') -Destination $testOutput
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'controller_tests.vcxproj') -Destination $testOutput
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = @(& $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe') | Select-Object -First 1
if (!$msbuild) { throw 'MSBuild was not found.' }
& $msbuild (Join-Path $testOutput 'controller_tests.vcxproj') /t:Build /nologo /v:minimal /p:Configuration=Release /p:Platform=x64
if ($LASTEXITCODE -ne 0) { throw 'Controller tests did not compile.' }
& (Join-Path $testOutput 'x64\Release\controller_tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Controller tests failed.' }
