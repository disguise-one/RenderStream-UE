[CmdletBinding()]
param(
    # Project root (folder containing the .uproject) to scaffold the editor bake module into.
    [Parameter(Mandatory = $true)]
    [string] $ProjectDir,

    # Runtime game module name. The bake module is named "<GameModule>Bake".
    # Defaults to a sanitized form of the .uproject base name.
    [string] $GameModule
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $ProjectDir)) {
    throw "Project directory not found: $ProjectDir"
}
$ProjectDir = (Resolve-Path -LiteralPath $ProjectDir).Path

$uproject = Get-ChildItem -LiteralPath $ProjectDir -Filter '*.uproject' -File | Select-Object -First 1
if (-not $uproject) {
    throw "No .uproject found in '$ProjectDir' - is that the project root?"
}

if (-not $GameModule) {
    $GameModule = ($uproject.BaseName -replace '[^A-Za-z0-9_]', '')
    if ($GameModule -match '^\d') { $GameModule = "M$GameModule" }
    if (-not $GameModule)         { $GameModule = 'Game' }
}
$BakeModule = "${GameModule}Bake"

$sourceDir = Join-Path $ProjectDir 'Source'
$moduleDir = Join-Path $sourceDir $BakeModule
$privateDir = Join-Path $moduleDir 'Private'

# Skip if the bake module's Build.cs already exists - keeps the script idempotent.
if (Test-Path -LiteralPath (Join-Path $moduleDir "$BakeModule.Build.cs")) {
    Write-Host "Bake module '$BakeModule' already exists - not scaffolding." -ForegroundColor DarkGray
    return
}

Write-Host "Scaffolding editor bake module '$BakeModule' into: $sourceDir" -ForegroundColor Cyan
New-Item -ItemType Directory -Path $privateDir -Force | Out-Null

# --- Source templates: the module's .cs/.cpp/.h live as real files under
# templates/bake_module so the IDE parses them as C++/C#. Tokens are substituted here.
$templatesDir = Join-Path $PSScriptRoot 'templates\bake_module'
function Read-Template([string] $Name, [hashtable] $Tokens = @{}) {
    $text = [System.IO.File]::ReadAllText((Join-Path $templatesDir $Name))
    foreach ($key in $Tokens.Keys) { $text = $text.Replace($key, $Tokens[$key]) }
    return $text
}

$buildCs       = Read-Template 'BakeModule.Build.cs'                @{ '__BAKE_MODULE__' = $BakeModule }
$moduleCpp     = Read-Template 'BakeModule.cpp'                     @{ '__BAKE_MODULE__' = $BakeModule }
$commandletH   = Read-Template 'RenderStreamTestBakeCommandlet.h'
$commandletCpp = Read-Template 'RenderStreamTestBakeCommandlet.cpp' @{ '__GAME_MODULE__' = $GameModule }

# Write UTF-8 without BOM (UBT's C#/C++ parsers dislike a leading BOM).
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
function Write-TextFile([string] $Path, [string] $Content) {
    [System.IO.File]::WriteAllText($Path, $Content, $utf8NoBom)
    Write-Host "  wrote $Path" -ForegroundColor DarkGray
}

Write-TextFile (Join-Path $moduleDir "$BakeModule.Build.cs")                     $buildCs
Write-TextFile (Join-Path $privateDir "$BakeModule.cpp")                          $moduleCpp
Write-TextFile (Join-Path $privateDir 'RenderStreamTestBakeCommandlet.h')         $commandletH
Write-TextFile (Join-Path $privateDir 'RenderStreamTestBakeCommandlet.cpp')       $commandletCpp

# --- Register the editor module in the .uproject ------------------------------
$json = Get-Content -Raw -LiteralPath $uproject.FullName | ConvertFrom-Json
$modules = @($json.Modules)
if ($modules.Name -contains $BakeModule) {
    Write-Host "  .uproject already declares module '$BakeModule'." -ForegroundColor DarkGray
} else {
    $modules += [pscustomobject]@{
        Name         = $BakeModule
        Type         = 'Editor'
        LoadingPhase = 'Default'
    }
    $json.Modules = $modules
    Write-TextFile $uproject.FullName ($json | ConvertTo-Json -Depth 20)
    Write-Host "  registered editor module '$BakeModule' in $($uproject.Name)" -ForegroundColor DarkGray
}

Write-Host "Bake module scaffolded." -ForegroundColor Green
