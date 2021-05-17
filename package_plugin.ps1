& .\generate_uplugin.ps1

[hashtable]$DefaultEngineDirectories = @{
    "4.24.([0-9])" = "C:\Program Files\Epic Games\UE_4.24";
    "4.25.([0-9])" = "C:\Program Files\Epic Games\UE_4.25";
    "4.26.([0-9])" = "D:\Epic\UE\UE_4.26"
}

$PluginName = "RenderStream-UE"
$UPluginName = ("{0}.uplugin" -f $PluginName)

$Uplugin = Get-Content -Raw -Path $UPluginName | ConvertFrom-Json

$EnginePath = ''
if ($args.count -gt 0) {
    $EnginePath = $args[0]    
} else {
    $EngineVersion = $Uplugin.EngineVersion
    foreach ($Version in $DefaultEngineDirectories.Keys) {
        if ($EngineVersion -match $Version) {
            $EnginePath = $DefaultEngineDirectories[$Version]
        }
    }

    if (-Not $EnginePath) {
        Write-Error "Default Engine Directories must be updated for new Unreal Version"
        exit 1
    }
}

Write-Information -MessageData ('Packging with engine at {0}' -f $EnginePath) -InformationAction Continue
$CurrentDirectory = Get-Location

$ProjectRoot = $(Get-Item .).parent.parent.parent.FullName # Up from current project plugins folder to the sibling of the current project. Need to go up because path length limitations.
$BuildTool = Join-Path -Path $EnginePath -ChildPath 'Engine\Build\BatchFiles\RunUAT.bat'
$PluginPath = Join-Path -Path $CurrentDirectory -ChildPath $UPluginName
$BuildPath = $ProjectRoot
$OutPath = Join-Path -Path $BuildPath -ChildPath $PluginName

# Remove the project-local intermediate directory, if it exists - this can interfere with a clean build of the plugin.
$LocalIntermediate = (Join-Path -Path $CurrentDirectory -ChildPath "Intermediate")
if (Test-Path $LocalIntermediate) {
	Remove-Item -Path $LocalIntermediate -Recurse
}

# Remove the path the build tool creates to package in - this can interfere with a clean build of the plugin, too.
if (Test-Path $OutPath) {
    Remove-Item -Path $OutPath -Recurse
}

& $BuildTool 'BuildPlugin', ('-Plugin={0}' -f $PluginPath), ('-Package={0}' -f $OutPath), '-CreateSubFolder', '-VS2019'

if ($LASTEXITCODE -eq 0) {
    Write-Information -MessageData 'Packaging complete. Zipping build contents.' -InformationAction Continue
	
	# Remove the intermediate results from the packaging process - not useful for distribution
	Remove-Item -Path (Join-Path -Path $OutPath -Child "Intermediate") -Recurse

	Remove-Item -Path (Join-Path -Path $OutPath -Child "Content/Cache") -Recurse

    $Compress = @{
        Path = $OutPath
        CompressionLevel = 'Optimal'
        DestinationPath = (Join-Path -Path $BuildPath -ChildPath ('{0}_{1}.zip' -f $PluginName, $Uplugin.VersionName))
    }
    
    Compress-Archive @Compress
}
