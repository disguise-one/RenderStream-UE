@echo off
REM Usage: setup_dev_environment.bat -ProjectDir "D:\path\to\UEProject" [options]
REM   Runs, in order:
REM     1) generate_uplugin.ps1        - creates RenderStream-UE.uplugin from the template
REM     2) link_to_project.ps1         - junction-links this repo into the project's Plugins folder
REM     3) generate_project_files.ps1  - generates VS Code (or -VisualStudio) project files
REM   Options (forwarded): -PluginName, -NoBackup, -VisualStudio, -IncludeEngine, -Open, -SkipUplugin

ECHO running setup_dev_environment.ps1...
Powershell.exe -ExecutionPolicy Bypass -File "%~dp0setup_dev_environment.ps1" %*

@IF %ERRORLEVEL% NEQ 0 (
  ECHO ERROR running setup_dev_environment.ps1 script. Please check log for issues
) ELSE (
  ECHO SUCCESS
)
pause
EXIT /b %ERRORLEVEL%
