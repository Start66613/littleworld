# Night Runner core. Dot-sourced by the other nightly scripts; not run directly.
# SPDX-License-Identifier: MIT
#
# Design rules that are NOT negotiable, see README.md section 3:
#   - no retry loop without a hard attempt cap
#   - every task runs under a wall-clock timeout
#   - a lock_key can only be held by one live task at a time
#   - the git worktree is snapshotted before and after, never "cleaned"
#   - flashing / serial / destructive git is refused outright
#   - the task list is static; nothing may add tasks at runtime

$ErrorActionPreference = 'Stop'

function Resolve-NightRoot {
    if ($env:LW_NIGHT_ROOT) { return $env:LW_NIGHT_ROOT }
    return (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
}

$script:Root      = Resolve-NightRoot
$script:PlanPath  = Join-Path $script:Root 'tools\nightly\night_plan.json'
$script:LogDir    = Join-Path $script:Root 'logs\nightly'
$script:ReportDir = Join-Path $script:Root 'reports\nightly'
$script:StateDir  = Join-Path $script:LogDir 'state'

function Initialize-NightDirs {
    foreach ($d in @($script:LogDir, $script:ReportDir, $script:StateDir)) {
        if (-not (Test-Path $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
    }
}

function Read-NightPlan {
    if (-not (Test-Path $script:PlanPath)) { throw "night plan missing: $($script:PlanPath)" }
    $raw = [System.IO.File]::ReadAllText($script:PlanPath)
    $plan = $raw | ConvertFrom-Json
    if ($plan.schema -ne 'littleworld.nightly/1') { throw "unexpected plan schema: $($plan.schema)" }
    return $plan
}

function Save-NightPlan {
    param($Plan)
    Initialize-NightDirs
    $json = $Plan | ConvertTo-Json -Depth 8
    # UTF-8 without BOM, written atomically so a crash cannot leave half a plan
    $tmp = "$($script:PlanPath).tmp"
    [System.IO.File]::WriteAllText($tmp, $json, (New-Object System.Text.UTF8Encoding($false)))
    Move-Item -Force $tmp $script:PlanPath
}

# --- guards ---------------------------------------------------------------

# Things that must never run unattended. A night script touching the board is
# exactly the class of mistake this list exists to prevent.
$script:ForbiddenPatterns = @(
    'flash_v3\.ps1',
    'serial_capture\.ps1',
    'board_shot\.ps1',
    'esptool',
    'idf\.py[^\r\n]*\bflash\b',
    'erase_flash',
    'erase-region',
    'write_flash',
    '-Authorized',
    'git\s+reset\s+--hard',
    'git\s+clean\s+-[a-z]*d[a-z]*x',
    'git\s+checkout\s+--\s+\.',
    'schtasks[^\r\n]*/create',
    'Register-ScheduledTask',
    'Remove-Item[^\r\n]*-Recurse[^\r\n]*build'
)

function Test-NightCommandAllowed {
    param([string]$Command)
    if ([string]::IsNullOrWhiteSpace($Command)) { return @{ ok = $false; reason = 'empty command' } }
    foreach ($p in $script:ForbiddenPatterns) {
        if ($Command -match $p) { return @{ ok = $false; reason = "matched forbidden pattern: $p" } }
    }
    return @{ ok = $true; reason = '' }
}

# --- git snapshot ---------------------------------------------------------

function Get-GitSnapshot {
    $out = & git -C $script:Root status --porcelain 2>$null
    $head = & git -C $script:Root rev-parse --short HEAD 2>$null
    return [pscustomobject]@{
        head  = ($head -join '').Trim()
        dirty = @($out | Where-Object { $_ -ne '' }) -join "`n"
        count = @($out | Where-Object { $_ -ne '' }).Count
    }
}

function Compare-GitSnapshot {
    param($Before, $After)
    if ($Before.head -ne $After.head) { return "HEAD changed $($Before.head) -> $($After.head)" }
    if ($Before.dirty -ne $After.dirty) { return "worktree changed ($($Before.count) -> $($After.count) dirty paths)" }
    return $null
}

# --- locks ----------------------------------------------------------------

function Get-NightLockPath { param([string]$Key) Join-Path $script:StateDir "lock-$Key.lock" }

function Enter-NightLock {
    param([string]$Key, [string]$TaskId)
    Initialize-NightDirs
    $path = Get-NightLockPath $Key
    if (Test-Path $path) {
        $info = Get-Content $path -Raw -ErrorAction SilentlyContinue | ConvertFrom-Json -ErrorAction SilentlyContinue
        $alive = $false
        if ($info -and $info.pid) {
            $p = Get-Process -Id $info.pid -ErrorAction SilentlyContinue
            $alive = ($null -ne $p)
        }
        if ($alive) {
            return @{ ok = $false; reason = "lock '$Key' held by $($info.task) pid=$($info.pid)" }
        }
        # stale lock from a dead process: reclaim, that is what makes resume safe
        Remove-Item $path -Force -ErrorAction SilentlyContinue
    }
    $payload = [pscustomobject]@{
        key      = $Key
        task     = $TaskId
        pid      = $PID
        since    = (Get-Date).ToString('o')
        hostname = $env:COMPUTERNAME
    } | ConvertTo-Json
    [System.IO.File]::WriteAllText($path, $payload, (New-Object System.Text.UTF8Encoding($false)))
    return @{ ok = $true; path = $path }
}

function Exit-NightLock {
    param([string]$Path)
    if ($Path -and (Test-Path $Path)) { Remove-Item $Path -Force -ErrorAction SilentlyContinue }
}

# --- task execution -------------------------------------------------------

function Write-NightEvent {
    param($Task, [string]$Phase, [string]$Result, $Extra)
    Initialize-NightDirs
    $rec = [ordered]@{
        at       = (Get-Date).ToString('o')
        phase    = $Phase
        task     = $Task.id
        title    = $Task.title
        kind     = $Task.kind
        result   = $Result
        attempts = $Task.last_run.attempts
    }
    if ($Extra) { foreach ($k in $Extra.Keys) { $rec[$k] = $Extra[$k] } }
    $line = ($rec | ConvertTo-Json -Compress)
    $file = Join-Path $script:LogDir ("tasks-{0}.jsonl" -f (Get-Date).ToString('yyyyMMdd'))
    Add-Content -Path $file -Value $line -Encoding UTF8
    Write-Output $line
}

function Invoke-NightCommand {
    param(
        [string]$Command,
        [int]$TimeoutSec,
        [string]$LogPath
    )
    Initialize-NightDirs
    $errPath = "$LogPath.err"
    $proc = Start-Process -FilePath 'powershell.exe' `
        -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', $Command) `
        -WorkingDirectory $script:Root `
        -RedirectStandardOutput $LogPath `
        -RedirectStandardError $errPath `
        -PassThru -WindowStyle Hidden
    $timedOut = $false
    try {
        $null = $proc.WaitForExit($TimeoutSec * 1000)
    } catch {
        # WaitForExit can throw if the process object is recycled; treat as not exited
    }
    if (-not $proc.HasExited) {
        $timedOut = $true
        try { & taskkill.exe /PID $proc.Id /T /F 2>$null | Out-Null } catch { }
        try { $proc.WaitForExit(30000) | Out-Null } catch { }
    }
    $exit = $null
    if ($proc.HasExited) { $exit = $proc.ExitCode }
    return [pscustomobject]@{
        exit     = $exit
        timedOut = $timedOut
        outLog   = $LogPath
        errLog   = $errPath
    }
}

function Test-NightSuccessPattern {
    param([string]$LogPath, [string]$Pattern)
    if (-not $Pattern) { return $false }
    if (-not (Test-Path $LogPath)) { return $false }
    $hit = Select-String -Path $LogPath -Pattern $Pattern -SimpleMatch -Quiet -ErrorAction SilentlyContinue
    return [bool]$hit
}

function Update-TaskStatus {
    param($Plan, [string]$TaskId, [string]$Status, $Note)
    foreach ($t in $Plan.tasks) {
        if ($t.id -eq $TaskId) {
            $t.status = $Status
            if ($null -ne $Note -and $Note -ne '') {
                if (-not $t.last_run) {
                    $t.last_run = [pscustomobject]@{ started = $null; ended = $null; result = $Status; note = $Note; attempts = 0 }
                } else {
                    $t.last_run.result = $Status
                    $t.last_run.note = $Note
                }
            }
            return $t
        }
    }
    throw "unknown task id: $TaskId"
}

function Test-TaskDue {
    param($Plan, $Task, [datetime]$Now)
    if (-not $Plan.t0) { return $true }   # nothing has started yet, T0 tasks are immediately due
    $t0 = [datetime]::Parse($Plan.t0, $null, [System.Globalization.DateTimeStyles]::RoundtripKind)
    $due = $t0.AddMinutes([double]$Task.due_after_min)
    return ($Now -ge $due)
}

function Invoke-NightTask {
    param($Plan, $Task)
    $guard = Test-NightCommandAllowed $Task.command
    if (-not $guard.ok) {
        Update-TaskStatus $Plan $Task.id 'blocked' $guard.reason | Out-Null
        Save-NightPlan $Plan
        Write-NightEvent $Task $Task.phase 'blocked' @{ reason = $guard.reason } | Out-Null
        return 'blocked'
    }

    $lock = Enter-NightLock $Task.lock_key $Task.id
    if (-not $lock.ok) {
        return 'skipped'   # another live task owns this build dir; do not queue behind it
    }

    try {
        if (-not $Task.last_run) {
            $Task.last_run = [pscustomobject]@{ started = $null; ended = $null; result = $null; note = ''; attempts = 0 }
        }
        $Task.last_run.attempts = [int]$Task.last_run.attempts + 1
        $Task.last_run.started = (Get-Date).ToString('o')
        $Task.status = 'running'
        if (-not $Plan.t0) { $Plan.t0 = (Get-Date).ToString('o') }
        Save-NightPlan $Plan

        $before = Get-GitSnapshot
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        $logPath = Join-Path $script:LogDir ("{0}-{1}.log" -f $Task.id, $stamp)
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $res = Invoke-NightCommand -Command $Task.command -TimeoutSec $Task.timeout_sec -LogPath $logPath
        $sw.Stop()
        $after = Get-GitSnapshot

        $result = 'failed'
        $note = ''
        if ($res.timedOut) {
            $result = 'timeout'
            $note = "exceeded $($Task.timeout_sec)s"
        } elseif (-not (Test-NightSuccessPattern $res.outLog $Task.success_pattern)) {
            $result = 'failed'
            $note = "success pattern '$($Task.success_pattern)' not found (exit $($res.exit))"
        } else {
            $result = 'done'
            $note = "exit $($res.exit), $([int]$sw.Elapsed.TotalSeconds)s"
        }

        $drift = Compare-GitSnapshot $before $after
        if ($drift) {
            $result = 'dirty'
            $note = "$note; git drift: $drift"
        }

        $missing = @()
        foreach ($a in @($Task.artifacts)) {
            if ($a -and -not (Test-Path (Join-Path $script:Root $a))) { $missing += $a }
        }
        if ($missing.Count -gt 0 -and $result -eq 'done') {
            $result = 'failed'
            $note = "$note; missing artifacts: $($missing -join ', ')"
        }

        $Task.last_run.ended = (Get-Date).ToString('o')
        $Task.status = $result
        $Task.last_run.result = $result
        $Task.last_run.note = $note
        Save-NightPlan $Plan
        Write-NightEvent $Task $Task.phase $result @{
            duration_s = [int]$sw.Elapsed.TotalSeconds
            exit       = $res.exit
            note       = $note
            log        = (Split-Path -Leaf $logPath)
        } | Out-Null
        return $result
    } finally {
        Exit-NightLock $lock.path
    }
}

function Get-NightNextTask {
    param($Plan)
    $now = Get-Date
    $order = $Plan.tasks | Sort-Object { [double]$_.due_after_min }
    foreach ($t in $order) {
        if ($t.status -ne 'pending') { continue }
        $attempts = 0
        if ($t.last_run) { $attempts = [int]$t.last_run.attempts }
        if ($attempts -ge [int]$t.max_attempts) { continue }   # hard stop, no infinite retry
        if (-not (Test-TaskDue $Plan $t $now)) { continue }
        return $t
    }
    return $null
}

function Get-NightSummary {
    param($Plan)
    $rows = @()
    foreach ($t in ($Plan.tasks | Sort-Object { [double]$_.due_after_min })) {
        $rows += [pscustomobject]@{
            id      = $t.id
            phase   = $t.phase
            kind    = $t.kind
            status  = $t.status
            attempts= $(if ($t.last_run) { $t.last_run.attempts } else { 0 })
            note    = $(if ($t.last_run) { $t.last_run.note } else { '' })
        }
    }
    return $rows
}

function Write-NightSummaryFile {
    param($Plan)
    Initialize-NightDirs
    $rows = Get-NightSummary $Plan
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $obj = [ordered]@{
        at    = (Get-Date).ToString('o')
        t0    = $Plan.t0
        plan  = $Plan.plan_id
        tasks = $rows
        counts = [ordered]@{
            done    = @($rows | Where-Object { $_.status -eq 'done' }).Count
            pending = @($rows | Where-Object { $_.status -eq 'pending' }).Count
            failed  = @($rows | Where-Object { $_.status -in @('failed','timeout','dirty','blocked') }).Count
        }
    }
    $path = Join-Path $script:ReportDir "summary-$stamp.json"
    [System.IO.File]::WriteAllText($path, ($obj | ConvertTo-Json -Depth 6), (New-Object System.Text.UTF8Encoding($false)))
    return $path
}
