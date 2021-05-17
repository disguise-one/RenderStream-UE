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
$GitTagsClean = ""
$GitTagsUnclean = ""
$GitTagsIsClean = $true

try {
    $GitBranch = & $GitExe rev-parse --abbrev-ref HEAD

    if ($GitBranch -eq "HEAD") {
        # we're probably in detached HEAD state, so print the SHA
        $GitBranch = & $GitExe rev-parse --short HEAD
    }

    $GitTagNoChange= & $GitExe describe --tags --abbrev=0 HEAD
    $GitTagShowChange = & $GitExe describe --tags HEAD
    $GitTagsNotClean = $GitTagNoChange -ne $GitTagShowChange
} catch {
    $GitBranch = "(git error)"
}

$GitUncommitted = $false
if (& $GitExe status --porcelain |Where {$_ -notmatch '^\?\?'}) {
    $GitUncommitted = $true
}

$Uplugin = Get-Content -Raw -Path 'uplugin_template.json' | ConvertFrom-Json

$Uplugin.IsExperimentalVersion = $true
$Uplugin.IsBetaVersion = $false
$Uplugin.CanContainContent = $true

$FriendlyName = $Uplugin.FriendlyName
$NewVersionName = $GitBranch + '_' + $GitTagShowChange

if ($GitUncommitted -or $GitTagsNotClean) {
    if ($GitUncommitted) {
        $NewVersionName += " (Uncommited Changes)"
    } else {
        $NewVersionName += " (Patches Applied)"
    }
    $FriendlyName += " (DEV)"
} else {
    if ($GitTagNoChange -match 'Gold Release') {
        $Uplugin.IsExperimentalVersion = $false
    } else {

        # Release candidate is beta but not experimental
        if ($GitTagNoChange -match 'release candidate') {
            $Uplugin.IsExperimentalVersion = $false
            $Uplugin.IsBetaVersion = $true
        }

        # custom release may contain beta features but is typically experimental
        if (Find-CustomReleaseName -Tag $GitTagNoChange) {
            $Uplugin.IsBetaVersion = $true
        }
    }
}

$Uplugin.FriendlyName = $FriendlyName
$Uplugin.VersionName = $NewVersionName

Write-Information -MessageData ".uplugin file generated:" -InformationAction Continue
Write-Information -MessageData (Out-String -InputObject $Uplugin) -InformationAction Continue
$Uplugin | ConvertTo-Json -Depth 10 | Out-File "RenderStream-UE.uplugin"