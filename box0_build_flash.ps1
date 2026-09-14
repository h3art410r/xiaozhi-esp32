# BOX0 one-click build + flash (PowerShell, portable across machines).
#
# Path handling (nothing hardcoded to one PC):
#   - Repo root  = the directory containing this script. This is the only
#                  place you edit; on machines whose repo path contains
#                  non-ASCII characters (e.g. a Chinese user profile) the
#                  script mirrors it to C:\Workspace\box0-repo-mirror with
#                  robocopy and builds there, because parts of the ESP-IDF 6
#                  toolchain (cmake, ccache, objdump, ...) cannot handle
#                  non-ASCII path strings and idf.py canonicalizes paths
#                  (junctions/subst do not help). The mirror is a build cache
#                  only - never edit it; it is overwritten from the real repo.
#   - ESP-IDF    = $env:ESP_IDF_DIR, else the first existing candidate:
#                  C:\Workspace\esp-idf-v6.0.2, %USERPROFILE%\Documents\Opts\esp-idf-v6.0.2,
#                  D:\Opts\esp-idf-v6.0.2.
#   - Tools/temp = redirected to C:\Workspace only when the C:\Workspace
#                  ESP-IDF copy is selected; ESP-IDF defaults otherwise.
#   - Serial port= $env:BOX0_PORT, else the single available COM port, else COM3.
#   - CPU limit  = full-core build by default; set BOX0_ONE_CORE=1 to pin the
#                  build to one core when the host must stay responsive.
$ErrorActionPreference = 'Continue'
if (Test-Path Env:MSYSTEM) { Remove-Item Env:MSYSTEM }
if (Test-Path Env:MSYS2_PATH_TYPE) { Remove-Item Env:MSYS2_PATH_TYPE }

# Pin the whole build (compiler child processes inherit affinity) to one core.
# Opt-in only: BOX0_ONE_CORE=1. Default is a full-core build - single-core was
# tried on the N150 host but proved too slow.
$oneCore = $env:BOX0_ONE_CORE
if ($oneCore -eq '1') {
    [System.Diagnostics.Process]::GetCurrentProcess().ProcessorAffinity = 1
    Write-Host '== single-core build mode (BOX0_ONE_CORE) =='
}

$repo = Split-Path -Parent $MyInvocation.MyCommand.Path

$idfDir = $env:ESP_IDF_DIR
if (-not $idfDir) {
    foreach ($c in @('C:\Workspace\esp-idf-v6.0.2',
                     (Join-Path $env:USERPROFILE 'Documents\Opts\esp-idf-v6.0.2'),
                     'D:\Opts\esp-idf-v6.0.2')) {
        if (Test-Path (Join-Path $c 'export.ps1')) { $idfDir = $c; break }
    }
}
if (-not $idfDir) { throw 'ESP-IDF not found. Set ESP_IDF_DIR to your ESP-IDF v6.0.2 directory.' }

if ($idfDir -like 'C:\Workspace\*') {
    $env:IDF_TOOLS_PATH = 'C:\Workspace\.espressif'
    New-Item -ItemType Directory -Force 'C:\Workspace\Temp' | Out-Null
    $env:TEMP = 'C:\Workspace\Temp'
    $env:TMP = 'C:\Workspace\Temp'
}

$port = $env:BOX0_PORT
if (-not $port) {
    $ports = @([System.IO.Ports.SerialPort]::GetPortNames())
    $port = if ($ports.Count -eq 1) { $ports[0] } else { 'COM3' }
}

$workDir = $repo
if ($repo.ToCharArray() | Where-Object { [int]$_ -gt 127 } | Select-Object -First 1) {
    $workDir = 'C:\Workspace\box0-repo-mirror'
    Write-Host "non-ASCII repo path - mirroring to $workDir (build cache, do not edit)"
    & robocopy $repo $workDir /MIR /XD build /XF npl_sycfg.h /R:2 /W:2 /NFL /NDL /NJH /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "repo mirror failed (robocopy exit $LASTEXITCODE)" }
}

Write-Host "== [1/4] Activate ESP-IDF ($idfDir) =="
. (Join-Path $idfDir 'export.ps1')
if (-not $env:IDF_PATH) { throw 'ESP-IDF activation failed (IDF_PATH not set)' }
$py = (Get-Command python).Source
Write-Host "python: $py"
if ($env:IDF_TOOLS_PATH -and -not $py.StartsWith($env:IDF_TOOLS_PATH)) {
    throw "venv python not on PATH: $py"
}

Write-Host '== [2/4] Generate box0_local_config.h =='
& python (Join-Path $workDir 'scripts\box0_gen_config.py')
if ($LASTEXITCODE -ne 0) { throw 'box0_gen_config.py failed' }

Write-Host '== [3/4] Build firmware =='
Push-Location $workDir
& python scripts\build.py alientek/atk-dnesp32s3-box0 --name atk-dnesp32s3-box0
if ($LASTEXITCODE -ne 0) { Pop-Location; throw 'build failed' }

Write-Host "== [4/4] Flash to $port =="
& idf.py -p $port flash
if ($LASTEXITCODE -ne 0) { Pop-Location; throw 'flash failed' }
Pop-Location
Write-Host '== BOX0 flashed successfully =='
