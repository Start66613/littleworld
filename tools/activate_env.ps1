# Process-local D-drive environment. No install, network request or device access.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$toolsRoot = 'D:\Espressif\LittleWorld'
$env:IDF_PATH = Join-Path $toolsRoot 'esp-idf-v5.5.5'
$env:IDF_TOOLS_PATH = Join-Path $toolsRoot 'idf-tools'
$env:IDF_PYTHON_ENV_PATH = Join-Path $env:IDF_TOOLS_PATH 'python_env/idf5.5_py3.12_env'
$env:TEMP = Join-Path $toolsRoot 'temp'
$env:TMP = $env:TEMP
$env:PIP_CACHE_DIR = Join-Path $toolsRoot 'cache/pip'
$env:PYTHONPYCACHEPREFIX = Join-Path $toolsRoot 'cache/pycache'
$env:IDF_COMPONENT_CACHE_PATH = Join-Path $toolsRoot 'cache/components'
$env:XDG_CACHE_HOME = Join-Path $toolsRoot 'cache/xdg'
$env:CCACHE_DIR = Join-Path $toolsRoot 'cache/ccache'
$env:IDF_PY_BUILD_JOBS = '2'
$env:CMAKE_BUILD_PARALLEL_LEVEL = '2'
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
$env:GIT_TERMINAL_PROMPT = '0'
New-Item -ItemType Directory -Force -Path $env:TEMP,$env:PYTHONPYCACHEPREFIX | Out-Null
$pythonBase = Join-Path $toolsRoot 'python/python.exe'
$exports = @(& $pythonBase (Join-Path $env:IDF_PATH 'tools/idf_tools.py') export --format key-value)
if ($LASTEXITCODE -ne 0) { throw 'Pinned D-drive ESP-IDF tool export failed.' }
foreach ($line in $exports) {
    if ($line -match '^([A-Z][A-Z0-9_]*)=(.*)$') {
        [Environment]::SetEnvironmentVariable($Matches[1],$Matches[2].Replace('%PATH%',$env:PATH).Replace('$PATH',$env:PATH),'Process')
    }
}
$env:PATH = (Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts') + ';' + $env:PATH + ';' + (Join-Path $env:IDF_PATH 'tools')
$env:SDKCONFIG_DEFAULTS = 'sdkconfig.defaults;sdkconfig.defaults.rev3_x'
