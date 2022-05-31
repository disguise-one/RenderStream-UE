// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using System.Linq;
//using System.Security.Cryptography;


public class RenderStream : ModuleRules
{
    public RenderStream(ReadOnlyTargetRules Target) : base(Target)
    {
        OptimizeCode = CodeOptimization.Never;
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[] 
            { 
                "Core", 
                "Sockets", 
                "Networking", 
                "InputCore", 
                "UMG",
                "OpenColorIO"
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
                "RHICore",
                "D3D11RHI", 
                "D3D12RHI", 
                "RenderCore", 
                "Projects", 
                "Json", 
                "JsonUtilities", 
                "DisplayCluster",
                "HeadMountedDisplay",
                "EngineSettings"
            });

        PrivateIncludePaths.AddRange(
            new string[]
            {
                Path.Combine(EngineDirectory, "Source/Runtime/D3D12RHI/Private"),
                Path.Combine(EngineDirectory, "Source/Runtime/D3D12RHI/Private/Windows"),
                Path.Combine(EngineDirectory, "Source/ThirdParty/Windows/D3DX12/Include")
            });

        DynamicallyLoadedModuleNames.AddRange(new string[] { });

        AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11");
        AddEngineThirdPartyPrivateStaticDependencies(Target, "DX12");
        //AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAPI");
        //AddEngineThirdPartyPrivateStaticDependencies(Target, "AMD_AGS");
        //AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAftermath");
        //AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelMetricsDiscovery");	}

        //using (var md5 = MD5.Create())
        //{
        //    using (var stream = File.OpenRead(Path.Combine(EngineDirectory, "Plugins/Runtime/nDisplay/Source/DisplayCluster/Private/Game/EngineClasses/Basics/DisplayClusterViewportClient.cpp")))
        //    {
        //        byte[] ExpectedDisplayClusterViewPortClientHash = { 123, 235, 73, 150, 222, 170, 14, 143, 41, 26, 222, 165, 172, 127, 241, 113 };
        //        byte[] ActualDisplayClusterViewPortClientHash = md5.ComputeHash(stream);
        //        if (!ActualDisplayClusterViewPortClientHash.SequenceEqual(ExpectedDisplayClusterViewPortClientHash))
        //            throw new BuildException("RenderStreamViewportClient.cpp is out of sync with DisplayClusterViewportClient.cpp.\n" 
        //                                     + "Apply all changes to RenderStreamViewportClient.cpp.\nThen change ExpectedDisplayClusterViewPortClientHash in RenderStream.Build.cs to { " + string.Join(", ", ActualDisplayClusterViewPortClientHash) + " }");
        //    }
        //}
    }
}
