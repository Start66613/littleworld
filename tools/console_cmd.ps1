# Sends one command to the lw> console and captures the reply.
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\console_cmd.ps1 -Command "status" -Seconds 3
#
# The console shares UART0 with the ESP_LOG stream, so the reply is whatever the
# device printed in the window; nothing is filtered except the escape noise.
# SPDX-License-Identifier: MIT
[CmdletBinding()]
param(
    [string]$Port = 'COM5',
    [int]$Baud = 115200,
    [Parameter(Mandatory = $true)][string]$Command,
    [int]$Seconds = 3,
    [switch]$Reset
)

$ErrorActionPreference = 'Stop'
$sp = New-Object System.IO.Ports.SerialPort($Port, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$sp.ReadTimeout = 500
$sp.NewLine = "`n"
$sp.DtrEnable = $false
$sp.RtsEnable = $false
$sp.Open()
try {
    if ($Reset) {
        $sp.DtrEnable = $false
        $sp.RtsEnable = $true
        Start-Sleep -Milliseconds 120
        $sp.RtsEnable = $false
        Start-Sleep -Milliseconds 600
    }
    $sp.DiscardInBuffer()
    $sp.WriteLine($Command)
    $deadline = (Get-Date).AddSeconds($Seconds)
    $sb = New-Object System.Text.StringBuilder
    while ((Get-Date) -lt $deadline) {
        try { $line = $sp.ReadLine() } catch { continue }
        if ($null -eq $line) { continue }
        [void]$sb.AppendLine($line.TrimEnd("`r"))
    }
    [Console]::Out.Write($sb.ToString())
} finally {
    if ($sp.IsOpen) { $sp.Close() }
    $sp.Dispose()
}
