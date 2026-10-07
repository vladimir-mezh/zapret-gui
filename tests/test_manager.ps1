param([string]$Compiler="$PSScriptRoot\..\.tools\zig-x86_64-windows-0.15.2\zig.exe")
$ErrorActionPreference='Stop'
$taskRoot = [IO.Path]::GetFullPath("$PSScriptRoot\..")
$taskScratch = [IO.Path]::GetFullPath((Join-Path $taskRoot ('.tools\manager-test-' + [Guid]::NewGuid().ToString('N'))))
if (!$taskScratch.StartsWith([IO.Path]::GetFullPath("$taskRoot\.tools\"),[StringComparison]::OrdinalIgnoreCase)) {throw 'Invalid test path'}
& $Compiler c++ "$PSScriptRoot\mcp_manager_tests.cpp" -std=c++17 -Os -municode -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN -lshell32 -lole32 -luuid -lwinhttp -lbcrypt -o "$taskRoot\dist\manager_tests.exe"
if ($LASTEXITCODE -ne 0) {throw 'Manager test build failed'}
try { & "$taskRoot\dist\manager_tests.exe" $taskScratch; if ($LASTEXITCODE -ne 0) {throw 'Manager tests failed'} }
finally { if (Test-Path -LiteralPath $taskScratch) {Remove-Item -LiteralPath $taskScratch -Recurse -Force} }
