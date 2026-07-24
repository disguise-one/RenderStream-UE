@echo off
REM Usage: generate_project_files.bat                 (uses last-linked project, generates VS Code files)
REM        generate_project_files.bat -ProjectDir "D:\path\to\UEProject"
REM        generate_project_files.bat -VisualStudio  (generate a .sln instead of VS Code files)
REM        generate_project_files.bat -Open          (open the workspace in VS Code when done)
REM Regenerates IDE project files for the linked UE project (see generate_project_files.ps1).

ECHO running generate_project_files.ps1...
Powershell.exe -ExecutionPolicy Bypass -File "%~dp0generate_project_files.ps1" %*

@IF %ERRORLEVEL% NEQ 0 (
  ECHO ERROR running generate_project_files.ps1 script. Please check log for issues
) ELSE (
  ECHO SUCCESS
)
pause
EXIT /b %ERRORLEVEL%
