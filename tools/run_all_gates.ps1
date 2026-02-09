# run_all_gates.ps1
# Purpose : Local one-shot runner for D-Engine gates (lint, build, smokes, ABI smoke, bench).
# Modes   :
#   -Fast          : policy lint (strict+modules), Release build, AllSmokes, ModuleSmoke; bench skipped.
#   -RequireBench  : bench is required; missing BenchRunner fails. Uses CI args (--warmup 1 --target-rsd 3 --max-repeat 7).
#   -RustModule    : build/copy Rust NullWindowModule via cargo before ModuleSmoke; fails if cargo missing.
#   -RequireRealBench     : fail if BenchRunner artifact is a placeholder stub.
#   -RequireBenchBaseline : fail if bench baseline is missing.
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

$gateState = [ordered]@{
    Policy      = [pscustomobject]@{ Status = 'PENDING'; Required = $true }
    Compile     = [pscustomobject]@{ Status = 'PENDING'; Required = $true }
    AllSmokes   = [pscustomobject]@{ Status = 'PENDING'; Required = $true }
    ModuleSmoke = [pscustomobject]@{ Status = 'PENDING'; Required = $true }
    Bench       = [pscustomobject]@{ Status = 'PENDING'; Required = $benchRequired }
}

$gateTiming = [ordered]@{
    Policy      = [timespan]::Zero
    Compile     = [timespan]::Zero
    AllSmokes   = [timespan]::Zero
    ModuleSmoke = [timespan]::Zero
    Bench       = [timespan]::Zero
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

function Find-Python {
    $py = Get-Command python -ErrorAction SilentlyContinue
    if (-not $py) { Fail "Python not found in PATH. Install Python 3.x and re-run." }
    return $py.Path
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

function Run-Exe([string]$pattern, [string]$friendly, [string[]]$preferredPaths) {
    $exe = Resolve-ExePath -preferredPaths $preferredPaths -fallbackPattern $pattern -friendly $friendly
    Write-Host "Running ${friendly}: $exe"
    $p = Start-Process -FilePath $exe -NoNewWindow -Wait -PassThru
    if ($p.ExitCode -ne 0) { Fail "$friendly failed with exit code $($p.ExitCode)" }
}

function Find-NullWindowModuleDll {
    $dll = Get-ChildItem -Path $repoRoot -Recurse -Filter 'NullWindowModule.dll' -ErrorAction SilentlyContinue |
        Sort-Object @{Expression = { $_.FullName -match 'Release' }; Descending = $true}, LastWriteTime -Descending |
        Select-Object -First 1
    return $dll
}

function Build-RustModule {
    $cargo = Get-Command cargo -ErrorAction SilentlyContinue
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

    $env:DNG_BENCH_OUT = "artifacts/bench"
    $benchArgs = @('--warmup', '1', '--target-rsd', '3', '--max-repeat', '7')
    Write-Host "Running BenchRunner: $($benchExe.FullName) $($benchArgs -join ' ') (affinity=1, priority=High)"
    $cmdArgs = @('/c', 'start', '/wait', '/affinity', '1', '/high', '', $benchExe.FullName) + $benchArgs
    $p = Start-Process -FilePath 'cmd.exe' -ArgumentList $cmdArgs -NoNewWindow -Wait -PassThru
    if ($p.ExitCode -ne 0) {
        Set-GateStatus 'Bench' 'FAIL'
        Fail "BenchRunner failed with exit code $($p.ExitCode)"
    }

    $latest = Get-ChildItem -Path "$($env:DNG_BENCH_OUT)" -Filter *.bench.json -Recurse -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $baseline = Join-Path $repoRoot 'bench/baselines/bench-runner-release-windows-x64-msvc.baseline.json'
    if (-not $latest) {
        Write-Host "BenchRunner output not found under $env:DNG_BENCH_OUT" -ForegroundColor Yellow
        return 'PARTIAL'
    }

    $benchContent = Get-Content -Raw -LiteralPath $latest.FullName -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    $isPlaceholder = $false
    if ($benchContent.metadata -and $benchContent.metadata.note -match 'placeholder') { $isPlaceholder = $true }
    if ($benchContent.benchmarks) {
        $placeholderHits = $benchContent.benchmarks | Where-Object { $_.name -match '^placeholder$' }
        if ($placeholderHits) { $isPlaceholder = $true }
    }

    if ($isPlaceholder) {
        $msg = "BenchRunner produced placeholder artifact ($($latest.FullName))."
        if ($RequireReal) {
            Set-GateStatus 'Bench' 'FAIL'
            Fail $msg
        }
        Write-Host "$msg Marking bench as skipped." -ForegroundColor Yellow
        return 'SKIPPED'
    }

    if (-not (Test-Path $baseline)) {
        $msg = "Bench baseline not found ($baseline). Latest result: $($latest.FullName)"
        if ($RequireBaseline) {
            Set-GateStatus 'Bench' 'FAIL'
            Fail $msg
        }
        Write-Host $msg -ForegroundColor Yellow
        return 'PARTIAL'
    }

    $python = Find-Python
    $compareArgs = @('tools/bench_compare.py', $baseline, $latest.FullName)
    Write-Host "Comparing bench output: python $($compareArgs -join ' ')"
    $p2 = Start-Process -FilePath $python -ArgumentList $compareArgs -NoNewWindow -Wait -PassThru
    if ($p2.ExitCode -ne 0) {
        Set-GateStatus 'Bench' 'FAIL'
        Fail "bench_compare reported regression"
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
        Run-Exe 'AllSmokes*.exe' 'AllSmokes' $allSmokesPreferred
        Set-GateStatus 'AllSmokes' 'OK'
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
        Run-Exe '*ModuleSmoke*.exe' 'ModuleSmoke' $moduleSmokesPreferred
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
