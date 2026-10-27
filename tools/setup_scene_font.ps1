$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$dir=Join-Path $root 'tools/fonts/cache'
New-Item -ItemType Directory -Force $dir | Out-Null
$file=Join-Path $dir 'LXGWWenKaiScreen.ttf'
if (-not (Test-Path -LiteralPath $file)) {Invoke-WebRequest 'https://github.com/lxgw/LxgwWenKai-Screen/releases/download/v1.522/LXGWWenKaiScreen.ttf' -OutFile $file}
if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne 'CD1A6FA39C4EA42FD8F4E289945789B0E510CF7016435640F8893CDAD9B220F3') {throw 'Official font checksum differs'}
$license=Join-Path $root 'firmware/components/lw_ui/fonts/OFL-WenKai.txt'
if (-not (Test-Path -LiteralPath $license)) {Invoke-WebRequest 'https://raw.githubusercontent.com/lxgw/LxgwWenKai-Screen/v1.522/OFL.txt' -OutFile $license}
Write-Output 'Pinned WenKai Screen 1.522 verified. No system font installation.'
