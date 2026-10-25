# Flashes the OS V3 + DSH combined firmware onto the ESP32-P4 board.
#
#   -Probe        read-only: identifies the chip, flash size and MAC on the port
#   -Authorized   required to actually write; without it the script stops after
#                 the image check so a stray run cannot touch the board
#
# What it writes: bootloader (0x2000), partition table (0x8000), app (0x10000).
# It never writes the nvs partition (0x9000), never runs erase_flash and never
# touches the C6 firmware. The pet/prefs/RPG saves live in NVS and survive.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/flash_v3.ps1 -Probe
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/flash_v3.ps1 -Authorized
# SPDX-License-Identifier: MIT
[CmdletBinding()]
param(
    [string]$Port = 'COM5',
    [string]$Config = 'os',
    [int]$Baud = 460800,
    [switch]$Probe,
    [switch]$Authorized
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'activate_env.ps1') | Out-Null

$build = Join-Path $root "firmware/build/$Config"
$app = Join-Path $build 'little_world.bin'
$boot = Join-Path $build 'bootloader/bootloader.bin'
$table = Join-Path $build 'partition_table/partition-table.bin'
foreach ($f in @($app, $boot, $table)) {
    if (-not (Test-Path $f)) { throw "missing build artefact: $f (run tools/build_v3_all.ps1 -Configs $Config)" }
}

Write-Output "=== image check ($Config) ==="
& python (Join-Path $PSScriptRoot 'image_check.py') $app --expect-rev 302
if ($LASTEXITCODE -ne 0) { throw 'image header check failed; refusing to flash' }

if ($Probe) {
    Write-Output ''
    Write-Output "=== probe $Port (read-only) ==="
    & python -m esptool --chip esp32p4 -p $Port -b 115200 chip_id
    if ($LASTEXITCODE -ne 0) { throw "no ESP32-P4 answered on $Port" }
    & python -m esptool --chip esp32p4 -p $Port -b 115200 flash_id
    if ($LASTEXITCODE -ne 0) { throw "flash_id failed on $Port" }
    Write-Output ''
    Write-Output 'Probe only: nothing was written.'
    return
}

if (-not $Authorized) {
    Write-Output ''
    Write-Output 'Refusing to write: pass -Authorized to flash for real.'
    Write-Output "Would run on $Port at $Baud baud:"
    Write-Output "  0x2000  $boot"
    Write-Output "  0x8000  $table"
    Write-Output "  0x10000 $app"
    exit 1
}

Write-Output ''
Write-Output "=== writing $Config to $Port ($Baud baud) ==="
Write-Output 'nvs at 0x9000 is NOT written; no erase; no C6 update.'
$esptoolArgs = @(
    '-m', 'esptool', '--chip', 'esp32p4', '-p', $Port, '-b', $Baud,
    '--before', 'default_reset', '--after', 'hard_reset',
    'write_flash',
    '--flash_mode', 'dio', '--flash_size', '32MB', '--flash_freq', '80m',
    '0x2000', $boot,
    '0x8000', $table,
    '0x10000', $app
)
& python @esptoolArgs
if ($LASTEXITCODE -ne 0) { throw "flash failed (exit $LASTEXITCODE)" }

Write-Output ''
Write-Output 'Flash done. The board reboots on its own (hard_reset after write).'
Write-Output 'Watch the boot log with:'
Write-Output "  powershell -NoProfile -ExecutionPolicy Bypass -File tools\\serial_capture.ps1 -Port $Port -Seconds 25"
