param([string]$Compiler = "$PSScriptRoot\.tools\zig-x86_64-windows-0.15.2\zig.exe",[string]$Output='ZapretGUI.exe')
$ErrorActionPreference = 'Stop'
if (!(Test-Path -LiteralPath $Compiler)) { throw 'Specify -Compiler with a portable Zig executable, or build using CMake + MSVC.' }
New-Item -ItemType Directory -Force -Path "$PSScriptRoot\dist" | Out-Null
& $Compiler c++ "$PSScriptRoot\src\main.cpp" -std=c++17 -Os -s -municode '-Wl,--subsystem,windows' -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN -luser32 -lgdi32 -lcomctl32 -luxtheme -lole32 -lshell32 -luuid -lwinhttp -lbcrypt -ladvapi32 -o "$PSScriptRoot\dist\$Output"
if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
Get-Item "$PSScriptRoot\dist\$Output" | Select-Object FullName,Length
