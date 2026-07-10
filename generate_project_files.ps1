[CmdletBinding()]
param(
    # Project root (folder containing the .uproject). If omitted, the last-linked
    # project from link_to_project is used (.link_to_project.last).
    [string] $ProjectDir,

    # Override engine location if it can't be resolved from the registry.
    [string] $EngineDir,

    # Include full engine source as a folder in the workspace (heavier, better for engine debugging).
    [switch] $IncludeEngine,

    # Generate a Visual Studio .sln instead of VS Code files.
    [switch] $VisualStudio,

    # Open the generated workspace when done.
    [switch] $Open
)

$ErrorActionPreference = 'Stop'

$RepoDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$stateFile = Join-Path $RepoDir '.link_to_project.last'

# --- Resolve the project directory -------------------------------------------
if (-not $ProjectDir) {
    if (Test-Path -LiteralPath $stateFile) {
        # State file stores the junction path: <Project>\Plugins\<PluginName>
        $linkPath = (Get-Content -LiteralPath $stateFile -Raw).Trim()
        if ($linkPath) {
            $ProjectDir = Split-Path -Parent (Split-Path -Parent $linkPath)
        }
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

# --- Warn if Blueprint-only (no scaffolding, per configuration) --------------
if (-not (Test-Path -LiteralPath (Join-Path $ProjectDir 'Source'))) {
    Write-Warning ("This project has no Source\ folder (Blueprint-only). Project files will be generated, but" + [Environment]::NewLine +
        "without a game C++ module there is no <Project>Editor build target. The plugin can still be built" + [Environment]::NewLine +
        "(the editor will offer to compile it on open), but you won't get a buildable game target in the IDE.")
}

# --- Resolve the engine root -------------------------------------------------
function Resolve-EngineDir {
    param([string] $Association, [string] $ProjectRoot)

    # Empty association => project lives under an engine's own folder tree.
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

    # A GUID => a custom/source build registered by UnrealVersionSelector.
    if ($Association -match '^\{?[0-9A-Fa-f-]{36}\}?$') {
        foreach ($buildsKey in @(
            'HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds',
            'HKLM:\SOFTWARE\Epic Games\Unreal Engine\Builds'
        )) {
            if (Test-Path $buildsKey) {
                $props = Get-ItemProperty -Path $buildsKey
                foreach ($name in @($Association, $Association.Trim('{','}'), "{$($Association.Trim('{','}'))}")) {
                    if ($props.PSObject.Properties.Name -contains $name) {
                        return $props.$name
                    }
                }
            }
        }
        throw "Custom engine build '$Association' is not registered in the registry. Pass -EngineDir."
    }

    # Otherwise a version string like "5.3" => a Launcher-installed engine.
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

# --- Locate UnrealBuildTool (UE5 layout) -------------------------------------
$ubtCandidates = @(
    'Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe',
    'Engine\Binaries\DotNET\UnrealBuildTool.exe'
) | ForEach-Object { Join-Path $EngineDir $_ }

$UBT = $ubtCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $UBT) {
    throw "UnrealBuildTool.exe not found under '$EngineDir'. Looked in:`n  $($ubtCandidates -join "`n  ")"
}

# --- Generate ----------------------------------------------------------------
$formatFlag = if ($VisualStudio) { '-VisualStudio' } else { '-VSCode' }
$ubtArgs = @('-projectfiles', "-project=$UprojectPath", '-game', $formatFlag)
if ($IncludeEngine) { $ubtArgs += '-engine' }

Write-Host ""
Write-Host "Generating project files ($($formatFlag.TrimStart('-')))..." -ForegroundColor Cyan
Write-Host "  $UBT $($ubtArgs -join ' ')" -ForegroundColor DarkGray
Write-Host ""

& $UBT @ubtArgs
if ($LASTEXITCODE -ne 0) {
    throw "UnrealBuildTool exited with code $LASTEXITCODE."
}

Write-Host ""
Write-Host "Project files generated." -ForegroundColor Green

# --- Pin the generated tasks to the engine's bundled .NET --------------------
# Build.bat -> GetDotnetPath.bat sets DOTNET_ROLL_FORWARD=LatestMajor. If its
# bundled-dotnet setup doesn't stick (a malformed PATH entry in the inherited
# environment can defeat it), a machine-wide dotnet runs the net6.0
# UnrealBuildTool and rolls forward to .NET 8/9 - which removed BinaryFormatter,
# so UBT throws PlatformNotSupportedException while writing target metadata.
# Force every task to use the engine's bundled .NET:
#   - UE_DOTNET_VERSION short-circuits GetDotnetPath (skips its roll-forward).
#   - DOTNET_ROOT + PATH pin the bundled runtime; MULTILEVEL_LOOKUP=0 ignores
#     any machine-wide install.

# Discover the engine's bundled dotnet (version + arch folder vary by engine).
$dotnetExe = Get-ChildItem (Join-Path $EngineDir 'Engine\Binaries\ThirdParty\DotNet') -Recurse -Filter 'dotnet.exe' -File -ErrorAction SilentlyContinue |
    Select-Object -First 1
if (-not $dotnetExe) {
    Write-Warning "Could not find a bundled dotnet under the engine; leaving task dotnet environment untouched."
} else {
    $bundledDir     = $dotnetExe.Directory.FullName          # ...\DotNet\<ver>\<arch>
    $bundledVersion = $dotnetExe.Directory.Parent.Name       # <ver>, matches GetDotnetPath's check
    $bundledEnv = [ordered]@{
        'UE_DOTNET_VERSION'        = $bundledVersion
        'DOTNET_ROOT'              = $bundledDir
        'DOTNET_MULTILEVEL_LOOKUP' = '0'
        'PATH'                     = "$bundledDir;" + '${env:PATH}'
    }
    Write-Host "Pinning tasks to bundled dotnet $bundledVersion at $bundledDir" -ForegroundColor DarkGray

    function Set-Prop {
        param($Obj, [string] $Name, $Value)
        if ($Obj.PSObject.Properties.Name -contains $Name) { $Obj.$Name = $Value }
        else { $Obj | Add-Member -NotePropertyName $Name -NotePropertyValue $Value -Force }
    }

    function Set-TaskDotnetEnv {
        # Add the bundled-dotnet env to every task's options.env.
        param($TasksContainer)
        if (-not $TasksContainer -or -not $TasksContainer.tasks) { return }
        foreach ($task in $TasksContainer.tasks) {
            if (-not ($task.PSObject.Properties.Name -contains 'options') -or -not $task.options) {
                Set-Prop $task 'options' ([pscustomobject]@{})
            }
            if (-not ($task.options.PSObject.Properties.Name -contains 'env') -or -not $task.options.env) {
                Set-Prop $task.options 'env' ([pscustomobject]@{})
            }
            foreach ($kv in $bundledEnv.GetEnumerator()) {
                Set-Prop $task.options.env $kv.Key $kv.Value
            }
        }
    }

    function Repair-TaskFile {
        # $Nested: workspace files hold tasks under a top-level "tasks" object;
        # a .vscode\tasks.json IS that object.
        param([string] $File, [switch] $Nested)
        if (-not (Test-Path -LiteralPath $File)) { return }
        $json = Get-Content -Raw -LiteralPath $File | ConvertFrom-Json
        Set-TaskDotnetEnv ($(if ($Nested) { $json.tasks } else { $json }))
        ($json | ConvertTo-Json -Depth 100) | Set-Content -LiteralPath $File -Encoding UTF8
        Write-Host "Pinned dotnet env in: $File" -ForegroundColor DarkGray
    }

    $wsFile = Get-ChildItem -LiteralPath $ProjectDir -Filter '*.code-workspace' -File | Select-Object -First 1
    if ($wsFile) { Repair-TaskFile -File $wsFile.FullName -Nested }
    # Also cover opening the project folder directly (tasks then live in .vscode\tasks.json).
    Repair-TaskFile -File (Join-Path $ProjectDir '.vscode\tasks.json')
}

# --- Optionally open ---------------------------------------------------------
$workspace = Join-Path $ProjectDir ("{0}.code-workspace" -f $uproject.BaseName)
if (-not (Test-Path -LiteralPath $workspace)) {
    # UBT sometimes names the workspace after the folder rather than the .uproject.
    $workspace = Get-ChildItem -LiteralPath $ProjectDir -Filter '*.code-workspace' -File | Select-Object -First 1 | ForEach-Object FullName
}

if ($VisualStudio) {
    $sln = Get-ChildItem -LiteralPath $ProjectDir -Filter '*.sln' -File | Select-Object -First 1
    if ($sln) { Write-Host "Solution: $($sln.FullName)" -ForegroundColor Green }
} elseif ($workspace) {
    Write-Host "Workspace: $workspace" -ForegroundColor Green
    if ($Open) {
        Write-Host "Opening in VS Code..." -ForegroundColor Cyan
        & code $workspace
    } else {
        Write-Host "Open it with:  code `"$workspace`"" -ForegroundColor Cyan
    }
}
