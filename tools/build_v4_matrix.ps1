# V4.1 build matrix.
#
# Builds every configuration the V4.1 mandate asks for, one idf.py invocation
# per config, each into its own build directory. Never flashes anything: this
# script only runs `idf.py build`.
#
# Why separate build dirs: an IDF build dir pins the absolute source path in
# CMakeCache.txt, so sharing or copying one between trees makes CMake abort with
# "The current CMakeCache.txt directory ... is different than the directory ...
# where CMakeCache.txt was created". Each config therefore gets a fresh dir.
#
# SPDX-License-Identifier: MIT
[CmdletBinding()]
param(
    [string[]]$Configs = @('os', 'legacy', 'noaudio', 'diag-a', 'diag-b', 'diag-c', 'nocard', 'wifi', 'diag-e'),
    # Where the machine-readable result goes. The V4.1 delivery report lives at
    # reports/integration-v4/build_matrix.json and is NOT rewritten by default:
    # a later sprint must not silently replace the artifact that was shipped.
    [string]$Report = 'reports\backend-sprint\build_matrix.json',
    # Where the per-config build logs go. The default keeps the historical
    # location so older reports' `log` fields stay valid; a new round passes its
    # own directory instead of inheriting v41 by accident.
    [string]$LogDir = 'reports\logs\v41',
    # Delete each config's generated sdkconfig before building.
    #
    # Needed whenever a fragment gains a symbol that the existing sdkconfig
    # already carries with a different value: idf.py treats build/<dir>/sdkconfig
    # as the user's own config, so it silently keeps the old value and the
    # fragment line never takes effect. This bit perf-ppa exactly once -
    # CONFIG_LV_DRAW_BUF_ALIGN stayed 4 while the fragment asked for 128, and the
    # build died in lv_draw_ppa_private.h with
    # "CONFIG_LV_DRAW_BUF_ALIGN must be equal to CONFIG_CACHE_L2_CACHE_LINE_SIZE!".
    # Costs a full rebuild, so it is opt-in rather than the default.
    [switch]$Fresh,
    [switch]$KeepGoing
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$fw = Join-Path $root 'firmware'
$logDir = if ([System.IO.Path]::IsPathRooted($LogDir)) { $LogDir } else { Join-Path $root $LogDir }
$logLabel = $LogDir -replace '\\', '/'
$reportPath = if ([System.IO.Path]::IsPathRooted($Report)) { $Report } else { Join-Path $root $Report }
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $reportPath) | Out-Null

# fragment: the sdkconfig fragment appended to the base chain ('' = none).
# dir:      the build directory under firmware/build/.
# wifi:     whether the hosted component overlay must be installed first.
$matrix = @{
    'os'      = @{ fragment = '';                        dir = 'os';      wifi = $false }
    'legacy'  = @{ fragment = ';sdkconfig.extra.legacy';  dir = 'legacy';  wifi = $false }
    'noaudio' = @{ fragment = ';sdkconfig.extra.noaudio'; dir = 'noaudio'; wifi = $false }
    'diag-a'  = @{ fragment = ';sdkconfig.extra.diag-a';  dir = 'diag-a';  wifi = $false }
    'diag-b'  = @{ fragment = ';sdkconfig.extra.diag-b';  dir = 'diag-b';  wifi = $false }
    'diag-c'  = @{ fragment = ';sdkconfig.extra.diag-c';  dir = 'diag-c';  wifi = $false }
    'nocard'  = @{ fragment = ';sdkconfig.extra.nocard';  dir = 'nocard';  wifi = $false }
    'wifi'    = @{ fragment = ';sdkconfig.extra.wifi';    dir = 'wifi';    wifi = $true  }
    'diag-e'  = @{ fragment = ';sdkconfig.extra.diag-e';  dir = 'diag-e';  wifi = $false }
    # V4.4 P0-E display benchmark variants. Each differs from `os` in exactly
    # one display-path parameter so the sched/fps numbers can be compared.
    'perf-ppa'    = @{ fragment = ';sdkconfig.extra.perf-ppa';    dir = 'perf-ppa';    wifi = $false }
    'perf-unit1'  = @{ fragment = ';sdkconfig.extra.perf-unit1';  dir = 'perf-unit1';  wifi = $false }
    'perf-double' = @{ fragment = ';sdkconfig.extra.perf-double'; dir = 'perf-double'; wifi = $false }
    'perf-full'   = @{ fragment = ';sdkconfig.extra.perf-full';   dir = 'perf-full';   wifi = $false }
}

$base = 'sdkconfig.defaults;sdkconfig.defaults.rev3_x;sdkconfig.os'
$results = @()
$failed = @()

# Which symbols each perf variant is supposed to change. The build report records
# their effective values so a variant that quietly kept the baseline config is
# visible in the JSON instead of only in a build failure.
$variantSymbols = @(
    'CONFIG_LV_USE_PPA',
    'CONFIG_LV_USE_PPA_IMG',
    'CONFIG_LV_DRAW_BUF_ALIGN',
    'CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT',
    'CONFIG_LW_TEAR_AVOID_DOUBLE_PARTIAL',
    'CONFIG_LW_TEAR_AVOID_TRIPLE_FULL',
    'CONFIG_LW_TEAR_AVOID_TRIPLE_PARTIAL'
)

function Get-VariantConfig {
    param([string]$BuildDir)
    $path = Join-Path $BuildDir 'sdkconfig'
    if (-not (Test-Path $path)) { return $null }
    $values = [ordered]@{}
    foreach ($line in Get-Content $path) {
        foreach ($symbol in $variantSymbols) {
            if ($line -eq "$symbol=y") { $values[$symbol] = 'y' }
            elseif ($line -match "^$symbol=(.+)$") { $values[$symbol] = $Matches[1] }
        }
    }
    return $values
}

foreach ($name in $Configs) {
    if (-not $matrix.ContainsKey($name)) {
        throw "unknown config '$name' (known: $($matrix.Keys -join ', '))"
    }
    $cfg = $matrix[$name]
    $buildDir = Join-Path $fw ("build\" + $cfg.dir)
    $log = Join-Path $logDir ("build-$name.log")
    $defaults = $base + $cfg.fragment

    Write-Output "=== $name -> build/$($cfg.dir) ==="
    $overlay = $false
    try {
        if ($Fresh) {
            # Only the generated config, never the source tree or the build
            # output: ninja reuses whatever objects still match.
            foreach ($stale in @('sdkconfig', 'sdkconfig.old')) {
                $path = Join-Path $buildDir $stale
                if (Test-Path $path) { Remove-Item $path -Force }
            }
        }
        if ($cfg.wifi) {
            & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'setup_wifi_components.ps1')
            if ($LASTEXITCODE -ne 0) { throw "wifi component overlay failed ($LASTEXITCODE)" }
            $overlay = $true
        }
        $inner = "Set-Location '$fw'; . '$PSScriptRoot\activate_env.ps1'; " +
                 "idf.py -B 'build/$($cfg.dir)' -DSDKCONFIG='build/$($cfg.dir)/sdkconfig' " +
                 "-DSDKCONFIG_DEFAULTS='$defaults' build *> '$log'"
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -Command $inner
        $code = $LASTEXITCODE
        if ($code -ne 0) { throw "idf.py build failed ($code); see $log" }

        $bin = Join-Path $buildDir 'little_world.bin'
        if (-not (Test-Path $bin)) { throw "no image at $bin" }
        $item = Get-Item $bin
        $sha = (Get-FileHash $bin -Algorithm SHA256).Hash.ToLower()
        $complete = Select-String -Path $log -Pattern 'Project build complete' -SimpleMatch
        if (-not $complete) { throw "log has no 'Project build complete' line: $log" }

        $row = [ordered]@{
            config   = $name
            build_dir = "firmware/build/$($cfg.dir)"
            bytes    = $item.Length
            sha256   = $sha
            defaults = $defaults
            log      = "$logLabel/build-$name.log"
            status   = 'built'
            # Proof that the fragment actually landed. Reading the generated
            # sdkconfig back is the only way to tell a real variant from one the
            # stale-config trap silently ignored.
            effective = Get-VariantConfig $buildDir
        }
        Write-Output ("    {0} B  {1}" -f $item.Length, $sha)
    } catch {
        Write-Output "    FAILED: $($_.Exception.Message)"
        $failed += $name
        $row = [ordered]@{
            config   = $name
            build_dir = "firmware/build/$($cfg.dir)"
            defaults = $defaults
            log      = "$logLabel/build-$name.log"
            status   = 'failed'
            error    = $_.Exception.Message
        }
        if (-not $KeepGoing) {
            $results += $row
            # Persist what we have before bailing out, so an early failure does
            # not erase the configs that already built.
            [System.IO.File]::WriteAllText($reportPath, (ConvertTo-Json -InputObject $results -Depth 5),
                (New-Object System.Text.UTF8Encoding($false)))
            throw
        }
    } finally {
        if ($overlay) {
            & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'setup_wifi_components.ps1') -Restore
        }
    }
    $results += $row
    # Write after every config: a later crash must not lose earlier results.
    [System.IO.File]::WriteAllText($reportPath, (ConvertTo-Json -InputObject $results -Depth 5),
        (New-Object System.Text.UTF8Encoding($false)))
}

Write-Output ''
Write-Output "matrix: $($results.Count) config(s), $($failed.Count) failure(s)"
Write-Output "report: $reportPath"
if ($failed.Count) { Write-Output ("failed: " + ($failed -join ', ')); exit 1 }
Write-Output 'V4 BUILD MATRIX OK'
