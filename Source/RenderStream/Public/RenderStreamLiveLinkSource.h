// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "ILiveLinkClient.h"
#include "ILiveLinkSource.h"
#include "RenderStreamSceneSelector.h"

/**
 * 
 */
class RENDERSTREAM_API FRenderStreamLiveLinkSource : public ILiveLinkSource
{
public:
    FRenderStreamLiveLinkSource() = default;

    virtual void ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid) override;

    virtual bool IsSourceStillValid() const override;

    virtual bool RequestSourceShutdown() override;

    virtual void Update() override;

    void PushFrameAnimData(const FName& SubjectName, const RenderStreamLink::FSkeletalLayout& Layout, const RenderStreamLink::FSkeletalPose& Pose);

    virtual FText GetSourceType() const override;
    virtual FText GetSourceMachineName() const override;
    virtual FText GetSourceStatus() const override;

    // settings
    virtual void InitializeSettings(ULiveLinkSourceSettings* Settings) override;
    virtual TSubclassOf< ULiveLinkSourceSettings > GetSettingsClass() const override { return ULiveLinkSourceSettings::StaticClass(); }
    virtual void OnSettingsChanged(ULiveLinkSourceSettings* Settings, const FPropertyChangedEvent& PropertyChangedEvent) override;

private:
    ILiveLinkClient* Client;

    // Our identifier in LiveLink
    FGuid SourceGuid;
};
