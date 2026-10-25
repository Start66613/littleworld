# Collects the linkage and partition evidence for the OS V3 + DSH combined image.
# Prints, for each configuration: size, SHA-256, the presence of one symbol from
# every layer (OS V3 UI, DSH services, RPG, pet), and the partition fit.
# SPDX-License-Identifier: MIT
[CmdletBinding()]
param([string[]]$Configs = @('os', 'legacy', 'noaudio'))

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'activate_env.ps1')

# nm lives in the RISC-V toolchain that idf.py already selected.
$nm = (Get-ChildItem -Path (Join-Path $env:IDF_TOOLS_PATH 'tools/riscv32-esp-elf') -Recurse -Filter 'riscv32-esp-elf-nm.exe' -ErrorAction SilentlyContinue |
       Select-Object -First 1).FullName
if (-not $nm) { throw 'riscv32-esp-elf-nm.exe not found under IDF_TOOLS_PATH' }

$symbols = @(
    @{ layer = 'OS V3 UI';     name = 'lw_os_start' },
    @{ layer = 'OS V3 UI';     name = 'lw_os_capabilities' },
    @{ layer = 'OS V3 UI';     name = 'lw_ui_start' },
    @{ layer = 'DSH services'; name = 'lw_services_poll' },
    @{ layer = 'DSH services'; name = 'lw_services_status' },
    @{ layer = 'DSH services'; name = 'lw_wifi_init' },
    @{ layer = 'DSH services'; name = 'lw_time_service_init' },
    @{ layer = 'DSH services'; name = 'lw_audio_init' },
    @{ layer = 'DSH services'; name = 'lw_net_esp_wifi_attach' },
    @{ layer = 'DSH core';     name = 'lw_boot_start' },
    @{ layer = 'DSH core';     name = 'lw_store_load' },
    @{ layer = 'RPG slice';    name = 'lw_rpg_init' },
    @{ layer = 'pet/room';     name = 'lw_pet_tick' }
)

foreach ($cfg in $Configs) {
    $dir = Join-Path $root "firmware/build/$cfg"
    $bin = Join-Path $dir 'little_world.bin'
    $elf = Join-Path $dir 'little_world.elf'
    if (-not (Test-Path $bin)) { Write-Output "[$cfg] missing $bin"; continue }
    $item = Get-Item $bin
    $hash = (Get-FileHash $bin -Algorithm SHA256).Hash.ToLower()
    Write-Output "=== $cfg ==="
    Write-Output ("image   {0:N0} B  sha256 {1}" -f $item.Length, $hash)
    if (Test-Path $elf) {
        $table = & $nm -C --defined-only $elf 2>$null
        foreach ($s in $symbols) {
            $hit = $table | Select-String -SimpleMatch " $($s.name)" | Select-Object -First 1
            $state = if ($hit) { 'linked' } else { 'MISSING' }
            Write-Output ("  [{0,-12}] {1,-24} {2}" -f $s.layer, $s.name, $state)
        }
    } else {
        Write-Output '  (no ELF, symbol evidence skipped)'
    }
}

# Partition fit: the factory app partition must be larger than the image.
$csv = Join-Path $root 'firmware/partitions.csv'
if (Test-Path $csv) {
    Write-Output '=== partitions.csv ==='
    $app = $null
    foreach ($line in Get-Content $csv) {
        $t = $line.Trim()
        if ($t -eq '' -or $t.StartsWith('#')) { continue }
        Write-Output "  $t"
        $f = $t -split ','
        if ($f.Count -ge 5 -and $f[0].Trim() -eq 'factory') {
            $sizeText = $f[4].Trim()
            switch -regex ($sizeText) {
                '^0x([0-9a-fA-F]+)$' { $size = [Convert]::ToInt64($Matches[1], 16); break }
                '^(\d+)[kK]$'        { $size = [int64]$Matches[1] * 1KB; break }
                '^(\d+)[mM]$'        { $size = [int64]$Matches[1] * 1MB; break }
                default              { $size = [int64]$sizeText }
            }
            $app = @{ offset = $f[3].Trim(); size = $size }
        }
    }
    $bin = Join-Path $root 'firmware/build/os/little_world.bin'
    if ($app -and (Test-Path $bin)) {
        $len = (Get-Item $bin).Length
        $pct = [math]::Round(100.0 * $len / $app.size, 1)
        Write-Output ("factory app at {0}, partition {1:N0} B, image {2:N0} B -> {3}% used, fits={4}" -f $app.offset, $app.size, $len, $pct, ($len -le $app.size))
    }
}
