param([Parameter(Mandatory = $true)][string]$SdkRoot)
$ErrorActionPreference = 'Stop'
$SdkRoot = [IO.Path]::GetFullPath($SdkRoot).TrimEnd('\', '/')
$settingsDir = Join-Path $SdkRoot 'settings'
$binariesDir = Join-Path $SdkRoot 'bins'
if (!(Test-Path -LiteralPath (Join-Path $binariesDir 'Launcher.exe'))) { throw 'Select an SDK root containing bins/Launcher.exe.' }
foreach ($editorProcess in @(Get-Process Launcher -ErrorAction SilentlyContinue)) {
    if ($editorProcess.Path -and [IO.Path]::GetDirectoryName($editorProcess.Path) -eq $binariesDir) {
        if (@($editorProcess.Modules | Where-Object ModuleName -eq 'SDKRuntime.dll').Count) {
            throw 'Save your work and close SDK editors before organizing settings.'
        }
    }
}

# Explicit allowlist: compiler configuration and project/game data are not migrated.
$names = @('actor.ini', 'actor_imgui.ini', 'level.ini', 'level_imgui.ini',
    'level_ingame_imgui.ini', 'particle.ini', 'particle_imgui.ini', 'shader.ini',
    'shader_imgui.ini', 'content_browser.ltx', 'ui_settings.cfg', 'fs.ltx',
    'fs_soc.ltx', 'fsfactory.ltx', 'fsgame.ltx', 'fsgame_soc.ltx')
$encoding = [Text.Encoding]::GetEncoding(28591) # Round-trip all original bytes, including legacy Cyrillic encodings.
$plan = @()
foreach ($name in $names) {
    $original = Join-Path $SdkRoot $name
    if (!(Test-Path -LiteralPath $original)) { continue }
    $bytes = [IO.File]::ReadAllBytes($original)
    if ($name -in @('fs.ltx', 'fs_soc.ltx', 'fsgame.ltx', 'fsgame_soc.ltx')) {
        $text = $encoding.GetString($bytes)
        $text = [regex]::Replace($text, '(?m)^(\$local_root\$[^\r\n=]*=[^\r\n]*\$sdk_root\$)[\t ]*(?=\r?$)', '$1| settings\')
        $bytes = $encoding.GetBytes($text)
    }
    $destination = Join-Path $settingsDir $name
    if (Test-Path -LiteralPath $destination) {
        if ([Convert]::ToBase64String([IO.File]::ReadAllBytes($destination)) -ne [Convert]::ToBase64String($bytes)) {
            throw "Existing settings differ: $destination. No files have been moved."
        }
    }
    $plan += [pscustomobject]@{ Name = $name; Original = $original; Destination = $destination; Bytes = $bytes }
}
if (!$plan.Count) { Write-Output 'Settings are already organized.'; exit 0 }
New-Item -ItemType Directory -Path $settingsDir -Force | Out-Null
foreach ($item in $plan) {
    [IO.File]::WriteAllBytes($item.Destination, $item.Bytes)
    if ([Convert]::ToBase64String([IO.File]::ReadAllBytes($item.Destination)) -ne [Convert]::ToBase64String($item.Bytes)) {
        throw "Verification failed: $($item.Destination). Originals have been kept."
    }
}
$backupDir = Join-Path $binariesDir ('backups/settings-layout-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$backupRoot = Join-Path $backupDir 'root'
$backupBins = Join-Path $backupDir 'bins'
New-Item -ItemType Directory -Path $backupRoot, $backupBins -Force | Out-Null
foreach ($item in $plan) { Move-Item -LiteralPath $item.Original -Destination (Join-Path $backupRoot $item.Name) }
# Obsolete copies in the binary directory are retained in the same backup.
foreach ($name in @('content_browser.ltx', 'ui_settings.cfg')) {
    $obsolete = Join-Path $binariesDir $name
    if (Test-Path -LiteralPath $obsolete) { Move-Item -LiteralPath $obsolete -Destination (Join-Path $backupBins $name) }
}
Write-Output "Organized $($plan.Count) files in $settingsDir"
Write-Output "Original files: $backupDir"
