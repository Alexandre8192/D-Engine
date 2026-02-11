param(
    [string]$Configuration = 'Release',
    [string]$Platform = 'x64'
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Split-Path -Parent $root
Set-Location $repo

function Get-MSBuildPath {
    if ($Env:MSBUILD_EXE -and (Test-Path $Env:MSBUILD_EXE)) { return $Env:MSBUILD_EXE }
    $msbuild = Get-Command msbuild -ErrorAction SilentlyContinue
    if ($msbuild) { return $msbuild.Path }

    $rootsToScan = @(
        (Join-Path ${Env:ProgramFiles(x86)} 'MSBuild')
        (Join-Path ${Env:ProgramFiles} 'Microsoft Visual Studio')
        (Join-Path ${Env:ProgramFiles(x86)} 'Microsoft Visual Studio')
    ) | Where-Object { $_ -and (Test-Path $_) }
    foreach ($root in $rootsToScan) {
        $direct = Get-ChildItem -Path $root -Recurse -Filter 'MSBuild.exe' -ErrorAction SilentlyContinue |
            Sort-Object @{Expression = { $_.FullName -match '\\Current\\Bin\\' }; Descending = $true}, LastWriteTime -Descending |
            Select-Object -First 1
        if ($direct) { return $direct.FullName }
    }

    $vswhere = "$Env:ProgramFiles(x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $path = & $vswhere -latest -requires Microsoft.Component.MSBuild -find "MSBuild\\**\\Bin\\MSBuild.exe" | Select-Object -First 1
        if ($path) { return $path }
    }

    throw 'msbuild not found. Install Visual Studio Build Tools or set MSBUILD_EXE.'
}

function Build-BenchRunner {
    $msbuild = Get-MSBuildPath
    $args = @('D-Engine.sln', '-m', "/p:Configuration=$Configuration", "/p:Platform=$Platform")
    Write-Host "Building BenchRunner: $msbuild $($args -join ' ')"
    $p = Start-Process -FilePath $msbuild -ArgumentList $args -NoNewWindow -Wait -PassThru
    if ($p.ExitCode -ne 0) { throw "msbuild failed for $Configuration|$Platform" }
}

function Run-BenchRunner {
    $benchExe = Join-Path $repo "x64/$Configuration/D-Engine-BenchRunner.exe"
    if (-not (Test-Path $benchExe)) { throw "BenchRunner not found after build: $benchExe" }

    $env:DNG_BENCH_OUT = 'artifacts/bench'
    $benchArgs = @('--warmup', '1', '--target-rsd', '3', '--max-repeat', '12', '--cpu-info', '--strict-stability')
    Write-Host "Running BenchRunner: $benchExe $($benchArgs -join ' ') (affinity=1, priority=High)"
    $argText = $benchArgs -join ' '
    $cmdText = 'start /wait /affinity 1 /high "" "' + $benchExe + '" ' + $argText
    & cmd.exe /c $cmdText
    if ($LASTEXITCODE -ne 0) { throw "BenchRunner failed with exit code $LASTEXITCODE" }
}

function Get-LatestBenchJson {
    $latest = Get-ChildItem -Path 'artifacts/bench' -Filter *.bench.json -Recurse -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $latest) { throw 'No bench json found under artifacts/bench' }
    return $latest.FullName
}

Build-BenchRunner
Run-BenchRunner

$latestPath = Get-LatestBenchJson
$baselineDir = Join-Path $repo 'bench/baselines'
$baselinePath = Join-Path $baselineDir 'bench-runner-release-windows-x64-msvc.baseline.json'
if (-not (Test-Path $baselineDir)) { New-Item -ItemType Directory -Path $baselineDir | Out-Null }

$benchContent = Get-Content -Raw -LiteralPath $latestPath -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
$placeholder = $false
if ($benchContent.metadata -and $benchContent.metadata.note -match 'placeholder') { $placeholder = $true }
if ($benchContent.benchmarks) {
    $hits = $benchContent.benchmarks | Where-Object { $_.name -match '^placeholder$' }
    if ($hits) { $placeholder = $true }
}
if ($placeholder) {
    Write-Host "Warning: latest bench artifact looks like a placeholder ($latestPath)." -ForegroundColor Yellow
}

Copy-Item -LiteralPath $latestPath -Destination $baselinePath -Force
Write-Host "Baseline updated: $baselinePath" -ForegroundColor Green
