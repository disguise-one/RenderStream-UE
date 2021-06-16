param(
    [Parameter(Mandatory=$true)][string]$unreal_engine_path, # path to the Unreal engine
    [string]$project_root = $null, # Optional project root path defualt is set to relative to current directory 
    [string[]] [Parameter (Position = 2, ValueFromRemainingArguments )] $RemainingArgs
)
$PsBoundParameters | Format-Table

if ($project_root -eq ""){
    $project_root = Get-Location
    write-host "project too set to $project_root"
}

$build_tool =  "$unreal_engine_path\Engine\Build\BatchFiles\RunUAT.bat"
$plugin_path =  "$project_root\disguiseuerenderstream.uplugin"
$out_path = "$project_root\disguiseuerenderstream"
$local_intermediate = "$project_root\Intermediate"


write-host "current paths"
Get-Variable -Scope script

# Remove the project-local intermediate directory, if it exists - this can interfere with a clean build of the plugin.
if (Test-Path $local_intermediate) {
	Remove-Item -Path $local_intermediate -Recurse
}

# Remove the path the build tool creates to package in - this can interfere with a clean build of the plugin, too.
if (Test-Path $out_path) {
    Remove-Item -Path $out_path -Recurse
}

$Uplugin = Get-Content $plugin_path | ConvertFrom-Json

Write-Host "Packaging Unreal plugin"
Write-Host "$build_tool BuildPlugin -Plugin=$plugin_path -Package=$out_path -CreateSubFolder -VS2019" 
Invoke-Expression "$build_tool BuildPlugin -Plugin=$plugin_path -Package=$out_path -CreateSubFolder -VS2019" -ErrorAction Stop


if ($LASTEXITCODE -eq 0) {
    Write-Information -MessageData 'Packaging complete. Zipping build contents.' -InformationAction Continue
	
	# Remove the intermediate results from the packaging process - not useful for distribution
	Remove-Item -Path (Join-Path -Path $out_path -Child "Intermediate") -Recurse

    $Compress = @{
        Path = $out_path
        CompressionLevel = 'Optimal'
        DestinationPath = (Join-Path -Path $out_path -ChildPath ('disguiseuerenderstream_{0}.zip' -f $Uplugin.VersionName))
    }
    
    Compress-Archive @Compress
}
