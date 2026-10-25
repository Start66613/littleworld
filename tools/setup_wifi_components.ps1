# Little World - enable the experimental ESP32-C6 hosted stack for one build.
#
# The offline manifest (firmware/main/idf_component.yml) deliberately does NOT
# declare esp_wifi_remote / esp_hosted. This script appends the overlay from
# firmware/idf_component_wifi.yml, which lets the component manager resolve the
# hosted packages for the os-wifi configuration, and restores the manifest when
# -Restore is given (also called by tools/build_v3_2_matrix.ps1).
#
# Nothing here touches the board, the C6 or the network: it only edits a manifest
# inside the worktree.

[CmdletBinding()]
param(
    [string]$Root = '',
    [switch]$Restore
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($Root)) {
    # $PSScriptRoot is empty while the default values are evaluated under -File,
    # so resolve the repo root at run time instead.
    $Root = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
}
$manifest = Join-Path $Root 'firmware\main\idf_component.yml'
$overlay = Join-Path $Root 'firmware\idf_component_wifi.yml'
$backup = Join-Path $Root 'firmware\main\idf_component.yml.offline'

if (-not (Test-Path $manifest)) { throw "manifest not found: $manifest" }

if ($Restore) {
    if (Test-Path $backup) {
        Copy-Item $backup $manifest -Force
        Remove-Item $backup -Force
        Write-Output 'wifi overlay: manifest restored (offline)'
    } else {
        Write-Output 'wifi overlay: nothing to restore'
    }
    return
}

if (-not (Test-Path $overlay)) { throw "overlay not found: $overlay" }
if (Test-Path $backup) { Remove-Item $backup -Force }   # stale backup from an interrupted run
Copy-Item $manifest $backup -Force
$text = Get-Content $manifest -Raw
if ($text -notmatch 'esp_wifi_remote') {
    $ov = Get-Content $overlay -Raw
    $text = $text.TrimEnd() + "`r`n" + $ov
    # Write without BOM: the component manager parses this file as UTF-8.
    [System.IO.File]::WriteAllText($manifest, $text, (New-Object System.Text.UTF8Encoding($false)))
}
Write-Output 'wifi overlay: manifest now declares esp_wifi_remote 1.2.5 + esp_hosted 1.4.*'
Get-Content $manifest | Select-Object -Last 6
