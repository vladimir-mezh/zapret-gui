$ErrorActionPreference='Stop'
$taskExport = Join-Path $PSScriptRoot 'mcp-repository'
New-Item -ItemType Directory -Force -Path "$taskExport\src","$taskExport\shared","$taskExport\third_party" | Out-Null
$taskSource = [IO.File]::ReadAllText("$PSScriptRoot\src\mcp.cpp").Replace('#include "store.hpp"','#include "../shared/store.hpp"')
[IO.File]::WriteAllText("$taskExport\src\main.cpp",$taskSource,[Text.UTF8Encoding]::new($false))
$taskLauncher = [IO.File]::ReadAllText("$PSScriptRoot\src\mcp_launcher.cpp").Replace('#include "store.hpp"','#include "../shared/store.hpp"')
[IO.File]::WriteAllText("$taskExport\src\launcher.cpp",$taskLauncher,[Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath "$PSScriptRoot\src\core.hpp","$PSScriptRoot\src\store.hpp" -Destination "$taskExport\shared"
Copy-Item -LiteralPath "$PSScriptRoot\third_party\json.hpp","$PSScriptRoot\third_party\LICENSE-json.txt" -Destination "$taskExport\third_party"
Copy-Item -LiteralPath "$PSScriptRoot\mcp-repo-template\CMakeLists.txt","$PSScriptRoot\mcp-repo-template\release.ps1","$PSScriptRoot\mcp-repo-template\README.md","$PSScriptRoot\mcp-package\connect.cmd" -Destination $taskExport
Copy-Item -LiteralPath "$PSScriptRoot\LICENSE" -Destination "$taskExport\LICENSE"
[IO.File]::WriteAllText("$taskExport\.gitignore","build/`ndist/`n",[Text.UTF8Encoding]::new($false))
Write-Output "Standalone MCP sources prepared: $taskExport"
