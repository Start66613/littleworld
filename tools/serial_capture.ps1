# Captures the board's serial boot log for a fixed number of seconds.
# Read-only: opens the port, prints everything, saves it under reports/logs/.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/serial_capture.ps1 -Port COM5 -Seconds 25
# SPDX-License-Identifier: MIT
[CmdletBinding()]
param(
    [string]$Port = 'COM5',
    [int]$Baud = 115200,
    [int]$Seconds = 25,
    [switch]$Reset
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$logDir = Join-Path $root 'reports/logs'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$log = Join-Path $logDir ("serial-{0}.log" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 500
$sp.DtrEnable = $false
$sp.RtsEnable = $false
$deadline = (Get-Date).AddSeconds($Seconds)
$buffer = New-Object System.Text.StringBuilder
try {
    $sp.Open()
    if ($Reset) {
        # IO0 stays high (normal boot); pulse EN low then high to reboot the app.
        $sp.DtrEnable = $false
        $sp.RtsEnable = $true
        Start-Sleep -Milliseconds 200
        $sp.RtsEnable = $false
        Start-Sleep -Milliseconds 80
        $deadline = (Get-Date).AddSeconds($Seconds)
    }
    Write-Output "listening on $Port at $Baud for $Seconds s -> $log"
    while ((Get-Date) -lt $deadline) {
        try {
            $chunk = $sp.ReadExisting()
        } catch {
            $chunk = ''
        }
        if ($chunk) {
            [void]$buffer.Append($chunk)
            Write-Host -NoNewline $chunk
        } else {
            Start-Sleep -Milliseconds 120
        }
    }
} finally {
    if ($sp.IsOpen) { $sp.Close() }
    $sp.Dispose()
}
[System.IO.File]::WriteAllText($log, $buffer.ToString(), (New-Object System.Text.UTF8Encoding($false)))
Write-Output ''
Write-Output "saved $log"
