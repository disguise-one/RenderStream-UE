param(
    [Parameter(Mandatory=$true)][string]$unreal_engine_path, # path to the Unreal engine
    [string]$plugin_folder = $null, # Optional project root path defualt is set to relative to current directory 
    [string[]] [Parameter (Position = 2, ValueFromRemainingArguments )] $RemainingArgs
)
$PsBoundParameters | Format-Table

if ($plugin_folder -eq ""){
    $plugin_folder = Get-Location
    write-host "project too set to $plugin_folder"
}

# Strip quoation marks from the provided UE path (drag and drop from file explorer)
$unreal_engine_path = $unreal_engine_path -replace '"', ""

$plugin_name = "RenderStream-UE"
$build_tool = "$($unreal_engine_path)\Engine\Build\BatchFiles\RunUAT.bat"
$plugin_path = "$($plugin_folder)\$($plugin_name).uplugin"
$zip_path = "$($plugin_folder)\Packaged"
$out_path = "$($plugin_folder)\Packaged\RenderStream-UE"
$local_intermediate = "$($plugin_folder)\Intermediate"

# Ensure spaces in the build_tool path are hard spaces or Invoke-Expression will fail. Important since default location of Unreal is, eg,
# C:/Program Files/Epic Games/UE_4.26
$build_tool = $build_tool -replace ' ','` '

write-host "current paths"
Get-Variable -Scope script

# Remove the project-local intermediate directory, if it exists - this can interfere with a clean build of the plugin.
if (Test-Path $local_intermediate) {
    Remove-Item -Path $local_intermediate -Recurse -Force
}

# Remove the path the build tool creates to package in - this can interfere with a clean build of the plugin, too.
if (Test-Path $zip_path) {
    Remove-Item -Path $zip_path -Recurse -Force
}

$Uplugin = Get-Content $plugin_path | ConvertFrom-Json

$launch_command = "$($build_tool) BuildPlugin -Plugin=""$($plugin_path)"" -Package=""$($out_path)"" -CreateSubFolder -VS2019"
Write-Host "Packaging Unreal plugin"
Write-Host "$launch_command" 

for ($i=1; $i -le 3; $i++) {
    Write-Host "Attempt: $i/3"
    Invoke-Expression -Command "$launch_command" -ErrorAction Stop

    if ($LASTEXITCODE -eq 0) {
        Write-Information -MessageData 'Packaging complete. Zipping build contents.' -InformationAction Continue
        
        # Remove the intermediate results from the packaging process - not useful for distribution
        Remove-Item -Path (Join-Path -Path "$out_path" -ChildPath "Intermediate") -Recurse

        # The version name sometimes contains a slash eg heads/r1.28_UE4.26_r1.28_UE4.26
        # depending on version of git (see generate_uplugin.ps1 which creates the .uplugin file)
        $versionName = $Uplugin.VersionName
        $versionName = $versionName -replace '/', '_'

        $out_file = "$($plugin_name)_{0}.zip" -f $versionName
        $destination = Join-Path -Path "$zip_path" -ChildPath "$out_file"
        $compress = @{
            Path = "$out_path"
            CompressionLevel = 'Optimal'
            DestinationPath = "$destination"
        }
        
        Compress-Archive @compress
        break
    }

    Write-Host "Packaging process failed"
}

if ($LASTEXITCODE -ne 0) {
    if ($LASTEXITCODE -eq $null) {
        # Ensure non-zero value is returned on exit
        exit -1
    }
    # Preserve exit code
    exit $LASTEXITCODE
}