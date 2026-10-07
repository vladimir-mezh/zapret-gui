param([string]$Compiler = "$PSScriptRoot\.tools\zig-x86_64-windows-0.15.2\zig.exe")
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path "$PSScriptRoot\dist" | Out-Null
& $Compiler c++ "$PSScriptRoot\tests\core_tests.cpp" -std=c++17 -Os -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN -lshell32 -lole32 -luuid -o "$PSScriptRoot\dist\core_tests.exe"
if ($LASTEXITCODE -ne 0) { throw 'Test build failed' }
& "$PSScriptRoot\dist\core_tests.exe"
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
