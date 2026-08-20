using UnrealBuildTool;

public class __BAKE_MODULE__ : ModuleRules
{
    public __BAKE_MODULE__(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "UnrealEd",          // NewBlankMap / SaveMap / FBlueprintEditorUtils / UEditorLevelUtils
            "BlueprintGraph",    // UEdGraphSchema_K2 pin categories / UK2Node_CustomEvent
            "CinematicCamera",   // ACineCameraActor / UCineCameraComponent
            "RenderStream",       // URenderStreamChannelDefinition
            "RenderStreamEditor", // FRenderStreamEditorModule::GenerateAssetMetadata (schema bake)
            "AssetRegistry",      // FAssetRegistryModule::AssetCreated for baked assets
            "MaterialEditor",     // UMaterialEditingLibrary for the face material
        });
    }
}