# Resume unfinished offline work. Safe to run any time, including after a
# power cut: tasks stuck in 'running' with a dead pid are put back to 'pending'.
# SPDX-License-Identifier: MIT
[CmdletBinding()]
param([int]$MaxTasks = 20, [switch]$StatusOnly)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lw_night_lib.ps1')

$plan = Read-NightPlan

# 1. reclaim tasks whose runner died
$reclaimed = @()
foreach ($t in $plan.tasks) {
    if ($t.status -ne 'running') { continue }
    $alive = $false
    foreach ($f in Get-ChildItem $script:StateDir -Filter 'lock-*.lock' -ErrorAction SilentlyContinue) {
        $info = Get-Content $f.FullName -Raw | ConvertFrom-Json -ErrorAction SilentlyContinue
        if ($info -and $info.task -eq $t.id -and $info.pid) {
            $alive = ($null -ne (Get-Process -Id $info.pid -ErrorAction SilentlyContinue))
        }
    }
    if ($alive) { Write-Output "$($t.id) is still running (pid alive); leaving it alone"; continue }
    $reclaimed += $t.id
    $t.status = 'pending'
    if ($t.last_run) { $t.last_run.note = 'resumed after abnormal exit' }
}
if ($reclaimed.Count -gt 0) {
    Save-NightPlan $plan
    Write-Output ("reclaimed: " + ($reclaimed -join ', '))
}

# 2. report what is left, split by who can actually do it
$pendingScript = @($plan.tasks | Where-Object { $_.status -eq 'pending' -and $_.kind -eq 'script' })
$pendingAgent  = @($plan.tasks | Where-Object { $_.status -eq 'pending' -and $_.kind -eq 'agent' })
Write-Output "pending scripts: $($pendingScript.Count)   pending agent tasks: $($pendingAgent.Count)"
foreach ($t in $pendingAgent) { Write-Output "  agent-only: $($t.id) - $($t.title)" }

if ($StatusOnly -or $pendingScript.Count -eq 0) { exit 0 }

# 3. continue: run due script tasks
$ran = 0
while ($ran -lt $MaxTasks) {
    $p = Read-NightPlan
    $task = Get-NightNextTask $p
    if (-not $task) { break }
    if ($task.kind -eq 'agent') { Write-Output "next is agent-only ($($task.id)); stopping here"; break }
    Write-Output "running $($task.id) ..."
    $result = Invoke-NightTask $p $task
    Write-Output "  -> $result"
    $ran++
    if ($result -in @('blocked', 'timeout', 'dirty')) { break }
}
$summary = Write-NightSummaryFile (Read-NightPlan)
Write-Output "summary: $summary"
