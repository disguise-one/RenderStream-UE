@echo off
REM Usage: link_to_project.bat "D:\path\to\UEProject" [-NoBackup]
REM        link_to_project.bat -Unlink          (no path needed - reuses last link)
REM Junction-links this repo into the project's Plugins folder (see link_to_project.ps1).

ECHO running link_to_project.ps1...
Powershell.exe -ExecutionPolicy Bypass -File "%~dp0link_to_project.ps1" %*

@IF %ERRORLEVEL% NEQ 0 (
  ECHO ERROR running link_to_project.ps1 script. Please check log for issues
) ELSE (
  ECHO SUCCESS
)
pause
EXIT /b %ERRORLEVEL%
