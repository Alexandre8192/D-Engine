# wdac_assist.ps1
# Purpose : Diagnose and (optionally) remediate WDAC/Smart App Control blocks
#           that prevent local unsigned developer binaries from running.
# Notes   : Disable action is intentionally dry-run by default.

[CmdletBinding()]
param(
    [ValidateSet('Diagnose', 'DisableSmartAppControl')]
    [string]$Action = 'Diagnose',
    [int]$SinceHours = 24,
    [int]$MaxEvents = 200,
    [switch]$Apply,
    [switch]$Restart
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Test-IsAdmin
{
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-SacSummary
{
    $mp = Get-MpComputerStatus
    $ciPath = 'HKLM:\SYSTEM\CurrentControlSet\Control\CI\Policy'
    $ci = Get-ItemProperty -Path $ciPath

    return [pscustomobject]@{
        SmartAppControlState            = $mp.SmartAppControlState
        VerifiedAndReputablePolicyState = $ci.VerifiedAndReputablePolicyState
        SAC_EnforcementReason           = $ci.SAC_EnforcementReason
        EmodePolicyRequired             = $ci.EmodePolicyRequired
        SkuPolicyRequired               = $ci.SkuPolicyRequired
    }
}

function Get-ActivePolicyEvents
{
    $start = (Get-Date).AddHours(-$SinceHours)
    return Get-WinEvent -FilterHashtable @{
        LogName   = 'Microsoft-Windows-CodeIntegrity/Operational'
        StartTime = $start
        Id        = 3099
    } -ErrorAction SilentlyContinue |
        Select-Object -First 20 TimeCreated, Id, Message
}

function Get-BlockEvents
{
    $start = (Get-Date).AddHours(-$SinceHours)
    $events = Get-WinEvent -FilterHashtable @{
        LogName   = 'Microsoft-Windows-CodeIntegrity/Operational'
        StartTime = $start
        Id        = @(3033, 3077)
    } -ErrorAction SilentlyContinue |
        Select-Object -First $MaxEvents TimeCreated, Id, Message

    $pattern = 'D-Engine|AllSmokes\.exe|MemoryStressSmokes\.exe|ModuleSmoke\.exe|D-Engine-BenchRunner\.exe'
    return $events | Where-Object { $_.Message -match $pattern }
}

function Show-DiagnoseReport
{
    $summary = Get-SacSummary
    $activePolicies = Get-ActivePolicyEvents
    $blocks = Get-BlockEvents

    Write-Host '=== WDAC / Smart App Control Summary ==='
    $summary | Format-List | Out-Host

    Write-Host '=== Active Code Integrity Policies (recent) ==='
    if ($activePolicies)
    {
        $activePolicies | Format-Table -AutoSize | Out-Host
    }
    else
    {
        Write-Host '(none in selected time window)'
    }

    Write-Host '=== Relevant Block Events (recent) ==='
    if ($blocks)
    {
        $blocks | Select-Object -First 20 | Format-List | Out-Host
    }
    else
    {
        Write-Host '(none in selected time window)'
    }

    if ($summary.SmartAppControlState -eq 'On')
    {
        Write-Host ''
        Write-Host 'Smart App Control is ON and can block unsigned local dev binaries.' -ForegroundColor Yellow
        Write-Host 'Recommended UI path: Windows Security > App & browser control > Smart App Control > Off'
    }
}

function Disable-SmartAppControl
{
    $summary = Get-SacSummary
    Write-Host 'Current Smart App Control state:' $summary.SmartAppControlState
    Write-Host 'Current VerifiedAndReputablePolicyState:' $summary.VerifiedAndReputablePolicyState
    Write-Host 'Warning: turning Smart App Control off is typically not reversible from UI without OS reset/reinstall.' -ForegroundColor Yellow

    if (-not $Apply)
    {
        Write-Host ''
        Write-Host 'Dry-run mode (no change made).' -ForegroundColor Yellow
        Write-Host 'To apply:'
        Write-Host '  powershell -ExecutionPolicy Bypass -File tools/wdac_assist.ps1 -Action DisableSmartAppControl -Apply -Restart'
        return
    }

    if (-not (Test-IsAdmin))
    {
        throw 'Disable action requires an elevated PowerShell session (Run as Administrator).'
    }

    Write-Host 'Disabling Smart App Control policy flag...' -ForegroundColor Yellow
    Set-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\CI\Policy' -Name 'VerifiedAndReputablePolicyState' -Type DWord -Value 0

    $updated = Get-SacSummary
    Write-Host 'Updated VerifiedAndReputablePolicyState:' $updated.VerifiedAndReputablePolicyState
    Write-Host 'A reboot is required for the change to fully take effect.' -ForegroundColor Yellow

    if ($Restart)
    {
        Write-Host 'Restarting now...'
        Restart-Computer -Force
    }
}

switch ($Action)
{
    'Diagnose' { Show-DiagnoseReport }
    'DisableSmartAppControl' { Disable-SmartAppControl }
    default { throw "Unsupported action: $Action" }
}
