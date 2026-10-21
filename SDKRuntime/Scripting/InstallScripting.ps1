param(
    [Parameter(Mandatory = $true)][string]$SdkRoot,
    [string]$RuntimeArchive
)
$ErrorActionPreference = 'Stop'
$SdkRoot = [IO.Path]::GetFullPath($SdkRoot)
$binaryDir = Join-Path $SdkRoot 'bins'
if (!(Test-Path -LiteralPath (Join-Path $binaryDir 'SDKRuntime.dll'))) { throw 'SDKRuntime.dll not found in SDK bins directory.' }
$url = 'https://www.python.org/ftp/python/3.13.17/python-3.13.17-embed-amd64.zip'
$sha256 = '9E35319BFFEDAB8B36AD44D8F377CB4F1733BCEC257A0B6D04D94F01486DDB38'
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('xray-python-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporary | Out-Null
try {
    if (!$RuntimeArchive) {
        $RuntimeArchive = Join-Path $temporary 'python.zip'
        Invoke-WebRequest -Uri $url -OutFile $RuntimeArchive
    }
    if ((Get-FileHash -LiteralPath $RuntimeArchive -Algorithm SHA256).Hash -ne $sha256) { throw 'Python archive SHA256 mismatch.' }
    $staged = Join-Path $temporary 'python'
    Expand-Archive -LiteralPath $RuntimeArchive -DestinationPath $staged
    $signature = Get-AuthenticodeSignature -LiteralPath (Join-Path $staged 'python313.dll')
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Python Software Foundation') { throw 'Python DLL signature is invalid.' }
    $pythonDir = Join-Path $binaryDir 'python'
    New-Item -ItemType Directory -Path $pythonDir -Force | Out-Null
    Copy-Item -Path (Join-Path $staged '*') -Destination $pythonDir -Force
    $apiDir = Join-Path $binaryDir 'scripting/xray'
    New-Item -ItemType Directory -Path $apiDir -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'python/xray/__init__.py') -Destination (Join-Path $apiDir '__init__.py') -Force
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'api/xray.pyi') -Destination (Join-Path $apiDir '__init__.pyi') -Force
    $apiDocs = Join-Path $binaryDir 'scripting/api'
    New-Item -ItemType Directory -Path $apiDocs -Force | Out-Null
    Copy-Item -Path (Join-Path $PSScriptRoot 'api/*') -Destination $apiDocs -Force
    $addonsDir = Join-Path $SdkRoot 'addons'
    New-Item -ItemType Directory -Path $addonsDir -Force | Out-Null
    foreach ($example in Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'examples')) {
        $destination = Join-Path $addonsDir $example.Name
        if (!(Test-Path -LiteralPath $destination)) { Copy-Item -LiteralPath $example.FullName -Destination $destination -Recurse }
    }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.md') -Destination (Join-Path $addonsDir 'README.md') -Force
    Write-Output 'Installed Python 3.13.17, the xray API, and disabled example add-ons.'
} finally {
    $resolvedTemporary = [IO.Path]::GetFullPath($temporary)
    $expectedParent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (!$resolvedTemporary.StartsWith($expectedParent, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid temporary directory.' }
    Remove-Item -LiteralPath $resolvedTemporary -Recurse -Force
}
