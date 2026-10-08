param([string]$Compiler = "$PSScriptRoot\.tools\zig-x86_64-windows-0.15.2\zig.exe",[string]$Version='0.3.1')
$ErrorActionPreference = 'Stop'
$taskPackage = "$PSScriptRoot\dist\mcp"
New-Item -ItemType Directory -Force -Path $taskPackage,"$PSScriptRoot\dist\optional" | Out-Null
if ($Version -notmatch '^[0-9][A-Za-z0-9._-]{0,63}$') { throw 'Invalid MCP version' }
[IO.File]::WriteAllText("$PSScriptRoot\dist\mcp-version.hpp",('#define MCP_VERSION "' + $Version + '"'),[Text.UTF8Encoding]::new($false))
& $Compiler c++ "$PSScriptRoot\src\mcp.cpp" -std=c++17 -Os -s -municode -include "$PSScriptRoot\dist\mcp-version.hpp" -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN -lole32 -lshell32 -luuid -o "$taskPackage\ZapretMCP.exe"
if ($LASTEXITCODE -ne 0) { throw 'MCP build failed' }
& $Compiler c++ "$PSScriptRoot\src\mcp_launcher.cpp" -std=c++17 -Os -s -municode -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN -lole32 -lshell32 -luuid -o "$taskPackage\ZapretMcpLauncher.exe"
if ($LASTEXITCODE -ne 0) { throw 'MCP launcher build failed' }
Copy-Item -LiteralPath "$PSScriptRoot\docs\MCP.md" -Destination "$taskPackage\README.md"
Copy-Item -LiteralPath "$PSScriptRoot\third_party\LICENSE-json.txt" -Destination "$taskPackage\LICENSE-json.txt"
Copy-Item -LiteralPath "$PSScriptRoot\mcp-package\connect.cmd" -Destination "$taskPackage\connect.cmd"
$taskManifest = @{name='zapret-gui-mcp';version=$Version;api_version=1;sha256=(Get-FileHash -LiteralPath "$taskPackage\ZapretMCP.exe" -Algorithm SHA256).Hash.ToLowerInvariant();launcher_sha256=(Get-FileHash -LiteralPath "$taskPackage\ZapretMcpLauncher.exe" -Algorithm SHA256).Hash.ToLowerInvariant();json_license=[IO.File]::ReadAllText("$PSScriptRoot\third_party\LICENSE-json.txt")}
[IO.File]::WriteAllText("$taskPackage\mcp-manifest.json",($taskManifest | ConvertTo-Json),[Text.UTF8Encoding]::new($false))
Compress-Archive -Path "$taskPackage\*" -DestinationPath "$PSScriptRoot\dist\optional\ZapretMCP.zip" -Force
Get-Item "$PSScriptRoot\dist\optional\ZapretMCP.zip" | Select-Object FullName,Length
