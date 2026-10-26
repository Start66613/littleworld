# Connectivity service tests (Wi-Fi state machine, time service, audio pipeline,
# service loop). Kept as a thin wrapper on purpose.
#
# Why: this script used to keep its own copy of the source list, and that copy
# went stale the moment lw_services.c joined tests/host/test_lw_net.c - the link
# failed with "unresolved external symbol lw_services_drain" while the real
# suite was green. There is now exactly one list (run_backend_tests.ps1) and
# this file only selects the 'net' suite from it.
#
# Usage: powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/run_net_tests.ps1
# SPDX-License-Identifier: MIT
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$runner = Join-Path $PSScriptRoot 'run_backend_tests.ps1'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $runner -Only net
exit $LASTEXITCODE
