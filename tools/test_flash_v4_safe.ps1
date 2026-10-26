# Offline tests for tools/flash_v4_safe.ps1.
#
# The helper is a device tool, so the only safe way to test it is to run it in
# every mode that must NOT write, and assert on what it printed and what exit
# code it returned. Nothing here opens a serial port or touches a board.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/test_flash_v4_safe.ps1
#
# Exit code 0 = all checks passed.
[CmdletBinding()]
param(
    [string]$Config = 'os'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$script = Join-Path $PSScriptRoot 'flash_v4_safe.ps1'
$ps = Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
if (-not (Test-Path $ps)) { $ps = 'powershell.exe' }

$checks = 0
$failures = 0
function Check {
    param([string]$Name, [bool]$Ok, [string]$Detail = '')
    $script:checks++
    if ($Ok) {
        Write-Host ("PASS  {0}" -f $Name)
    } else {
        $script:failures++
        Write-Host ("FAIL  {0}  {1}" -f $Name, $Detail)
    }
}

function Invoke-Helper {
    param([string[]]$ExtraArgs)
    $out = & $ps -NoProfile -ExecutionPolicy Bypass -File $script @ExtraArgs 2>&1 | Out-String
    [pscustomobject]@{ Text = $out; Code = $LASTEXITCODE }
}

Write-Host '=== flash_v4_safe.ps1 offline tests ==='
Write-Host "config: $Config"
Write-Host ''

# 1. Default is a dry run: exit 0, says so, writes nothing.
$r = Invoke-Helper @('-Config', $Config)
Check 'default run is a dry run' ($r.Code -eq 0) "exit=$($r.Code)"
Check 'default run says nothing was written' ($r.Text -match 'DRY RUN: nothing was written') ''
Check 'default run prints a write plan' ($r.Text -match 'write plan:') ''

# 2. The plan must contain the three real addresses and no address 0.
Check 'plan writes bootloader at 0x2000' ($r.Text -match '0x002000') ''
Check 'plan writes partition table at 0x8000' ($r.Text -match '0x008000') ''
Check 'plan writes the app at 0x10000' ($r.Text -match '0x010000') ''
Check 'plan never contains a 0x000000 whole-flash write' ($r.Text -notmatch '0x000000') ''

# 3. The protected partitions must be named as untouched.
Check 'nvs is reported as protected' ($r.Text -match 'nvs \(0x9000\.\.0xF000\)') ''
Check 'no erase_flash in the plan' ($r.Text -notmatch 'erase_flash.*write_flash') ''

# 4. -Execute alone must refuse, and refuse for the right reason.
$r = Invoke-Helper @('-Config', $Config, '-Execute')
Check '-Execute without -Authorized is refused' ($r.Code -ne 0) "exit=$($r.Code)"
Check 'the refusal names -Authorized' ($r.Text -match 'without -Authorized') ''

# 5. -Authorized alone is still a dry run (a stray flag cannot write).
$r = Invoke-Helper @('-Config', $Config, '-Authorized')
Check '-Authorized alone stays a dry run' ($r.Code -eq 0 -and $r.Text -match 'DRY RUN') "exit=$($r.Code)"

# 6. -Stage app writes only the app.
$r = Invoke-Helper @('-Config', $Config, '-Stage', 'app')
Check '-Stage app exits cleanly' ($r.Code -eq 0) "exit=$($r.Code)"
Check '-Stage app omits the bootloader address' ($r.Text -notmatch '0x002000\s+22016') ''
Check '-Stage app still writes 0x10000' ($r.Text -match '0x010000') ''

# 7. A missing build must fail with a message that names the path.
$r = Invoke-Helper @('-Config', 'nosuchconfig')
Check 'missing build fails' ($r.Code -ne 0) "exit=$($r.Code)"
Check 'missing build names the artefact' ($r.Text -match 'missing build artefact') ''

# 8. -ListPorts never touches a build and exits cleanly.
$r = Invoke-Helper @('-ListPorts')
Check '-ListPorts exits 0' ($r.Code -eq 0) "exit=$($r.Code)"
Check '-ListPorts writes nothing' ($r.Text -match 'nothing was opened') ''

# 9. Every run leaves a log file behind.
$logDir = Join-Path $root 'reports/logs/flash'
$logs = @(Get-ChildItem $logDir -Filter 'flash-*.log' -ErrorAction SilentlyContinue)
Check 'runs leave a log file' ($logs.Count -gt 0) "count=$($logs.Count)"

Write-Host ''
Write-Host ("{0} checks, {1} failure(s)" -f $checks, $failures)
if ($failures -eq 0) {
    Write-Host 'FLASH HELPER TESTS PASSED'
    exit 0
}
Write-Host 'FLASH HELPER TESTS FAILED'
exit 1
