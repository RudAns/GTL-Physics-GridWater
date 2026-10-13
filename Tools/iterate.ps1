# Build -> (optionally) rebuild assets -> run the scripted capture. Usage:
#   powershell -File Tools/iterate.ps1 [-Assets] [-NoCapture] [-NoBuild]
param([switch]$Assets, [switch]$NoCapture, [switch]$NoBuild, [int]$ResX = 1600, [int]$ResY = 900)
$ErrorActionPreference = "Continue"
$p = Split-Path -Parent $PSScriptRoot
$ue = Join-Path (& "$PSScriptRoot\find_ue.ps1") "Engine"
$proj = "$p\GridWater.uproject"

if (-not $NoBuild) {
    $build = & "$ue\Build\BatchFiles\Build.bat" GridWaterEditor Win64 Development -Project="$proj" -WaitMutex 2>&1
    $build | Select-String -Pattern "error C|error LNK|Result:" | ForEach-Object { $_.Line }
    if (-not ($build | Select-String -Pattern "Result: Succeeded")) { "BUILD FAILED - stopping"; exit 1 }
}
if ($Assets) {
    Get-ChildItem "$p\Content" -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $_.IsReadOnly } | ForEach-Object { $_.IsReadOnly = $false }
    & "$ue\Binaries\Win64\UnrealEditor-Cmd.exe" "$proj" -run=pythonscript -script="$p\Tools\build_assets.py" -unattended -nop4 -nosplash -stdout 2>&1 |
        Select-String -Pattern "build_assets|LogPython: Error|Error:|error X|\[SM6\]|Failed to compile" | ForEach-Object { $_.Line }
}
if (-not $NoCapture) {
    $out = "$p\Saved\Capture"
    New-Item -ItemType Directory -Force $out | Out-Null
    Get-ChildItem $out -Filter *.png | Remove-Item -Force
    $proc = Start-Process -FilePath "$ue\Binaries\Win64\UnrealEditor.exe" -ArgumentList @("`"$proj`"", "-game", "-windowed", "-ResX=$ResX", "-ResY=$ResY", "-GWCapture=$out", "-RenderOffScreen", "-ForceRes", "-ExecCmds=`"r.SetRes ${ResX}x${ResY}w`"", "-unattended", "-nosound", "-nosplash", "-log=capture.log") -PassThru
    if (-not $proc.WaitForExit(600000)) { $proc.Kill(); "capture timeout" }
    Get-ChildItem $out -Filter *.png | ForEach-Object { $_.Name }
    Select-String -Path "$p\Saved\Logs\capture.log" -Pattern "GWCapture|Error: |error X|Failed to compile|Ensure condition" | Select-Object -First 30 | ForEach-Object { $_.Line -replace '^\[[^\]]*\]\[[^\]]*\]', '' }
}
