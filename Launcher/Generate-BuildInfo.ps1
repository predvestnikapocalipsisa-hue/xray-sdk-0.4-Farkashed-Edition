param(
    [Parameter(Mandatory=$true)][string]$Root,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$SdkDirectory
)
$ErrorActionPreference = 'Stop'
$versionPath = Join-Path $Root 'version.json'
$version = [string]((Get-Content -LiteralPath $versionPath -Raw -Encoding UTF8 | ConvertFrom-Json).version)
if ([string]::IsNullOrWhiteSpace($version) -or $version -match '[\r\n]') { throw 'version.json must contain a nonempty single-line version string.' }
$hash = 'unknown'; $branch = 'unknown'
if (Get-Command git -ErrorAction SilentlyContinue) {
    try {
        $taskHash = & git -C $Root rev-parse --short=12 HEAD 2>$null
        if ($LASTEXITCODE -eq 0) { $hash = [string]$taskHash }
    } catch {}
    try {
        $taskBranch = & git -C $Root symbolic-ref --short -q HEAD 2>$null
        if ($LASTEXITCODE -eq 0) { $branch = [string]$taskBranch }
        elseif ($hash -ne 'unknown') { $branch = 'detached' }
    } catch { if ($hash -ne 'unknown') { $branch = 'detached' } }
}
$inputs = @($version, $hash, $branch)
foreach ($name in @('SDKRuntime.dll','ActorEditor.dll','LevelEditor.dll','ParticleEditor.dll','ShaderEditor.dll')) {
    $file = Get-Item -LiteralPath (Join-Path $SdkDirectory $name) -ErrorAction SilentlyContinue
    if ($file) { $inputs += "$name|$($file.Length)|$($file.LastWriteTimeUtc.Ticks)" }
}
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $Root 'Launcher') -File | Sort-Object Name) {
    if ($file.Extension -in '.cpp','.h','.rc','.ps1','.targets') { $inputs += "$($file.Name)|$($file.Length)|$($file.LastWriteTimeUtc.Ticks)" }
}
$signature = $inputs -join "`n"
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$signaturePath = Join-Path $OutputDirectory 'SdkBuildInfo.signature'
$infoPath = Join-Path $OutputDirectory 'SdkBuildInfo.txt'
$includePath = Join-Path $OutputDirectory 'SdkBuildInfo.rcinc'
if ((Test-Path -LiteralPath $signaturePath) -and (Test-Path -LiteralPath $infoPath) -and
    (Test-Path -LiteralPath $includePath) -and [IO.File]::ReadAllText($signaturePath) -eq $signature) { return }
$date = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
$info = "$version`n$date`n$hash`n$branch`n"
$numbers = @([regex]::Matches($version,'\d+') | Select-Object -First 4 | ForEach-Object { [Math]::Min(65535,[int]$_.Value) })
while ($numbers.Count -lt 4) { $numbers += 0 }
$escaped = $version.Replace('\','\\').Replace('"','\"')
$include = "#define SDK_VERSION_NUMBERS $($numbers -join ',')`n#define SDK_VERSION_TEXT `"$escaped\0`"`n"
$utf8 = New-Object Text.UTF8Encoding($false)
foreach ($entry in @(@($infoPath,$info),@($includePath,$include),@($signaturePath,$signature))) {
    if (!(Test-Path -LiteralPath $entry[0]) -or [IO.File]::ReadAllText($entry[0]) -ne $entry[1]) {
        [IO.File]::WriteAllText($entry[0],$entry[1],$utf8)
    }
}
