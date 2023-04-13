// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RenderStreamUncooked : ModuleRules
{
	public RenderStreamUncooked(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateIncludePaths.AddRange(new string[] {"RenderStreamUncooked/Private"});
        PublicDependencyModuleNames.AddRange(new string[] { 
			"Core", 
            "InputCore"
        });
        PrivateDependencyModuleNames.AddRange (new string[] { 
			"CoreUObject", 
            "Engine", 
			"RenderStream",
			"BlueprintGraph",
            "AnimGraph"
        });
    }
}
