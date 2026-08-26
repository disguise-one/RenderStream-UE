[CmdletBinding()]
param(
    # Project root (folder containing the .uproject). If omitted, the last-linked
    # project from link_to_project is used (.link_to_project.last).
    [string] $ProjectDir,

    # Override engine location if it can't be resolved from the registry.
    [string] $EngineDir,

    # Skip the compile step and just run the bake commandlet (assumes the editor target is already built).
    [switch] $SkipBuild
)

$ErrorActionPreference = 'Stop'

$RepoDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$stateFile = Join-Path $RepoDir '.link_to_project.last'

# --- Resolve the project directory (mirrors generate_project_files.ps1) -------
if (-not $ProjectDir) {
    if (Test-Path -LiteralPath $stateFile) {
        $linkPath = (Get-Content -LiteralPath $stateFile -Raw).Trim()
        if ($linkPath) { $ProjectDir = Split-Path -Parent (Split-Path -Parent $linkPath) }
    }
    if (-not $ProjectDir) {
        throw "No -ProjectDir given and no saved link found. Run link_to_project first, or pass -ProjectDir."
    }
    Write-Host "Using last-linked project: $ProjectDir" -ForegroundColor Cyan
}
if (-not (Test-Path -LiteralPath $ProjectDir)) {
    throw "Project directory not found: $ProjectDir"
}
$ProjectDir = (Resolve-Path -LiteralPath $ProjectDir).Path

$uproject = Get-ChildItem -LiteralPath $ProjectDir -Filter '*.uproject' -File | Select-Object -First 1
if (-not $uproject) {
    throw "No .uproject found in '$ProjectDir' - is that the project root?"
}
$UprojectPath = $uproject.FullName
Write-Host "Project: $UprojectPath" -ForegroundColor Cyan

# --- Find the editor target (produced by scaffold_game_module.ps1) ------------
$editorTargetFile = Get-ChildItem -LiteralPath (Join-Path $ProjectDir 'Source') -Filter '*Editor.Target.cs' -File -ErrorAction SilentlyContinue |
    Select-Object -First 1
if (-not $editorTargetFile) {
    throw "No *Editor.Target.cs found under Source\ - cannot build the editor target."
}
$EditorTarget = $editorTargetFile.Name -replace '\.Target\.cs$', ''
Write-Host "Editor target: $EditorTarget" -ForegroundColor Cyan

# --- Resolve the engine root (mirrors generate_project_files.ps1) -------------
function Resolve-EngineDir {
    param([string] $Association, [string] $ProjectRoot)

    if (-not $Association) {
        $probe = $ProjectRoot
        while ($probe) {
            if (Test-Path -LiteralPath (Join-Path $probe 'Engine\Binaries')) { return $probe }
            $parent = Split-Path -Parent $probe
            if ($parent -eq $probe) { break }
            $probe = $parent
        }
        throw "Project has an empty EngineAssociation and no engine was found above '$ProjectRoot'. Pass -EngineDir."
    }

    if ($Association -match '^\{?[0-9A-Fa-f-]{36}\}?$') {
        foreach ($buildsKey in @(
            'HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds',
            'HKLM:\SOFTWARE\Epic Games\Unreal Engine\Builds'
        )) {
            if (Test-Path $buildsKey) {
                $props = Get-ItemProperty -Path $buildsKey
                foreach ($name in @($Association, $Association.Trim('{','}'), "{$($Association.Trim('{','}'))}")) {
                    if ($props.PSObject.Properties.Name -contains $name) { return $props.$name }
                }
            }
        }
        throw "Custom engine build '$Association' is not registered in the registry. Pass -EngineDir."
    }

    foreach ($installKey in @(
        "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$Association",
        "HKLM:\SOFTWARE\WOW6432Node\EpicGames\Unreal Engine\$Association"
    )) {
        if (Test-Path $installKey) {
            $dir = (Get-ItemProperty -Path $installKey).InstalledDirectory
            if ($dir) { return $dir }
        }
    }
    throw "Could not find installed engine '$Association' in the registry. Pass -EngineDir."
}

if (-not $EngineDir) {
    $association = (Get-Content -Raw -LiteralPath $UprojectPath | ConvertFrom-Json).EngineAssociation
    $EngineDir = Resolve-EngineDir -Association $association -ProjectRoot $ProjectDir
}
if (-not (Test-Path -LiteralPath $EngineDir)) {
    throw "Engine directory not found: $EngineDir"
}
$EngineDir = (Resolve-Path -LiteralPath $EngineDir).Path
Write-Host "Engine:  $EngineDir" -ForegroundColor Cyan

# --- Pin the engine's bundled .NET so UBT can write target metadata -----------
# Build.bat -> GetDotnetPath.bat is meant to select the bundled dotnet, but if its
# setup doesn't stick (e.g. a machine-wide dotnet on PATH), UBT runs under .NET 8/9
# which removed BinaryFormatter and throws PlatformNotSupportedException while writing
# metadata (exit 6). Force the bundled runtime for this process (inherited by Build.bat):
#   - UE_DOTNET_VERSION short-circuits GetDotnetPath's roll-forward.
#   - DOTNET_ROOT + PATH pin the bundled runtime; MULTILEVEL_LOOKUP=0 ignores machine installs.
$dotnetExe = Get-ChildItem (Join-Path $EngineDir 'Engine\Binaries\ThirdParty\DotNet') -Recurse -Filter 'dotnet.exe' -File -ErrorAction SilentlyContinue |
    Select-Object -First 1
if (-not $dotnetExe) {
    Write-Warning "Could not find a bundled dotnet under the engine; the build may fail with a BinaryFormatter error."
} else {
    $bundledDir     = $dotnetExe.Directory.FullName      # ...\DotNet\<ver>\<arch>
    $bundledVersion = $dotnetExe.Directory.Parent.Name   # <ver>, matches GetDotnetPath's check
    $env:UE_DOTNET_VERSION        = $bundledVersion
    $env:DOTNET_ROOT              = $bundledDir
    $env:DOTNET_MULTILEVEL_LOOKUP = '0'
    $env:PATH                     = "$bundledDir;$env:PATH"
    Write-Host "Pinned bundled dotnet $bundledVersion at $bundledDir" -ForegroundColor DarkGray
}

# --- Build the editor target --------------------------------------------------
# Build.bat handles the bundled-dotnet setup UBT needs, so prefer it over calling UBT directly.
if (-not $SkipBuild) {
    $buildBat = Join-Path $EngineDir 'Engine\Build\BatchFiles\Build.bat'
    if (-not (Test-Path -LiteralPath $buildBat)) {
        throw "Build.bat not found at '$buildBat'."
    }
    Write-Host ""
    Write-Host "Building $EditorTarget (Win64 Development)..." -ForegroundColor Cyan
    & $buildBat $EditorTarget Win64 Development -Project="$UprojectPath" -WaitMutex -FromMsBuild
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed (exit code $LASTEXITCODE)."
    }
    Write-Host "Build succeeded." -ForegroundColor Green
}

# --- Run the bake commandlet headless -----------------------------------------
$editorCmd = @(
    'Engine\Binaries\Win64\UnrealEditor-Cmd.exe',
    'Engine\Binaries\Win64\UE4Editor-Cmd.exe'
) | ForEach-Object { Join-Path $EngineDir $_ } | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $editorCmd) {
    throw "UnrealEditor-Cmd.exe not found under '$EngineDir\Engine\Binaries\Win64'."
}

$cmdArgs = @(
    $UprojectPath,
    '-run=RenderStreamTestBake',
    '-unattended',
    '-nullrhi',
    '-nosplash',
    '-nopause',
    '-stdout',
    '-FullStdOutLogOutput'
)

Write-Host ""
Write-Host "Running bake commandlet..." -ForegroundColor Cyan
Write-Host "  $editorCmd $($cmdArgs -join ' ')" -ForegroundColor DarkGray
# The commandlet's own log is the only place plugin-side errors appear; stdout shows little.
Write-Host "  log: $ProjectDir\Saved\Logs\$($uproject.BaseName).log" -ForegroundColor DarkGray
Write-Host ""

& $editorCmd @cmdArgs
if ($LASTEXITCODE -ne 0) {
    throw "Bake commandlet exited with code $LASTEXITCODE."
}

# --- Verify the maps were produced --------------------------------------------
# The bake authors two persistent maps, each with a streaming sub-level.
$expectedMaps = @(
    'RenderStreamTest.umap',
    'RenderStreamTest2.umap',
    'RenderStreamTest_Sub.umap',
    'RenderStreamTest2_Sub.umap'
)
Write-Host ""
$missing = @()
foreach ($m in $expectedMaps) {
    $mapFile = Join-Path $ProjectDir "Content\Maps\$m"
    if (Test-Path -LiteralPath $mapFile) {
        Write-Host "Baked map: $mapFile" -ForegroundColor Green
    } else {
        $missing += $m
    }
}
if ($missing.Count -gt 0) {
    Write-Warning "Bake commandlet returned success but these maps were not found: $($missing -join ', '). Check the log above."
}

# --- Verify the RenderStream schema was produced ------------------------------
# The commandlet still exits 0 when GenerateAssetMetadata bails (e.g. the installed d3 is
# older than the plugin's minimum), so the maps check alone reports a false success.
$schemaFile = Join-Path $ProjectDir ("rs_{0}.json" -f $uproject.BaseName.ToLower())
if (Test-Path -LiteralPath $schemaFile) {
    Write-Host "Baked schema: $schemaFile" -ForegroundColor Green
} else {
    Write-Warning "No RenderStream schema at '$schemaFile' - schema generation was skipped."
    Write-Warning "Check '$ProjectDir\Saved\Logs\$($uproject.BaseName).log' for a d3 version error (schema generation needs a recent d3 install)."
}
