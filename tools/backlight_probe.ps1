# Read-only backlight forensics. Nothing is flashed and nothing is written to
# NVS: this only sends console commands over the existing UART and listens.
#
# It answers one question with evidence: does changing the brightness slider
# actually rewrite the LEDC duty, and how many times?
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\backlight_probe.ps1 -Port COM5
# SPDX-License-Identifier: MIT
[CmdletBinding()]
param(
    [string]$Port = 'COM5',
    [int]$Baud = 115200,
    [int]$Seconds = 20
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$logDir = Join-Path $root 'reports/logs'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$log = Join-Path $logDir "backlight-$stamp.log"

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 300
$sp.DtrEnable = $false
$sp.RtsEnable = $false

$lines = New-Object System.Collections.ArrayList

function Read-For {
    param([int]$Milliseconds)
    $deadline = (Get-Date).AddMilliseconds($Milliseconds)
    while ((Get-Date) -lt $deadline) {
        try {
            $chunk = $sp.ReadExisting()
            if ($chunk) {
                foreach ($l in ($chunk -split "`r?`n")) {
                    if ($l -and $l.Trim().Length -gt 0) { $null = $lines.Add($l.TrimEnd()) }
                }
            }
        } catch { }
        Start-Sleep -Milliseconds 50
    }
}

try {
    $sp.Open()
    Read-For -Milliseconds 800 | Out-Null          # drain boot noise

    Write-Output '--- querying status ---'
    $sp.Write("status`r`n")
    Read-For -Milliseconds 1200 | Out-Null

    Write-Output '--- brightness sweep: low, then high ---'
    # The console has no brightness command, so the slider is driven through the
    # touch UI in the real test. Here we only record what the firmware logs when
    # brightness changes, to see whether it logs at all.
    $sp.Write("status`r`n")
    Read-For -Milliseconds 800 | Out-Null

    Read-For -Milliseconds (($Seconds - 3) * 1000) | Out-Null
} finally {
    try { $sp.Close() } catch { }
    try { $sp.Dispose() } catch { }
}

$text = $lines -join "`n"
[System.IO.File]::WriteAllText($log, $text, (New-Object System.Text.UTF8Encoding($false)))

$bright = @($lines | Where-Object { $_ -match 'brightness|backlight|ledc|Backlight' })
$status = @($lines | Where-Object { $_ -match 'heap|display|capabilit' })
Write-Output "captured $($lines.Count) lines -> $log"
if ($bright.Count -gt 0) {
    Write-Output "brightness-related lines: $($bright.Count)"
    $bright | Select-Object -Last 20 | ForEach-Object { "  $_" }
} else {
    Write-Output 'NO brightness/backlight/ledc lines appeared in the log'
}
if ($status.Count -gt 0) {
    Write-Output 'status lines:'
    $status | Select-Object -Last 10 | ForEach-Object { "  $_" }
}
