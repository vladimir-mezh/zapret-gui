param([Parameter(Mandatory=$true)][string]$WinDivertDll,[string]$Compiler='')
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath("$PSScriptRoot\..")
if(!$Compiler){$Compiler=Join-Path "$taskRoot\.tools\zig-x86_64-windows-0.15.2" 'zig.exe'}
$taskHeader=Join-Path "$taskRoot\.tools" 'windivert.h'
if(!(Test-Path -LiteralPath $taskHeader)){Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/basil00/WinDivert/v2.2.2/include/windivert.h' -OutFile $taskHeader -UseBasicParsing}
& $Compiler c++ "$PSScriptRoot\divert_filter_tests.cpp" -std=c++17 -Os -municode -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN -lshell32 -lole32 -luuid -lwinhttp -lbcrypt -ladvapi32 -o "$taskRoot\.tools\filter_check.exe"
if($LASTEXITCODE -ne 0){throw 'Filter test build failed'}
& "$taskRoot\.tools\filter_check.exe" ([IO.Path]::GetFullPath($WinDivertDll))
if($LASTEXITCODE -ne 0){throw 'Filter tests failed'}
