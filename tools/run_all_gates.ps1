# run_all_gates.ps1
# Purpose: Local one-shot runner for D-Engine gates (lint, build, smokes, ABI smoke, bench).
# Contract: Fails fast on first error; exits non-zero on any failure. ASCII-only comments.

param()

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $repoRoot

$results = [ordered]@{
    Policy     = 'PENDING'
    Compile    = 'PENDING'
    AllSmokes  = 'PENDING'
    ModuleSmoke= 'PENDING'
    Bench      = 'PENDING'
}

function Fail($msg) {
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
    $cmds = @(
        @($python, "tools/policy_lint.py"),
        @($python, "tools/policy_lint.py", "--strict"),
        @($python, "tools/policy_lint.py", "--strict", "--modules")
    )
    foreach ($c in $cmds) {
        Write-Host "Running: $($c -join ' ')"
        $p = Start-Process -FilePath $c[0] -ArgumentList $c[1..($c.Length-1)] -NoNewWindow -Wait -PassThru
        if ($p.ExitCode -ne 0) { Fail "policy_lint failed (command: $($c -join ' '))" }
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

    # Fallback search under common Visual Studio install roots (C:\Program Files / Program Files (x86)).
    $roots = @($Env:ProgramFiles, ${Env:ProgramFiles(x86)}) | Where-Object { $_ -and (Test-Path $_) }
    foreach ($root in $roots) {
        $candidate = Get-ChildItem -Path (Join-Path $root 'Microsoft Visual Studio') -Recurse -Filter 'MSBuild.exe' -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -like '*\\MSBuild\\*\\Bin\\MSBuild.exe' } |
            Sort-Object LastWriteTime -Descending |
            Select-Object -First 1
        if ($candidate) { return $candidate.FullName }
    }

    # Legacy .NET Framework fallback (DevCmd often sets this).
    $legacy = Join-Path $Env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\MSBuild.exe'
    if (Test-Path $legacy) { return $legacy }

    Fail "msbuild not found via PATH or Visual Studio install roots. Install Visual Studio Build Tools or add msbuild to PATH."
}

function Run-MSBuild($config) {
    $msbuild = Get-MSBuildPath
    $args = @("D-Engine.sln", "/m", "/p:Configuration=$config", "/p:Platform=x64")
    Write-Host "Building $config with: $msbuild $($args -join ' ')"
    $p = Start-Process -FilePath $msbuild -ArgumentList $args -NoNewWindow -Wait -PassThru
    if ($p.ExitCode -ne 0) { Fail "msbuild failed for $config" }
}

function Run-Exe($pattern, $friendly) {
    $candidates = Get-ChildItem -Path $repoRoot -Recurse -Filter $pattern -ErrorAction SilentlyContinue |
        Sort-Object @{Expression = { $_.FullName -match 'Release' }; Descending = $true}, LastWriteTime -Descending
    if (-not $candidates) { Fail "$friendly executable not found (pattern $pattern)" }
    $exe = $candidates[0].FullName
    Write-Host "Running ${friendly}: $exe"
    $p = Start-Process -FilePath $exe -NoNewWindow -Wait -PassThru
    if ($p.ExitCode -ne 0) { Fail "$friendly failed with exit code $($p.ExitCode)" }
}

function Run-Bench {
    $benchExe = Get-ChildItem -Path $repoRoot -Recurse -Filter 'D-Engine-BenchRunner.exe' -ErrorAction SilentlyContinue |
        Sort-Object @{Expression = { $_.FullName -match 'Release' }; Descending = $true}, LastWriteTime -Descending |
        Select-Object -First 1
    if (-not $benchExe) { Fail "BenchRunner executable not found" }
    $env:DNG_BENCH_OUT = "artifacts/bench"
    $args = @("--warmup", "1", "--target-rsd", "3", "--max-repeat", "7")
    Write-Host "Running BenchRunner: $($benchExe.FullName) $($args -join ' ')"
    $p = Start-Process -FilePath $benchExe.FullName -ArgumentList $args -NoNewWindow -Wait -PassThru
    if ($p.ExitCode -ne 0) { Fail "BenchRunner failed with exit code $($p.ExitCode)" }

    $latest = Get-ChildItem -Path "$($env:DNG_BENCH_OUT)" -Filter *.bench.json -Recurse -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $baseline = Join-Path $repoRoot "bench/baselines/bench-runner-release-windows-x64-msvc.baseline.json"
    if (-not $latest) {
        Write-Host "BenchRunner output not found under $env:DNG_BENCH_OUT"
        return
    }
    if (-not (Test-Path $baseline)) {
        Write-Host "Bench baseline not found ($baseline). Latest result: $($latest.FullName)"
        return
    }
    $python = Find-Python
    $compareArgs = @("tools/bench_compare.py", $baseline, $latest.FullName)
    Write-Host "Comparing bench output: python $($compareArgs -join ' ')"
    $p2 = Start-Process -FilePath $python -ArgumentList $compareArgs -NoNewWindow -Wait -PassThru
    if ($p2.ExitCode -ne 0) { Fail "bench_compare reported regression" }
}

try {
    Write-Host "=== Policy gate ==="
    Run-PolicyLint
    $results["Policy"] = 'OK'

    Write-Host "=== Compile gate ==="
    Run-MSBuild "Debug"
    Run-MSBuild "Release"
    $results["Compile"] = 'OK'

    Write-Host "=== Runtime gate (AllSmokes) ==="
    Run-Exe 'AllSmokes*.exe' 'AllSmokes'
    $results["AllSmokes"] = 'OK'

    Write-Host "=== ABI runtime gate (ModuleSmoke) ==="
    Run-Exe '*ModuleSmoke*.exe' 'ModuleSmoke'
    $results["ModuleSmoke"] = 'OK'

    Write-Host "=== Perf gate (BenchRunner) ==="
    Run-Bench
    $results["Bench"] = 'OK'
}
catch {
    $err = $_.Exception.Message
    Write-Host "Stopping due to failure: $err" -ForegroundColor Red
    foreach ($k in @($results.Keys)) {
        if ($results[$k] -eq 'PENDING') { $results[$k] = 'SKIPPED' }
    }
    $results["FAILED_STEP"] = $err
    $exitCode = 1
}
finally {
    Write-Host "=== Gate Summary ==="
    foreach ($k in $results.Keys) {
        Write-Host "$k : $($results[$k])"
    }
    if (-not $exitCode) { $exitCode = 0 }
}

if ($exitCode -ne 0) { exit $exitCode }
Write-Host "ALL GATES PASSED" -ForegroundColor Green
exit 0
