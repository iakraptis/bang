# Windows counterpart of train.sh: build, test, then run the League PPO campaign.
# Usage: .\train.ps1 [train.py options], e.g. .\train.ps1 --rehearsal --run-dir runs/rehearsal
# Not "Stop": Windows PowerShell treats any native stderr line (CMake warnings,
# training telemetry) as an error. Exit codes are checked explicitly instead.
$ErrorActionPreference = "Continue"

$Root = $PSScriptRoot
$Build = Join-Path $Root "build-train"

$Python = $env:BANG_PYTHON
if (-not $Python) {
  $VenvPython = Join-Path $Root ".venv\Scripts\python.exe"
  $Python = if (Test-Path $VenvPython) { $VenvPython } else { "python" }
}

& $Python -c "import torch" 2>$null
if ($LASTEXITCODE -ne 0) {
  Write-Error "PyTorch is required. Install it for $Python before training."
  exit 2
}

# Load the MSVC x64 toolchain unless this shell already has it.
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
  $Vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
  $VsPath = & $Vswhere -products * -latest `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  if (-not $VsPath) {
    Write-Error "MSVC x64 build tools were not found; install the VS 2022 C++ workload."
    exit 2
  }
  $Vcvars = Join-Path $VsPath "VC\Auxiliary\Build\vcvars64.bat"
  # vcvars probes for vswhere on PATH.
  $env:PATH = "$(Split-Path $Vswhere);$env:PATH"
  cmd /c "`"$Vcvars`" >nul 2>&1 && set" | ForEach-Object {
    if ($_ -match "^([^=]+)=(.*)$") {
      [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], "Process")
    }
  }
}

$TorchPrefix = & $Python -c "import torch; print(torch.utils.cmake_prefix_path)"
$TorchLib = & $Python -c "import os, torch; print(os.path.join(os.path.dirname(torch.__file__), 'lib'))"
# Windows has no rpath: bang_duel.exe finds the Torch DLLs through PATH.
$env:PATH = "$TorchLib;$env:PATH"
# Native telemetry is UTF-8; without this Python decodes pipes and files as cp1252.
$env:PYTHONUTF8 = "1"

function Invoke-Checked {
  & $args[0] @($args | Select-Object -Skip 1)
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host "> Configuring optimized training build"
Invoke-Checked cmake -S $Root -B $Build -G Ninja -Wno-dev `
  -DCMAKE_BUILD_TYPE=Release `
  -DBANG_ENABLE_TORCH=ON `
  -DBANG_BUILD_TESTS=ON `
  "-DCMAKE_PREFIX_PATH=$TorchPrefix" `
  "-DPython3_EXECUTABLE=$Python"
Write-Host "> Building with 6 parallel jobs"
Invoke-Checked cmake --build $Build --parallel 6
Write-Host "> Running native correctness and information-boundary tests"
Invoke-Checked ctest --test-dir $Build --output-on-failure
Write-Host "> Starting League PPO campaign"
& $Python (Join-Path $Root "tools\train.py") --repo-root $Root @args
exit $LASTEXITCODE
