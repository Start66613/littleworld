# Human-readable progress view. Read-only: never changes the plan.
# SPDX-License-Identifier: MIT
[CmdletBinding()]
param([string]$TaskName = 'LittleWorld-Nightly-Run')

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lw_night_lib.ps1')

$plan = Read-NightPlan
$now = Get-Date
$t0 = $null
if ($plan.t0) { $t0 = [datetime]::Parse($plan.t0, $null, [System.Globalization.DateTimeStyles]::RoundtripKind) }

Write-Output '=== Little World Night Runner ==='
Write-Output "plan     : $($plan.plan_id)"
if ($t0) {
    $elapsed = [int]($now - $t0).TotalMinutes
    Write-Output "t0       : $($t0.ToString('yyyy-MM-dd HH:mm:ss'))  (elapsed ${elapsed} min)"
} else {
    Write-Output 't0       : not started'
}
Write-Output ''

$fmt = '{0,-24} {1,-6} {2,-7} {3,-9} {4,3}  {5}'
Write-Output ($fmt -f 'TASK', 'PHASE', 'KIND', 'STATUS', 'TRY', 'NOTE')
Write-Output ('-' * 120)
foreach ($t in ($plan.tasks | Sort-Object { [double]$_.due_after_min })) {
    $due = ''
    if ($t0) {
        $at = $t0.AddMinutes([double]$t.due_after_min)
        $due = $at.ToString('HH:mm')
    } else {
        $due = "+$($t.due_after_min)m"
    }
    $attempts = if ($t.last_run) { $t.last_run.attempts } else { 0 }
    $note = if ($t.last_run) { $t.last_run.note } else { '' }
    if ($note.Length -gt 60) { $note = $note.Substring(0, 57) + '...' }
    Write-Output ($fmt -f $t.id, $t.phase, $t.kind, $t.status, $attempts, "$due  $note")
}

Write-Output ''
$counts = @{}
foreach ($t in $plan.tasks) { $counts[$t.status] = 1 + [int]$counts[$t.status] }
$parts = $counts.GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Key)=$($_.Value)" }
Write-Output ("totals   : " + ($parts -join '  '))

$agentPending = @($plan.tasks | Where-Object { $_.kind -eq 'agent' -and $_.status -ne 'done' })
if ($agentPending.Count -gt 0) {
    Write-Output ''
    Write-Output 'agent-only (needs a DSH session, a scheduled script cannot do these):'
    foreach ($t in $agentPending) { Write-Output "  - $($t.id): $($t.title)" }
}

$sched = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
Write-Output ''
if ($sched) {
    $info = Get-ScheduledTaskInfo -TaskName $TaskName -ErrorAction SilentlyContinue
    Write-Output "scheduled: $TaskName  state=$($sched.State)  last=$($info.LastRunTime)  result=$($info.LastTaskResult)"
} else {
    Write-Output 'scheduled: <not registered>'
}
$verifyFile = Join-Path $script:LogDir 'scheduled-verify.json'
if (Test-Path $verifyFile) {
    $v = Get-Content $verifyFile -Raw | ConvertFrom-Json
    Write-Output "verified : task really ran at $($v.marker_at) (pid $($v.marker_pid))"
}
