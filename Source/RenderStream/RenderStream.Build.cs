// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using System.Linq;
using System.Security.Cryptography;


public class RenderStream : ModuleRules
{
    public RenderStream(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[] 
            { 
                "Core", 
                "Sockets", 
                "Networking", 
                "MediaIOCore", 
                "MediaUtils", 
                "InputCore", 
                "UMG",
                "ProceduralMeshComponent"
            });
        PrivateDependencyModuleNames.AddRange(
            new string[] 
            { 
                "CoreUObject", 
                "Engine", 
                "Slate", 
                "SlateCore", 
                "CinematicCamera", 
                "RHI", 
                "D3D11RHI", 
                "D3D12RHI", 
                "RenderCore", 
                "Renderer",
                "Projects", 
                "Json", 
                "JsonUtilities", 
                "DisplayCluster",
                "HeadMountedDisplay",
                "TextureShare",
                "TextureShareCore",
                "EngineSettings"
            });

        PrivateIncludePaths.AddRange(
            new string[]
            {
                Path.Combine(EngineDirectory, "Source/Runtime/D3D12RHI/Public"),
                Path.Combine(EngineDirectory, "Source/Runtime/D3D12RHI/Private"),
                Path.Combine(EngineDirectory, "Source/Runtime/D3D12RHI/Private/Windows"),
                Path.Combine(EngineDirectory, "Source/ThirdParty/Windows/D3DX12/Include"),
                Path.Combine(EngineDirectory, "Source/Runtime/Renderer/Private"),
                Path.Combine(EngineDirectory, "Source/Runtime/Renderer/Private/PostProcess"),
                Path.Combine(EngineDirectory, "Source/Runtime/Renderer/Public")
            });

        DynamicallyLoadedModuleNames.AddRange(new string[] { });

        AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11");
        AddEngineThirdPartyPrivateStaticDependencies(Target, "DX12");
        //AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAPI");
        //AddEngineThirdPartyPrivateStaticDependencies(Target, "AMD_AGS");
        //AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAftermath");
        //AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelMetricsDiscovery");	

        using (var md5 = MD5.Create())
        {
            using (var stream = File.OpenRead(Path.Combine(EngineDirectory, "Plugins/Runtime/nDisplay/Source/DisplayCluster/Private/Game/EngineClasses/Basics/DisplayClusterViewportClient.cpp")))
            {
                byte[] ExpectedDisplayClusterViewPortClientHash = { 129, 125, 54, 121, 82, 208, 1, 85, 113, 228, 11, 71, 10, 47, 160, 45 };
                byte[] ActualDisplayClusterViewPortClientHash = md5.ComputeHash(stream);
                if (!ActualDisplayClusterViewPortClientHash.SequenceEqual(ExpectedDisplayClusterViewPortClientHash))
                    throw new BuildException("RenderStreamViewportClient.cpp is out of sync with DisplayClusterViewportClient.cpp.\n" 
                                             + "Update and change ExpectedDisplayClusterViewPortClientHash in RenderStream.Build.cs to { " + string.Join(", ", ActualDisplayClusterViewPortClientHash) + " }");
            }
        }
    }
}
