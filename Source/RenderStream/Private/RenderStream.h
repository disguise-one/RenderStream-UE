// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Core.h"
#include "Core/Public/Modules/ModuleInterface.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include <set>
#include <memory>

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

public:
    static EUnit distanceUnit();
    bool PopulateStreamPool();

    static FRenderStreamModule* Get();
    
    void LoadSchemas(const UWorld& World);
    void ApplyScene(uint32_t sceneId);

    TUniquePtr<FStreamPool> StreamPool;
    FRenderStreamSyncFrameData m_syncFrame;
    std::unique_ptr<RenderStreamSceneSelector> m_sceneSelector;

    void ApplyCameras(const RenderStreamLink::FrameData& frameData);

    void OnModulesChanged(FName ModuleName, EModuleChangeReason ReasonForChange);
    void OnPostLoadMapWithWorld(UWorld* InWorld);

    TSharedPtr<FRenderStreamProjectionPolicyFactory> ProjectionPolicyFactory;
    TSharedPtr<FRenderStreamLogOutputDevice, ESPMode::ThreadSafe> m_logDevice = nullptr;
    const UWorld* m_World; // temporary - needs to be held by Scene Selector.
    double m_LastTime = 0;
};
