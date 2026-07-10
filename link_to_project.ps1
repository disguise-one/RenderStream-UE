[CmdletBinding()]
param(
    [string] $ProjectDir,

    [string] $RepoDir,

    [string] $PluginName,

    [switch] $Unlink,

    # Delete the existing real plugin folder instead of backing it up.
    [switch] $NoBackup
)

$ErrorActionPreference = 'Stop'

if (-not $RepoDir) 
{
    $RepoDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
}
$RepoDir = (Resolve-Path -LiteralPath $RepoDir).Path

# Remember the last-linked junction path here, so -Unlink works without re-typing the project
$stateFile = Join-Path $RepoDir '.link_to_project.last'

function Test-IsLink([string] $path) {
    if (-not (Test-Path -LiteralPath $path)) { return $false }
    return [bool]((Get-Item -LiteralPath $path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)
}

# --- Determine the junction path ---------------------------------------------
$linkPath = $null
if ($ProjectDir) 
{
    if (-not (Test-Path -LiteralPath $ProjectDir)) 
    {
        throw "Project directory not found: $ProjectDir"
    }
    $ProjectDir = (Resolve-Path -LiteralPath $ProjectDir).Path

    if (-not (Get-ChildItem -LiteralPath $ProjectDir -Filter '*.uproject' -File)) 
    {
        throw "No .uproject found in '$ProjectDir' - is that the project root?"
    }

    if (-not $PluginName) 
    {
        $PluginName = Split-Path -Leaf $RepoDir
    }

    $pluginsDir = Join-Path $ProjectDir 'Plugins'
    $linkPath   = Join-Path $pluginsDir $PluginName
}
elseif ($Unlink) 
{
    # No project given - fall back to the last-linked path we saved.
    if (Test-Path -LiteralPath $stateFile) 
    {
        $linkPath = (Get-Content -LiteralPath $stateFile -Raw).Trim()
    }
    if (-not $linkPath) 
    {
        throw "No -ProjectDir given and no saved link found. Pass -ProjectDir to specify which project to unlink."
    }
}
else 
{
    throw "-ProjectDir is required when linking."
}

# --- Unlink mode -------------------------------------------------------------
if ($Unlink) {
    if (Test-IsLink $linkPath) {
        # Remove the junction only - Remove-Item on a reparse point does not touch the target.
        (Get-Item -LiteralPath $linkPath -Force).Delete()
        Write-Host "Removed junction: $linkPath" -ForegroundColor Green
    } elseif (Test-Path -LiteralPath $linkPath) {
        Write-Warning "'$linkPath' exists but is NOT a junction - leaving it alone."
    } else {
        Write-Host "Nothing to remove at: $linkPath"
    }
    if (Test-Path -LiteralPath $stateFile) { Remove-Item -LiteralPath $stateFile -Force }
    return
}

# --- Link mode ---------------------------------------------------------------
if (-not (Test-Path -LiteralPath $pluginsDir)) {
    New-Item -ItemType Directory -Path $pluginsDir | Out-Null
}

if (Test-IsLink $linkPath) {
    $existingTarget = (Get-Item -LiteralPath $linkPath -Force).Target
    if ($existingTarget -and ((Resolve-Path -LiteralPath $existingTarget).Path -eq $RepoDir)) {
        Write-Host "Already linked: $linkPath -> $RepoDir" -ForegroundColor Green
        return
    }
    Write-Host "Replacing existing junction (was -> $existingTarget)" -ForegroundColor Yellow
    (Get-Item -LiteralPath $linkPath -Force).Delete()
}
elseif (Test-Path -LiteralPath $linkPath) {
    # A real folder (possibly with uncommitted edits).
    if ($NoBackup) {
        Write-Warning "Deleting existing plugin folder (-NoBackup):`n    $linkPath"
        Remove-Item -LiteralPath $linkPath -Recurse -Force
    }
    else {
        # Back it up OUTSIDE the Plugins tree, backups left inside Plugins would be scanned by UnrealBuildTool and cause duplicate-module build errors
        $stamp     = Get-Date -Format 'yyyyMMdd_HHmmss'
        $backupDir = Join-Path (Split-Path -Parent $ProjectDir) '_plugin_link_backups'
        if (-not (Test-Path -LiteralPath $backupDir)) {
            New-Item -ItemType Directory -Path $backupDir | Out-Null
        }
        $backup = Join-Path $backupDir "$PluginName.bak_$stamp"
        Write-Warning "A real plugin folder already exists at:`n    $linkPath"
        Write-Warning "Moving it to a backup outside the project (pass -NoBackup to skip this):`n    $backup"
        Move-Item -LiteralPath $linkPath -Destination $backup
    }
}

New-Item -ItemType Junction -Path $linkPath -Target $RepoDir | Out-Null

# Save so a later -Unlink needs no arguments
Set-Content -LiteralPath $stateFile -Value $linkPath -NoNewline

Write-Host ""
Write-Host "Linked:  $linkPath" -ForegroundColor Green
Write-Host "     ->  $RepoDir"  -ForegroundColor Green
Write-Host ""
Write-Host "You can now edit/build the plugin in the project; git status will see it live." -ForegroundColor Cyan
