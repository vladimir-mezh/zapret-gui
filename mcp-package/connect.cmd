@echo off
"%~dp0ZapretMCP.exe" --client-config > "%~dp0mcp-config.json"
if errorlevel 1 exit /b 1
start "" notepad.exe "%~dp0mcp-config.json"
