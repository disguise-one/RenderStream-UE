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

## Development setup

To work on the plugin against a real Unreal project, use the dev setup scripts. From a fresh checkout:

```
setup_dev_environment.bat -ProjectDir "D:\path\to\UEProject" [options]
```

This runs three steps in order:
1. `generate_uplugin.ps1` — builds the `.uplugin` from `uplugin_template.json`.
2. `link_to_project.ps1` — junction-links this repo into the project's `Plugins` folder, so edits and `git status` stay live in place.
3. `generate_project_files.ps1` — generates VS Code (or `-VisualStudio`) project files via UnrealBuildTool. Blueprint-only projects (no `Source` folder) are auto-scaffolded with a minimal C++ game module first.

Options (forwarded to the relevant step):

| Option | Effect |
| --- | --- |
| `-PluginName <name>` | Name of the plugin folder created under `Plugins` (defaults to the repo folder name). |
| `-NoBackup` | Delete an existing real plugin folder instead of backing it up. |
| `-VisualStudio` | Generate a Visual Studio `.sln` instead of VS Code files. |
| `-IncludeEngine` | Include full engine source in the workspace (heavier, better for engine debugging). |
| `-Open` | Open the generated workspace when done. |
| `-SkipUplugin` | Skip regenerating the `.uplugin`. |

The individual steps can also be run on their own:
* `link_to_project.bat "D:\path\to\UEProject"` — link only; `link_to_project.bat -Unlink` removes the last-created junction.
* `generate_project_files.bat` — regenerate project files for the last-linked project (or pass `-ProjectDir`).

Notes:

![alt text](https://download.disguise.one/media/6066/d3-renderstream-unreal.png)

This project provides RenderStream input from Unreal Engine to [disguise designer](https://www.disguise.one/en/products/designer/).

For the plugin setup process - please visit the [RenderStream and Unreal Engine](https://help.disguise.one/workflows/renderstream/unreal-engine/renderstream-unreal) page for more details.

A **Demo Unreal Project** can be found on the [disguise Resources page](https://download.disguise.one/#resources)

_Please note that from version 1.26 onwards, the plugin has been renamed from "disguiseuerenderstream" to "RenderStream-UE". Existing Unreal projects will need to be updated to reflect this._
