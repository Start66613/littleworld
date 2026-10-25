# Validates the three combined images with the project's own header checker.
# SPDX-License-Identifier: MIT
[CmdletBinding()]
param([string[]]$Configs = @('os', 'legacy', 'noaudio'))

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'activate_env.ps1')
foreach ($cfg in $Configs) {
    $bin = Join-Path $root "firmware/build/$cfg/little_world.bin"
    Write-Output "=== $cfg ==="
    & python (Join-Path $PSScriptRoot 'image_check.py') $bin --expect-rev 302
    Write-Output "exit=$LASTEXITCODE"
}
