@echo off
ECHO running package_plugin.ps1...
Powershell.exe -ExecutionPolicy RemoteSigned -File "%~dp0package_plugin.ps1" %1

@IF %ERRORLEVEL% NEQ 0 (
  ECHO ERROR running package_plugin.ps1 script. Please check log for issues
) ELSE (
  ECHO SUCCESS
)
pause
EXIT /b %ERRORLEVEL%