# Entry point invoked BY the Windows scheduled task. One shot, then self-delete.
# SPDX-License-Identifier: MIT
#
# -VerifyMode : instead of waiting for a trigger, prove the task can really run:
#               register it, force Start-ScheduledTask, wait for evidence, unregister.
#               This exists because "registered successfully" is not evidence.
[CmdletBinding()]
param(
    [string]$TaskName = 'LittleWorld-Nightly-Run',
    [switch]$VerifyMode,
    [int]$WaitSeconds = 300
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lw_night_lib.ps1')

Initialize-NightDirs
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$log = Join-Path $script:LogDir "scheduled-$stamp.log"

function Write-Both {
    param([string]$Message)
    Write-Output $Message
    Add-Content -Path $log -Value ("[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $Message) -Encoding UTF8
}

Write-Both "night task start pid=$PID verify=$($VerifyMode.IsPresent)"

if ($VerifyMode) {
    # Registration alone proves nothing, so drive it and look for the marker the
    # task itself writes. Any of: no task, no run, no marker -> hard fail.
    $existing = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    if ($existing) { Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false }
    $marker = Join-Path $script:ReportDir 'last-scheduled-run.json'
    if (Test-Path $marker) { Remove-Item $marker -Force }

    $arg = "-NoProfile -ExecutionPolicy Bypass -File `"$PSScriptRoot\lw_night_task.ps1`" -TaskName $TaskName"
    $act = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument $arg -WorkingDirectory $script:Root
    # A Once trigger from New-ScheduledTaskTrigger ships without EndBoundary on
    # some builds, and Register-ScheduledTask then fails with 0x80041319
    # ("task XML is missing a required element"). An explicit boundary fixes it
    # and gives constraint 10 its auto-expiry for free.
    $trg = New-ScheduledTaskTrigger -Once -At ((Get-Date).AddHours(6)) -RandomDelay (New-TimeSpan -Seconds 1)
    $trg.EndBoundary = ((Get-Date).AddHours(6).AddMinutes(30)).ToString('yyyy-MM-ddTHH:mm:ss')
    $prc = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" -LogonType Interactive -RunLevel Limited
    $set = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
        -ExecutionTimeLimit (New-TimeSpan -Minutes 30) -DeleteExpiredTaskAfter (New-TimeSpan -Hours 2) `
        -MultipleInstances IgnoreNew
    Register-ScheduledTask -TaskName $TaskName -Action $act -Trigger $trg -Principal $prc -Settings $set `
        -Description 'Little World Night Runner: one-shot offline test pass.' | Out-Null
    Write-Both "registered $TaskName for verification"

    Start-ScheduledTask -TaskName $TaskName
    $deadline = (Get-Date).AddSeconds($WaitSeconds)
    $ok = $false
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 2
        if (Test-Path $marker) { $ok = $true; break }
    }
    $info = Get-ScheduledTaskInfo -TaskName $TaskName -ErrorAction SilentlyContinue
    Write-Both "last result=$($info.LastTaskResult) at=$($info.LastRunTime)"

    if ($ok) {
        $payload = Get-Content $marker -Raw | ConvertFrom-Json
        Write-Both "marker pid=$($payload.pid) at=$($payload.at)"
        $proof = [ordered]@{
            at          = (Get-Date).ToString('o')
            task        = $TaskName
            ran         = $true
            marker_pid  = $payload.pid
            marker_at   = $payload.at
            schtask_result = $info.LastTaskResult
        }
        [System.IO.File]::WriteAllText((Join-Path $script:LogDir 'scheduled-verify.json'),
            ($proof | ConvertTo-Json -Depth 4), (New-Object System.Text.UTF8Encoding($false)))
        # The spawned task deletes itself when it finishes (constraint 10), so by
        # now it is usually already gone. That is success, not a failure.
        if (Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue) {
            Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
        }
        Write-Both 'self-deleted scheduled task (finished run)'
        Write-Both 'NIGHTLY SCHEDULED RUN CONFIRMED'
        exit 0
    }
    Write-Both 'NIGHTLY SCHEDULED RUN NOT CONFIRMED'
    if (Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue) {
        Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
    }
    exit 1
}

# --- normal one-shot body ---------------------------------------------------
try {
    $proof = [ordered]@{ at = (Get-Date).ToString('o'); pid = $PID; task = $TaskName; host = $env:COMPUTERNAME }
    [System.IO.File]::WriteAllText((Join-Path $script:ReportDir 'last-scheduled-run.json'),
        ($proof | ConvertTo-Json -Depth 4), (New-Object System.Text.UTF8Encoding($false)))
    Write-Both 'marker written'

    $plan = Read-NightPlan
    $task = Get-NightNextTask $plan
    if (-not $task) {
        Write-Both 'no due task; exiting'
    } elseif ($task.kind -eq 'agent') {
        Write-Both "next due task '$($task.id)' is agent-only; a scheduled script cannot run it"
    } else {
        $result = Invoke-NightTask $plan $task
        Write-Both "task $($task.id) -> $result"
    }
    $summary = Write-NightSummaryFile (Read-NightPlan)
    Write-Both "summary $summary"
} catch {
    Write-Both "ERROR $($_.Exception.Message)"
    exit 1
} finally {
    # Constraint 10: this run is one-shot. Remove the task so nothing recurs.
    $existing = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    if ($existing) {
        Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
        Write-Both "self-deleted scheduled task $TaskName"
    }
}
exit 0
