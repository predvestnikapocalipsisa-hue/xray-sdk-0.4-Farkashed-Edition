Set-StrictMode -Version Latest

function Copy-SdkFile {
    param([Parameter(Mandatory)][string]$Source, [Parameter(Mandatory)][string]$Destination)
    $sourceFile = Get-Item -LiteralPath $Source -ErrorAction Stop
    if (Test-Path -LiteralPath $Destination -PathType Leaf) {
        $destinationFile = Get-Item -LiteralPath $Destination
        if ($sourceFile.FullName -eq $destinationFile.FullName) { return $false }
        if ($sourceFile.Length -eq $destinationFile.Length) {
            if ($sourceFile.LastWriteTimeUtc -eq $destinationFile.LastWriteTimeUtc) { return $false }
            # Preserve destination timestamps when a relink regenerated identical bytes.
            if ((Get-FileHash -LiteralPath $Source).Hash -eq (Get-FileHash -LiteralPath $Destination).Hash) {
                return $false
            }
        }
    }
    $parent = Split-Path -Parent $Destination
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination -Force -ErrorAction Stop
    (Get-Item -LiteralPath $Destination).LastWriteTimeUtc = $sourceFile.LastWriteTimeUtc
    return $true
}

Export-ModuleMember -Function Copy-SdkFile
