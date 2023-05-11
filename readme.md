# RenderStream Unreal Engine Plugin

Prerequisites: 
1. have Microsoft Visual Studio installed 
2. have git.exe 
3. have corresponding Unreal Engine (UE) version installed 

To Generate Plugin: 

Step 1: 
* run `generate_uplugin.bat`
   * if cmd failed with error message stating "generate_uplugin.ps1 cannot be loaded. The file generate_uplugin.ps1 is not digitally signed."
      * you might need to:
         * edit the bat file to use `"-ExecutionPolicy Bypass"`
         * or edit the LocalMachine's PowerShell execution policy to "bypass".
   * if cmd failed with error message stating "could not find git.exe in Path Variable" 
      * edit Windows Environment Variables -> "PATH" variable to include the location of `git.exe`.
      
Result: 
"RenderStream-UE.uplugin" created.

Step 2: 
* run `package_plugin.bat`
* when prompted, enter unreal_engine_path, for example `"E:\UE_4.27"`
   * Note: you should have the UE version matching the plugin version installed 

Result:
A new folder "Packaged" is created and the packaged plugin is created. 

Notes:

![alt text](https://download.disguise.one/media/6066/d3-renderstream-unreal.png)

This project provides RenderStream input from Unreal Engine to [disguise designer](https://www.disguise.one/en/products/designer/).

For the plugin setup process - please visit the [RenderStream and Unreal Engine](https://help.disguise.one/Content/Configuring/Render-engines/RenderStream-Unreal.htm) page for more details.

A **Demo Unreal Project** can be found on the [disguise Resources page](https://download.disguise.one/#resources)

_Please note that from version 1.26 onwards, the plugin has been renamed from "disguiseuerenderstream" to "RenderStream-UE". Existing Unreal projects will need to be updated to reflect this._
