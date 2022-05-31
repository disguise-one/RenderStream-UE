// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RenderStreamEditor : ModuleRules
{
	public RenderStreamEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateIncludePaths.AddRange(new string[] {"RenderStreamEditor/Private"});
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "InputCore" });
        PrivateDependencyModuleNames.AddRange (new string[] { "CoreUObject", "Engine", "EngineSettings", "UnrealEd", "RenderStream", "DisplayCluster", "Slate", "SlateCore", "EditorStyle", "PropertyEditor", "SourceControl", "LevelEditor" });
    }
}
