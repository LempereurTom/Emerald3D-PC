# Runs the PC development build in a window (Windows).
#   powershell -ExecutionPolicy Bypass -File pc_port\run-dev.ps1            (English)
#   powershell -ExecutionPolicy Bypass -File pc_port\run-dev.ps1 -Lang fr   (French)
# -Lang picks the tree a language was built in: en = build\upstream-pc,
# anything else = build\upstream-pc-<lang> (bootstrap.py --dir ..., J7).
# Saves, settings and port.log go to build\upstream-pc\3ds_port\build\pc-play,
# whatever the language (one save, as on the console's SD card).
# Default keys (OPTIONS > CONTROLS changes them): arrows = D-pad, X = A, Z = B, S = X, A = Y,
# Q = L, W = R, Enter = START, Backspace = SELECT, 1/2 = ZL/ZR.
# The mouse on the bottom screen is the stylus.
# F9 = next screen layout (Shift+F9: previous), Tab = swap the screens,
# F11 or Alt+Enter = fullscreen. The layout is kept in settings.txt.
param([string]$Lang = 'en')
$root = Split-Path -Parent $PSScriptRoot
$tree = if ($Lang -eq 'en') { 'upstream-pc' } else { "upstream-pc-$Lang" }
$dir = Join-Path $root "build\$tree\3ds_port"
if (-not (Test-Path (Join-Path $dir 'emerald-pc.exe'))) {
    Write-Error "No build for language '$Lang' in $dir (pc_port\README.md: building a language)"
    exit 1
}
$env:EMERALD_PC_ROMFS = Join-Path $dir 'build\pc-romfs'
$env:EMERALD_PC_HOME = Join-Path $root 'build\upstream-pc\3ds_port\build\pc-play'
New-Item -ItemType Directory -Force $env:EMERALD_PC_HOME | Out-Null
# An empty debug.txt turns the log on (port.log), as on the console.
$marker = Join-Path $env:EMERALD_PC_HOME 'debug.txt'
if (-not (Test-Path $marker)) { New-Item -ItemType File $marker | Out-Null }
Push-Location $dir
try { & (Join-Path $dir 'emerald-pc.exe') } finally { Pop-Location }
