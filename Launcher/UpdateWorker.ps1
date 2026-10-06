param([Parameter(Mandatory=$true)][string]$Target, [Parameter(Mandatory=$true)][string]$LocalVersion,
      [Parameter(Mandatory=$true)][string]$State, [switch]$Install)
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$work = Split-Path -Parent $State
$encoding = New-Object Text.UTF8Encoding($false)
$installationLock = $null
function Report($phase, $message, $progress = 0, $speed = 0) {
    $message = $message -replace '[\r\n]', ' '
    $text = "$phase`n$message`n$progress`n$speed"
    [IO.File]::WriteAllText("$State.tmp", $text, $encoding)
    Move-Item -LiteralPath "$State.tmp" -Destination $State -Force
}
function VersionOf([string]$value) {
    if ($value -notmatch '\d+(?:\.\d+)+') { throw "Invalid SDK version: $value" }
    $parts = $Matches[0].Split('.')
    if ($parts.Count -gt 4) { throw 'Unsupported SDK version.' }
    while ($parts.Count -lt 4) { $parts += '0' }
    return [version]($parts -join '.')
}
function MachineOf([string]$path) {
    $stream = [IO.File]::OpenRead($path)
    $reader = New-Object IO.BinaryReader($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw 'Invalid SDK executable.' }
        $stream.Position=0x3C; $offset=$reader.ReadInt32()
        if ($offset -lt 0 -or $offset -gt $stream.Length-6) { throw 'Invalid SDK executable header.' }
        $stream.Position=$offset
        if ($reader.ReadUInt32() -ne 0x4550) { throw 'Invalid SDK executable signature.' }
        return $reader.ReadUInt16()
    } finally { $reader.Dispose() }
}
try {
    Report 'checking' 'Checking GitHub releases...'
    $repo = 'https://api.github.com/repos/predvestnikapocalipsisa-hue/xray-sdk-0.4-Farkashed-Edition'
    if ($Install) { $release = Get-Content -LiteralPath (Join-Path $work 'release.json') -Raw | ConvertFrom-Json }
    else {
        $release = Invoke-RestMethod -Uri "$repo/releases/latest" -Headers @{ 'User-Agent'='X-Ray-SDK-Updater'; 'Accept'='application/vnd.github+json' } -TimeoutSec 30
        [IO.File]::WriteAllText((Join-Path $work 'release.json'), ($release | ConvertTo-Json -Depth 12), $encoding)
    }
    $local = VersionOf $LocalVersion
    $remote = VersionOf $release.tag_name
    if ($local -gt $remote) { Report 'current' "Installed $LocalVersion is newer than GitHub $($release.tag_name)."; exit }
    if ($local -eq $remote) { Report 'current' "SDK $LocalVersion is up to date."; exit }
    $assets = @($release.assets | Where-Object { $_.name -match '(?i)^SDK.*\.(zip|rar|7z)$' })
    if ($assets.Count -ne 1) { throw 'Release must contain exactly one SDK binary archive (SDK*.zip/rar/7z).' }
    $asset = $assets[0]
    if ($asset.name -match '[/\\:]' -or $asset.size -le 0) { throw 'Invalid SDK asset.' }
    if (-not $Install) { Report 'available' "Installed $LocalVersion / available $($release.tag_name) ($([math]::Round($asset.size/1MB,1)) MB)."; exit }
    $targetRoot = [IO.Path]::GetFullPath($Target).TrimEnd('\')
    if (-not (Test-Path -LiteralPath "$targetRoot\Launcher.exe" -PathType Leaf)) { throw 'Target is not an SDK binaries directory.' }
    $archive = Join-Path $work $asset.name
    $uri = [uri]$asset.browser_download_url
    if ($uri.Scheme -ne 'https' -or $uri.Host -ne 'github.com') { throw 'Unexpected release download URL.' }
    $request = [Net.HttpWebRequest]::Create($uri)
    $request.UserAgent = 'X-Ray-SDK-Updater'; $request.Timeout = 30000; $request.ReadWriteTimeout = 30000
    $response = $request.GetResponse()
    $downloadStream = $response.GetResponseStream()
    $output = [IO.File]::Create($archive)
    $clock = [Diagnostics.Stopwatch]::StartNew(); $bytes = 0L; $lastBytes = 0L; $lastTime = 0.0
    try {
        $buffer = New-Object byte[] 262144
        while (($read = $downloadStream.Read($buffer,0,$buffer.Length)) -gt 0) {
            $output.Write($buffer,0,$read); $bytes += $read
            if ($clock.Elapsed.TotalSeconds - $lastTime -ge .2) {
                $rate = ($bytes-$lastBytes)/($clock.Elapsed.TotalSeconds-$lastTime)
                Report 'downloading' "Downloading $($asset.name): $([math]::Round($bytes/1MB,1)) / $([math]::Round($asset.size/1MB,1)) MB" ($bytes/[double]$asset.size) $rate
                $lastBytes=$bytes; $lastTime=$clock.Elapsed.TotalSeconds
            }
        }
    } finally { $output.Dispose(); $downloadStream.Dispose(); $response.Dispose() }
    if ($bytes -ne $asset.size) { throw 'Incomplete archive download.' }
    if ($asset.digest -match '^sha256:(.+)$' -and (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $Matches[1]) { throw 'Archive checksum does not match GitHub.' }
    Report 'extracting' 'Extracting and validating SDK binaries...'
    $stage = Join-Path $work 'stage'
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    # Windows bsdtar supports ZIP/RAR/7z; never execute anything from the archive.
    $tar = Join-Path $env:SystemRoot 'System32\tar.exe'
    if (-not (Test-Path -LiteralPath $tar)) { throw 'Windows tar.exe is required to extract the SDK release.' }
    $entries = @(& $tar -tf $archive 2>&1)
    if ($LASTEXITCODE -ne 0) { throw 'This archive cannot be read by Windows tar.exe.' }
    foreach ($entry in $entries) {
        $name = [string]$entry
        if ($name -match '(^[/\\]|(^|[/\\])\.\.([/\\]|$)|:)') { throw 'Unsafe path in SDK archive.' }
    }
    $listing = @(& $tar -tvf $archive 2>&1)
    if ($LASTEXITCODE -ne 0 -or ($listing | Where-Object { [string]$_ -match '^[lh]' })) { throw 'SDK archive contains links or cannot be validated.' }
    & $tar -xf $archive -C $stage 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'SDK archive extraction failed.' }
    if (Get-ChildItem -LiteralPath $stage -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) { throw 'SDK archive contains links.' }
    $launchers = @(Get-ChildItem -LiteralPath $stage -Filter Launcher.exe -Recurse -File)
    if ($launchers.Count -ne 1) { throw 'Archive must contain one Launcher.exe with SDK binaries.' }
    $bin = $launchers[0].DirectoryName
    $packageVersion = VersionOf ([Diagnostics.FileVersionInfo]::GetVersionInfo($launchers[0].FullName).FileVersion)
    if ($packageVersion -ne $remote) { throw 'SDK binary version does not match the GitHub release.' }
    $machine = MachineOf "$targetRoot\Launcher.exe"
    if ((MachineOf $launchers[0].FullName) -ne $machine) { throw 'Release architecture does not match the installed SDK.' }
    foreach ($required in @('SDKRuntime.dll','LauncherAssets.dll','ActorEditor.dll','LevelEditor.dll','ParticleEditor.dll','ShaderEditor.dll')) {
        if (-not (Test-Path -LiteralPath (Join-Path $bin $required) -PathType Leaf)) { throw "Missing SDK binary: $required" }
    }
    # Update binaries only, preserving projects, preferences and libraries.
    $files = @(Get-ChildItem -LiteralPath $bin -File | Where-Object { $_.Extension -in '.dll','.exe' })
    foreach ($file in $files) {
        if ((MachineOf $file.FullName) -ne $machine) { throw "SDK binary architecture mismatch: $($file.Name)" }
    }
    $total = ($files | Measure-Object Length -Sum).Sum
    # EditorHost refuses new editor sessions while this lock is held.
    $installationLock = [IO.File]::Open((Join-Path $targetRoot '.sdk-update.lock'),'OpenOrCreate','ReadWrite','None')
    $handles = New-Object 'Collections.Generic.List[IO.FileStream]'
    Report 'waiting' 'Close SDK editors and other launchers to install. This updater can remain open.'
    $deadline = [DateTime]::UtcNow.AddMinutes(10)
    do {
        $locked = $false
        try {
            foreach ($file in $files) {
                $dest = Join-Path $targetRoot $file.Name
                if (Test-Path -LiteralPath $dest) { $handles.Add([IO.File]::Open($dest,'Open','ReadWrite','None')) }
            }
        } catch [IO.IOException] { $locked=$true }
        finally { foreach ($handle in $handles) { $handle.Dispose() }; $handles.Clear() }
        if ($locked) { Start-Sleep -Milliseconds 500 }
        if ([DateTime]::UtcNow -gt $deadline) { throw 'SDK is still in use. Close editors and try again.' }
    } while ($locked)
    $backup = Join-Path $work 'backup'; New-Item -ItemType Directory -Path $backup -Force | Out-Null
    foreach ($file in $files) {
        $dest = Join-Path $targetRoot $file.Name
        if (Test-Path -LiteralPath $dest) { Copy-Item -LiteralPath $dest -Destination (Join-Path $backup $file.Name) }
    }
    $written = New-Object 'Collections.Generic.List[string]'
    $clock.Restart(); $bytes=0L
    try {
        foreach ($file in $files) {
            $dest = Join-Path $targetRoot $file.Name
            $written.Add($file.Name)
            Copy-Item -LiteralPath $file.FullName -Destination $dest -Force
            $bytes += $file.Length
            Report 'installing' "Installing $($file.Name): $([math]::Round($bytes/1MB,1)) / $([math]::Round($total/1MB,1)) MB" ($bytes/[double]$total) ($bytes/[math]::Max(.001,$clock.Elapsed.TotalSeconds))
        }
    } catch {
        $failure = $_.Exception.Message
        $restoreErrors = @()
        foreach ($name in $written) {
            $saved = Join-Path $backup $name; $dest = Join-Path $targetRoot $name
            try {
                if (Test-Path -LiteralPath $saved) { Copy-Item -LiteralPath $saved -Destination $dest -Force }
                elseif (Test-Path -LiteralPath $dest) { Remove-Item -LiteralPath $dest -Force }
            } catch { $restoreErrors += $name }
        }
        if ($restoreErrors.Count) { throw "$failure Recovery incomplete for $($restoreErrors -join ', '). Backup: $backup" }
        throw "$failure Previous SDK binaries restored."
    }
    Report 'done' "SDK $($release.tag_name) installed. You can open the updated launcher." 1
} catch { Report 'error' $_.Exception.Message; exit 1 }
finally { if ($installationLock) { $installationLock.Dispose() } }
