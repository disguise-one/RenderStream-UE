using UnrealBuildTool;

public class RenderStreamTests : ModuleRules
{
    public RenderStreamTests(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] { "Core" });

        PrivateDependencyModuleNames.AddRange(new string[] { "CoreUObject", "Engine", "RenderStream" });
    }
}
