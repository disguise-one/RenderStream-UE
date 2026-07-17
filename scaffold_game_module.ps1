[CmdletBinding()]
param(
    # Project root (folder containing the .uproject) to scaffold a C++ game module into.
    [Parameter(Mandatory = $true)]
    [string] $ProjectDir,

    # Module/target name. Defaults to a sanitized form of the .uproject base name.
    [string] $ModuleName,

    # Extra module dependencies to add to the Build.cs (e.g. RenderStream). Empty by default
    # so existing callers get the same minimal module they always did.
    [string[]] $ExtraModules = @()
)

$ErrorActionPreference = 'Stop'

# --- Resolve the project and its .uproject -----------------------------------
if (-not (Test-Path -LiteralPath $ProjectDir)) {
    throw "Project directory not found: $ProjectDir"
}
$ProjectDir = (Resolve-Path -LiteralPath $ProjectDir).Path

$uproject = Get-ChildItem -LiteralPath $ProjectDir -Filter '*.uproject' -File | Select-Object -First 1
if (-not $uproject) {
    throw "No .uproject found in '$ProjectDir' - is that the project root?"
}

# --- Derive a valid module name ----------------------------------------------
if (-not $ModuleName) {
    # Strip anything that isn't a C++ identifier char, then ensure it can't start with a digit.
    $ModuleName = ($uproject.BaseName -replace '[^A-Za-z0-9_]', '')
    if ($ModuleName -match '^\d') { $ModuleName = "M$ModuleName" }
    if (-not $ModuleName)         { $ModuleName = 'Game' }
}

$sourceDir = Join-Path $ProjectDir 'Source'

# Skip only if a build target already exists - an empty or targetless Source\
# folder still needs a module (UBT's VSCode generator crashes on zero targets).
$existingTarget = if (Test-Path -LiteralPath $sourceDir) {
    Get-ChildItem -LiteralPath $sourceDir -Recurse -Filter '*.Target.cs' -File -ErrorAction SilentlyContinue |
        Select-Object -First 1
}
if ($existingTarget) {
    Write-Host "A C++ build target already exists ($($existingTarget.Name)) - not scaffolding." -ForegroundColor DarkGray
    return
}

Write-Host "Scaffolding minimal C++ game module '$ModuleName' into: $sourceDir" -ForegroundColor Cyan

$moduleDir = Join-Path $sourceDir $ModuleName
New-Item -ItemType Directory -Path $moduleDir -Force | Out-Null

# --- Target / module rules + a stub module implementation --------------------
# DefaultBuildSettings = Latest is the only version-agnostic value: it exists in
# every UE4/UE5 engine (unlike V5, which is UE5.4+), so the stub compiles against
# whatever engine the project targets. The editor rewrites these if the user
# later adds real C++ from the editor.
$targetCs = @"
using UnrealBuildTool;

public class ${ModuleName}Target : TargetRules
{
    public ${ModuleName}Target(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        ExtraModuleNames.Add("$ModuleName");
    }
}
"@

$editorTargetCs = @"
using UnrealBuildTool;

public class ${ModuleName}EditorTarget : TargetRules
{
    public ${ModuleName}EditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        ExtraModuleNames.Add("$ModuleName");
    }
}
"@

# Base modules every game module needs, plus any extras the caller requested.
$moduleDeps = @('Core', 'CoreUObject', 'Engine', 'InputCore') + $ExtraModules | Select-Object -Unique
$moduleDepList = ($moduleDeps | ForEach-Object { "`"$_`"" }) -join ', '

$buildCs = @"
using UnrealBuildTool;

public class $ModuleName : ModuleRules
{
    public $ModuleName(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { $moduleDepList });
    }
}
"@

$moduleH = @"
#pragma once

#include "CoreMinimal.h"
"@

$moduleCpp = @"
#include "$ModuleName.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, $ModuleName, "$ModuleName");
"@

# Write UTF-8 without BOM - UBT's C# parsers dislike a leading BOM in .cs files.
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
function Write-TextFile([string] $Path, [string] $Content) {
    [System.IO.File]::WriteAllText($Path, $Content, $utf8NoBom)
    Write-Host "  wrote $Path" -ForegroundColor DarkGray
}

Write-TextFile (Join-Path $sourceDir "$ModuleName.Target.cs")        $targetCs
Write-TextFile (Join-Path $sourceDir "${ModuleName}Editor.Target.cs") $editorTargetCs
Write-TextFile (Join-Path $moduleDir "$ModuleName.Build.cs")          $buildCs
Write-TextFile (Join-Path $moduleDir "$ModuleName.h")                 $moduleH
Write-TextFile (Join-Path $moduleDir "$ModuleName.cpp")               $moduleCpp

# --- Register the module in the .uproject ------------------------------------
$json = Get-Content -Raw -LiteralPath $uproject.FullName | ConvertFrom-Json
if ($json.PSObject.Properties.Name -contains 'Modules' -and $json.Modules) {
    Write-Host "  .uproject already declares Modules - leaving it unchanged." -ForegroundColor DarkGray
} else {
    $moduleEntry = [pscustomobject]@{
        Name         = $ModuleName
        Type         = 'Runtime'
        LoadingPhase = 'Default'
    }
    if ($json.PSObject.Properties.Name -contains 'Modules') {
        $json.Modules = @($moduleEntry)
    } else {
        $json | Add-Member -NotePropertyName 'Modules' -NotePropertyValue @($moduleEntry)
    }
    Write-TextFile $uproject.FullName ($json | ConvertTo-Json -Depth 20)
    Write-Host "  registered module '$ModuleName' in $($uproject.Name)" -ForegroundColor DarkGray
}

Write-Host "Game module scaffolded. The project is now a C++ code project." -ForegroundColor Green
