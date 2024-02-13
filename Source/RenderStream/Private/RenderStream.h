// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Core/Public/Modules/ModuleInterface.h"
#include "DisplayClusterConfigurationTypes_Viewport.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <vector>
#include <map>

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
class UGameInstance;

bool IsInCluster();
bool IsDX11();

DECLARE_MULTICAST_DELEGATE_OneParam(FOnActorSpawned, AActor*);

struct FRenderStreamViewportInfo
{
    TWeakObjectPtr<ACameraActor> Template = nullptr;
    TWeakObjectPtr<ACameraActor> Camera = nullptr;
    int32_t PlayerId = -1;
    RenderStreamLink::CameraHandle CameraHandleLast = 0;
    
    std::mutex m_frameResponsesLock;
    std::map<uint64, RenderStreamLink::CameraResponseData> m_frameResponsesMap;
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
    void OnSystemError();
    void OnEndFrame();

    void GameInstanceStarted(UGameInstance* Instance);
    void AppWillTerminate();
    
    void EnableStats() const;

    TArray<TWeakObjectPtr<ARenderStreamEventHandler>> m_eventHandlers;

public:
    bool PopulateStreamPool();
    void ConfigureStream(FFrameStreamPtr Stream);

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
    void OnActorSpawned(AActor* InActor);
    void HideDefaultPawns();

    FRenderStreamViewportInfo& GetViewportInfo(FString const& ViewportId);

    void PushAnimDataToSource(const RenderStreamLink::FAnimDataKey& Key, const FString& SubjectName, const RenderStreamLink::FSkeletalLayout& Layout, const RenderStreamLink::FSkeletalPose& Pose);
    const FName* GetSkeletalParamName(const RenderStreamLink::FAnimDataKey& Key) const;
    const RenderStreamLink::FSkeletalLayout* GetSkeletalLayout(const FName& SubjectName) const;
    const RenderStreamLink::FSkeletalPose* GetSkeletalPose(const FName& SubjectName) const;

    TMap<FString, TSharedPtr<FRenderStreamViewportInfo>> ViewportInfos;
    TSharedPtr<FRenderStreamProjectionPolicyFactory> ProjectionPolicyFactory;
    TSharedPtr<FRenderStreamPostProcessFactory> PostProcessFactory;
    TSharedPtr<FRenderStreamLogOutputDevice, ESPMode::ThreadSafe> m_logDevice = nullptr;
    double m_LastTime = 0;
    bool m_gameInstanceStarted = false;
    
    TMap<RenderStreamLink::FAnimDataKey, FName> SkeletalParamNames;
    TMap<FName, RenderStreamLink::FSkeletalLayout> SkeletalLayouts;
    TMap<FName, RenderStreamLink::FSkeletalPose> SkeletalPoses;
    mutable FOnActorSpawned OnActorSpawnedDelegate;
};
