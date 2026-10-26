# Safe stop. Disables and unregisters the scheduled task but does NOT kill a
# build that is already running: killing mid-compile is how you get a broken
# build dir. In-flight work finishes and records its own result.
# SPDX-License-Identifier: MIT
[CmdletBinding()]
param([string]$TaskName = 'LittleWorld-Nightly-Run', [switch]$KillRunning)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lw_night_lib.ps1')

$sched = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
if ($sched) {
    try { Disable-ScheduledTask -TaskName $TaskName -ErrorAction Stop | Out-Null; Write-Output "disabled $TaskName" } catch { Write-Output "disable failed: $($_.Exception.Message)" }
    if ($KillRunning) {
        Stop-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
        Write-Output "stopped running instance of $TaskName (-KillRunning)"
    } else {
        $running = @(Get-ScheduledTask -TaskName $TaskName | Get-ScheduledTaskInfo | Where-Object { $_.LastTaskResult -eq 267009 })
        if ($running.Count -gt 0) { Write-Output 'a run is in progress; it will finish on its own (use -KillRunning to abort)' }
    }
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
    Write-Output "unregistered $TaskName"
} else {
    Write-Output "$TaskName was not registered"
}

# Release locks owned by dead processes only. A live lock belongs to a live task.
$freed = 0
foreach ($f in Get-ChildItem $script:StateDir -Filter 'lock-*.lock' -ErrorAction SilentlyContinue) {
    $info = Get-Content $f.FullName -Raw | ConvertFrom-Json -ErrorAction SilentlyContinue
    $alive = $false
    if ($info -and $info.pid) { $alive = ($null -ne (Get-Process -Id $info.pid -ErrorAction SilentlyContinue)) }
    if (-not $alive) { Remove-Item $f.FullName -Force; $freed++ }
}
Write-Output "released $freed stale lock(s)"

$plan = Read-NightPlan
$leftover = @($plan.tasks | Where-Object { $_.status -eq 'pending' })
Write-Output "remaining pending tasks: $($leftover.Count)"
foreach ($t in $leftover) { Write-Output "  - $($t.id) ($($t.kind))" }
Write-Output 'nothing was flashed, no serial port was opened, git history was not touched'
