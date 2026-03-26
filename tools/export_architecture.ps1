Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$WorkspacePath = Join-Path $RepoRoot "Docs/architecture/workspace.dsl"
$GeneratedDir = Join-Path $RepoRoot "Docs/_generated/architecture"
$LocalStructurizr = Join-Path $env:LOCALAPPDATA "Programs\StructurizrCLI\current\structurizr.bat"

if (-not (Test-Path $WorkspacePath)) {
    throw "Missing workspace file: $WorkspacePath"
}

New-Item -ItemType Directory -Force -Path $GeneratedDir | Out-Null

$Java = Get-Command java -ErrorAction SilentlyContinue
if (-not $Java) {
    $MicrosoftJdkRoot = "C:\Program Files\Microsoft"
    if (Test-Path $MicrosoftJdkRoot) {
        $JavaCandidate = Get-ChildItem -Path $MicrosoftJdkRoot -Directory -Filter "jdk-*" |
            Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName "bin\java.exe" } |
            Where-Object { Test-Path $_ } |
            Select-Object -First 1
        if ($JavaCandidate) {
            $env:PATH = (Split-Path $JavaCandidate -Parent) + ";" + $env:PATH
            $Java = Get-Command java -ErrorAction SilentlyContinue
        }
    }
}

$Structurizr = Get-Command structurizr -ErrorAction SilentlyContinue
if (-not $Structurizr -and (Test-Path $LocalStructurizr)) {
    $Structurizr = Get-Item $LocalStructurizr
}

if (-not $Structurizr) {
    Write-Warning "Structurizr CLI not found in PATH. Skipping Mermaid export."
    Write-Warning "The checked-in generated pages will continue to work."
    return
}

$TempDir = Join-Path $env:TEMP ("dengine-structurizr-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $TempDir | Out-Null

try {
    $StructurizrCommand = if ($Structurizr.PSObject.Properties.Match("Source").Count -gt 0) {
        $Structurizr.Source
    } else {
        $Structurizr.FullName
    }

    & $StructurizrCommand export `
        -workspace $WorkspacePath `
        -format mermaid `
        -output $TempDir

    $Views = @(
        @{ Key = "landscape"; Title = "Landscape"; Out = "landscape.md" },
        @{ Key = "containers"; Title = "Containers"; Out = "containers.md" }
    )

    foreach ($View in $Views) {
        $InFile = Join-Path $TempDir ($View.Key + ".mmd")
        if (-not (Test-Path $InFile)) {
            $Match = Get-ChildItem -Path $TempDir -Filter "*.mmd" |
                Where-Object { $_.BaseName -like ("*" + $View.Key + "*") } |
                Select-Object -First 1
            if ($Match) {
                $InFile = $Match.FullName
            } else {
                Write-Warning "Expected Mermaid export for view '$($View.Key)' was not found."
                continue
            }
        }

        $Mermaid = (Get-Content -Path $InFile -Raw -Encoding UTF8).Trim()
        $Markdown = @(
            "# $($View.Title)",
            "",
            "This page is generated from the Structurizr workspace.",
            "It may be overwritten by tooling.",
            "",
            '```mermaid',
            $Mermaid,
            '```',
            ""
        ) -join "`n"

        $OutFile = Join-Path $GeneratedDir $View.Out
        Set-Content -Path $OutFile -Value $Markdown -Encoding UTF8
    }
}
finally {
    if (Test-Path $TempDir) {
        Remove-Item -Recurse -Force $TempDir
    }
}
