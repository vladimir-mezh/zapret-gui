param([string]$Version='0.2.0')
$ErrorActionPreference='Stop'
if ($Version -notmatch '^[0-9][A-Za-z0-9._-]{0,63}$') {throw 'Invalid release version'}
cmake -S $PSScriptRoot -B "$PSScriptRoot\build" "-DMCP_RELEASE_VERSION=$Version"
if ($LASTEXITCODE -ne 0) {throw 'Configuration failed'}
cmake --build "$PSScriptRoot\build" --config Release
if ($LASTEXITCODE -ne 0) {throw 'Build failed'}
New-Item -ItemType Directory -Force -Path "$PSScriptRoot\dist" | Out-Null
Copy-Item -LiteralPath "$PSScriptRoot\build\Release\ZapretMCP.exe" -Destination "$PSScriptRoot\dist\ZapretMCP.exe"
Copy-Item -LiteralPath "$PSScriptRoot\build\Release\ZapretMcpLauncher.exe" -Destination "$PSScriptRoot\dist\ZapretMcpLauncher.exe"
Copy-Item -LiteralPath "$PSScriptRoot\connect.cmd","$PSScriptRoot\README.md","$PSScriptRoot\third_party\LICENSE-json.txt" -Destination "$PSScriptRoot\dist"
$taskManifest=@{name='zapret-gui-mcp';version=$Version;api_version=1;sha256=(Get-FileHash -LiteralPath "$PSScriptRoot\dist\ZapretMCP.exe" -Algorithm SHA256).Hash.ToLowerInvariant();launcher_sha256=(Get-FileHash -LiteralPath "$PSScriptRoot\dist\ZapretMcpLauncher.exe" -Algorithm SHA256).Hash.ToLowerInvariant();json_license=[IO.File]::ReadAllText("$PSScriptRoot\third_party\LICENSE-json.txt")}
[IO.File]::WriteAllText("$PSScriptRoot\dist\mcp-manifest.json",($taskManifest|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
Compress-Archive -Path "$PSScriptRoot\dist\ZapretMCP.exe","$PSScriptRoot\dist\ZapretMcpLauncher.exe","$PSScriptRoot\dist\mcp-manifest.json","$PSScriptRoot\dist\connect.cmd","$PSScriptRoot\dist\README.md","$PSScriptRoot\dist\LICENSE-json.txt" -DestinationPath "$PSScriptRoot\dist\ZapretMCP.zip" -Force
