param([string]$Compiler="$PSScriptRoot\..\.tools\zig-x86_64-windows-0.15.2\zig.exe",[switch]$Live)
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath("$PSScriptRoot\..")
$taskScratch=Join-Path "$taskRoot\.tools" ('backend-test-'+[Guid]::NewGuid().ToString('N'))
& $Compiler c++ "$PSScriptRoot\backend_tests.cpp" -std=c++17 -Os -municode -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN -lshell32 -lole32 -luuid -lwinhttp -lbcrypt -ladvapi32 -o "$taskRoot\.tools\backend_tests.exe"
if($LASTEXITCODE -ne 0){throw 'Backend test build failed'}
if($Live){& "$taskRoot\.tools\backend_tests.exe" $taskScratch --live}else{& "$taskRoot\.tools\backend_tests.exe" $taskScratch}
if($LASTEXITCODE -ne 0){throw 'Backend tests failed'}
