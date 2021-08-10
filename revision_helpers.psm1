$HgTags = 'tip', 'qtip', 'qbase'

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

function Get-HgOutput {
    param ([string[]]$ExePath, [string[]]$ArgsList, [string]$Pattern)
    $Output = & $ExePath $ArgsList
    foreach ($Line in $Output) {
        if ($Line -match $Pattern) {
            return $Matches[1]
        }
    }
    return $null
}

function Remove-SingleTag {
    param([string]$InTag, [string]$RemoveTag)
    if ($InTag -eq $RemoveTag) {
        return ''
    }

    $Pattern = '(?:(.*)(?: {0} )(.*))|(?:(?:^{1} )(.*))|(?:(?:(.*) {2}$))' -f $RemoveTag, $RemoveTag, $RemoveTag
    if ($InTag -match $Pattern) {
        $Remaining = ''
        foreach ($Key in $Matches.Keys) {
            if ($Key -ne 0) {
                $Remaining += $Matches[$Key] + ' '
            }
        }
        return $Remaining.Trim()
    }
    return $InTag
}

function Remove-HgTags {
    param ([string]$InTag)
    $SanitizedTag = $InTag
    foreach ($Tag in $HgTags) {
        $SanitizedTag = Remove-SingleTag -InTag $SanitizedTag -RemoveTag $Tag
    }
    return $SanitizedTag.Trim()
}

function Find-TagInfo {
    param ([string]$ExePath, [int]$RevisionCount, [string]$Revision)
    $TagOutput = & $ExePath 'log', '-r', ('limit(reverse(ancestors({0})),{1})' -f $Revision, $RevisionCount), '--template', '{tags}|'
    $TagOutput = $TagOutput.Split([System.Environment]::NewLine) | Select-Object -Last 1

    foreach ($Tag in $TagOutput.Split('|')) {
        $SanitizedTag = Remove-HgTags -InTag $Tag
        if ($SanitizedTag) {
            Write-Information -MessageData ('Found Tags: {0}' -f $SanitizedTag) -InformationAction Continue
            return $SanitizedTag
        }
    }
    return ''
}

function Find-RecentTagInfo {
    param ([string]$ExePath, [string[]]$Revision)
    return Find-TagInfo -ExePath $ExePath -RevisionCount 2 -Revision $Revision
}

function Find-CustomReleaseName {
    param([string]$Tag)
    if ($Tag -match '(?:special|custom)\s*(?:release|build|installer)\s*(?:(?:(?:released\s+)?to|for)\s*)?([^.]*)') {
        return $Matches[1]
    }
    return ''
}

function Find-Phases {
    param([string]$ExePath, [string]$Revision, [int]$RevisionCount, [string[]]$SearchPhases)
    $Output = & $ExePath 'log', '-r', ('limit(reverse(ancestors({0})),{1})' -f $Revision, $RevisionCount), '--template', '{phase},'
    $PhaseList = $Output.Split(',')
    $Found = 0
    foreach ($Phase in $PhaseList) {
        if ($SearchPhases -contains $Phase) {
            $Found += 1
        }
    }
    return $Found
}
function Find-LocalPatches {
    param([string]$ExePath, [string]$Revision, [bool]$CleanWorkingCopy)
    $LocalPatches = $false
    $AppliedPatches = Get-HgOutput -ExePath $ExePath -ArgsList 'summary' -Pattern 'mq: *([0-9]+ applied)'
    if ($null -ne $AppliedPatches) {
        Write-Information -MessageData ('Found Applied Patches: {0}! Setting LocalPatches flag!' -f $AppliedPatches) -InformationAction Continue
        $LocalPatches = $true
    }
    $FoundDrafts = Find-Phases -ExePath $ExePath -Revision $Revision -RevisionCount 100 -SearchPhases 'draft'
    if ($FoundDrafts) {
        Write-Information -MessageData ('Found {0} un-pushed, committed patches! Setting LocalPatches flag!' -f $FoundDrafts) -InformationAction Continue
        $LocalPatches = $true
    }
    if (-Not $CleanWorkingCopy) {
        Write-Information -MessageData 'Found unclean working copy! Setting LocalPatches flag!' -InformationAction Continue
        $LocalPatches = $true
    }
    return $LocalPatches
}

function Get-HgResults {
    param ([string]$HgExe)
    $Branch = Get-HgOutput -ExePath $HgExe -ArgsList 'branch' -Pattern '(.*)'
    $Revision = Get-HgOutput -ExePath $HgExe -ArgsList 'summary' -Pattern 'parent: *([0-9]*):.*'
    $RevisionHash = Get-HgOutput -ExePath $HgExe -ArgsList 'summary' -Pattern 'parent:.*: *([^ ]*)'
    $CleanWorkingCopy = $null -eq (Get-HgOutput -ExePath $HgExe -ArgsList 'diff', '--stat' -Pattern '(.*)')
    $MostRecentTag = Find-RecentTagInfo -ExePath $HgExe -Revision $Revision
    $LocalPatches = Find-LocalPatches -ExePath $HgExe -Revision $Revision -CleanWorkingCopy $CleanWorkingCopy
    $IsReleaseCandidate = $MostRecentTag -imatch 'release candidate'
    $IsReleaseVersion = $MostRecentTag -imatch 'Gold Release'
    $CustomReleaseName = Find-CustomReleaseName -Tag $MostRecentTag
    return [hashtable][ordered]@{ Branch = $Branch; 
                        Revision = $Revision; 
                        RevisionHash = $RevisionHash; 
                        CleanWorkingCopy = $CleanWorkingCopy;
                        MostRecentTag = $MostRecentTag;
                        LocalPatches = $LocalPatches;
                        IsReleaseCandidate = $IsReleaseCandidate;
                        IsReleaseVersion = $IsReleaseVersion;
                        CustomReleaseName = $CustomReleaseName 
                    }
}
