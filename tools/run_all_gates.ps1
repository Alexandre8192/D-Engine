# run_all_gates.ps1
# Purpose : Local one-shot runner for D-Engine gates (lint, build, smokes, ABI smoke, bench, leak checks).
# Modes   :
#   -Fast          : policy lint (strict+modules), Release build, AllSmokes, ModuleSmoke; memory stress + bench skipped.
#   -RequireBench  : bench is required; missing BenchRunner fails. Runs both core and memory perf compares.
#   -RustModule    : build/copy Rust NullWindowModule via cargo before ModuleSmoke; fails if cargo missing.
#   -RequireRealBench     : fail if BenchRunner artifact is a placeholder stub.
#   -RequireBenchBaseline : fail if bench baseline is missing.
# Bench tuning env vars:
#   - BENCH_AFFINITY_MASK (default 1)
#   - BENCH_NORMAL_PRIORITY=1 (default High priority)
#   - BENCH_CORE_WARMUP / BENCH_CORE_TARGET_RSD / BENCH_CORE_MAX_REPEAT
#   - BENCH_MEMORY_WARMUP / BENCH_MEMORY_TARGET_RSD / BENCH_MEMORY_MAX_REPEAT
# Exit codes : PASS=0 (no skips), PARTIAL=2 (required gates pass, optional skipped), FAIL=1.

[CmdletBinding()]
param(
    [switch]$Fast,
    [switch]$RequireBench,
    [switch]$RustModule,
    [switch]$RequireRealBench,
    [switch]$RequireBenchBaseline
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $repoRoot

$benchRequired = [bool]($RequireBench -or $RequireRealBench -or $RequireBenchBaseline)
$memoryStressRequired = [bool](-not $Fast)

$gateState = [ordered]@{
    Policy             = [pscustomobject]@{ Status = 'PENDING'; Required = $true }
    Compile            = [pscustomobject]@{ Status = 'PENDING'; Required = $true }
    AllSmokes          = [pscustomobject]@{ Status = 'PENDING'; Required = $true }
    MemoryStressSmokes = [pscustomobject]@{ Status = 'PENDING'; Required = $memoryStressRequired }
    ModuleSmoke        = [pscustomobject]@{ Status = 'PENDING'; Required = $true }
    Bench              = [pscustomobject]@{ Status = 'PENDING'; Required = $benchRequired }
}

$gateTiming = [ordered]@{
    Policy             = [timespan]::Zero
    Compile            = [timespan]::Zero
    AllSmokes          = [timespan]::Zero
    MemoryStressSmokes = [timespan]::Zero
    ModuleSmoke        = [timespan]::Zero
    Bench              = [timespan]::Zero
}

function Measure-Gate([string]$name, [scriptblock]$action) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    try {
        & $action
    }
    finally {
        $sw.Stop()
        if ($gateTiming.Contains($name)) { $gateTiming[$name] = $sw.Elapsed }
    }
}

function Set-GateStatus([string]$name, [string]$status) {
    if ($gateState.Contains($name)) { $gateState[$name].Status = $status }
}

function Fail([string]$msg) {
    Write-Host "FAIL: $msg" -ForegroundColor Red
    throw $msg
}

function Ensure-Directory([string]$path) {
    if (-not (Test-Path $path)) {
        New-Item -ItemType Directory -Path $path | Out-Null
    }
}

function Assert-NoLeakMarkers([string]$logPath, [string]$context) {
    if (-not (Test-Path $logPath)) { return }
    $matches = Select-String -Path $logPath -Pattern '=== MEMORY LEAKS DETECTED ===', 'TOTAL LEAKS:' -SimpleMatch -ErrorAction SilentlyContinue
    if ($matches) {
        Fail "$context emitted leak markers. See log: $logPath"
    }
}

function Find-Python {
    $py = Get-Command python -ErrorAction SilentlyContinue
    if (-not $py) { Fail "Python not found in PATH. Install Python 3.x and re-run." }
    return $py.Path
}

function Get-CargoCommand {
    $cargo = Get-Command cargo -ErrorAction SilentlyContinue
    if ($cargo) { return $cargo }

    $userCargo = Join-Path $env:USERPROFILE '.cargo\bin\cargo.exe'
    if (Test-Path $userCargo) {
        return [pscustomobject]@{ Path = $userCargo }
    }

    return $null
}

function Run-PolicyLint {
    $python = Find-Python
    $invoke = {
        param([string[]]$cmdArgs)
        Write-Host "Running: $python $($cmdArgs -join ' ')"
        $p = Start-Process -FilePath $python -ArgumentList $cmdArgs -NoNewWindow -Wait -PassThru
        if ($p.ExitCode -ne 0) { Fail "policy_lint failed (command: $python $($cmdArgs -join ' '))" }
    }

    if ($Fast) {
        & $invoke @('tools/policy_lint.py', '--strict', '--modules')
        & $invoke @('tools/policy_lint_selftest.py')
        return
    }

    & $invoke @('tools/policy_lint.py')
    & $invoke @('tools/policy_lint.py', '--strict')
    & $invoke @('tools/policy_lint.py', '--strict', '--modules')
    & $invoke @('tools/policy_lint_selftest.py')
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

    $roots = @($Env:ProgramFiles, ${Env:ProgramFiles(x86)}) | Where-Object { $_ -and (Test-Path $_) }
    foreach ($root in $roots) {
        $candidate = Get-ChildItem -Path (Join-Path $root 'Microsoft Visual Studio') -Recurse -Filter 'MSBuild.exe' -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -like '*\\MSBuild\\*\\Bin\\MSBuild.exe' } |
            Sort-Object LastWriteTime -Descending |
            Select-Object -First 1
        if ($candidate) { return $candidate.FullName }
    }

    $legacy = Join-Path $Env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\MSBuild.exe'
    if (Test-Path $legacy) { return $legacy }

    Fail "msbuild not found via PATH or Visual Studio install roots. Install Visual Studio Build Tools or add msbuild to PATH."
}

function Run-MSBuild([string]$config) {
    $msbuild = Get-MSBuildPath
    $args = @("D-Engine.sln", "/m", "/p:Configuration=$config", "/p:Platform=x64")
    Write-Host "Building $config with: $msbuild $($args -join ' ')"
    $p = Start-Process -FilePath $msbuild -ArgumentList $args -NoNewWindow -Wait -PassThru
    if ($p.ExitCode -ne 0) { Fail "msbuild failed for $config" }
}

function Run-MSBuildMemTrackingCompileOnly {
    $msbuild = Get-MSBuildPath
    $args = @(
        "D-Engine.vcxproj",
        "/p:Configuration=Debug",
        "/p:Platform=x64",
        '/p:PreprocessorDefinitions="DNG_MEM_TRACKING=1;DNG_MEM_CAPTURE_CALLSITE=1;%(PreprocessorDefinitions)"',
        "/p:BuildProjectReferences=false"
    )
    Write-Host "Building mem tracking compile-only with: $msbuild $($args -join ' ')"
    $p = Start-Process -FilePath $msbuild -ArgumentList $args -NoNewWindow -Wait -PassThru
    if ($p.ExitCode -ne 0) { Fail "msbuild failed for mem tracking compile-only" }
}

function Resolve-ExePath([string[]]$preferredPaths, [string]$fallbackPattern, [string]$friendly) {
    foreach ($p in $preferredPaths) {
        $full = Join-Path $repoRoot $p
        if (Test-Path $full) { return $full }
    }

    $candidates = Get-ChildItem -Path $repoRoot -Recurse -Filter $fallbackPattern -ErrorAction SilentlyContinue |
        Sort-Object @{Expression = { $_.FullName -match 'Release' }; Descending = $true}, LastWriteTime -Descending
    if (-not $candidates) { Fail "$friendly executable not found" }
    Write-Host "WARNING: using fallback exe discovery: $($candidates[0].FullName)" -ForegroundColor Yellow
    return $candidates[0].FullName
}

function Run-Exe([string]$pattern, [string]$friendly, [string[]]$preferredPaths, [switch]$LeakCheck) {
    $exe = Resolve-ExePath -preferredPaths $preferredPaths -fallbackPattern $pattern -friendly $friendly
    $logDir = Join-Path $repoRoot 'artifacts/gates'
    Ensure-Directory $logDir
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $safeName = ($friendly -replace '[^A-Za-z0-9_.-]', '_')
    $logPath = Join-Path $logDir "$safeName-$stamp.log"

    Write-Host "Running ${friendly}: $exe (log: $logPath)"
    & $exe 2>&1 | Tee-Object -FilePath $logPath
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        Fail "$friendly failed with exit code $exitCode (log: $logPath)"
    }

    if ($LeakCheck) {
        Assert-NoLeakMarkers -logPath $logPath -context $friendly
    }
}

function Find-NullWindowModuleDll {
    $dll = Get-ChildItem -Path $repoRoot -Recurse -Filter 'NullWindowModule.dll' -ErrorAction SilentlyContinue |
        Sort-Object @{Expression = { $_.FullName -match 'Release' }; Descending = $true}, LastWriteTime -Descending |
        Select-Object -First 1
    return $dll
}

function Build-RustModule {
    $cargo = Get-CargoCommand
    if (-not $cargo) { Fail "cargo not found in PATH. Install Rust (MSVC toolchain) or drop -RustModule." }

    $moduleRoot = Join-Path $repoRoot 'External/Rust/NullWindowModule'
    if (-not (Test-Path $moduleRoot)) { Fail "NullWindowModule source not found ($moduleRoot)" }

    Push-Location $moduleRoot
    try {
        $args = @('build', '--release')
        Write-Host "Building NullWindowModule with: $($cargo.Path) $($args -join ' ')"
        $p = Start-Process -FilePath $cargo.Path -ArgumentList $args -NoNewWindow -Wait -PassThru
        if ($p.ExitCode -ne 0) { Fail "cargo build failed for NullWindowModule" }

        $built = Join-Path $moduleRoot 'target/release/rust_null_window_module.dll'
        if (-not (Test-Path $built)) { Fail "NullWindowModule artifact missing ($built)" }

        $destinations = @(
            (Join-Path $repoRoot 'x64\Release\NullWindowModule.dll')
            (Join-Path $repoRoot 'x64\Debug\NullWindowModule.dll')
        )

        foreach ($dest in $destinations) {
            $destDir = Split-Path $dest -Parent
            if (-not (Test-Path $destDir)) { New-Item -ItemType Directory -Path $destDir | Out-Null }
            Copy-Item -Path $built -Destination $dest -Force
        }
    }
    finally {
        Pop-Location
    }
}

function Run-Bench {
    param(
        [switch]$RequireReal,
        [switch]$RequireBaseline
    )
    if ($Fast) { return 'SKIPPED' }

    $benchExe = $null
    $benchCandidates = @(
        (Join-Path $repoRoot 'x64\Release\D-Engine-BenchRunner.exe'),
        (Join-Path $repoRoot 'x64\Debug\D-Engine-BenchRunner.exe')
    )

    foreach ($candidate in $benchCandidates) {
        if (Test-Path $candidate) {
            $benchExe = Get-Item $candidate
            break
        }
    }

    if (-not $benchExe) {
        if ($benchRequired) {
            Set-GateStatus 'Bench' 'FAIL'
            Fail "BenchRunner executable not found"
        }
        Write-Host "BenchRunner executable not found; skipping bench gate." -ForegroundColor Yellow
        return 'SKIPPED'
    }

    $python = Find-Python
    $gateLogDir = Join-Path $repoRoot 'artifacts/gates'
    Ensure-Directory $gateLogDir

    $coreOut = Join-Path $repoRoot 'artifacts/bench/core'
    $memoryOut = Join-Path $repoRoot 'artifacts/bench/memory'
    Ensure-Directory $coreOut
    Ensure-Directory $memoryOut

    $coreBaseline = Join-Path $repoRoot 'bench/baselines/bench-runner-release-windows-x64-msvc.baseline.json'
    $memoryBaseline = Join-Path $repoRoot 'bench/baselines/bench-runner-memory-release-windows-x64-msvc.baseline.json'

    if (-not (Test-Path $coreBaseline) -or -not (Test-Path $memoryBaseline)) {
        $msg = "Bench baseline missing (core=$coreBaseline, memory=$memoryBaseline)"
        if ($RequireBaseline) {
            Set-GateStatus 'Bench' 'FAIL'
            Fail $msg
        }
        Write-Host "$msg. Bench gate marked PARTIAL." -ForegroundColor Yellow
        return 'PARTIAL'
    }

    $coreLog = Join-Path $gateLogDir ("Bench-core-" + (Get-Date -Format 'yyyyMMdd-HHmmss') + ".log")
    $env:DNG_BENCH_OUT = $coreOut
    $benchAffinity = if ($env:BENCH_AFFINITY_MASK) { $env:BENCH_AFFINITY_MASK } else { '1' }
    $benchPriorityArg = if ($env:BENCH_NORMAL_PRIORITY -eq '1') { '' } else { ' /high' }
    $benchPriorityLabel = if ($env:BENCH_NORMAL_PRIORITY -eq '1') { 'Normal' } else { 'High' }
    $coreWarmup = if ($env:BENCH_CORE_WARMUP) { $env:BENCH_CORE_WARMUP } else { '1' }
    $coreTargetRsd = if ($env:BENCH_CORE_TARGET_RSD) { $env:BENCH_CORE_TARGET_RSD } else { '3' }
    $coreMaxRepeat = if ($env:BENCH_CORE_MAX_REPEAT) { $env:BENCH_CORE_MAX_REPEAT } else { '20' }
    $coreArgs = '--warmup ' + $coreWarmup + ' --target-rsd ' + $coreTargetRsd + ' --max-repeat ' + $coreMaxRepeat + ' --cpu-info'
    Write-Host "Running BenchRunner (core): $($benchExe.FullName) $coreArgs (affinity=$benchAffinity, priority=$benchPriorityLabel, log=$coreLog)"
    $coreCmd = 'start /wait /affinity ' + $benchAffinity + $benchPriorityArg + ' "" "' + $benchExe.FullName + '" ' + $coreArgs
    & cmd.exe /c $coreCmd 2>&1 | Tee-Object -FilePath $coreLog
    if ($LASTEXITCODE -ne 0) {
        Set-GateStatus 'Bench' 'FAIL'
        Fail "BenchRunner core run failed with exit code $LASTEXITCODE"
    }
    Assert-NoLeakMarkers -logPath $coreLog -context 'BenchRunner core run'

    $coreLatest = Get-ChildItem -Path $coreOut -Filter *.bench.json -Recurse -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $coreLatest) {
        Set-GateStatus 'Bench' 'FAIL'
        Fail "BenchRunner core output not found under $coreOut"
    }

    $benchContent = Get-Content -Raw -LiteralPath $coreLatest.FullName -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    $isPlaceholder = $false
    if ($benchContent.metadata -and $benchContent.metadata.note -match 'placeholder') { $isPlaceholder = $true }
    if ($benchContent.benchmarks) {
        $placeholderHits = $benchContent.benchmarks | Where-Object { $_.name -match '^placeholder$' }
        if ($placeholderHits) { $isPlaceholder = $true }
    }
    if ($isPlaceholder) {
        $msg = "BenchRunner produced placeholder artifact ($($coreLatest.FullName))."
        if ($RequireReal) {
            Set-GateStatus 'Bench' 'FAIL'
            Fail $msg
        }
        Write-Host "$msg Marking bench as skipped." -ForegroundColor Yellow
        return 'SKIPPED'
    }

    $coreCompareArgs = @(
        'tools/bench_compare.py',
        $coreBaseline,
        $coreLatest.FullName,
        '--allow-unstable', 'baseline_loop'
    )
    Write-Host "Comparing core bench output: python $($coreCompareArgs -join ' ')"
    $coreCompare = Start-Process -FilePath $python -ArgumentList $coreCompareArgs -NoNewWindow -Wait -PassThru
    if ($coreCompare.ExitCode -ne 0) {
        Set-GateStatus 'Bench' 'FAIL'
        Fail "bench_compare reported core regression"
    }

    $memoryLog = Join-Path $gateLogDir ("Bench-memory-" + (Get-Date -Format 'yyyyMMdd-HHmmss') + ".log")
    $env:DNG_BENCH_OUT = $memoryOut
    $memoryWarmup = if ($env:BENCH_MEMORY_WARMUP) { $env:BENCH_MEMORY_WARMUP } else { '2' }
    $memoryTargetRsd = if ($env:BENCH_MEMORY_TARGET_RSD) { $env:BENCH_MEMORY_TARGET_RSD } else { '8' }
    $memoryMaxRepeat = if ($env:BENCH_MEMORY_MAX_REPEAT) { $env:BENCH_MEMORY_MAX_REPEAT } else { '24' }
    $memoryArgs = '--warmup ' + $memoryWarmup + ' --target-rsd ' + $memoryTargetRsd + ' --max-repeat ' + $memoryMaxRepeat + ' --cpu-info --memory-only --memory-matrix'
    Write-Host "Running BenchRunner (memory): $($benchExe.FullName) $memoryArgs (affinity=$benchAffinity, priority=$benchPriorityLabel, log=$memoryLog)"
    $memoryCmd = 'start /wait /affinity ' + $benchAffinity + $benchPriorityArg + ' "" "' + $benchExe.FullName + '" ' + $memoryArgs
    & cmd.exe /c $memoryCmd 2>&1 | Tee-Object -FilePath $memoryLog
    if ($LASTEXITCODE -ne 0) {
        Set-GateStatus 'Bench' 'FAIL'
        Fail "BenchRunner memory run failed with exit code $LASTEXITCODE"
    }
    Assert-NoLeakMarkers -logPath $memoryLog -context 'BenchRunner memory run'

    $memoryLatest = Get-ChildItem -Path $memoryOut -Filter *.bench.json -Recurse -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $memoryLatest) {
        Set-GateStatus 'Bench' 'FAIL'
        Fail "BenchRunner memory output not found under $memoryOut"
    }

    $memoryThreshold = if ($env:PERF_MEMORY_THRESHOLD_PCT) { $env:PERF_MEMORY_THRESHOLD_PCT } else { '8' }
    $memoryIgnore = if ($env:PERF_MEMORY_IGNORE_BENCH) {
        $env:PERF_MEMORY_IGNORE_BENCH
    } else {
        'small_object_alloc_free_16b,small_object_alloc_free_small'
    }
    $memoryCompareArgs = @(
        'tools/bench_compare.py',
        $memoryBaseline,
        $memoryLatest.FullName,
        '--perf-threshold-pct', $memoryThreshold,
        '--perf-threshold-pct-tracking', $memoryThreshold,
        '--allow-unstable-from-baseline',
        '--ignore-benchmark', $memoryIgnore
    )
    Write-Host "Comparing memory bench output: python $($memoryCompareArgs -join ' ')"
    $memoryCompare = Start-Process -FilePath $python -ArgumentList $memoryCompareArgs -NoNewWindow -Wait -PassThru
    if ($memoryCompare.ExitCode -ne 0) {
        Set-GateStatus 'Bench' 'FAIL'
        Fail "bench_compare reported memory regression"
    }

    return 'OK'
}

try {
    Measure-Gate 'Policy' { Write-Host "=== Policy gate ==="; Run-PolicyLint; Set-GateStatus 'Policy' 'OK' }

    Measure-Gate 'Compile' {
        Write-Host "=== Compile gate ==="
        if ($Fast) { Run-MSBuild 'Release' } else { Run-MSBuild 'Debug'; Run-MSBuild 'Release' }
        Run-MSBuildMemTrackingCompileOnly
        Set-GateStatus 'Compile' 'OK'
    }

    Measure-Gate 'AllSmokes' {
        Write-Host "=== Runtime gate (AllSmokes) ==="
        $allSmokesPreferred = @('x64\Release\AllSmokes.exe', 'x64\Debug\AllSmokes.exe')
        Run-Exe 'AllSmokes*.exe' 'AllSmokes' $allSmokesPreferred -LeakCheck
        Set-GateStatus 'AllSmokes' 'OK'
    }

    Measure-Gate 'MemoryStressSmokes' {
        if ($Fast) {
            Set-GateStatus 'MemoryStressSmokes' 'SKIPPED'
            return
        }

        Write-Host "=== Runtime gate (MemoryStressSmokes) ==="
        $memoryStressPreferred = @('x64\Release\MemoryStressSmokes.exe', 'x64\Debug\MemoryStressSmokes.exe')
        Run-Exe '*MemoryStressSmokes*.exe' 'MemoryStressSmokes' $memoryStressPreferred -LeakCheck
        Set-GateStatus 'MemoryStressSmokes' 'OK'
    }

    Measure-Gate 'ModuleSmoke' {
        Write-Host "=== ABI runtime gate (ModuleSmoke) ==="
        if ($RustModule) {
            Build-RustModule
        }
        else {
            $nullDll = Find-NullWindowModuleDll
            if (-not $nullDll) { Fail "NullWindowModule.dll not found. Build the C/C++ null module (solution build) or use -RustModule." }
        }
        $moduleSmokesPreferred = @('x64\Release\ModuleSmoke.exe', 'x64\Debug\ModuleSmoke.exe')
        Run-Exe '*ModuleSmoke*.exe' 'ModuleSmoke' $moduleSmokesPreferred -LeakCheck
        Set-GateStatus 'ModuleSmoke' 'OK'
    }

    Measure-Gate 'Bench' {
        Write-Host "=== Perf gate (BenchRunner) ==="
        $benchStatus = Run-Bench -RequireReal:$RequireRealBench -RequireBaseline:$RequireBenchBaseline
        if ($benchStatus -eq 'SKIPPED') { Set-GateStatus 'Bench' 'SKIPPED' }
        elseif ($benchStatus -eq 'PARTIAL') { Set-GateStatus 'Bench' 'PARTIAL' }
        elseif ($benchStatus -eq 'OK') { Set-GateStatus 'Bench' 'OK' }
    }
}
catch {
    $err = $_.Exception.Message
    Write-Host "Stopping due to failure: $err" -ForegroundColor Red
    foreach ($k in @($gateState.Keys)) {
        if ($gateState[$k].Status -eq 'PENDING') { $gateState[$k].Status = 'SKIPPED' }
    }
    $gateState['FAILED_STEP'] = [pscustomobject]@{ Status = $err; Required = $true }
}
finally {
    Write-Host "=== Gate Summary ==="
    foreach ($k in $gateState.Keys) {
        if ($k -eq 'FAILED_STEP') { continue }
        $duration = $gateTiming[$k]
        $secs = [math]::Round($duration.TotalSeconds, 2)
        Write-Host "$k : $($gateState[$k].Status) (${secs}s)"
    }
}

$anyFail = ($gateState.Values | Where-Object { $_.Status -eq 'FAIL' }).Count -gt 0
$anySkip = ($gateState.Values | Where-Object { $_.Status -in @('SKIPPED','PARTIAL') }).Count -gt 0
$requiredIncomplete = ($gateState.Values | Where-Object { $_.Required -and $_.Status -ne 'OK' }).Count -gt 0

if ($anyFail) {
    Write-Host "OVERALL: FAIL" -ForegroundColor Red
    exit 1
}
elseif (-not $requiredIncomplete -and $anySkip) {
    Write-Host "OVERALL: PARTIAL PASS (some gates skipped)" -ForegroundColor Yellow
    exit 2
}
elseif (-not $requiredIncomplete -and -not $anySkip) {
    Write-Host "OVERALL: PASS" -ForegroundColor Green
    exit 0
}
else {
    Write-Host "OVERALL: FAIL" -ForegroundColor Red
    exit 1
}
