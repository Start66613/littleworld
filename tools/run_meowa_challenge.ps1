[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$private=Join-Path $root 'private/meowa-challenge'
New-Item -ItemType Directory -Force -Path $private | Out-Null
$guard=Join-Path $private 'SUBMISSION_LOCK.json'
if (Test-Path -LiteralPath $guard) { throw 'One submission already attempted. Recover its job; never resubmit automatically.' }
$prompt='Turn this sprite into her matching boy companion: fluffy dark hair with long naturally parted bangs, warm dark eyes and a gentle cheerful smile; loose white T-shirt with dark collar trim, dark shorts and sneakers. Keep the standing pose, approximately 2.5-head proportions, 198px visible height on the 256px canvas, warm charcoal outlines, refined pixel clusters and soft shading. A single full-body boy for frame animation.'
$expected=12
if ($expected -gt 40) {throw 'Budget cap exceeded'}
[ordered]@{attempted_at=(Get-Date -Format o);command='image-edit-run';expected_credits=$expected;cap=40;attempts=1;real_photo_uploaded=$false} | ConvertTo-Json | Set-Content -LiteralPath $guard -Encoding utf8NoBOM
$env:PYTHONUTF8='1'; $env:PYTHONIOENCODING='utf-8'
$env:PYTHONPYCACHEPREFIX='D:\Espressif\LittleWorld\cache\pycache'
$env:TEMP='D:\Espressif\LittleWorld\temp'; $env:TMP=$env:TEMP
& pwsh -NoProfile -File D:\LittleWorld-Meowa\tools\meowa.ps1 image-edit-run --reference-image D:\Espressif\LittleWorld\sources\DesktopPet\assets\sprites\idle\idle_01.png --prompt $prompt --mode pixel --strict --generation-model nano-banana --resolution 1K --remove-bg-method none --no-multi-pixelation --output-dir "$private/generated"
exit $LASTEXITCODE
