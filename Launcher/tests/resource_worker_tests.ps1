# Offline model checks. Does not call Run(), execute 7-Zip, or access the network.
$ErrorActionPreference = 'Stop'
Add-Type -Path (Join-Path $PSScriptRoot '../ResourceWorker.cs') -ReferencedAssemblies System.dll,System.Core.dll,System.Web.Extensions.dll
function Assert($value, [string]$message) { if (-not $value) { throw $message } }
function Reject([scriptblock]$action, [string]$message) {
    $rejected = $false
    try { & $action | Out-Null } catch { $rejected = $true }
    Assert $rejected $message
}
function Release([int]$number) {
    $name = 'resources.7z.{0:000}' -f $number
    return @{ draft = $false; prerelease = $false; assets = @(@{
        id = 100 + $number; name = $name; size = 100; digest = 'sha256:' + ('a' * 64)
        browser_download_url = "https://github.com/lunardontcry/SDK-DATA-PASS/releases/download/$number/$name"
    }) }
}
$releases = @((Release 3), (Release 2), (Release 1))
$parts = [SDKResourceWorker]::ParseReleases(($releases | ConvertTo-Json -Depth 8))
Assert ($parts.Count -eq 3) 'parts spread across releases were omitted'
Assert ($parts[0].Number -eq 1 -and $parts[2].Number -eq 3) 'parts were not sorted numerically'
Reject { [SDKResourceWorker]::ParseReleases((@((Release 1), (Release 3)) | ConvertTo-Json -Depth 8)) } 'missing volume accepted'
$bad = Release 1; $bad.assets[0].digest = $null
Reject { [SDKResourceWorker]::ParseReleases((ConvertTo-Json -InputObject @($bad) -Depth 8)) } 'missing SHA-256 accepted'
$bad = Release 1; $bad.assets[0].browser_download_url = 'https://example.com/resources.7z.001'
Reject { [SDKResourceWorker]::ParseReleases((ConvertTo-Json -InputObject @($bad) -Depth 8)) } 'external asset URL accepted'

Assert ([SDKResourceWorker]::ValidateResume(206, 'bytes 100-999/1000', 900, 100, 1000)) 'valid resume rejected'
Assert (-not [SDKResourceWorker]::ValidateResume(200, '', 1000, 100, 1000)) 'full response must restart instead of append'
Reject { [SDKResourceWorker]::ValidateResume(206, 'bytes 0-999/1000', 1000, 100, 1000) } 'wrong range offset accepted'
Reject { [SDKResourceWorker]::ValidateResume(206, 'bytes 100-999/2000', 900, 100, 1000) } 'wrong range total accepted'
Reject { [SDKResourceWorker]::ValidateResume(403, '', 0, 100, 1000) } 'HTTP error body accepted'
foreach ($unsafe in @('../evil', '/evil', 'C:/evil', 'gamedata/../evil', 'gamedata/file:stream', 'settings/CON.ini', 'settings/name.', 'settings//file')) {
    Reject { [SDKResourceWorker]::SafeName($unsafe) } "unsafe path accepted: $unsafe"
}

$listing = @'
Path = xray sdk 4.8/settings
Folder = +
Size = 0
Attributes = D

Path = xray sdk 4.8/settings/fs.ltx
Folder = -
Size = 123
Attributes = A
Encrypted = -

Path = xray sdk 4.8/gamedata
Folder = +
Size = 0
Attributes = D

Path = xray sdk 4.8/gamedata/file
Folder = -
Size = 456
Attributes = A
Encrypted = -

Path = xray sdk 4.8/game/gamedata.db0
Size = 987
Attributes = A

Path = xray sdk 4.8/bins/InGame/bin/xrEngine.exe
Size = 654
Attributes = A

Path = xray sdk 4.8/bins/Launcher.exe
Size = 789
Attributes = A

Path = xray sdk 4.8/.git/config
Size = 1
Attributes = A

'@
$entries = [SDKResourceWorker]::ParseListing($listing)
Assert ($entries.Count -eq 6) 'SDK files outside the resource folders were selected'
Assert ($entries[1].Relative -eq 'settings/fs.ltx') 'wrapper directory not stripped'
Assert ($entries[3].Size -eq 456) 'entry size lost'
Assert ($entries.Relative -contains 'game/gamedata.db0') 'game archives omitted'
Assert ($entries.Relative -contains 'bins/InGame/bin/xrEngine.exe') 'nested InGame omitted'
$flat = [SDKResourceWorker]::ParseListing($listing.Replace('bins/InGame/', 'InGame/'))
Assert ($flat.Relative -contains 'InGame/bin/xrEngine.exe') 'root InGame omitted'
Reject { [SDKResourceWorker]::ParseListing($listing.Replace('bins/InGame/', 'bins/Other/')) } 'missing InGame accepted'
Reject { [SDKResourceWorker]::ParseListing($listing.Replace('gamedata', 'other')) } 'missing required gamedata accepted'
Reject { [SDKResourceWorker]::ParseListing($listing.Replace('Encrypted = -', 'Encrypted = +')) } 'encrypted files accepted'
Reject { [SDKResourceWorker]::ParseListing($listing + "`nPath = xray sdk 4.8/GAMEDATA/FILE`nSize = 1`nAttributes = A`n`n") } 'case-insensitive duplicate accepted'
Reject { [SDKResourceWorker]::ParseListing($listing.Replace('Size = 456', "Size = 456`nSymbolic Link = outside")) } 'symbolic link accepted'

$header = New-Object byte[] 32
[byte[]]$signature = 0x37,0x7a,0xbc,0xaf,0x27,0x1c
[Array]::Copy($signature, 0, $header, 0, 6)
[Array]::Copy([BitConverter]::GetBytes([uint64]10000000000), 0, $header, 12, 8)
[Array]::Copy([BitConverter]::GetBytes([uint64]100), 0, $header, 20, 8)
Assert (([SDKResourceWorker]::SevenZipLength($header)) -eq 10000000132) '64-bit split archive length is incorrect'
$header[0] = 0
Reject { [SDKResourceWorker]::SevenZipLength($header) } 'invalid 7z signature accepted'
Write-Output 'PASS: release discovery, volume ordering/gaps, SHA-256 metadata, ranges, paths, listing, wrapper and 64-bit header'
