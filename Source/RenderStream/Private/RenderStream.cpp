// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "RenderStream.h"
#include "RenderStreamLink.h"

#include "RenderStreamSettings.h"
#include "RenderStreamStatus.h"
#include "RenderStreamSceneSelector.h"
#include "SceneSelector_None.h"
#include "SceneSelector_StreamingLevels.h"
#include "SceneSelector_Maps.h"

#include "Core/Public/Modules/ModuleManager.h"
#include "CoreUObject/Public/Misc/PackageName.h"
#include "Misc/CoreDelegates.h"
#include "Json/Public/Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"

#include "Kismet/GameplayStatics.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/World.h"
#include "ShaderCore.h"

#include "Interfaces/IPluginManager.h"
#include "IDisplayCluster.h"
#include "Cluster/DisplayClusterClusterEvent.h"
#include "RenderStreamProjectionPolicy.h"
#include "Render/IDisplayClusterRenderManager.h"

#include "AssetRegistryModule.h"

#include "Containers/Map.h"

#include "FrameStream.h"

#include "Engine/GameEngine.h"


#include "RenderStreamLogOutputDevice.h"
#include "RenderStreamStats.h"

#include <map>
#include <string>
#include <stdexcept>
#include <vector>
#include <Camera/CameraActor.h>



#include "CineCameraComponent.h"
#include "DisplayClusterConfigurationTypes_Viewport.h"
#include "RenderStreamCapturePostProcess.h"
#include "RenderStreamChannelDefinition.h"
#include "RenderStreamStats.h"
#include "ShaderCompiler.h"
#include "Stats/StatsData.h"

#include "Engine/Public/HardwareInfo.h"
#include "RenderStreamEventHandler.h"

#include "RSUCHelpers.inl"
#include "Camera/CameraComponent.h"
#include "Config/IDisplayClusterConfigManager.h"
#include "Render/Viewport/IDisplayClusterViewportManager.h"

#include "RenderStreamSceneViewExtension.h"

DEFINE_LOG_CATEGORY(LogRenderStream);

#define LOCTEXT_NAMESPACE "FRenderStreamModule"

namespace 
{
    ID3D11Device* GetDX11Device() {
        auto dx11device = static_cast<ID3D11Device*>(GDynamicRHI->RHIGetNativeDevice());
        return dx11device;
    }

    bool IsInCluster()
    {
        return IDisplayCluster::IsAvailable() && IDisplayCluster::Get().GetOperationMode() == EDisplayClusterOperationMode::Cluster;
    }
}

class FRenderStreamMonitor : public FRunnable
{
public:
    virtual ~FRenderStreamMonitor()
    {
        if (Thread)
            Close();
    }

    void Open()
    {
        bStopThread = false;
        Thread = FRunnableThread::Create(
            this,
            TEXT("RenderStreamMonitor")
        );
    }

    void Close()
    {
        delete Thread;
        Thread = nullptr;
    }

private:
    virtual uint32 Run() override
    {
        while (!bStopThread)
        {
            if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
            {
                const FString Message = FString::Printf(TEXT("Compiling %d Shaders"), GShaderCompilingManager->GetNumRemainingJobs());
                RenderStreamLink::instance().rs_setNewStatusMessage(TCHAR_TO_ANSI(*Message));
                bIsClear = false;
            }
            else if (!bIsClear)
            {
                RenderStreamLink::instance().rs_setNewStatusMessage("");
                bIsClear = true;
            }

            FPlatformProcess::Sleep(1);
        }

        RenderStreamLink::instance().rs_setNewStatusMessage("");
        bIsClear = true;
        return 0;
    }

    virtual void Stop() override
    {
        bStopThread = true;
    }

    FRunnableThread* Thread = nullptr;
    bool bStopThread = false;
    bool bIsClear = false;
};

FRenderStreamMonitor Monitor;

static const FName DisplayClusterModuleName(TEXT("DisplayCluster"));

void FRenderStreamModule::StartupModule()
{
    FString ShaderDirectory = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT(RS_PLUGIN_NAME))->GetBaseDir(), TEXT("Shaders"));
    AddShaderSourceDirectoryMapping("/" RS_PLUGIN_NAME, ShaderDirectory);

    // This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
    RenderStreamStatus().InputOutput("Initialising stream", "Waiting for data from d3", RSSTATUS_ORANGE);

    if (!RenderStreamLink::instance().loadExplicit())
    {
        UE_LOG(LogRenderStream, Error, TEXT ("Failed to load RenderStream DLL - d3 not installed?"));
        RenderStreamStatus().InputOutput("Error", "Failed to load RenderStream DLL - d3 not installed?", RSSTATUS_RED);
    }
    else
    {
        m_logDevice = MakeShared<FRenderStreamLogOutputDevice, ESPMode::ThreadSafe>();
        
        int errCode = RenderStreamLink::instance().rs_initialise(RENDER_STREAM_VERSION_MAJOR, RENDER_STREAM_VERSION_MINOR);
        
        if (errCode != RenderStreamLink::RS_ERROR_SUCCESS)
        {
            if (errCode == RenderStreamLink::RS_ERROR_INCOMPATIBLE_VERSION)
            {
                UE_LOG(LogRenderStream, Error, TEXT("Unsupported RenderStream library, expected version %i.%i"), RENDER_STREAM_VERSION_MAJOR, RENDER_STREAM_VERSION_MINOR);
                RenderStreamStatus().InputOutput("Error", "Unsupported RenderStream library", RSSTATUS_RED);
                RenderStreamLink::instance().unloadExplicit();
                return;
            }

            UE_LOG(LogRenderStream, Error, TEXT("Unable to initialise RenderStream library error code %d"), errCode);
            RenderStreamStatus().InputOutput("Error", "Unable to initialise RenderStream library", RSSTATUS_RED);
            RenderStreamLink::instance().unloadExplicit();
            return;
        }
        
        FCoreDelegates::OnHandleSystemError.AddRaw(this, &FRenderStreamModule::OnSystemError);

        FCoreUObjectDelegates::PostLoadMapWithWorld.AddRaw(this, &FRenderStreamModule::OnPostLoadMapWithWorld);
        FCoreDelegates::OnBeginFrame.AddRaw(this, &FRenderStreamModule::OnBeginFrame);
        FCoreDelegates::OnEndFrame.AddRaw(this, &FRenderStreamModule::OnEndFrame);
        FCoreDelegates::OnPostEngineInit.AddRaw(this, &FRenderStreamModule::OnPostEngineInit);
        Monitor.Open();
    }

    if (IDisplayCluster::IsAvailable())
    {
        // Should not happen, but handle just in case
        UE_LOG(LogRenderStream, Warning, TEXT("Unexpected module startup order"));
        OnModulesChanged(DisplayClusterModuleName, EModuleChangeReason::ModuleLoaded);
    }
    else
    {
        FModuleManager::Get().OnModulesChanged().AddRaw(this, &FRenderStreamModule::OnModulesChanged);
    }

    ViewExtension = FRenderStreamSceneViewExtension::Create();

}

void FRenderStreamModule::ShutdownModule()
{
    if (!RenderStreamLink::instance().isAvailable())
        return;

    UE_LOG(LogRenderStream, Log, TEXT("Shutting down RenderStream"));

    Monitor.Close();

    FModuleManager::Get().OnModulesChanged().RemoveAll(this);

    StreamPool.Reset();

    if (IDisplayCluster::IsAvailable())
    {
        if (IsInCluster())
        {
            IDisplayClusterClusterManager* ClusterMgr = IDisplayCluster::Get().GetClusterMgr();
            if (ClusterMgr)
            {
                ClusterMgr->UnregisterSyncObject(&m_syncFrame);
            }
        }

        IDisplayClusterRenderManager* RenderMgr = IDisplayCluster::Get().GetRenderMgr();
        if (RenderMgr)
        {
            if (!RenderMgr->UnregisterProjectionPolicyFactory(FRenderStreamProjectionPolicy::RenderStreamPolicyType))
            {
                UE_LOG(LogRenderStream, Warning, TEXT("An error occurred during un-registering the <%s> projection factory"), FRenderStreamProjectionPolicy::RenderStreamPolicyType);
            }

            if (!RenderMgr->UnregisterPostProcessFactory(FRenderStreamPostProcessFactory::RenderStreamPostProcessType))
            {
                UE_LOG(LogRenderStream, Warning, TEXT("An error occurred during un-registering the <%s> post process factory"), FRenderStreamPostProcessFactory::RenderStreamPostProcessType);
            }
        }
    }

    FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);
    FCoreDelegates::OnBeginFrame.RemoveAll(this);
    FCoreDelegates::OnPostEngineInit.RemoveAll(this);

    // This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
    // we call this function before unloading the module.
    if (!RenderStreamLink::instance ().unloadExplicit ())
    {
        UE_LOG (LogRenderStream, Warning, TEXT ("Failed to free render stream module."));
    }
}

bool FRenderStreamModule::SupportsAutomaticShutdown ()
{
    return true;
}

bool FRenderStreamModule::SupportsDynamicReloading ()
{
    return true;
}

void FRenderStreamModule::LoadSchemas(const UWorld& World)
{
    if (!m_sceneSelector)
    {
        OnPostEngineInit();
        FCoreDelegates::OnPostEngineInit.RemoveAll(this);
    }
    m_sceneSelector->LoadSchemas(World);
}

void FRenderStreamModule::ApplyScene(uint32_t sceneId)
{
    check(m_sceneSelector != nullptr);
    m_sceneSelector->ApplyScene(*GWorld, sceneId);
}

EUnit FRenderStreamModule::distanceUnit()
{
    // Unreal defaults to centimeters so we might as well do the same
    static EUnit ret = EUnit::Unspecified;
    if (ret == EUnit::Unspecified)
    {
        ret = EUnit::Centimeters;

        FString ValueReceived;
        if (!GConfig->GetString(TEXT("/Script/UnrealEd.EditorProjectAppearanceSettings"), TEXT("DistanceUnits"), ValueReceived, GEditorIni))
            return ret;

        TOptional<EUnit> currentUnit = FUnitConversion::UnitFromString(*ValueReceived);
        if (currentUnit.IsSet())
            ret = currentUnit.GetValue();
    }
    return ret;
}

void FRenderStreamModule::ConfigureStream(FFrameStreamPtr Stream)
{
    FString const& Name = Stream->Name();
    IDisplayClusterClusterManager* ClusterMgr = IDisplayCluster::Get().GetClusterMgr();
    IDisplayClusterRenderManager* RenderMgr = IDisplayCluster::Get().GetRenderMgr();

    const IDisplayClusterConfigManager* const ConfigMgr = IDisplayCluster::Get().GetConfigMgr();
    check(ConfigMgr);

    const UDisplayClusterConfigurationViewport* Viewport = ConfigMgr->GetLocalViewport(Name);
    if (!Viewport)
    {
        UE_LOG(LogRenderStream, Error, TEXT("Policy '%s' created without corresponding viewport"), *Name);
        return;
    }

    const FIntPoint Offset(Viewport->Region.X, Viewport->Region.Y);
    const FIntPoint Resolution(Viewport->Region.W, Viewport->Region.H);
    if (!Stream)
    {
        UE_LOG(LogRenderStream, Warning, TEXT("Policy '%s' created for unknown stream"), *Name);
    }
    if (Stream && Stream->Resolution() != Resolution)
    {
        UE_LOG(LogRenderStream, Error, TEXT("Policy '%s' created with incorrect resolution: %dx%d vs expected %dx%d"), *Name, Resolution.X, Resolution.Y, Stream->Resolution().X, Stream->Resolution().Y);
        return;
    }

    FRenderStreamViewportInfo& Info = GetViewportInfo(Name);
    const FString Channel = Stream ? Stream->Channel() : "";
    const TWeakObjectPtr<ACameraActor> ChannelCamera = URenderStreamChannelDefinition::GetChannelCamera(Channel);
    if (Info.Template != ChannelCamera)
    {
        Info.Template = ChannelCamera;
        if (Info.Template.IsValid())
        {
            UE_LOG(LogRenderStream, Log, TEXT("Channel '%s' currently mapped to camera '%s'"), *Channel, *ChannelCamera->GetName());

            URenderStreamChannelDefinition* Definition = Info.Template->FindComponentByClass<URenderStreamChannelDefinition>();
            if (Definition)
            {
                const bool DefaultVisible = Definition->DefaultVisibility == EVisibilty::Visible;
                const TSet<TSoftObjectPtr<AActor>> Actors = DefaultVisible ? Definition->Hidden : Definition->Visible;
                const FString TypeString = DefaultVisible ? "visible" : "hidden";
                UE_LOG(LogRenderStreamPolicy, Log, TEXT("%d cameras registered to channel, filtering %d actors, default visibility '%s'"),
                    URenderStreamChannelDefinition::GetChannelCameraNum(Channel), Actors.Num(), *TypeString);
            }

            // Spawn the instance of the template camera needed for this policy / view.
            FActorSpawnParameters ActorSpawnParameters;
            ActorSpawnParameters.Template = Info.Template.Get();
            Info.Camera = Info.Template->GetWorld()->SpawnActor<class ACameraActor>(Info.Template->GetClass(), ActorSpawnParameters);
            if (URenderStreamChannelDefinition* ClonedDefinition = Info.Camera->FindComponentByClass<URenderStreamChannelDefinition>())
                ClonedDefinition->UnregisterCamera();

            Info.Camera->SetOwner(Info.Template->GetOwner());
            Info.Camera->AttachToActor(Info.Template->GetAttachParentActor(), FAttachmentTransformRules::KeepWorldTransform);

            USceneComponent* RootComponent = Info.Template->GetRootComponent();
            if (RootComponent)
                Info.Camera->SetActorRelativeTransform(Info.Template->GetRootComponent()->GetRelativeTransform());

            APlayerController* Controller = UGameplayStatics::GetPlayerControllerFromID(GWorld, Info.PlayerId);
            if (!Controller)
            {
                Controller = UGameplayStatics::CreatePlayer(GWorld);
                if (Controller)
                    Info.PlayerId = UGameplayStatics::GetPlayerControllerID(Controller);
                else
                {
                    UE_LOG(LogRenderStreamPolicy, Warning, TEXT("Could not set new view target for capturing."));
                    Info.PlayerId = -1;
                    Info.Camera = nullptr;
                }
            }

            if (Controller != nullptr)
                Controller->SetViewTargetWithBlend(Info.Camera.Get());
            else
                UE_LOG(LogRenderStream, Warning, TEXT("Could not set new view target for capturing, no valid controller."));

            if (Definition && Info.Camera.IsValid())
                Definition->AddCameraInstance(Info.Camera);
        }
        else
            UE_LOG(LogRenderStream, Log, TEXT("Channel '%s' currently not mapped to a camera"), *Channel);
    }
}

bool FRenderStreamModule::PopulateStreamPool()
{
    if (!StreamPool)
        return false;

    if (RenderStreamLink::instance().isAvailable())
    {
        std::vector<uint8_t> descMem;
        uint32_t nBytes = 0;
        RenderStreamLink::instance().rs_getStreams(nullptr, &nBytes);

        const static int MAX_TRIES = 3;
        int iterations = 0;

        RenderStreamLink::RS_ERROR res = RenderStreamLink::RS_ERROR_BUFFER_OVERFLOW;
        do
        {
            descMem.resize(nBytes);
            res = RenderStreamLink::instance().rs_getStreams(reinterpret_cast<RenderStreamLink::StreamDescriptions*>(descMem.data()), &nBytes);

            if (res == RenderStreamLink::RS_ERROR_SUCCESS)
                break;

            ++iterations;
        } while (res == RenderStreamLink::RS_ERROR_BUFFER_OVERFLOW && iterations < MAX_TRIES);
        
        if (res != RenderStreamLink::RS_ERROR_SUCCESS)
            return false;

        const RenderStreamLink::StreamDescriptions* header = nBytes >= sizeof(RenderStreamLink::StreamDescriptions) ? reinterpret_cast<const RenderStreamLink::StreamDescriptions*>(descMem.data()) : nullptr;
        const size_t numStreams = header ? header->nStreams : 0;
        TArray<FStreamInfo> streamInfoArray;
        for (size_t i = 0; i < numStreams; ++i)
        {
            const RenderStreamLink::StreamDescription& description = header->streams[i];
            const FString Name(description.name);
            const FIntPoint Resolution(description.width, description.height);
            const FString Channel(description.channel);
            const RenderStreamLink::ProjectionClipping clipping = description.clipping;
            const FBox2D Region(FVector2D(clipping.left, clipping.top), FVector2D(clipping.right, clipping.bottom));
            streamInfoArray.Push({ Channel, Name, Region });

            auto Stream = StreamPool->GetStream(Name);
            if (!Stream)  // Stream does not already exist in pool
            {
                // Add new stream to pool
                UE_LOG(LogRenderStream, Log, TEXT("Discovered new stream %s at %dx%d"), *Name, Resolution.X, Resolution.Y);
                StreamPool->AddNewStreamToPool(Name, Resolution, Channel, description.clipping, description.handle, description.format);
                Stream = StreamPool->GetStream(Name);
            }

            ConfigureStream(Stream);
        }

        // Broadcast streams changed event
        for (TWeakObjectPtr<ARenderStreamEventHandler> eventHandler : m_eventHandlers)
        {
            if (eventHandler.IsValid())
                eventHandler->onStreamsChanged(streamInfoArray);
        }

        return true;
    }
    return false;
}

void FRenderStreamModule::ApplyCameras(const RenderStreamLink::FrameData& frameData)
{
    for (auto& pair  : ViewportInfos)
    {
        const FFrameStreamPtr stream = StreamPool->GetStream(pair.Key);
        if (!stream)
            continue;

        RenderStreamLink::CameraData cameraData;
        if (RenderStreamLink::instance().rs_getFrameCamera(stream->Handle(), &cameraData) == RenderStreamLink::RS_ERROR_SUCCESS)
            ApplyCameraData(*pair.Value, frameData, cameraData);
    }
}

void FRenderStreamModule::ApplyCameraData(FRenderStreamViewportInfo& info, const RenderStreamLink::FrameData& frameData, const RenderStreamLink::CameraData& cameraData)
{
    // Each call must always have a frame response, because there will be a corresponding render call.
    {
        std::lock_guard<std::mutex> guard(info.m_frameResponsesLock);
        info.m_frameResponses.push_back({ frameData.tTracked, cameraData });
    }

    if (!info.Camera.IsValid())
        return;

    // Attach the instanced Camera to the Capture object for this view.
    USceneComponent* SceneComponent = info.Camera->K2_GetRootComponent();
    UCameraComponent* CameraComponent = info.Camera->GetCameraComponent();

    if (cameraData.cameraHandle == 0)  // 2D mapping, just set aspect ratio
    {
        if (CameraComponent)
            CameraComponent->SetAspectRatio(cameraData.sensorX / cameraData.sensorY);
        return;
    }
    else if (CameraComponent && cameraData.orthoWidth > 0.f)  // Use an orthographic camera
    {
        CameraComponent->ProjectionMode = ECameraProjectionMode::Orthographic;
        CameraComponent->OrthoWidth = FUnitConversion::Convert(float(cameraData.orthoWidth), EUnit::Meters, FRenderStreamModule::distanceUnit());
        CameraComponent->SetAspectRatio(cameraData.sensorX / cameraData.sensorY);
    }
    else if (UCineCameraComponent* CineCamera = dynamic_cast<UCineCameraComponent*>(CameraComponent))
    {
        CineCamera->Filmback.SensorWidth = cameraData.sensorX;
        CineCamera->Filmback.SensorHeight = cameraData.sensorY;
        CineCamera->SetCurrentFocalLength(cameraData.focalLength); // RecalcDerivedData
    }
    else if (CameraComponent)
    {
        float throwRatioH = cameraData.focalLength / cameraData.sensorX;
        float fovH = 2.f * FMath::Atan(0.5f / throwRatioH);
        CameraComponent->SetFieldOfView(fovH * 180.f / PI);
        CameraComponent->SetAspectRatio(cameraData.sensorX / cameraData.sensorY);
    }

    if (SceneComponent)
    {
        float _pitch = cameraData.rx;
        float _yaw = cameraData.ry;
        float _roll = cameraData.rz;
        FQuat rotationQuat = FQuat::MakeFromEuler(FVector(_roll, _pitch, _yaw));
        SceneComponent->SetRelativeRotation(rotationQuat);

        FVector pos;
        pos.X = FUnitConversion::Convert(float(cameraData.z), EUnit::Meters, FRenderStreamModule::distanceUnit());
        pos.Y = FUnitConversion::Convert(float(cameraData.x), EUnit::Meters, FRenderStreamModule::distanceUnit());
        pos.Z = FUnitConversion::Convert(float(cameraData.y), EUnit::Meters, FRenderStreamModule::distanceUnit());
        SceneComponent->SetRelativeLocation(pos);
    }
}

void FRenderStreamModule::OnPostEngineInit()
{

    int errCode = RenderStreamLink::RS_ERROR_SUCCESS;

    auto toggle = FHardwareInfo::GetHardwareInfo(NAME_RHI);

    if (toggle == "D3D11")
    {
        ID3D11Device* device = GetDX11Device();
        errCode = RenderStreamLink::instance().rs_initialiseGpGpuWithDX11Device(device);
    }
    else if (toggle == "D3D12")
    {
        FRHICommandListImmediate& RHICmdList = GRHICommandList.GetImmediateCommandList();
        void* queue = nullptr, * list = nullptr;
        D3D12RHI::GetGfxCommandListAndQueue(RHICmdList, list, queue);
        ID3D12CommandQueue* cmdQueue = reinterpret_cast<ID3D12CommandQueue*>(queue);
        auto dx12device = static_cast<ID3D12Device*>(GDynamicRHI->RHIGetNativeDevice());

        errCode = RenderStreamLink::instance().rs_initialiseGpGpuWithDX12DeviceAndQueue(dx12device, cmdQueue);
    }

    if (errCode != RenderStreamLink::RS_ERROR_SUCCESS)
    {
        UE_LOG(LogRenderStream, Error, TEXT("Unable to initialise RenderStream library error code %d"), errCode);
        RenderStreamStatus().InputOutput("Error", "Unable to initialise RenderStream library", RSSTATUS_RED);
        RenderStreamLink::instance().unloadExplicit();
        return;
    }




    StreamPool = MakeUnique<FStreamPool>();

    const URenderStreamSettings* settings = GetDefault<URenderStreamSettings>();
    switch (settings->SceneSelector)
    {
    case ERenderStreamSceneSelector::None:
        UE_LOG(LogRenderStream, Log, TEXT("Using the 'none' scene selector"));
        m_sceneSelector = std::make_unique<SceneSelector_None>();
        break;

    case ERenderStreamSceneSelector::StreamingLevels:
        UE_LOG(LogRenderStream, Log, TEXT("Using the 'streaming levels' scene selector"));
        m_sceneSelector = std::make_unique<SceneSelector_StreamingLevels>();
        break;

    case ERenderStreamSceneSelector::Maps:
        UE_LOG(LogRenderStream, Log, TEXT("Using the 'maps' scene selector"));
        m_sceneSelector = std::make_unique<SceneSelector_Maps>();
        break;

    default:
        UE_LOG(LogRenderStream, Error, TEXT("Unknown scene selector option %d - defaulting to none"), settings->SceneSelector);
        m_sceneSelector = std::make_unique<SceneSelector_None>();
    }


}

void FRenderStreamModule::OnSystemError()
{
    RenderStreamLink& link = RenderStreamLink::instance();
    if (link.isAvailable())
    {
        link.rs_logToD3("Unexpected system error - process will terminate");
    }
}

void FRenderStreamModule::OnBeginFrame()
{
    if (!IsInCluster())
        return;

    // UpdateSyncObject
    IDisplayClusterClusterManager* ClusterMgr = IDisplayCluster::IsAvailable() ? IDisplayCluster::Get().GetClusterMgr() : nullptr;
    const bool IsController = !ClusterMgr || !ClusterMgr->IsSlave();
    if (IsController)
        m_syncFrame.ControllerReceive();
}

void FRenderStreamModule::OnModulesChanged(FName ModuleName, EModuleChangeReason ReasonForChange)
{
    if (ReasonForChange == EModuleChangeReason::ModuleLoaded && ModuleName == DisplayClusterModuleName)
    {
        IDisplayClusterRenderManager* RenderMgr = IDisplayCluster::Get().GetRenderMgr();
        check(RenderMgr);
        {
            // Policies need to be available early for view setup
            ProjectionPolicyFactory = MakeShared<FRenderStreamProjectionPolicyFactory>();
            UE_LOG(LogRenderStream, Log, TEXT("Registering <%s> projection policy factory..."), FRenderStreamProjectionPolicy::RenderStreamPolicyType);

            TSharedPtr<IDisplayClusterProjectionPolicyFactory> basePtr = StaticCastSharedPtr<IDisplayClusterProjectionPolicyFactory>(ProjectionPolicyFactory);
            if (!RenderMgr->RegisterProjectionPolicyFactory(FRenderStreamProjectionPolicy::RenderStreamPolicyType, basePtr))
            {
                UE_LOG(LogRenderStream, Warning, TEXT("Couldn't register <%s> projection policy factory"), FRenderStreamProjectionPolicy::RenderStreamPolicyType);
            }
        }
        {
            // Policies need to be available early for view setup
            PostProcessFactory = MakeShared<FRenderStreamPostProcessFactory>();
            UE_LOG(LogRenderStream, Log, TEXT("Registering <%s> post process factory..."), FRenderStreamProjectionPolicy::RenderStreamPolicyType);

            TSharedPtr<IDisplayClusterPostProcessFactory> basePtr = StaticCastSharedPtr<IDisplayClusterPostProcessFactory>(PostProcessFactory);
            if (!RenderMgr->RegisterPostProcessFactory(FRenderStreamPostProcessFactory::RenderStreamPostProcessType, basePtr))
            {
                UE_LOG(LogRenderStream, Warning, TEXT("Couldn't register <%s> post process factory"), FRenderStreamProjectionPolicy::RenderStreamPolicyType);
            }
        }
    }
}

void FRenderStreamModule::OnPostLoadMapWithWorld(UWorld* InWorld)
{
    if (IsInCluster())
    {
        IDisplayClusterClusterManager* ClusterMgr = IDisplayCluster::Get().GetClusterMgr();
        check(ClusterMgr);
        // Manager is cleared on map load, so register here instead of on module load
        ClusterMgr->RegisterSyncObject(&m_syncFrame, EDisplayClusterSyncGroup::PreTick);
    }

    // Find all event handlers
    if (InWorld)
    {
        m_eventHandlers.Empty();
        TArray<AActor*> FoundActors;
        UGameplayStatics::GetAllActorsOfClass(InWorld, ARenderStreamEventHandler::StaticClass(), FoundActors);

        for (AActor* Actor : FoundActors)
        {
            if (ARenderStreamEventHandler* EventHandler = Cast<ARenderStreamEventHandler>(Actor))
                m_eventHandlers.Add(EventHandler);
        }
    }

    // Broadcast streams changed event with initial streams
    if (StreamPool)
    {
        TArray<FStreamInfo> streamInfoArray;
        for (const FFrameStreamPtr& stream : StreamPool->GetAllStreams())
        {
            if (stream)
            {
                const RenderStreamLink::ProjectionClipping clipping = stream->Clipping();
                const FBox2D Region(FVector2D(clipping.left, clipping.top), FVector2D(clipping.right, clipping.bottom));
                streamInfoArray.Push({ FString(stream->Channel()), FString(stream->Name()), Region });
            }
        }

        for (TWeakObjectPtr<ARenderStreamEventHandler> eventHandler : m_eventHandlers)
        {
            if (eventHandler.IsValid())
                eventHandler->onStreamsChanged(streamInfoArray);
        }
    }

    EnableStats();
}

#if STATS
void EnableStatGroup(UObject* WorldContextObject, FName GroupName)
{
    if (FGameThreadStatsData* StatsData = FLatestGameThreadStatsData::Get().Latest)
    {
        const FString GroupNameString = FString(TEXT("STATGROUP_")) + GroupName.ToString();
        const FName GroupNameFull = FName(*GroupNameString, EFindName::FNAME_Find);
        if (StatsData->GroupNames.Contains(GroupNameFull))
            return;
    }

    if (APlayerController* TargetPC = UGameplayStatics::GetPlayerController(WorldContextObject, 0))
        TargetPC->ConsoleCommand(FString(TEXT("stat ")) + GroupName.ToString() + FString(TEXT(" -nodisplay")), /*bWriteToLog=*/false);
}

float GetStatValue(const FComplexStatMessage& Message)
{
    if (Message.NameAndInfo.GetFlag(EStatMetaFlags::IsCycle))
        return FPlatformTime::ToMilliseconds(Message.GetValue_Duration(EComplexStatField::ExcAve));

    const EStatDataType::Type Type = Message.NameAndInfo.GetField<EStatDataType>();
    if (Type == EStatDataType::ST_double)
        return (float)Message.GetValue_double(EComplexStatField::ExcAve);
    if (Type == EStatDataType::ST_int64)
        return (float)Message.GetValue_int64(EComplexStatField::ExcAve);

    // Unsupported
    return 0.f;
}

void ParseMessages(TArray<RenderStreamLink::ProfilingEntry>& Entries, TMap<FString, const char*>& MessageNames, const TArray<FComplexStatMessage>& Messages)
{
    if (MessageNames.Num() != 0)
    {
        for (const FComplexStatMessage& Message : Messages)
        {
            FString Name = Message.GetShortName().ToString();
            const char** Entry = MessageNames.Find(Name);
            if (Entry)
            {
                Entries.Push({ *Entry, GetStatValue(Message) });
                MessageNames.Remove(Name);
            }

            if (MessageNames.Num() == 0)
                break;
        }
    }
}

void FetchStats(TArray<RenderStreamLink::ProfilingEntry>& Entries)
{
    FGameThreadStatsData* StatsData = FLatestGameThreadStatsData::Get().Latest;
    if (StatsData)
    {
        TMap<FString, const char*> FlatStats = {
            {FStat_STAT_AwaitFrame::GetStatName(), FStat_STAT_AwaitFrame::GetStatName()},
            {FStat_STAT_ReceiveFrame::GetStatName(), FStat_STAT_ReceiveFrame::GetStatName()}
        };
        TMap<FString, const char*> CounterStats = {
            // This is giving weird values, requires more investigation.
            //{FStat_STAT_RHITriangles::GetStatName(), FStat_STAT_RHITriangles::GetStatName()}
        };

        for (const FActiveStatGroupInfo& Group : StatsData->ActiveStatGroups)
        {
            ParseMessages(Entries, FlatStats, Group.FlatAggregate);
            ParseMessages(Entries, CounterStats, Group.CountersAggregate);
        }
    }
}
#endif

void FRenderStreamModule::EnableStats() const
{
#if STATS
    //EnableStatGroup(GWorld, "RHI");
    //EnableStatGroup(GWorld, "RenderStream");
#endif
}

void FRenderStreamModule::OnEndFrame()
{
    if (!IsInCluster())
        return;

    TArray<RenderStreamLink::ProfilingEntry> Entries;
#if STATS
    FetchStats(Entries);
#endif

    float DiffTime;
    if (FApp::IsBenchmarking() || FApp::UseFixedTimeStep())
    {
        const double CurrentTime = FPlatformTime::Seconds();
        if (m_LastTime == 0)
            m_LastTime = CurrentTime;

        DiffTime = CurrentTime - m_LastTime;
        m_LastTime = CurrentTime;
    }
    else
        DiffTime = FApp::GetCurrentTime() - FApp::GetLastTime();

    float gpuTime = 0;
    for (uint32 GPUIndex : FRHIGPUMask::All())
    {
        // Get the time from the busiest gpu.
        const uint32 GPUCycles = RHIGetGPUFrameCycles(GPUIndex);
        gpuTime = FGenericPlatformMath::Max(gpuTime, FPlatformTime::ToMilliseconds(GPUCycles));
    }

    // Get the time we idled this frame.
    double WaitTime = FThreadIdleStats::Get().Waits;

    Entries.Push({ "Frame Time", DiffTime * 1000.0f });
    Entries.Push({ "Game Time", FPlatformTime::ToMilliseconds(GGameThreadTime) });
    Entries.Push({ "Render Time", FPlatformTime::ToMilliseconds(GRenderThreadTime) });
    Entries.Push({ "RHI Time", FPlatformTime::ToMilliseconds(GRHIThreadTime) });
    Entries.Push({ "GPU Time", gpuTime });
    Entries.Push({ "Unreal Idle Time", FPlatformTime::ToMilliseconds(WaitTime) });

    // Because their stats api is weird for now we are manually timing this.
    IDisplayClusterClusterManager* ClusterMgr = IDisplayCluster::IsAvailable() ? IDisplayCluster::Get().GetClusterMgr() : nullptr;
    const bool IsController = !ClusterMgr || !ClusterMgr->IsSlave();
    if (IsController)
        Entries.Push({ "Await Time", (float)m_syncFrame.AwaitTime });
    else
        Entries.Push({ "Receive Time", (float)m_syncFrame.ReceiveTime });

    RenderStreamLink::instance().rs_sendProfilingData(Entries.GetData(), Entries.Num());
}

FRenderStreamViewportInfo& FRenderStreamModule::GetViewportInfo(FString const& ViewportId)
{
    const auto Info = ViewportInfos.Find(ViewportId);
    if (Info)
        return *(*Info);
    
    return *ViewportInfos.Add(ViewportId, MakeShareable<FRenderStreamViewportInfo>(new FRenderStreamViewportInfo()));
}

/*static*/ FRenderStreamModule* FRenderStreamModule::Get()
{
    return &FModuleManager::GetModuleChecked<FRenderStreamModule>("RenderStream");
}

#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(FRenderStreamModule, RenderStream)
