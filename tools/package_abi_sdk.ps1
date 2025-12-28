param(
    [string]$OutDir = "out/abi_sdk"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Split-Path -Parent $root
$absOut = Join-Path $repo $OutDir
$incDir = Join-Path $absOut "include/dng"
$docsDir = Join-Path $absOut "docs"
$zipPath = Join-Path $repo "abi_sdk.zip"

# Clean output
if (Test-Path $absOut) { Remove-Item -Recurse -Force $absOut }
if (Test-Path $zipPath) { Remove-Item -Force $zipPath }

New-Item -ItemType Directory -Path $incDir | Out-Null
New-Item -ItemType Directory -Path $docsDir | Out-Null

$abiSrc = Join-Path $repo "Source/Core/Abi"
Copy-Item -Path (Join-Path $abiSrc "*.h") -Destination $incDir -Force

$authorDoc = Join-Path $repo "Docs/ABI_Module_Authoring.md"
if (Test-Path $authorDoc) {
    Copy-Item -Path $authorDoc -Destination $docsDir -Force
}

# Create zip
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($absOut, $zipPath)

Write-Host "ABI SDK packaged." -ForegroundColor Green
Write-Host "Headers: $incDir"
if (Test-Path $authorDoc) {
    Write-Host "Docs:    $docsDir"
}
Write-Host "Zip:     $zipPath"
