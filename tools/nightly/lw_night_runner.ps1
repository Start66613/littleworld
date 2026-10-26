# Night Runner entry point.
# SPDX-License-Identifier: MIT
#
#   -Action Status    [default] print where the plan stands
#   -Action RunNext   run the single next due task, then stop
#   -Action RunDue    run due tasks until none remain due
#   -Action Resume    reclaim stale 'running' tasks, then behave like RunDue
#   -Action Install   register the one-shot Windows scheduled task
#   -Action Verify    prove the scheduled task actually starts and runs
#   -Action Stop      disable + unregister the scheduled task (safe stop)
#   -Action Uninstall delete the scheduled task only
#   -Action SelfTest  exercise the guards, locks, timeout and resume logic
#
# This script never flashes, never opens a serial port and never touches
# git history. See README.md.
[CmdletBinding()]
param(
    [ValidateSet('Status', 'RunNext', 'RunDue', 'Resume', 'Install', 'Verify', 'Stop', 'Uninstall', 'SelfTest')]
    [string]$Action = 'Status',
    [int]$MaxTasks = 20,
    [string]$TaskName = 'LittleWorld-Nightly-Run'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lw_night_lib.ps1')

function Show-Status {
    $plan = Read-NightPlan
    Initialize-NightDirs
    Write-Output "plan      : $($plan.plan_id)"
    Write-Output "t0        : $(if ($plan.t0) { $plan.t0 } else { '<not started>' })"
    Write-Output "plan file : $($script:PlanPath)"
    Write-Output ''
    $rows = Get-NightSummary $plan
    $rows | Format-Table -AutoSize id, phase, kind, status, attempts, note | Out-String -Width 160 | Write-Output
    $next = Get-NightNextTask $plan
    if ($next) {
        Write-Output "next due  : $($next.id)  ($($next.title))"
    } else {
        Write-Output 'next due  : <none>'
    }
    $sched = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    if ($sched) {
        $info = Get-ScheduledTaskInfo -TaskName $TaskName -ErrorAction SilentlyContinue
        Write-Output "scheduled : $TaskName state=$($sched.State) last=$($info.LastRunTime) result=$($info.LastTaskResult) next=$($info.NextRunTime)"
    } else {
        Write-Output 'scheduled : <not registered>'
    }
}

function Invoke-Due {
    param([int]$Limit)
    $ran = 0
    while ($ran -lt $Limit) {
        $plan = Read-NightPlan
        $task = Get-NightNextTask $plan
        if (-not $task) { break }
        if ($task.kind -eq 'agent') {
            # An 'agent' task needs the DSH session: it cannot be executed by a
            # scheduled script. Mark it for the next interactive launch and stop.
            Write-Output "agent-only task waiting for the next DSH launch: $($task.id)"
            break
        }
        Write-Output "running $($task.id) ..."
        $result = Invoke-NightTask $plan $task
        Write-Output "  -> $result"
        $ran++
        if ($result -in @('blocked', 'timeout', 'dirty')) { break }
    }
    $plan = Read-NightPlan
    $summary = Write-NightSummaryFile $plan
    Write-Output "summary: $summary"
    return $ran
}

switch ($Action) {
    'Status' { Show-Status }
    'RunNext' {
        $plan = Read-NightPlan
        $task = Get-NightNextTask $plan
        if (-not $task) { Write-Output 'no due task'; break }
        if ($task.kind -eq 'agent') { Write-Output "agent-only task waiting for the next DSH launch: $($task.id)"; break }
        $result = Invoke-NightTask $plan $task
        Write-Output "$($task.id) -> $result"
    }
    'RunDue' { $null = Invoke-Due -Limit $MaxTasks }
    'Resume' {
        $plan = Read-NightPlan
        $reclaimed = 0
        foreach ($t in $plan.tasks) {
            if ($t.status -ne 'running') { continue }
            $t.status = 'pending'
            if ($t.last_run) { $t.last_run.note = 'resumed after an abnormal exit' }
            $reclaimed++
        }
        if ($reclaimed -gt 0) {
            Save-NightPlan $plan
            Write-Output "reclaimed $reclaimed task(s) stuck in 'running'"
        } else {
            Write-Output 'nothing to reclaim'
        }
        $null = Invoke-Due -Limit $MaxTasks
    }
    'Install' {
        # One-shot, self-deleting, user-visible. NOT an autonomous agent: it runs
        # exactly one verified offline script and then removes itself.
        $exe = 'powershell.exe'
        $arg = "-NoProfile -ExecutionPolicy Bypass -File `"$PSScriptRoot\lw_night_task.ps1`" -TaskName $TaskName"
        $start = (Get-Date).AddMinutes(2)
        $existing = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
        if ($existing) {
            Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
        }
        $act = New-ScheduledTaskAction -Execute $exe -Argument $arg -WorkingDirectory $script:Root
        $trg = New-ScheduledTaskTrigger -Once -At $start
        $prc = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" -LogonType Interactive -RunLevel Limited
        $set = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
            -ExecutionTimeLimit (New-TimeSpan -Minutes 120) -DeleteExpiredTaskAfter (New-TimeSpan -Hours 6) `
            -MultipleInstances IgnoreNew -StartWhenAvailable
        Register-ScheduledTask -TaskName $TaskName -Action $act -Trigger $trg -Principal $prc -Settings $set `
            -Description 'Little World Night Runner: one-shot offline test pass. Deletes itself after one run.' | Out-Null
        Write-Output "registered $TaskName (one-shot at $($start.ToString('HH:mm:ss')))"
        Write-Output "NOTE: registration is not proof. Run -Action Verify to watch it execute."
    }
    'Uninstall' {
        $existing = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
        if ($existing) { Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false; Write-Output "unregistered $TaskName" }
        else { Write-Output "$TaskName was not registered" }
    }
    'Stop' {
        & (Join-Path $PSScriptRoot 'stop_nightly.ps1') -TaskName $TaskName
    }
    'Verify' {
        & (Join-Path $PSScriptRoot 'lw_night_task.ps1') -TaskName $TaskName -VerifyMode
    }
    'SelfTest' {
        & (Join-Path $PSScriptRoot 'lw_night_selftest.ps1')
    }
    default { Show-Status }
}
