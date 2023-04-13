function Find-InEnvPath {
    param ([string]$Exe)
    $Vars = $Env:Path.Split(';')
    foreach ($Var in $Vars) {
        if ($Var) {
            $FilePath = Join-Path -Path $Var -ChildPath $Exe
            if ( Test-Path $FilePath) {
                return $FilePath
            }
        }
    }
}

function Find-CustomReleaseName {
    param([string]$Tag)
    if ($Tag -match '(?:special|custom)\s*(?:release|build|installer)\s*(?:(?:(?:released\s+)?to|for)\s*)?([^.]*)') {
        return $Matches[1]
    }
    return ''
}

$GitExe = Find-InEnvPath -Exe 'git.exe'

if ($null -eq $GitExe) {
    Write-Error "Could not find git.exe in Path Variable. Exiting..."
    exit 1
}

$GitBranch = "(git error)"
$GitTag = ""
try {
    $GitBranch = & $GitExe rev-parse --abbrev-ref HEAD

    if ($GitBranch -eq "HEAD") {
        # we're probably in detached HEAD state, so print the SHA
        $GitBranch = & $GitExe rev-parse --short HEAD
    }

    $GitTagNoChange= & $GitExe describe --tags --abbrev=0 HEAD
    $GitTagShowChange = & $GitExe describe --tags HEAD
    if ($GitTagNoChange -eq $GitTagShowChange) {
        $GitTag = $GitTagNoChange
    }
} catch {
    $GitBranch = "(git error)"
}

$GitAheadOfRemote = $false
$GitUncommitted = $false
# include branch local/remote status with -b
$GitStatus = & $GitExe status -b --porcelain

# search status string for a branch status with "ahead #" in it (where # is any number)
if ($GitStatus -match '^## (\S+) \[ahead (\d+)(, behind (\d+))?\]') {
    $GitAheadOfRemote = $true
}

# branch message starts with ## anything else is some entry in the working tree and therefore uncommitted
if ($GitStatus -notmatch '^##') {
    $GitUncommitted = $true
}

$Uplugin = Get-Content -Raw -Path 'uplugin_template.json' | ConvertFrom-Json

$Uplugin.IsExperimentalVersion = $true
$Uplugin.IsBetaVersion = $false
$Uplugin.CanContainContent = $true

$FriendlyName = $Uplugin.FriendlyName
$NewVersionName = $GitBranch
if ($GitTag) {
    # if we have a tag there's no need to include the branch name as the tag identifies the build accurately enough
    $NewVersionName = $GitTag
}

if ($GitUncommitted -or $GitAheadOfRemote) {
    if ($GitUncommitted) {
        $NewVersionName += " (Uncommited Changes)"
    } else {
        $NewVersionName += " (Patches Applied)"
    }
    $FriendlyName += " (DEV)"
} else {
    if ($GitTag -match 'Gold Release') {
        $Uplugin.IsExperimentalVersion = $false
    } else {

        # Release candidate is beta but not experimental
        if ($GitTag -match 'release candidate') {
            $Uplugin.IsExperimentalVersion = $false
            $Uplugin.IsBetaVersion = $true
        }

        # custom release may contain beta features but is typically experimental
        if (Find-CustomReleaseName -Tag $GitTag) {
            $Uplugin.IsBetaVersion = $true
        }
    }
}

$Uplugin.FriendlyName = $FriendlyName
$Uplugin.VersionName = $NewVersionName

$PluginFileName = "RenderStream-UE.uplugin"

Write-Information -MessageData ('{1}{0} file generated: {1}  FriendlyName: {2} {1}  VersionName: {3}{1}' -f $PluginFileName, [Environment]::NewLine, $Uplugin.FriendlyName, $Uplugin.VersionName) -InformationAction Continue

$Uplugin | ConvertTo-Json -Depth 10 | Out-File $PluginFileName