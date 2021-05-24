// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "StereoRendering.h"
#include "SceneViewExtension.h"
#include "StereoRenderTargetManager.h"
#include "Subsystems/EngineSubsystem.h"


#include "Render/Device/IDisplayClusterRenderDevice.h"
#include "Render/Device/IDisplayClusterRenderDeviceFactory.h"

#include "RHI.h"
#include "RHIResources.h"
#include "RHICommandList.h"

DECLARE_LOG_CATEGORY_EXTERN(LogRenderStreamStereoRendering, Log, All);

/**
 * 
 */


class RENDERSTREAM_API FRenderStreamRenderDeviceFactory : public IDisplayClusterRenderDeviceFactory
{
public:
	FRenderStreamRenderDeviceFactory();
	~FRenderStreamRenderDeviceFactory();

	TSharedPtr<IDisplayClusterRenderDevice, ESPMode::ThreadSafe> Create(const FString& InDeviceType, const FString& InRHIName) override;
};


// this has to inherit from DisplayClusterRenderDevice
class RENDERSTREAM_API FRenderStreamStereoRenderDevice : public IDisplayClusterRenderDevice, public IStereoRenderTargetManager, public FSceneViewExtensionBase
{
public:
	FRenderStreamStereoRenderDevice(const FAutoRegister&);
	virtual ~FRenderStreamStereoRenderDevice();

	static TSharedPtr<FRenderStreamStereoRenderDevice, ESPMode::ThreadSafe> CreateMultiViewRenderingDevice();

	//////////////////////////////
	// FSceneViewExtensionBase
	//////////////////////////////
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) {}
	virtual void PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) {}
	virtual void PreRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) {}
	virtual void PostRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily);
    virtual void PostRenderBasePass_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) override;

	//////////////////////////////
	// IDisplayClusterRenderDevice
	//////////////////////////////
	virtual bool Initialize() override;
	virtual void StartScene(UWorld* World) override;
	virtual void EndScene() override;
	virtual void SetViewportCamera(const FString& CameraId = FString(), const FString& ViewportId = FString());
	virtual void SetStartPostProcessingSettings(const FString& ViewportId, const FPostProcessSettings& StartPostProcessingSettings) override;
	virtual void SetOverridePostProcessingSettings(const FString& ViewportId, const FPostProcessSettings& OverridePostProcessingSettings, float BlendWeight = 1.0f) override;
	virtual void SetFinalPostProcessingSettings(const FString& ViewportId, const FPostProcessSettings& FinalPostProcessingSettings) override;
	virtual bool GetViewportRect(const FString& ViewportId, FIntRect& OutRect) override;
	virtual bool GetViewportProjectionPolicy(const FString& ViewportId, TSharedPtr<IDisplayClusterProjectionPolicy>& OutProjectionPolicy) override;
	virtual bool GetViewportContext(const FString& ViewportId, int ViewIndex, FDisplayClusterRenderViewContext& OutViewContext) override;

	virtual bool SetBufferRatio(const FString& ViewportId, float InBufferRatio) override;
	virtual bool GetBufferRatio(const FString& ViewportId, float& OutBufferRatio) const override;
	virtual bool SetBufferRatio(int32 ViewportIdx, float InBufferRatio)override;
	virtual bool GetBufferRatio(int32 ViewportIdx, float& OutBufferRatio) const override;
	virtual const FDisplayClusterRenderViewport* GetRenderViewport(const FString& ViewportId) const override;
	virtual const FDisplayClusterRenderViewport* GetRenderViewport(int32 ViewportIdx) const override;
	virtual const void GetRenderViewports(TArray<FDisplayClusterRenderViewport>& OutViewports) const override;
	virtual uint32 GetViewsAmountPerViewport() const override;


	/////////////////////////
	// IStereoRenderingDevice
	/////////////////////////
	virtual bool IsStereoEnabled() const override;
	virtual bool IsStereoEnabledOnNextFrame() const override;
	virtual bool EnableStereo(bool stereo = true) override;
	virtual void CalculateStereoViewOffset(const enum EStereoscopicPass StereoPassType, FRotator& ViewRotation,
		const float InWorldToMeters, FVector& ViewLocation) override;
	virtual void AdjustViewRect(enum EStereoscopicPass StereoPass, int32& X, int32& Y, uint32& SizeX, uint32& SizeY) const override;
	virtual void SetFinalViewRect(const enum EStereoscopicPass StereoPass, const FIntRect& FinalViewRect) override;

	virtual FMatrix GetStereoProjectionMatrix(const enum EStereoscopicPass StereoPassType) const override;
	virtual void InitCanvasFromView(class FSceneView* InView, class UCanvas* Canvas) override;
	virtual IStereoRenderTargetManager* GetRenderTargetManager() override;

	virtual int32 GetDesiredNumberOfViews(bool bStereoRequested) const override;
	virtual EStereoscopicPass GetViewPassForIndex(bool bStereoRequested, uint32 ViewIndex) const override;
	virtual uint32 GetViewIndexForPass(EStereoscopicPass StereoPassType) const override;
	virtual bool DeviceIsAPrimaryView(const FSceneView& View) override;
	virtual bool DeviceIsAPrimaryPass(EStereoscopicPass Pass) override;
	virtual bool DeviceIsASecondaryView(const FSceneView& View) override;
	virtual bool DeviceIsASecondaryPass(EStereoscopicPass Pass) override;
	virtual bool DeviceIsAnAdditionalView(const FSceneView& View) override;
	virtual bool DeviceIsAnAdditionalPass(EStereoscopicPass Pass) override;

	virtual void RenderTexture_RenderThread(class FRHICommandListImmediate& RHICmdList, class FRHITexture2D* BackBuffer, class FRHITexture2D* SrcTexture, FVector2D WindowSize) const override;

	/////////////////////////////
	// IStereoRenderTargetManager
	/////////////////////////////
	virtual bool ShouldUseSeparateRenderTarget() const override;
	virtual void UpdateViewport(bool bUseSeparateRenderTarget, const class FViewport& Viewport, class SViewport* ViewportWidget = nullptr) override;
	virtual void CalculateRenderTargetSize(const class FViewport& Viewport, uint32& InOutSizeX, uint32& InOutSizeY) override;
	virtual bool NeedReAllocateViewportRenderTarget(const class FViewport& Viewport) override;

protected:
	////////////////////////////////////////
	//Copied from DisplayClusterDeviceBase.h
	////////////////////////////////////////
	enum EDisplayClusterEyeType
	{
		StereoLeft = 0,
		Mono = 1,
		StereoRight = 2,
		COUNT
	};

	// Adds a new viewport with specified parameters and projection policy object
	void AddViewport(const FString& InViewportId, const FIntPoint& InViewportLocation, const FIntPoint& InViewportSize,
		TSharedPtr<IDisplayClusterProjectionPolicy> InProjPolicy, const FString& InCameraId, float InBufferRatio = 1.f,
		int GPUIndex = INDEX_NONE, bool bAllowCrossGPUTransfer = true, bool bIsShared = false);

	virtual FRHICustomPresent* CreatePresentationObject(FViewport* const Viewport, TSharedPtr<IDisplayClusterRenderSyncPolicy>& SyncPolicy);

private:

	bool m_isStereoEnabled;

	////////////////////////////////////////
	//Copied from DisplayClusterDeviceBase.h
	////////////////////////////////////////
	struct FOverridePostProcessingSettings
	{
		float BlendWeight = 1.0f;
		FPostProcessSettings PostProcessingSettings;
	};

	// Data access synchronization
	mutable FCriticalSection InternalsSyncScope;
	// Viewports
	mutable TArray<FDisplayClusterRenderViewport> RenderViewports;
	// Views per viewport (render passes)
	uint32 ViewsAmountPerViewport = 0;
	// UE4 main viewport
	FViewport* MainViewport = nullptr;

	bool bIsSceneOpen = false;
	bool bIsCustomPresentSet = false;

	// Custom post processing settings
	TMap<int, FPostProcessSettings> ViewportStartPostProcessingSettings;
	TMap<int, FOverridePostProcessingSettings> ViewportOverridePostProcessingSettings;
	TMap<int, FPostProcessSettings> ViewportFinalPostProcessingSettings;


    ////////////////////////////////////////
    // GBuffer and depth extraction
    ////////////////////////////////////////
private:
	
    mutable FTexture2DRHIRef m_intermediateDepth;
    mutable FTexture2DRHIRef m_intermediateAlbedoAO;
    mutable FTexture2DRHIRef m_intermediateDistortion;
    mutable FTexture2DRHIRef m_intermediateWorldNormals;

    void copyToIntermediateBuffer_RenderThread(FRHICommandListImmediate& RHICmdList, FTexture2DRHIRef& Src, FTexture2DRHIRef& Dst);

	mutable int32 m_viewFamilyIdx = 0;
	mutable int32 m_numViewFamilies = 1;
	mutable int32 m_renderThreadViewCount = 0;
    
    bool m_depthEnabled;
    bool m_albedoEnabled;
    bool m_normalsEnabled;
    bool m_distortionEnabled;

public:
	void UpdateViewFamilyIndex(const int32 InViewFamilyIdx) const;
	void UpdateNumViewFamilies(const int32 InNumFamilies) const;
	
};
