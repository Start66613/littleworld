# Builds the Little World V3.2 firmware matrix from one worktree.
#
#   os-offline : OS V3 desktop, no radio (default, the only shippable one)
#   diag-a     : diagnostic A - standard V3.1 brightness control (LEDC runtime duty)
#   diag-b     : diagnostic B - backlight fixed at boot, no runtime duty writes
#   diag-c     : diagnostic C - minimal display load, UI animations suspended
#   os-wifi-experimental : OS V3 + ESP32-C6 hosted Wi-Fi stack
#   legacy     : pre-OS shell fallback
#   noaudio    : OS V3 with the ES8311 probe compiled out
#
# Diagnostic builds are for one board comparison run each; they are NOT
# candidates for daily use. Nothing here touches the device.
#
# The Wi-Fi configuration needs the hosted packages in firmware/managed_components
# and a temporarily extended component manifest; tools/setup_wifi_components.ps1
# applies and (always) reverts that overlay, even if the build throws.
#
# Usage: powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/build_v3_2_matrix.ps1
#        ... -Configs os-offline,diag-a
# SPDX-License-Identifier: MIT
[CmdletBinding()]
param([string[]]$Configs = @('os-offline', 'diag-a', 'diag-b', 'diag-c', 'os-wifi-experimental'))

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
New-Item -ItemType Directory -Force -Path (Join-Path $root 'reports/v3.2') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $root 'reports/logs/v3.2') | Out-Null
. (Join-Path $PSScriptRoot 'activate_env.ps1')
Set-Location (Join-Path $root 'firmware')

$base = 'sdkconfig.defaults;sdkconfig.defaults.rev3_x;sdkconfig.os'
$extras = @{
    'os-offline'           = ''
    'legacy'               = ';sdkconfig.extra.legacy'
    'noaudio'              = ';sdkconfig.extra.noaudio'
    'diag-a'               = ';sdkconfig.extra.diag-a'
    'diag-b'               = ';sdkconfig.extra.diag-b'
    'diag-c'               = ';sdkconfig.extra.diag-c'
    'os-wifi-experimental' = ';sdkconfig.extra.wifi'
}
# Directory names stay short and ascii so the flash tooling can find them.
$dirs = @{
    'os-offline'           = 'os'
    'diag-a'               = 'diag-a'
    'diag-b'               = 'diag-b'
    'diag-c'               = 'diag-c'
    'os-wifi-experimental' = 'wifi'
}

$results = @()
foreach ($cfg in $Configs) {
    $extra = $extras[$cfg]
    if ($null -eq $extra) { throw "unknown configuration '$cfg'" }
    $dirName = $dirs[$cfg]
    $dir = if ($dirName) { "build/$dirName" } else { "build/$cfg" }
    $wifi = ($cfg -eq 'os-wifi-experimental')

    Write-Output "=== build $cfg (dir: $dir, defaults: $base$extra) ==="
    $log = Join-Path $root ("reports/logs/v3.2/build-$cfg.log")
    if ($wifi) {
        & (Join-Path $PSScriptRoot 'setup_wifi_components.ps1') | Out-Null
    }
    try {
        & idf.py -B $dir -DSDKCONFIG="$dir/sdkconfig" -DSDKCONFIG_DEFAULTS="$base$extra" build *> $log
        $code = $LASTEXITCODE
    } finally {
        if ($wifi) {
            & (Join-Path $PSScriptRoot 'setup_wifi_components.ps1') -Restore | Out-Null
        }
    }
    if ($code -ne 0) {
        Get-Content -Path $log -Tail 40
        throw "build $cfg failed (exit $code), full log: $log"
    }
    Select-String -Path $log -Pattern 'Project build complete' | Select-Object -Last 1 | ForEach-Object { $_.Line.Trim() }

    $bin = Join-Path $dir 'little_world.bin'
    $hash = (Get-FileHash $bin -Algorithm SHA256).Hash.ToLowerInvariant()
    $size = (Get-Item $bin).Length
    $results += [pscustomobject]@{ config = $cfg; dir = $dir; bytes = $size; sha256 = $hash }
    # Save incrementally, so a later failure cannot erase the results of the
    # configurations that already built.
    $results | ConvertTo-Json -Depth 3 | Set-Content -Path (Join-Path $root 'reports/v3.2/build_matrix.json') -Encoding utf8
}

Write-Output ''
Write-Output 'configuration             bytes       sha256'
foreach ($r in $results) {
    Write-Output ('{0,-25} {1,-11} {2}' -f $r.config, $r.bytes, $r.sha256)
}
$results | ConvertTo-Json -Depth 3 | Set-Content -Path (Join-Path $root 'reports/v3.2/build_matrix.json') -Encoding utf8
Write-Output ('written: ' + (Join-Path $root 'reports/v3.2/build_matrix.json'))
