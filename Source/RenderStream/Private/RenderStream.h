// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Core.h"
#include "Core/Public/Modules/ModuleInterface.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

#include "RenderStreamLogOutputDevice.h"
#include "Math/UnitConversion.h"

#include "RenderStreamLink.h"
#include "StreamPool.h"
#include "SyncFrameData.h"

DECLARE_LOG_CATEGORY_EXTERN(LogRenderStream, Log, All);

class UCameraComponent;
class AActor;
class RenderStreamSceneSelector;
class FRenderStreamProjectionPolicyFactory;
class FRenderStreamPostProcessFactory;
class ARenderStreamEventHandler;

struct FRenderStreamViewportInfo
{
    TWeakObjectPtr<ACameraActor> Template = nullptr;
    TWeakObjectPtr<ACameraActor> Camera = nullptr;
    int32_t PlayerId = -1;
    
    std::mutex m_frameResponsesLock;
    std::deque<RenderStreamLink::CameraResponseData> m_frameResponses;
};

class FRenderStreamModule : public IModuleInterface
{
public:
    /** IModuleInterface implementation */
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
    virtual bool SupportsAutomaticShutdown () override;
    virtual bool SupportsDynamicReloading () override;

protected:
    void OnPostEngineInit();
    void OnBeginFrame();
    void OnEndFrame();

    void EnableStats() const;

    TArray<TWeakObjectPtr<ARenderStreamEventHandler>> m_eventHandlers;

public:
    static EUnit distanceUnit();
    bool PopulateStreamPool();
    void ConfigureStream(TSharedPtr<FFrameStream> Stream);

    static FRenderStreamModule* Get();
    
    void LoadSchemas(const UWorld& World);
    void ApplyScene(uint32_t sceneId);

    TUniquePtr<FStreamPool> StreamPool;
    FRenderStreamSyncFrameData m_syncFrame;
    std::unique_ptr<RenderStreamSceneSelector> m_sceneSelector;

    void ApplyCameras(const RenderStreamLink::FrameData& frameData);
    void ApplyCameraData(FRenderStreamViewportInfo& info, const RenderStreamLink::FrameData& frameData,
        const RenderStreamLink::CameraData& cameraData);

    void OnModulesChanged(FName ModuleName, EModuleChangeReason ReasonForChange);
    void OnPostLoadMapWithWorld(UWorld* InWorld);

    FRenderStreamViewportInfo& GetViewportInfo(FString const& ViewportId);

    TMap<FString, TSharedPtr<FRenderStreamViewportInfo>> ViewportInfos;
    TSharedPtr<FRenderStreamProjectionPolicyFactory> ProjectionPolicyFactory;
    TSharedPtr<FRenderStreamPostProcessFactory> PostProcessFactory;
    TSharedPtr<FRenderStreamLogOutputDevice, ESPMode::ThreadSafe> m_logDevice = nullptr;
    const UWorld* m_World; // temporary - needs to be held by Scene Selector.
    double m_LastTime = 0;
};
