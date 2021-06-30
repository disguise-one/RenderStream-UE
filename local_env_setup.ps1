# sets up the local enviroment and calls the package_plugin sctipt 


if (-not(Test-Path ".\local_env.json")){
    write-host "local_env.json does not exist, creating one please fill out the required paths and re-run the script"

    $default = '{
        "unreal_engine_directory" : "",
        "plugin_folder" : ""
    }'

    New-Item ".\local_env.json" -ItemType "file" -value $default 

    Exit -1

} 

write-host "found local_env.json"
write-host ""

$env = Get-Content ".\local_env.json" | ConvertFrom-Json

$unreal_path = $env.unreal_engine_directory
$plugin_folder = $env.plugin_folder


write-host "unreal path: $unreal_path"
write-host "project root: $plugin_folder"

$command = ".\package_plugin.ps1 -unreal_engine_path '$unreal_path' -plugin_folder '$plugin_folder'"

Write-Host $command
Invoke-Expression $command





