# watch_muse.ps1 - did the cloud agent push anything new?
#
# The user runs Muse Code on a remote machine; the only way to see its work is
# GitHub. This script fetches, lists every agent/* branch, and reports the
# commits that appeared since the last run. It never touches the working tree,
# so it is safe to run while a build is in progress.
#
# Usage:
#   .\tools\watch_muse.ps1              # fetch, report, remember
#   .\tools\watch_muse.ps1 -NoFetch     # report from the refs already on disk
#   .\tools\watch_muse.ps1 -Diffstat    # also show per-branch diffstat

[CmdletBinding()]
param(
    [string]$State = 'reports/perf-v4-5/muse-watch.json',
    [switch]$NoFetch,
    [switch]$Diffstat
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Push-Location $root
try {
    if (-not $NoFetch) {
        git fetch origin --prune --quiet 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { Write-Output 'fetch failed (offline?) - reporting cached refs' }
    }

    $seen = @{}
    if (Test-Path $State) {
        $old = Get-Content $State -Raw | ConvertFrom-Json
        foreach ($p in $old.PSObject.Properties) { $seen[$p.Name] = [string]$p.Value }
    }

    $branches = git for-each-ref --format='%(refname:short)|%(objectname:short)|%(committerdate:short)|%(subject)' refs/remotes/origin |
        Where-Object { $_ -match '^origin/(agent/|perf-optimization|backup/)' } |
        Sort-Object

    $now = [ordered]@{}
    $newcommits = 0
    foreach ($line in $branches) {
        $parts = $line -split '\|', 4
        $name = $parts[0]
        $sha = $parts[1]
        $when = $parts[2]
        $subject = $parts[3]
        $now[$name] = $sha

        $old = $seen[$name]
        if (-not $old) {
            Write-Output "$name  $sha  $when  NEW BRANCH  $subject"
            $newcommits++
        } elseif ($old -ne $sha) {
            $count = (git rev-list --count "$old..$sha" 2>$null)
            Write-Output "$name  $old -> $sha  $when  +$count commit(s)"
            git log --format='    %h %ad %s' --date=short "$old..$sha" 2>$null | Select-Object -First 12
            if ($Diffstat) {
                git diff --stat "$old" "$sha" 2>$null | Select-Object -Last 1 | ForEach-Object { "    $_" }
            }
            $newcommits += [int]$count
        }
    }

    if ($newcommits -eq 0) { Write-Output 'no new commits on any agent/* branch' }

    $dir = Split-Path $State -Parent
    if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
    $json = [ordered]@{ fetchedAt = (Get-Date).ToString('s'); branches = $now }
    $json | ConvertTo-Json -Depth 4 | Set-Content -Path $State -Encoding utf8
    Write-Output "state: $State"
}
finally {
    Pop-Location
}
