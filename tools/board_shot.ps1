# Reads one base64 BMP produced by the on-board "shot" command.
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\board_shot.ps1 -Port COM5 -Out reports\board\01-desktop.bmp
# SPDX-License-Identifier: MIT
[CmdletBinding()]
param(
    [string]$Port = 'COM5',
    [int]$Baud = 921600,
    [string]$Command = 'shot',
    [string]$Out = 'D:\LittleWorld-V3\reports\board\shot.bmp',
    [int]$TimeoutMs = 90000
)

$ErrorActionPreference = 'Stop'
$sp = New-Object System.IO.Ports.SerialPort($Port, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$sp.ReadTimeout = 2000
$sp.NewLine = "`n"
$sp.DtrEnable = $false
$sp.RtsEnable = $false
$sp.Open()
try {
    Start-Sleep -Milliseconds 200
    $sp.DiscardInBuffer()
    $sp.WriteLine($Command)

    $b64 = New-Object System.Text.StringBuilder
    $state = 'idle'
    $failed = $null
    $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    while ((Get-Date) -lt $deadline) {
        $line = $null
        try { $line = $sp.ReadLine() } catch { continue }
        if ($null -eq $line) { continue }
        $line = $line.TrimEnd("`r")
        if ($line.StartsWith('LCAP BMP ')) {
            $state = 'bmp'
            [void]$b64.Clear()
            [void]$b64.Append($line.Substring(9).Trim())
            continue
        }
        if ($line.StartsWith('LCAP FAIL')) { $failed = $line; break }
        if ($line -eq 'LCAP END') { break }
        if ($state -eq 'bmp' -or $state -eq 'data') {
            if ($line -match '^[A-Za-z0-9+/=]+$') { [void]$b64.Append($line.Trim()) }
        }
    }
} finally {
    $sp.Close()
}

if ($failed) { throw "board reported: $failed" }
if ($b64.Length -eq 0) { throw 'no LCAP data received' }

$bytes = [Convert]::FromBase64String($b64.ToString())
$dir = Split-Path -Parent $Out
if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
[System.IO.File]::WriteAllBytes($Out, $bytes)
Write-Output ("{0}: {1} bytes ({2} base64 chars)" -f $Out, $bytes.Length, $b64.Length)
