param([string]$Compiler="$PSScriptRoot\..\.tools\zig-x86_64-windows-0.15.2\zig.exe",[switch]$Live)
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath("$PSScriptRoot\..")
$taskScratch=Join-Path "$taskRoot\.tools" ('testing-test-'+[Guid]::NewGuid().ToString('N'))
& $Compiler c++ "$PSScriptRoot\test_process_fixture.cpp" -std=c++17 -Os -o "$taskRoot\.tools\test_process_fixture.exe"
if($LASTEXITCODE -ne 0){throw 'Process fixture build failed'}
& $Compiler c++ "$PSScriptRoot\testing_tests.cpp" -std=c++17 -Os -municode -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN -lshell32 -lole32 -luuid -lwinhttp -lbcrypt -ladvapi32 -o "$taskRoot\.tools\testing_tests.exe"
if($LASTEXITCODE -ne 0){throw 'Testing tests build failed'}
if($Live){& "$taskRoot\.tools\testing_tests.exe" $taskScratch "$taskRoot\.tools\test_process_fixture.exe" --live}else{& "$taskRoot\.tools\testing_tests.exe" $taskScratch "$taskRoot\.tools\test_process_fixture.exe"}
if($LASTEXITCODE -ne 0){throw 'Testing tests failed'}
