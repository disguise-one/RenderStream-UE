[CmdletBinding()]
param(
    # Destination folder for the new project. Created if it does not exist.
    [string] $ProjectDir,

    # Project / module name. Defaults to a sanitized form of the ProjectDir leaf folder.
    [string] $ProjectName,

    # Engine version to associate (e.g. "5.3"). Defaults to the plugin template's EngineVersion.
    [string] $EngineAssociation,

    # Default graphics RHI for the generated project. RenderStream supports all three.
    [ValidateSet('D3D12', 'D3D11', 'Vulkan')]
    [string] $Rhi = 'D3D12',

    # RenderStream scene-selection mode: None (RenderStream manages nothing), Maps, or
    # StreamingLevels. Defaults to None.
    [ValidateSet('None', 'Maps', 'StreamingLevels')]
    [string] $Mode = 'None',

    # Print usage and exit.
    [switch] $Help
)

$ErrorActionPreference = 'Stop'

if ($Help) {
    Write-Host @"
Usage: create_project.ps1 -ProjectDir <path> [options]

Generates a RenderStream test project from scratch: .uproject, config, a C++ game module
and an editor bake module whose commandlet builds the test scene (rotating cube, plate
cameras, texture/text/lighting params and custom events) and the RenderStream schema.

  -ProjectDir <path>              Destination folder for the new project (created if missing).
  -ProjectName <name>             Project/module name (defaults to the -ProjectDir folder name).
  -EngineAssociation <v>          Engine version to associate, e.g. 5.3 (defaults to the plugin template's).
  -Rhi <D3D12|D3D11|Vulkan>       Default graphics RHI for the project (defaults to D3D12).
  -Mode <None|Maps|StreamingLevels>  RenderStream scene selector (defaults to None).
  -Help                           Show this help and exit.
"@
    return
}

if (-not $ProjectDir) {
    throw "-ProjectDir is required (run with -Help for usage)."
}

$RepoDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }

# --- Resolve names -----------------------------------------------------------
$leaf = Split-Path -Leaf $ProjectDir
if (-not $ProjectName) { $ProjectName = $leaf }

# A UE module/target name must be a C++ identifier.
$ModuleName = ($ProjectName -replace '[^A-Za-z0-9_]', '')
if ($ModuleName -match '^\d') { $ModuleName = "M$ModuleName" }
if (-not $ModuleName)         { $ModuleName = 'Game' }

# Default the engine association from the plugin template so the project matches the plugin it hosts.
if (-not $EngineAssociation) {
    $templatePath = Join-Path $RepoDir 'uplugin_template.json'
    if (Test-Path -LiteralPath $templatePath) {
        $engineVersion = (Get-Content -Raw -LiteralPath $templatePath | ConvertFrom-Json).EngineVersion
        if ($engineVersion -match '^(\d+\.\d+)') { $EngineAssociation = $Matches[1] }
    }
    if (-not $EngineAssociation) {
        throw "Could not derive an engine version from uplugin_template.json. Pass -EngineAssociation (e.g. '5.3')."
    }
}

# --- Guard against clobbering an existing project ----------------------------
if (Test-Path -LiteralPath $ProjectDir) {
    $existing = Get-ChildItem -LiteralPath $ProjectDir -Filter '*.uproject' -File -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($existing) {
        throw "A project already exists here ($($existing.Name)). Omit -Create to set up the existing project instead."
    }
} else {
    New-Item -ItemType Directory -Path $ProjectDir | Out-Null
}
$ProjectDir = (Resolve-Path -LiteralPath $ProjectDir).Path

Write-Host "Creating project '$ProjectName' (module '$ModuleName', engine $EngineAssociation) in:" -ForegroundColor Cyan
Write-Host "  $ProjectDir" -ForegroundColor Cyan
Write-Host "  RHI: $Rhi   SceneSelector: $Mode" -ForegroundColor Cyan

# Write UTF-8 without BOM - keeps the files clean for UBT's C#/JSON parsers.
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
function Write-TextFile([string] $Path, [string] $Content) {
    [System.IO.File]::WriteAllText($Path, $Content, $utf8NoBom)
    Write-Host "  wrote $Path" -ForegroundColor DarkGray
}

# --- .uproject ----------------------------------------------------------------
# Modules are left to scaffold_game_module.ps1 (single source of truth for the target files).
# RenderStream-UE is the .uplugin base name; enabling it auto-enables its declared
# dependencies (LiveLink, nDisplay, ConcertSyncClient).
$uproject = [ordered]@{
    FileVersion       = 3
    EngineAssociation = $EngineAssociation
    Category          = ''
    Description       = "RenderStream test project ($ProjectName)"
    Plugins           = @(
        [ordered]@{ Name = 'RenderStream-UE'; Enabled = $true }
    )
}
$uprojectPath = Join-Path $ProjectDir "$ProjectName.uproject"
Write-TextFile $uprojectPath ($uproject | ConvertTo-Json -Depth 20)

# --- Config -------------------------------------------------------------------
$configDir = Join-Path $ProjectDir 'Config'
New-Item -ItemType Directory -Path $configDir -Force | Out-Null

# The default map does not exist yet - the headless bake (Stage 3) creates it.
$defaultMap = '/Game/Maps/RenderStreamTest.RenderStreamTest'

# Scene-selection mode -> URenderStreamSettings::SceneSelector (Config=Engine).
$sceneSelector = $Mode

# RHI selection -> WindowsTargetSettings::DefaultGraphicsRHI.
$graphicsRhi = switch ($Rhi) {
    'D3D11'  { 'DefaultGraphicsRHI_DX11' }
    'Vulkan' { 'DefaultGraphicsRHI_Vulkan' }
    default  { 'DefaultGraphicsRHI_DX12' }
}

$defaultEngineIni = @"
[/Script/EngineSettings.GameMapsSettings]
EditorStartupMap=$defaultMap
GameDefaultMap=$defaultMap

[/Script/WindowsTargetPlatform.WindowsTargetSettings]
DefaultGraphicsRHI=$graphicsRhi

[/Script/RenderStream.RenderStreamSettings]
SceneSelector=$sceneSelector
GenerateEvents=True
"@
Write-TextFile (Join-Path $configDir 'DefaultEngine.ini') $defaultEngineIni

$defaultGameIni = @"
[/Script/EngineSettings.GeneralProjectSettings]
ProjectName=$ProjectName
"@
Write-TextFile (Join-Path $configDir 'DefaultGame.ini') $defaultGameIni

# --- Content ------------------------------------------------------------------
New-Item -ItemType Directory -Path (Join-Path $ProjectDir 'Content') -Force | Out-Null

# --- Game module --------------------------------------------------------------
# Delegate the C++ module to scaffold_game_module.ps1, adding a RenderStream dependency.
# It runs in-process (& on a .ps1) and shares our 'Stop' preference, so a real failure throws.
& (Join-Path $RepoDir 'scaffold_game_module.ps1') -ProjectDir $ProjectDir -ModuleName $ModuleName -ExtraModules @('RenderStream')

# --- Cube-rain spawner (runtime actor in the game module) ---------------------
# A rigid-body cube-rain effect for the sub-levels. It ticks at runtime, reads the sub-level's
# exposed params off the level's RenderStream blueprint actor, and spawns physics cube actors that fall + collide.
# Lives in the runtime game module so it also works outside the editor; the bake places one in
# each sub-level (by class path, so no cross-module build dependency).
$gameModuleDir = Join-Path $ProjectDir "Source\$ModuleName"

$templatesDir = Join-Path $RepoDir 'templates\game_module'
$spawnerH   = [System.IO.File]::ReadAllText((Join-Path $templatesDir 'CubeRainSpawner.h'))
$spawnerCpp = [System.IO.File]::ReadAllText((Join-Path $templatesDir 'CubeRainSpawner.cpp'))

Write-TextFile (Join-Path $gameModuleDir 'CubeRainSpawner.h')   $spawnerH
Write-TextFile (Join-Path $gameModuleDir 'CubeRainSpawner.cpp') $spawnerCpp

# --- Editor bake module -------------------------------------------------------
# Adds the commandlet that builds the test scene during the headless bake (Stage 3).
& (Join-Path $RepoDir 'scaffold_bake_module.ps1') -ProjectDir $ProjectDir -GameModule $ModuleName

Write-Host ""
Write-Host "Project skeleton created." -ForegroundColor Green
Write-Host "  Default map '$defaultMap' will be generated by the headless bake." -ForegroundColor DarkGray
