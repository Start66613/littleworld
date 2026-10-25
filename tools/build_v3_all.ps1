# Builds the three Little World firmware configurations from one worktree.
#   os      : Codex OS V3 desktop (default)
#   legacy  : pre-OS shell fallback
#   noaudio : OS V3 with the ES8311 probe compiled out
# Every configuration gets its own build directory and sdkconfig so they cannot
# poison each other. SPDX-License-Identifier: MIT
[CmdletBinding()]
param([string[]]$Configs = @('os', 'legacy', 'noaudio'))

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'activate_env.ps1')
Set-Location (Join-Path $root 'firmware')

$base = 'sdkconfig.defaults;sdkconfig.defaults.rev3_x;sdkconfig.os'
$extras = @{
    os      = ''
    legacy  = ';sdkconfig.extra.legacy'
    noaudio = ';sdkconfig.extra.noaudio'
}

foreach ($cfg in $Configs) {
    $extra = $extras[$cfg]
    if ($null -eq $extra) { throw "unknown configuration '$cfg'" }
    $dir = "build/$cfg"
    Write-Output "=== build $cfg (defaults: $base$extra) ==="
    & idf.py -B $dir -DSDKCONFIG="$dir/sdkconfig" -DSDKCONFIG_DEFAULTS="$base$extra" build
    if ($LASTEXITCODE -ne 0) { throw "build $cfg failed" }
    Write-Output "=== $cfg ok ==="
}
