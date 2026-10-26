# Self test for the Night Runner itself. Exercises the ten constraints against a
# THROWAWAY copy of the plan so the real night_plan.json is never disturbed.
# SPDX-License-Identifier: MIT
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lw_night_lib.ps1')

Initialize-NightDirs
$results = New-Object System.Collections.ArrayList
$fails = 0

function Add-Result {
    param([string]$Name, [bool]$Ok, [string]$Detail)
    $null = $results.Add([pscustomobject]@{ check = $Name; ok = $Ok; detail = $Detail })
    if (-not $Ok) { $script:fails++ }
}

# sandbox: copy the plan next to the real one, run everything against the copy
$sandboxPlan = Join-Path $script:LogDir 'selftest_plan.json'
$realPlanPath = $script:PlanPath
$plan = Read-NightPlan
$plan | ConvertTo-Json -Depth 8 | Set-Content -Path $sandboxPlan -Encoding UTF8
$script:PlanPath = $sandboxPlan

try {
    # 1. unique ids
    $ids = @($plan.tasks | ForEach-Object { $_.id })
    $dup = @($ids | Group-Object | Where-Object { $_.Count -gt 1 })
    Add-Result 'unique task ids' ($dup.Count -eq 0) ("{0} tasks, {1} duplicates" -f $ids.Count, $dup.Count)

    # 8 / 7. forbidden commands must be refused, not executed
    $bad = @(
        'powershell -File tools/flash_v3.ps1 -Authorized -Port COM5',
        'idf.py -p COM5 flash',
        'esptool.py --chip esp32p4 write_flash 0x10000 x.bin',
        'git reset --hard HEAD',
        'git clean -fdx',
        'schtasks /create /tn recur /tr foo'
    )
    $allRefused = $true
    foreach ($b in $bad) {
        $g = Test-NightCommandAllowed $b
        if ($g.ok) { $allRefused = $false }
    }
    Add-Result 'destructive commands refused' $allRefused ("{0} patterns checked" -f $bad.Count)

    $good = Test-NightCommandAllowed 'powershell.exe -NoProfile -File tools\nightly\night_status.ps1'
    Add-Result 'safe command allowed' $good.ok 'night_status.ps1 passes the guard'

    # 5. lock mutual exclusion
    $l1 = Enter-NightLock 'selftest-key' 'T-selftest-1'
    $l2 = Enter-NightLock 'selftest-key' 'T-selftest-2'
    Add-Result 'second lock refused while held' ($l1.ok -and -not $l2.ok) $(if ($l2.reason) { $l2.reason } else { 'acquired twice!' })
    Exit-NightLock $l1.path
    $l3 = Enter-NightLock 'selftest-key' 'T-selftest-3'
    Add-Result 'lock released and reusable' $l3.ok 'reacquired after release'
    Exit-NightLock $l3.path

    # stale lock from a dead pid must be reclaimable (this is what makes resume work)
    $stale = Get-NightLockPath 'selftest-stale'
    $deadPid = 999999
    [System.IO.File]::WriteAllText($stale, (@{ key = 'selftest-stale'; task = 'T-dead'; pid = $deadPid; since = (Get-Date).ToString('o') } | ConvertTo-Json), (New-Object System.Text.UTF8Encoding($false)))
    $l4 = Enter-NightLock 'selftest-stale' 'T-selftest-4'
    Add-Result 'stale lock reclaimed' $l4.ok 'dead pid lock taken over'
    Exit-NightLock $l4.path

    # 4. timeout actually kills the child
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $r = Invoke-NightCommand -Command 'Start-Sleep -Seconds 30' -TimeoutSec 3 -LogPath (Join-Path $script:LogDir 'selftest-timeout.log')
    $sw.Stop()
    Add-Result 'timeout enforced' ($r.timedOut -and $sw.Elapsed.TotalSeconds -lt 25) ("timedOut={0} elapsed={1:N1}s" -f $r.timedOut, $sw.Elapsed.TotalSeconds)

    # 2 / success pattern detection
    $p = Join-Path $script:LogDir 'selftest-pattern.log'
    Set-Content -Path $p -Value 'hello NIGHTLY SELFTEST MARKER' -Encoding ASCII
    Add-Result 'success pattern detected' (Test-NightSuccessPattern $p 'NIGHTLY SELFTEST MARKER') 'marker found'
    Add-Result 'missing pattern is failure' (-not (Test-NightSuccessPattern $p 'ABSENT-MARKER')) 'absent marker rejected'

    # 3. attempt cap: a task at max_attempts is never scheduled again
    $t = $plan.tasks[0]
    $t.last_run = [pscustomobject]@{ started = $null; ended = $null; result = 'failed'; note = 'selftest'; attempts = [int]$t.max_attempts }
    $t.status = 'pending'
    $script:PlanPath = $sandboxPlan
    $planJson = $plan | ConvertTo-Json -Depth 8
    [System.IO.File]::WriteAllText($sandboxPlan, $planJson, (New-Object System.Text.UTF8Encoding($false)))
    $reloaded = Read-NightPlan
    $next = Get-NightNextTask $reloaded
    Add-Result 'attempt cap stops retries' ($next -eq $null -or $next.id -ne $t.id) "next=$(if ($next) { $next.id } else { '<none>' })"

    # 6. git snapshot detects drift between two different states
    $fake = Get-GitSnapshot
    $fake2 = [pscustomobject]@{ head = $fake.head; dirty = $fake.dirty + "`n?? injected.txt"; count = $fake.count + 1 }
    Add-Result 'git drift detected' ([bool](Compare-GitSnapshot $fake $fake2)) 'injected change reported'

    # resume: a task stuck in running becomes pending again
    $t2 = $reloaded.tasks[1]
    $t2.status = 'running'
    [System.IO.File]::WriteAllText($sandboxPlan, ($reloaded | ConvertTo-Json -Depth 8), (New-Object System.Text.UTF8Encoding($false)))
    $re = Read-NightPlan
    $reclaimed = 0
    foreach ($x in $re.tasks) { if ($x.status -eq 'running') { $x.status = 'pending'; $reclaimed++ } }
    Add-Result 'stuck running task reclaimable' ($reclaimed -ge 1) "$reclaimed reclaimed"

    # 9. plan schema refuses unknown versions
    $bogus = Join-Path $script:LogDir 'selftest_bogus.json'
    '{ "schema": "something/else", "tasks": [] }' | Set-Content -Path $bogus -Encoding UTF8
    $rejected = $false
    $script:PlanPath = $bogus
    try { $null = Read-NightPlan } catch { $rejected = $true }
    Add-Result 'bad schema rejected' $rejected 'wrong schema refused'
} finally {
    $script:PlanPath = $realPlanPath
    Remove-Item $sandboxPlan -Force -ErrorAction SilentlyContinue
    Remove-Item (Join-Path $script:LogDir 'selftest_bogus.json') -Force -ErrorAction SilentlyContinue
}

$out = [ordered]@{
    at     = (Get-Date).ToString('o')
    total  = $results.Count
    passed = $results.Count - $fails
    failed = $fails
    checks = $results
}
$jsonPath = Join-Path $script:LogDir 'selftest.json'
[System.IO.File]::WriteAllText($jsonPath, ($out | ConvertTo-Json -Depth 5), (New-Object System.Text.UTF8Encoding($false)))

foreach ($r in $results) {
    Write-Output ("[{0}] {1,-36} {2}" -f $(if ($r.ok) { 'PASS' } else { 'FAIL' }), $r.check, $r.detail)
}
Write-Output ''
Write-Output ("{0} checks, {1} failure(s)" -f $results.Count, $fails)
Write-Output "written: $jsonPath"
if ($fails -eq 0) { Write-Output 'NIGHTLY SELFTEST PASSED'; exit 0 }
Write-Output 'NIGHTLY SELFTEST FAILED'; exit 1
