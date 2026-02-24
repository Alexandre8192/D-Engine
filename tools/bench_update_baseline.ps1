param(
    [ValidateSet('core', 'memory', 'both')]
    [string]$Mode = 'both',
    [string]$Configuration = 'Release',
    [string]$Platform = 'x64',
    [string]$AffinityMask = '1',
    [switch]$NormalPriority,
    [switch]$AllowUnstable,
    [switch]$SkipBuild,
    [switch]$Promote
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Split-Path -Parent $root
Set-Location $repo

function Ensure-Directory([string]$path) {
    if (-not (Test-Path $path)) {
        New-Item -ItemType Directory -Path $path | Out-Null
    }
}

function Get-MSBuildPath {
    if ($Env:MSBUILD_EXE -and (Test-Path $Env:MSBUILD_EXE)) { return $Env:MSBUILD_EXE }
    $msbuild = Get-Command msbuild -ErrorAction SilentlyContinue
    if ($msbuild) { return $msbuild.Path }

    $rootsToScan = @(
        (Join-Path ${Env:ProgramFiles(x86)} 'MSBuild')
        (Join-Path ${Env:ProgramFiles} 'Microsoft Visual Studio')
        (Join-Path ${Env:ProgramFiles(x86)} 'Microsoft Visual Studio')
    ) | Where-Object { $_ -and (Test-Path $_) }
    foreach ($scanRoot in $rootsToScan) {
        $direct = Get-ChildItem -Path $scanRoot -Recurse -Filter 'MSBuild.exe' -ErrorAction SilentlyContinue |
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

function Assert-NoLeakMarkers([string]$logPath, [string]$context) {
    if (-not (Test-Path $logPath)) { return }
    $matches = Select-String -Path $logPath -Pattern '=== MEMORY LEAKS DETECTED ===', 'TOTAL LEAKS:' -SimpleMatch -ErrorAction SilentlyContinue
    if ($matches) {
        throw "$context emitted leak markers. See log: $logPath"
    }
}

function Get-LatestBenchJson([string]$outRoot) {
    $latest = Get-ChildItem -Path $outRoot -Filter *.bench.json -Recurse -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if (-not $latest) { throw "No bench json found under $outRoot" }
    return $latest.FullName
}

function Assert-NotPlaceholder([string]$jsonPath, [string]$label) {
    $benchContent = Get-Content -Raw -LiteralPath $jsonPath -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    $placeholder = $false
    if ($benchContent.metadata -and $benchContent.metadata.note -match 'placeholder') { $placeholder = $true }
    if ($benchContent.benchmarks) {
        $hits = $benchContent.benchmarks | Where-Object { $_.name -match '^placeholder$' }
        if ($hits) { $placeholder = $true }
    }
    if ($placeholder) {
        throw "$label produced a placeholder artifact ($jsonPath)"
    }
}

function Run-BenchProfile {
    param(
        [string]$BenchExe,
        [string[]]$BenchArgs,
        [string]$OutputDir,
        [string]$Label,
        [string]$LogPath
    )

    Ensure-Directory $OutputDir
    Ensure-Directory (Split-Path $LogPath -Parent)
    $env:DNG_BENCH_OUT = $OutputDir

    $priorityArg = if ($NormalPriority) { '' } else { ' /high' }
    $argText = $BenchArgs -join ' '
    $cmdText = 'start /wait /affinity ' + $AffinityMask + $priorityArg + ' "" "' + $BenchExe + '" ' + $argText
    Write-Host "Running ${Label}: $BenchExe $argText (affinity=$AffinityMask, priority=$(if ($NormalPriority) { 'Normal' } else { 'High' }))"
    & cmd.exe /c $cmdText 2>&1 | Tee-Object -FilePath $LogPath
    if ($LASTEXITCODE -ne 0) { throw "$Label failed with exit code $LASTEXITCODE" }

    Assert-NoLeakMarkers -logPath $LogPath -context $Label
    return Get-LatestBenchJson -outRoot $OutputDir
}

function Backup-And-PromoteBaseline {
    param(
        [string]$CandidateJson,
        [string]$BaselinePath,
        [string]$Stamp
    )

    $baselineDir = Split-Path $BaselinePath -Parent
    Ensure-Directory $baselineDir
    $archiveDir = Join-Path $baselineDir 'archive'
    Ensure-Directory $archiveDir

    $archived = $null
    if (Test-Path $BaselinePath) {
        $baseName = [System.IO.Path]::GetFileNameWithoutExtension($BaselinePath)
        $ext = [System.IO.Path]::GetExtension($BaselinePath)
        $archived = Join-Path $archiveDir ($baseName + '.' + $Stamp + $ext)
        Copy-Item -LiteralPath $BaselinePath -Destination $archived -Force
    }

    Copy-Item -LiteralPath $CandidateJson -Destination $BaselinePath -Force
    return @{
        Baseline = $BaselinePath
        Archived = $archived
        Source = $CandidateJson
    }
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$captureRoot = Join-Path $repo ("artifacts/bench/baseline-capture/" + $stamp)
$logRoot = Join-Path $repo 'artifacts/gates'
Ensure-Directory $captureRoot
Ensure-Directory $logRoot

$benchExe = Join-Path $repo ("x64/" + $Configuration + "/D-Engine-BenchRunner.exe")
if (-not $SkipBuild) {
    Build-BenchRunner
}
if (-not (Test-Path $benchExe)) {
    throw "BenchRunner not found: $benchExe"
}

$strictArg = @()
if (-not $AllowUnstable) {
    $strictArg = @('--strict-stability')
}

$coreResult = $null
$memoryResult = $null
$modeLower = $Mode.ToLowerInvariant()
if ($modeLower -in @('core', 'both')) {
    $coreOut = Join-Path $captureRoot 'core'
    $coreLog = Join-Path $logRoot ("bench-baseline-core-" + $stamp + ".log")
    $coreArgs = @('--warmup', '1', '--target-rsd', '3', '--max-repeat', '20', '--cpu-info') + $strictArg
    $coreJson = Run-BenchProfile -BenchExe $benchExe -BenchArgs $coreArgs -OutputDir $coreOut -Label 'BenchRunner core baseline capture' -LogPath $coreLog
    Assert-NotPlaceholder -jsonPath $coreJson -label 'Core baseline capture'
    $coreBaselinePath = Join-Path $repo 'bench/baselines/bench-runner-release-windows-x64-msvc.baseline.json'
    if ($Promote) {
        $coreResult = Backup-And-PromoteBaseline -CandidateJson $coreJson -BaselinePath $coreBaselinePath -Stamp $stamp
        $coreResult['Promoted'] = $true
        Write-Host "Core baseline promoted: $coreBaselinePath" -ForegroundColor Green
    }
    else {
        $coreResult = @{
            Source = $coreJson
            Baseline = $coreBaselinePath
            Archived = $null
            Promoted = $false
        }
        Write-Host "Core baseline captured (no promotion). Re-run with -Promote to overwrite: $coreBaselinePath" -ForegroundColor Yellow
    }
}

if ($modeLower -in @('memory', 'both')) {
    $memoryOut = Join-Path $captureRoot 'memory'
    $memoryLog = Join-Path $logRoot ("bench-baseline-memory-" + $stamp + ".log")
    $memoryArgs = @('--warmup', '2', '--target-rsd', '8', '--max-repeat', '24', '--cpu-info', '--memory-only', '--memory-matrix') + $strictArg
    $memoryJson = Run-BenchProfile -BenchExe $benchExe -BenchArgs $memoryArgs -OutputDir $memoryOut -Label 'BenchRunner memory baseline capture' -LogPath $memoryLog
    Assert-NotPlaceholder -jsonPath $memoryJson -label 'Memory baseline capture'
    $memoryBaselinePath = Join-Path $repo 'bench/baselines/bench-runner-memory-release-windows-x64-msvc.baseline.json'
    if ($Promote) {
        $memoryResult = Backup-And-PromoteBaseline -CandidateJson $memoryJson -BaselinePath $memoryBaselinePath -Stamp $stamp
        $memoryResult['Promoted'] = $true
        Write-Host "Memory baseline promoted: $memoryBaselinePath" -ForegroundColor Green
    }
    else {
        $memoryResult = @{
            Source = $memoryJson
            Baseline = $memoryBaselinePath
            Archived = $null
            Promoted = $false
        }
        Write-Host "Memory baseline captured (no promotion). Re-run with -Promote to overwrite: $memoryBaselinePath" -ForegroundColor Yellow
    }
}

$reportDir = Join-Path $repo 'artifacts/bench/baselines'
Ensure-Directory $reportDir
$reportPath = Join-Path $reportDir ("baseline-capture-" + $stamp + ".md")
$priorityLabel = if ($NormalPriority) { 'Normal' } else { 'High' }
$strictStabilityLabel = if ($AllowUnstable) { 'false' } else { 'true' }

$reportLines = @(
    '# Baseline Capture',
    '',
    "- generatedAt: $(Get-Date -Format 'yyyy-MM-ddTHH:mm:ssK')",
    "- mode: $Mode",
    "- configuration: $Configuration",
    "- platform: $Platform",
    "- affinityMask: $AffinityMask",
    "- priority: $priorityLabel",
    "- promoted: $($Promote.ToString().ToLowerInvariant())",
    "- strictStability: $strictStabilityLabel",
    "- captureRoot: $captureRoot",
    ''
)

if ($coreResult) {
    $reportLines += @(
        '## Core Baseline',
        '',
        "- source: $($coreResult.Source)",
        "- baseline: $($coreResult.Baseline)",
        "- promoted: $($coreResult.Promoted.ToString().ToLowerInvariant())"
    )
    if ($coreResult.Archived) {
        $reportLines += "- archivedPrevious: $($coreResult.Archived)"
    }
    $reportLines += ''
}

if ($memoryResult) {
    $reportLines += @(
        '## Memory Baseline',
        '',
        "- source: $($memoryResult.Source)",
        "- baseline: $($memoryResult.Baseline)",
        "- promoted: $($memoryResult.Promoted.ToString().ToLowerInvariant())"
    )
    if ($memoryResult.Archived) {
        $reportLines += "- archivedPrevious: $($memoryResult.Archived)"
    }
    $reportLines += ''
}

Set-Content -Path $reportPath -Value ($reportLines -join [Environment]::NewLine) -Encoding UTF8
Write-Host "Capture report: $reportPath" -ForegroundColor Green
if (-not $Promote) {
    Write-Host 'No canonical baseline files were modified (capture-only mode). Use -Promote after review.' -ForegroundColor Yellow
}
