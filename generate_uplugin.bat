@echo off
ECHO running generate_plugin.ps1...
Powershell.exe -ExecutionPolicy RemoteSigned -File "%~dp0generate_uplugin.ps1"

@IF %ERRORLEVEL% NEQ 0 (
  ECHO ERROR running generate_uplugin.ps1 script. Please check log for issues
) ELSE (
  ECHO SUCCESS
)
pause
EXIT /b %ERRORLEVEL%
