@echo off
REM Usage: setup_dev_environment.bat -ProjectDir "D:\path\to\UEProject" [options]
REM   Runs, in order:
REM     0) create_project.ps1          - (optional, -Create) creates a fresh RenderStream test project
REM     1) generate_uplugin.ps1        - creates RenderStream-UE.uplugin from the template
REM     2) link_to_project.ps1         - junction-links this repo into the project's Plugins folder
REM     3) generate_project_files.ps1  - generates VS Code (or -VisualStudio) project files
REM        (Blueprint-only projects with no Source folder are auto-scaffolded with a
REM         minimal C++ game module first, via scaffold_game_module.ps1)
REM     4) bake_project.ps1            - (optional, -Bake) builds the editor target + bakes the test scene
REM   Options (forwarded): -Create, -ProjectName, -Rhi, -Mode, -NoBackup,
REM                        -VisualStudio, -IncludeEngine, -Open, -SkipUplugin, -Bake

ECHO running setup_dev_environment.ps1...
Powershell.exe -ExecutionPolicy Bypass -File "%~dp0setup_dev_environment.ps1" %*

@IF %ERRORLEVEL% NEQ 0 (
  ECHO ERROR running setup_dev_environment.ps1 script. Please check log for issues
) ELSE (
  ECHO SUCCESS
)
pause
EXIT /b %ERRORLEVEL%
