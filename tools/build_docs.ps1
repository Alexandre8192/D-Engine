Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$LocalPython = Join-Path $RepoRoot ".venv-docs\Scripts\python.exe"
$LocalMkDocs = Join-Path $RepoRoot ".venv-docs\Scripts\mkdocs.exe"

$Python = if (Test-Path $LocalPython) {
    Get-Item $LocalPython
} else {
    Get-Command python -ErrorAction SilentlyContinue
}

$MkDocs = if (Test-Path $LocalMkDocs) {
    Get-Item $LocalMkDocs
} else {
    Get-Command mkdocs -ErrorAction SilentlyContinue
}

if (-not $Python) {
    throw "Python not found in PATH."
}

if (-not $MkDocs) {
    throw "mkdocs not found in PATH. Install mkdocs-material first."
}

Push-Location $RepoRoot

try {
    $PythonCommand = if ($Python.PSObject.Properties.Match("Source").Count -gt 0) {
        $Python.Source
    } else {
        $Python.FullName
    }
    $MkDocsCommand = if ($MkDocs.PSObject.Properties.Match("Source").Count -gt 0) {
        $MkDocs.Source
    } else {
        $MkDocs.FullName
    }

    & $PythonCommand "tools/docs_sync.py"
    if ($LASTEXITCODE -ne 0) {
        throw "tools/docs_sync.py failed with exit code $LASTEXITCODE"
    }

    powershell -ExecutionPolicy Bypass -File "tools/export_architecture.ps1"
    if ($LASTEXITCODE -ne 0) {
        throw "tools/export_architecture.ps1 failed with exit code $LASTEXITCODE"
    }

    & $MkDocsCommand build --clean
    if ($LASTEXITCODE -ne 0) {
        throw "mkdocs build failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}
