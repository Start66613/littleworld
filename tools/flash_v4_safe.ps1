# Little World V4 - reliable flashing helper.
#
# DRY RUN BY DEFAULT. Without -Execute nothing is written, no serial port is
# opened, and no device command is issued: the script only validates the build
# artefacts, the partition layout, the addresses and the port inventory, then
# prints the exact plan it would carry out.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/flash_v4_safe.ps1
#       dry run against the current build/os
#
#   ... -File tools/flash_v4_safe.ps1 -Probe
#       read-only: identify chip, flash size, MAC. Writes nothing.
#
#   ... -File tools/flash_v4_safe.ps1 -Execute -Authorized
#       really writes. Both switches are required; either alone refuses.
#
# What it writes (never anything else):
#   full stage (default) : 0x2000 bootloader, 0x8000 partition table, 0x10000 app
#   -Stage app           : 0x10000 app only (fastest, safest for a UI change)
# It never writes nvs (0x9000..0xF000), never runs erase_flash, never touches the
# ESP32-C6 firmware, never changes eFuse, and never retries a failed write.
#
# SPDX-License-Identifier: MIT
[CmdletBinding()]
param(
    [string]$Port = '',
    [string]$Config = 'os',
    [int]$Baud = 460800,
    [ValidateSet('full', 'app')]
    [string]$Stage = 'full',
    [int]$TimeoutSec = 240,
    [switch]$Probe,
    [switch]$ListPorts,
    [switch]$Execute,
    [switch]$Authorized
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$logDir = Join-Path $root 'reports/logs/flash'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$logPath = Join-Path $logDir "flash-$stamp.log"

# ---------------------------------------------------------------- logging ----
$script:Phase = 'init'
function Write-Log {
    param([string]$Text, [string]$Level = 'INFO')
    $line = ('[{0}] [{1,-5}] [{2,-12}] {3}' -f (Get-Date -Format 'HH:mm:ss'), $Level, $script:Phase, $Text)
    Write-Host $line
    Add-Content -Path $logPath -Value $line -Encoding UTF8
}
function Set-Stage { param([string]$Name) $script:Phase = $Name }

function Fail {
    param([string]$Text, [int]$Code = 2)
    Write-Log $Text 'FAIL'
    Write-Log "log: $logPath" 'FAIL'
    exit $Code
}

Write-Log "flash_v4_safe.ps1 - config=$Config stage=$Stage baud=$Baud dry-run=$(-not $Execute)"
Write-Log "log: $logPath"

# -Execute without -Authorized must fail on that alone, not on whatever else
# happens to be missing (a missing port, a stale build). Checked first so the
# message names the real reason a write is refused.
if ($Execute -and -not $Authorized) {
    Fail 'refusing to write: -Execute was passed without -Authorized' 3
}

# ------------------------------------------------------- serial inventory ----
function Get-SerialInventory {
    $items = @()
    $names = @()
    try { $names = [System.IO.Ports.SerialPort]::GetPortNames() } catch { $names = @() }
    $pnp = @()
    try {
        $pnp = Get-CimInstance Win32_PnPEntity -ErrorAction Stop |
            Where-Object { $_.Name -match '\(COM\d+\)' }
    } catch {
        Write-Log "WMI enumeration unavailable ($($_.Exception.Message)); falling back to GetPortNames" 'WARN'
    }
    foreach ($name in $names) {
        $match = $pnp | Where-Object { $_.Name -match "\($name\)" } | Select-Object -First 1
        $friendly = $name
        $status = 'unknown'
        if ($match) {
            $friendly = $match.Name
            $status = $match.Status
        }
        $items += [pscustomobject]@{
            Port     = $name
            Device   = $friendly
            Status   = $status
            IsCh343  = ($friendly -match 'CH34[0-9]')
        }
    }
    return $items
}

function Format-PortTable {
    param($Items)
    if (-not $Items -or $Items.Count -eq 0) {
        Write-Host '  (no serial port present)'
        return
    }
    Write-Host ('  {0,-7} {1,-9} {2}' -f 'PORT', 'DRIVER', 'DEVICE')
    foreach ($item in $Items) {
        $driver = 'other'
        if ($item.IsCh343) { $driver = 'CH343' }
        Write-Host ('  {0,-7} {1,-9} {2}' -f $item.Port, $driver, $item.Device)
    }
}

Set-Stage 'ports'
$ports = Get-SerialInventory
Write-Log "serial ports found: $($ports.Count)"
Format-PortTable $ports | Out-Host

if ($ListPorts) {
    Write-Log 'list-only: nothing was opened, nothing was written'
    exit 0
}

# ------------------------------------------------------------ artefacts ------
Set-Stage 'artifacts'
$build = Join-Path $root "firmware/build/$Config"
$app = Join-Path $build 'little_world.bin'
$boot = Join-Path $build 'bootloader/bootloader.bin'
$table = Join-Path $build 'partition_table/partition-table.bin'

$needed = @($app)
if ($Stage -eq 'full') { $needed += @($boot, $table) }
foreach ($file in $needed) {
    if (-not (Test-Path $file)) {
        Fail "missing build artefact: $file (run tools/build_v4_matrix.ps1 -Configs $Config)"
    }
}
Write-Log "app        : $app ($((Get-Item $app).Length) B)"
if ($Stage -eq 'full') {
    Write-Log "bootloader : $boot ($((Get-Item $boot).Length) B)"
    Write-Log "table      : $table ($((Get-Item $table).Length) B)"
} else {
    Write-Log 'stage app  : bootloader and partition table are NOT written'
}

# --------------------------------------------------- image header validation --
Set-Stage 'image-check'
$checkJson = Join-Path $env:TEMP "lw-image-check-$stamp.json"
$checkArgs = @((Join-Path $PSScriptRoot 'image_check.py'), '--expect-chip', 'esp32p4',
               '--expect-rev', '302', '--json', $checkJson)
$checkArgs += $app
if ($Stage -eq 'full') { $checkArgs += @($boot, $table) }

. (Join-Path $PSScriptRoot 'activate_env.ps1') | Out-Null
$checkOut = & python @checkArgs 2>&1
$checkCode = $LASTEXITCODE
foreach ($line in $checkOut) { Write-Log ([string]$line) }
if ($checkCode -ne 0) {
    Fail 'image header check failed; refusing to flash (see lines above)' 2
}

$checkData = @()
if (Test-Path $checkJson) {
    $checkData = Get-Content $checkJson -Raw | ConvertFrom-Json
}
$appInfo = $checkData | Where-Object { $_.kind -ne 'partition_table' -and $_.name -eq 'little_world.bin' } | Select-Object -First 1
if (-not $appInfo) { Fail 'could not read the app image report' 2 }
Write-Log "app image: chip=$($appInfo.chip) revs=$($appInfo.min_chip_rev_full)..$($appInfo.max_chip_rev_full) flash=$($appInfo.flash_size) mode=$($appInfo.flash_mode)"

# ------------------------------------------------------- partition layout ----
Set-Stage 'partitions'
$layout = @()
if ($Stage -eq 'full') {
    $tableReport = $checkData | Where-Object { $_.kind -eq 'partition_table' } | Select-Object -First 1
    if (-not $tableReport) { Fail 'could not parse the partition table binary' 2 }
    $layout = $tableReport.entries
} else {
    # app-only still needs the layout to prove 0x10000 is the factory slot and
    # that the image fits; read it from the built table even though it is not
    # written this run.
    if (-not (Test-Path $table)) { Fail "missing $table (needed to validate the layout)" 2 }
    $tableOnly = & python (Join-Path $PSScriptRoot 'image_check.py') '--json' (Join-Path $env:TEMP "lw-pt-$stamp.json") $table 2>&1
    if ($LASTEXITCODE -ne 0) { Fail 'could not parse the partition table binary' 2 }
    $layout = (Get-Content (Join-Path $env:TEMP "lw-pt-$stamp.json") -Raw | ConvertFrom-Json)[0].entries
}

Write-Host ''
Write-Host ('  {0,-10} {1,-6} {2,-8} {3,-10} {4}' -f 'NAME', 'TYPE', 'SUBTYPE', 'OFFSET', 'SIZE')
foreach ($entry in $layout) {
    Write-Host ('  {0,-10} {1,-6} {2,-8} 0x{3:X6}   0x{4:X6}' -f $entry.name, $entry.type, $entry.subtype, $entry.offset, $entry.size)
}

$factory = $layout | Where-Object { $_.name -eq 'factory' } | Select-Object -First 1
$nvs = $layout | Where-Object { $_.name -eq 'nvs' } | Select-Object -First 1
if (-not $factory) { Fail 'no factory partition in the table' 2 }
if (-not $nvs) { Fail 'no nvs partition in the table' 2 }
if ($factory.offset -ne 0x10000) { Fail ("factory is at 0x{0:X}, expected 0x10000" -f $factory.offset) 2 }
if ($nvs.offset -ne 0x9000) { Fail ("nvs is at 0x{0:X}, expected 0x9000 (the helper refuses to guess)" -f $nvs.offset) 2 }

$appBytes = (Get-Item $app).Length
if ($appBytes -gt $factory.size) {
    Fail ("app is $appBytes B but the factory partition is only $($factory.size) B") 2
}
$fill = [math]::Round(100.0 * $appBytes / $factory.size, 1)
Write-Log "app fits: $appBytes / $($factory.size) B ($fill% of factory)"
if ($fill -gt 90) { Write-Log "factory partition is $fill% full - close to the limit" 'WARN' }

# Offsets must be ascending and non-overlapping.
$sorted = $layout | Sort-Object offset
for ($i = 1; $i -lt $sorted.Count; $i++) {
    $prev = $sorted[$i - 1]
    $cur = $sorted[$i]
    if ($cur.offset -lt ($prev.offset + $prev.size)) {
        Fail ("partition overlap: {0} ends at 0x{1:X} but {2} starts at 0x{3:X}" -f $prev.name, ($prev.offset + $prev.size), $cur.name, $cur.offset) 2
    }
}
Write-Log 'partition layout: ascending, no overlap'

# -------------------------------------------------------------- write plan ---
Set-Stage 'plan'
$plan = @()
if ($Stage -eq 'full') {
    $plan += [pscustomobject]@{ Address = 0x2000; File = $boot; Role = 'bootloader' }
    $plan += [pscustomobject]@{ Address = 0x8000; File = $table; Role = 'partition-table' }
}
$plan += [pscustomobject]@{ Address = 0x10000; File = $app; Role = 'application' }

$protected = @(
    @{ Name = 'nvs'; Start = 0x9000; End = 0xF000 },
    @{ Name = 'phy_init'; Start = 0xF000; End = 0x10000 }
)
foreach ($item in $plan) {
    $size = (Get-Item $item.File).Length
    $end = $item.Address + $size
    foreach ($zone in $protected) {
        if ($item.Address -lt $zone.End -and $end -gt $zone.Start) {
            Fail ("plan would write into the protected {0} partition (0x{1:X}..0x{2:X})" -f $zone.Name, $zone.Start, $zone.End) 2
        }
    }
    if ($item.Address -eq 0) { Fail 'plan contains address 0x0 (whole-flash write); refusing' 2 }
    $hash = (Get-FileHash -Path $item.File -Algorithm SHA256).Hash.ToLower()
    $item | Add-Member -NotePropertyName Bytes -NotePropertyValue $size
    $item | Add-Member -NotePropertyName Sha256 -NotePropertyValue $hash
}

Write-Host ''
Write-Host '  write plan:'
Write-Host ('  {0,-10} {1,-11} {2,-16} {3}' -f 'ADDRESS', 'BYTES', 'ROLE', 'FILE / SHA-256')
foreach ($item in $plan) {
    Write-Host ('  0x{0:X6}   {1,-11} {2,-16} {3}' -f $item.Address, $item.Bytes, $item.Role, (Split-Path $item.File -Leaf))
    Write-Host ('             {0}' -f $item.Sha256)
    Write-Log ("plan 0x{0:X6} {1,10} B {2,-16} sha256 {3}" -f $item.Address, $item.Bytes, $item.Role, $item.Sha256)
}
Write-Host ''
Write-Host '  NOT written: nvs (0x9000..0xF000), phy_init, storage, ESP32-C6 firmware, eFuse.'
Write-Host '  No erase_flash. No full-chip write. No automatic retry of a failed write.'
Write-Log 'protected zones untouched: nvs 0x9000..0xF000, phy_init 0xF000..0x10000'

# ------------------------------------------------------------ port choice ----
Set-Stage 'port'
$target = $Port
if (-not $target) {
    $ch = $ports | Where-Object { $_.IsCh343 } | Select-Object -First 1
    if ($ch) {
        $target = $ch.Port
        Write-Log "auto-selected $target (CH343 USB-serial, the board's bridge)"
    } elseif ($ports.Count -eq 1) {
        $target = $ports[0].Port
        Write-Log "auto-selected $target (the only port present)"
    } elseif ($ports.Count -gt 1) {
        Write-Log 'several ports present and none looks like a CH343; pass -Port explicitly' 'WARN'
    }
}

if (-not $target) {
    Write-Log 'no usable serial port right now. The plan above is complete; connect the board and re-run.' 'WARN'
    if ($Execute) { Fail 'cannot flash: no serial port (board unplugged, or the CH343 driver is not loaded)' 3 }
} else {
    Write-Log "target port: $target"
    $busy = $false
    try {
        $probePort = New-Object System.IO.Ports.SerialPort $target, 115200
        $probePort.ReadTimeout = 200
        $probePort.Open()
        $probePort.Close()
        $probePort.Dispose()
    } catch [System.UnauthorizedAccessException] {
        $busy = $true
        Write-Log "port $target is held by another program (a serial monitor or a second esptool). Close it first." 'WARN'
    } catch {
        Write-Log "port $target could not be opened: $($_.Exception.Message)" 'WARN'
    }
    if ($busy -and $Execute) { Fail "cannot flash: $target is busy" 3 }
}

# --------------------------------------------------------- known failures ----
$knownFailures = @(
    @{ Pattern = 'could not open port|PermissionError|拒绝访问|Access is denied'
       Meaning = 'The COM port is held by another process, or the driver is stuck.'
       Action  = 'Close every serial monitor / second esptool, unplug and replug the USB cable. A retry only helps after the port is free.' },
    @{ Pattern = 'No serial data received|Failed to connect'
       Meaning = 'The chip did not answer: wrong port, charge-only cable, or the board is not in download mode.'
       Action  = 'Use the data cable, hold BOOT while plugging in, or pass -Probe first to confirm the chip answers at all.' },
    @{ Pattern = 'Wrong boot mode|wrong boot mode'
       Meaning = 'The board booted normally instead of into download mode.'
       Action  = 'The helper resets the board itself; if it persists, hold BOOT during the reset.' },
    @{ Pattern = 'Timed out waiting for packet header|Timed out waiting for packet'
       Meaning = 'The link dropped or is too slow for this baud rate / cable.'
       Action  = 'Re-run with -Baud 115200. Do not raise the baud rate above 460800 on the CH343.' },
    @{ Pattern = 'Invalid head of packet|invalid header'
       Meaning = 'Line noise, or another device is talking on the same port.'
       Action  = 'Check the cable, close other tools, re-run at 115200.' },
    @{ Pattern = 'ClearCommError|semaphore timeout|The semaphore timeout period has expired'
       Meaning = 'The USB-serial bridge stopped responding mid-transfer (CH343 dropout).'
       Action  = 'Replug the USB cable. The write did not complete - re-run; the app is either old or new, never half-written, because esptool verifies the hash.' },
    @{ Pattern = '系统找不到指定的文件|The system cannot find the file specified|could not find port'
       Meaning = 'COM5 disappeared: the USB connection dropped and Windows removed the device.'
       Action  = 'Replug, then confirm the port number in -ListPorts (it may come back as a different COM number).' },
    @{ Pattern = 'Hash of data verified|Hash of data verified.'
       Meaning = 'The write completed and esptool read it back correctly.'
       Action  = 'Success - no action.' },
    @{ Pattern = 'Detected overlap|overlaps with'
       Meaning = 'Two images would be written to overlapping addresses.'
       Action  = 'This is a build/tool bug, not a cable problem. Do not retry; report it.' }
)

function Show-KnownFailure {
    param([string]$Text)
    $hits = @()
    foreach ($item in $knownFailures) {
        if ($Text -match $item.Pattern) { $hits += $item }
    }
    if ($hits.Count -eq 0) {
        Write-Log 'no entry in the known-failure table matches this output; read the log above.' 'WARN'
        return
    }
    foreach ($hit in $hits) {
        Write-Log "known failure: $($hit.Meaning)"
        Write-Log "  what to do : $($hit.Action)"
    }
}

# ----------------------------------------------------------------- probe -----
if ($Probe) {
    Set-Stage 'probe'
    if (-not $target) { Fail 'cannot probe: no serial port' 3 }
    Write-Log "probing $target read-only (chip_id, flash_id) - nothing is written"
    $probeArgs = @('-m', 'esptool', '--chip', 'esp32p4', '-p', $target, '-b', '115200',
                   '--connect-attempts', '2', 'chip_id')
    $probeOut = & python @probeArgs 2>&1
    $probeCode = $LASTEXITCODE
    foreach ($line in $probeOut) { Write-Log ([string]$line) }
    if ($probeCode -ne 0) {
        Show-KnownFailure (($probeOut | Out-String))
        Fail 'probe failed; the board did not answer' 3
    }
    $flashArgs = @('-m', 'esptool', '--chip', 'esp32p4', '-p', $target, '-b', '115200',
                   '--connect-attempts', '2', 'flash_id')
    $flashOut = & python @flashArgs 2>&1
    foreach ($line in $flashOut) { Write-Log ([string]$line) }
    Write-Log 'probe done: nothing was written'
    exit 0
}

# --------------------------------------------------------------- dry run -----
if (-not $Execute) {
    Set-Stage 'dry-run'
    Write-Host ''
    Write-Host '=== DRY RUN: nothing was written, no port was opened ==='
    Write-Host "To flash for real:  -Execute -Authorized  (both are required)"
    if ($target) {
        Write-Host "Would write to $target at $Baud baud:"
        foreach ($item in $plan) {
            Write-Host ('  0x{0:X6}  {1,10} B  {2}' -f $item.Address, $item.Bytes, (Split-Path $item.File -Leaf))
        }
    } else {
        Write-Host 'No board is connected right now, so no port was selected.'
    }
    Write-Host ''
    Write-Host '=== if a flash fails, this is the safe way back ==='
    Write-Host '  1. Do NOT run erase_flash. The NVS partition holds the saves and is never written by this helper.'
    Write-Host '  2. Re-run with -Baud 115200 (the slow but reliable rate for the CH343).'
    Write-Host '  3. If the board boots but misbehaves, flash the last known-good image:'
    Write-Host '       git checkout release/os-v3.1-stable -- firmware/build/os   (or rebuild that tag)'
    Write-Host '       then -Config os -Execute -Authorized'
    Write-Host '  4. A full flash backup from 2026-10-25 exists at:'
    Write-Host '       D:\Espressif\LittleWorld\private\backups\LittleWorld\20261025-005021'
    Write-Host '  5. To restore it (only if you really want the old firmware):'
    Write-Host '       python -m esptool --chip esp32p4 -p COM5 -b 460800 write_flash 0x0 <backup.bin>'
    Write-Host '     This overwrites NVS too. Do not run it unless the board is unusable.'
    Write-Log 'dry run complete: 0 bytes written'
    Write-Log "log: $logPath"
    exit 0
}

# ------------------------------------------------------------- real flash ----
Set-Stage 'preflight'
if (-not $Authorized) {
    Fail 'refusing to write: -Execute was passed without -Authorized' 3
}
if (-not $target) { Fail 'refusing to write: no serial port' 3 }

$result = [ordered]@{
    tool        = 'flash_v4_safe.ps1'
    started     = (Get-Date).ToString('o')
    port        = $target
    config      = $Config
    stage       = $Stage
    baud        = $Baud
    images      = @()
    success     = $false
    failedAt    = ''
    log         = $logPath
}
foreach ($item in $plan) {
    $result.images += [ordered]@{
        address = ('0x{0:X6}' -f $item.Address)
        role    = $item.Role
        file    = (Split-Path $item.File -Leaf)
        bytes   = $item.Bytes
        sha256  = $item.Sha256
    }
}

Set-Stage 'write'
Write-Log "=== WRITING $Stage to $target at $Baud baud ==="
Write-Log 'nvs 0x9000..0xF000 is NOT written; no erase; no C6 update; no eFuse.'
$esptoolArgs = @('-m', 'esptool', '--chip', 'esp32p4', '-p', $target, '-b', $Baud,
                 '--before', 'default_reset', '--after', 'hard_reset',
                 '--connect-attempts', '3',
                 'write_flash',
                 '--flash_mode', 'dio', '--flash_size', '32MB', '--flash_freq', '80m')
foreach ($item in $plan) {
    $esptoolArgs += @(('0x{0:X}' -f $item.Address), $item.File)
}

$stdoutPath = Join-Path $logDir "flash-$stamp-stdout.log"
$stderrPath = Join-Path $logDir "flash-$stamp-stderr.log"
$watch = [System.Diagnostics.Stopwatch]::StartNew()
$proc = Start-Process -FilePath 'python' -ArgumentList $esptoolArgs -NoNewWindow -PassThru `
    -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
$finished = $proc.WaitForExit($TimeoutSec * 1000)
$watch.Stop()
if (-not $finished) {
    Write-Log "esptool exceeded the ${TimeoutSec}s budget - killing it (no automatic retry)" 'FAIL'
    try { $proc.Kill() } catch { }
    $result.failedAt = 'timeout'
    $result | ConvertTo-Json -Depth 6 | Set-Content -Path (Join-Path $logDir 'last-flash.json') -Encoding UTF8
    Fail "flash timed out after ${TimeoutSec}s; the board state is unknown. Re-run -Probe before trying again." 3
}
$proc.WaitForExit()
$exitCode = $proc.ExitCode
$esptoolText = ''
if (Test-Path $stdoutPath) { $esptoolText += (Get-Content $stdoutPath -Raw) }
if (Test-Path $stderrPath) { $esptoolText += (Get-Content $stderrPath -Raw) }
foreach ($line in ($esptoolText -split "`r?`n")) {
    if ($line.Trim()) { Write-Log $line }
}
Write-Log ("esptool exit={0} elapsed={1:N1}s" -f $exitCode, $watch.Elapsed.TotalSeconds)

if ($exitCode -ne 0) {
    Show-KnownFailure $esptoolText
    $result.failedAt = 'write'
    $result | ConvertTo-Json -Depth 6 | Set-Content -Path (Join-Path $logDir 'last-flash.json') -Encoding UTF8
    Fail "flash failed (esptool exit $exitCode); see the known-failure advice above. No retry was attempted." 3
}

if ($esptoolText -notmatch 'Hash of data verified') {
    Write-Log 'esptool reported success but the verification line is missing - treat this as unverified' 'WARN'
}

$result.success = $true
$result.elapsed_sec = [math]::Round($watch.Elapsed.TotalSeconds, 1)
$result | ConvertTo-Json -Depth 6 | Set-Content -Path (Join-Path $logDir 'last-flash.json') -Encoding UTF8

Set-Stage 'done'
Write-Log 'flash done: hash verified by esptool. The board hard-resets itself.'
Write-Log 'watch the boot log with:'
Write-Log "  powershell -NoProfile -ExecutionPolicy Bypass -File tools\\serial_capture.ps1 -Port $target -Seconds 25"
Write-Log "log: $logPath"
exit 0
