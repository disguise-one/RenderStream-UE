// Fill out your copyright notice in the Description page of Project Settings.


#include "RenderStreamStereoRenderDevice.h"
#include "DisplayClusterConfigurationTypes.h"
#include "Render/Projection/IDisplayClusterProjectionPolicyFactory.h"
#include "Render/Projection/IDisplayClusterProjectionPolicy.h"
#include "Config/IDisplayClusterConfigManager.h"
#include "IDisplayCluster.h"
#include "Render/IDisplayClusterRenderManager.h"
#include "Render/PostProcess/IDisplayClusterPostProcess.h"
#include "Render/Device/DisplayClusterRenderViewport.h"

#include "Renderer/Private/PostProcess/SceneRenderTargets.h"

#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "GenericPlatform/GenericPlatformFile.h"

#include "ITextureShare.h"
#include "ITextureShareItem.h"

#include "Serialization/BufferArchive.h"

#include "RenderStreamLink.h"
#include "RenderStreamProjectionPolicy.h"
#include "CoreGlobals.h"

class IDisplayClusterPostProcess;
class FDisplayClusterPresentationBase;
class FSceneView;


DEFINE_LOG_CATEGORY(LogRenderStreamStereoRendering)

FRenderStreamRenderDeviceFactory::FRenderStreamRenderDeviceFactory()
{

}

FRenderStreamRenderDeviceFactory::~FRenderStreamRenderDeviceFactory()
{

}
TSharedPtr<IDisplayClusterRenderDevice, ESPMode::ThreadSafe> FRenderStreamRenderDeviceFactory::Create(const FString& InDeviceType, const FString& InRHIName)
{
	UE_LOG(LogRenderStreamStereoRendering, Log, TEXT("Instantiating RenderStream stereo rendering device"));
	return FRenderStreamStereoRenderDevice::CreateMultiViewRenderingDevice();
}

FRenderStreamStereoRenderDevice::FRenderStreamStereoRenderDevice(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister)
{
	ViewsAmountPerViewport = 1;
}
FRenderStreamStereoRenderDevice::~FRenderStreamStereoRenderDevice()
{
}

TSharedPtr<FRenderStreamStereoRenderDevice, ESPMode::ThreadSafe> FRenderStreamStereoRenderDevice::CreateMultiViewRenderingDevice()
{
	auto MVPRenderingDevice = FSceneViewExtensions::NewExtension<FRenderStreamStereoRenderDevice>();
	return MVPRenderingDevice;
}

bool FRenderStreamStereoRenderDevice::Initialize()
{
	// Turning this off so it doesnt spam the console about instanced stereo.
	//GEngine->Exec(nullptr, TEXT("log LogDisplayClusterGame off")); //TODO find a better place to put this

    if (GConfig)
    {
        check(GConfig->GetBool(TEXT("/Script/RenderStream.EnhancedContentSettings"), TEXT("enableDepth"), m_depthEnabled, GEngineIni));
        check(GConfig->GetBool(TEXT("/Script/RenderStream.EnhancedContentSettings"), TEXT("enableAlbedo"), m_albedoEnabled, GEngineIni));
        check(GConfig->GetBool(TEXT("/Script/RenderStream.EnhancedContentSettings"), TEXT("enableNormals"), m_normalsEnabled, GEngineIni));
        check(GConfig->GetBool(TEXT("/Script/RenderStream.EnhancedContentSettings"), TEXT("enableDistortion"), m_distortionEnabled, GEngineIni));

        UE_LOG(LogRenderStreamStereoRendering, Log, TEXT("Depth Enabled: %d"), (int)m_depthEnabled);
        UE_LOG(LogRenderStreamStereoRendering, Log, TEXT("Albedo Enabled: %d"), (int)m_albedoEnabled);
        UE_LOG(LogRenderStreamStereoRendering, Log, TEXT("Normals Enabled: %d"), (int)m_normalsEnabled);
        UE_LOG(LogRenderStreamStereoRendering, Log, TEXT("Distortion Enabled: %d"), (int)m_distortionEnabled);
    }

	if (IDisplayCluster::Get().GetOperationMode() == EDisplayClusterOperationMode::Disabled)
	{
		return false;
	}

	// Get local node configuration
	const UDisplayClusterConfigurationClusterNode* LocalNode = IDisplayCluster::Get().GetConfigMgr()->GetLocalNode();
	if (!LocalNode)
	{
		UE_LOG(LogRenderStreamStereoRendering, Error, TEXT("Couldn't get configuration data for current cluster node"));
		return false;
	}

	// Initialize all local viewports
	for (const auto& it : LocalNode->Viewports)
	{
		TSharedPtr<IDisplayClusterProjectionPolicyFactory> ProjPolicyFactory = IDisplayCluster::Get().GetRenderMgr()->GetProjectionPolicyFactory(it.Value->ProjectionPolicy.Type);
		if (ProjPolicyFactory.IsValid())
		{
			const FString RHIName = GDynamicRHI->GetName();
			TSharedPtr<IDisplayClusterProjectionPolicy> ProjPolicy = ProjPolicyFactory->Create(it.Value->ProjectionPolicy.Type, RHIName, it.Key, it.Value->ProjectionPolicy.Parameters);
			if (ProjPolicy.IsValid())
			{
				AddViewport(it.Key, FIntPoint(it.Value->Region.X, it.Value->Region.Y), FIntPoint(it.Value->Region.W, it.Value->Region.H), ProjPolicy, it.Value->Camera, it.Value->BufferRatio, it.Value->GPUIndex, it.Value->bAllowCrossGPUTransfer, it.Value->bIsShared);
			}
			else
			{
				UE_LOG(LogRenderStreamStereoRendering, Warning, TEXT("Invalid projection policy: type '%s', RHI '%s', viewport '%s'"), *it.Value->ProjectionPolicy.Type, *RHIName, *it.Key);
			}
		}
		else
		{
			UE_LOG(LogRenderStreamStereoRendering, Warning, TEXT("No projection factory found for projection type '%s'"), *it.Value->ProjectionPolicy.Type);
		}
	}

	if (RenderViewports.Num() < 1)
	{
		UE_LOG(LogRenderStreamStereoRendering, Error, TEXT("No viewports created. At least one must present."));
		return false;
	}
	TMap<FString, IDisplayClusterRenderManager::FDisplayClusterPPInfo> Postprocess = IDisplayCluster::Get().GetRenderMgr()->GetRegisteredPostprocessOperations();

	// Initialize all local postprocess operations
	for (const auto& it : LocalNode->Postprocess)
	{
		if (Postprocess.Contains(it.Value.Type))
		{
			Postprocess[it.Value.Type].Operation->InitializePostProcess(it.Value.Parameters);
		}
	}
	return true;
}

void FRenderStreamStereoRenderDevice::UpdateViewFamilyIndex(const int32 InViewFamilyIdx) const
{
    m_viewFamilyIdx = InViewFamilyIdx;
}
void FRenderStreamStereoRenderDevice::UpdateNumViewFamilies(const int32 InNumFamilies) const
{
    m_numViewFamilies = InNumFamilies;
}



void FRenderStreamStereoRenderDevice::StartScene(UWorld* World)
{
	bIsSceneOpen = true;

	for (FDisplayClusterRenderViewport& Viewport : RenderViewports)
	{
		Viewport.GetProjectionPolicy()->StartScene(World);
	}
}
void FRenderStreamStereoRenderDevice::EndScene()
{
	bIsSceneOpen = false;
}


void FRenderStreamStereoRenderDevice::PostRenderBasePass_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView)
{
    FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
    SceneContext.AdjustGBufferRefCount(RHICmdList, 1);

    if (m_normalsEnabled)
    {
        FTexture2DRHIRef normalsTex = (FTexture2DRHIRef)SceneContext.GBufferA->GetRenderTargetItem().TargetableTexture;
        copyToIntermediateBuffer_RenderThread(RHICmdList, normalsTex, m_intermediateWorldNormals);

        UE_LOG(LogRenderStreamStereoRendering, Verbose, TEXT("Copied Normals texture on PostRenderBasePass_RenderThread"));
    }
    if (m_distortionEnabled)
    {
        FTexture2DRHIRef distortionTex = (FTexture2DRHIRef)SceneContext.GBufferB->GetRenderTargetItem().TargetableTexture; //SceneContext.GetGBufferBTexture();
        copyToIntermediateBuffer_RenderThread(RHICmdList, distortionTex, m_intermediateDistortion);

        UE_LOG(LogRenderStreamStereoRendering, Verbose, TEXT("Copied Distortion texture on PostRenderBasePass_RenderThread"));
    }
    if (m_albedoEnabled)
    {
        FTexture2DRHIRef albedoTex = (FTexture2DRHIRef)SceneContext.GBufferC->GetRenderTargetItem().TargetableTexture; //SceneContext.GetGBufferCTexture();
        copyToIntermediateBuffer_RenderThread(RHICmdList, albedoTex, m_intermediateAlbedoAO);

        UE_LOG(LogRenderStreamStereoRendering, Verbose, TEXT("Copied Albedo texture on PostRenderBasePass_RenderThread"));
    }

    SceneContext.AdjustGBufferRefCount(RHICmdList, -1);


}
void FRenderStreamStereoRenderDevice::PostRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily)
{
    if (m_depthEnabled)
    {
        FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
        FTexture2DRHIRef depthTex = SceneContext.GetSceneDepthSurface();
        copyToIntermediateBuffer_RenderThread(RHICmdList, depthTex, m_intermediateDepth);

        UE_LOG(LogRenderStreamStereoRendering, Verbose, TEXT("Copied SceneDepth texture on PostRenderViewFamily_RenderThread"));
    }
    m_renderThreadViewCount++;
    if (m_renderThreadViewCount >= m_numViewFamilies)
    {
        m_renderThreadViewCount = 0;
    }

}
void FRenderStreamStereoRenderDevice::copyToIntermediateBuffer_RenderThread(FRHICommandListImmediate& RHICmdList, FTexture2DRHIRef& Src, FTexture2DRHIRef& Dst)
{
    if (!Dst.IsValid() || Dst->GetSizeXY() != Src->GetSizeXY())
    {
        FRHIResourceCreateInfo createInfo;
        Dst = RHICreateTexture2D(Src->GetSizeX(),
            Src->GetSizeY(),
            Src->GetFormat(),
            Src->GetNumMips(),
            Src->GetNumSamples(),
            Src->GetFlags(),
            createInfo);
        FString name(Src->GetName().ToString() + " intermediate");
        Dst->SetName(FName(name));
        UE_LOG(LogRenderStreamStereoRendering, Verbose, TEXT("Intermediate texture created for %s"), *Src->GetName().ToString());
    }

    if (RHICmdList.IsOutsideRenderPass())
    {
        FRHICopyTextureInfo copyInfo;
        uint32 sizeX = Src->GetSizeX() / m_numViewFamilies;
        uint32 sizeY = Src->GetSizeY();
        copyInfo.SourcePosition = FIntVector(0, 0, 0);
        copyInfo.DestPosition = FIntVector(sizeX * m_renderThreadViewCount, 0, 0);
        copyInfo.Size = FIntVector(sizeX, sizeY, 1);
        RHICmdList.CopyTexture(Src, Dst, copyInfo);
    }
    else
    {
        UE_LOG(LogRenderStreamStereoRendering, Verbose, TEXT("Copying %s into %s"), *Src->GetName().ToString(), *Dst->GetName().ToString());
        FResolveParams params;
        uint32 sizeX = Src->GetSizeX() / m_numViewFamilies;
        uint32 sizeY = Src->GetSizeY();
        params.Rect = FResolveRect(FIntRect(FIntPoint(0, 0), FIntPoint(sizeX, sizeY)));
        params.DestRect = FResolveRect(FIntRect(FIntPoint(sizeX * m_renderThreadViewCount, 0), FIntPoint(sizeX * m_renderThreadViewCount + sizeX, sizeY)));
        RHICmdList.CopyToResolveTarget(Src, Dst, params);
    }
}

void FRenderStreamStereoRenderDevice::RenderTexture_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* BackBuffer, FRHITexture2D* SrcTexture, FVector2D WindowSize) const
{
    check(IsInRenderingThread());

    const FIntPoint SrcSize = SrcTexture->GetSizeXY();
    const FIntPoint BBSize = BackBuffer->GetSizeXY();

    UE_LOG(LogRenderStreamStereoRendering, Verbose, TEXT("Back Buffer Size: [%dx%d]"), BBSize.X, BBSize.Y);

    // Calculate sub regions to copy
    const int SubSrcSizeX = SrcSize.X / (m_numViewFamilies * 2);

    FResolveParams copyParamsResolve;
    copyParamsResolve.DestArrayIndex = 0;
    copyParamsResolve.SourceArrayIndex = 0;
    copyParamsResolve.Rect.X1 = 0;
    copyParamsResolve.Rect.Y1 = 0;
    copyParamsResolve.Rect.X2 = BBSize.X;
    copyParamsResolve.Rect.Y2 = BBSize.Y;
    copyParamsResolve.DestRect.X1 = 0;
    copyParamsResolve.DestRect.Y1 = 0;
    copyParamsResolve.DestRect.X2 = BBSize.X;
    copyParamsResolve.DestRect.Y2 = BBSize.Y;

    UE_LOG(LogRenderStreamStereoRendering, Verbose, TEXT("CopyToResolveTarget: [%d,%d - %d,%d] -> [%d,%d - %d,%d]"),
        copyParamsResolve.Rect.X1, copyParamsResolve.Rect.Y1, copyParamsResolve.Rect.X2, copyParamsResolve.Rect.Y2,
        copyParamsResolve.DestRect.X1, copyParamsResolve.DestRect.Y1, copyParamsResolve.DestRect.X2, copyParamsResolve.DestRect.Y2);

    RHICmdList.CopyToResolveTarget(SrcTexture, BackBuffer, copyParamsResolve);

    for (FDisplayClusterRenderViewport& Viewport : RenderViewports)
    {
        FRenderStreamProjectionPolicy* RSPolicy = static_cast<FRenderStreamProjectionPolicy*>(Viewport.GetProjectionPolicy().Get());
        if (RSPolicy)
        {
            RSPolicy->SendEnhancedContent_RenderThread(RHICmdList, SrcTexture, m_intermediateDepth, m_intermediateWorldNormals,
                m_intermediateAlbedoAO, m_intermediateDistortion, Viewport.GetRect());
        }
        else
        {
            UE_LOG(LogRenderStreamStereoRendering, Verbose, TEXT("Unable to cast to RenderStreamProjectionPolicy"));
        }
    }
}




///////////////////////////////////////////////////////////////////////
// Copy-pasta from DisplayClusterDeviceBase and stereo rendering setup
///////////////////////////////////////////////////////////////////////

void FRenderStreamStereoRenderDevice::SetViewportCamera(const FString& InCameraId, const FString& InViewportId)
{
	// Assign to all viewports if camera ID is empty (default camera will be used by all viewports)
	if (InViewportId.IsEmpty())
	{
		for (FDisplayClusterRenderViewport& Viewport : RenderViewports)
		{
			Viewport.SetCameraId(InCameraId);
		}

		UE_LOG(LogRenderStreamStereoRendering, Log, TEXT("Camera '%s' was assigned to all viewports"), *InCameraId);

		return;
	}

	// Ok, we have a request for a particular viewport. Let's find it.
	FDisplayClusterRenderViewport* const DesiredViewport = RenderViewports.FindByPredicate([InViewportId](const FDisplayClusterRenderViewport& ItemViewport)
		{
			return InViewportId.Equals(ItemViewport.GetId(), ESearchCase::IgnoreCase);
		});

	// Check if requested viewport exists
	if (!DesiredViewport)
	{
		UE_LOG(LogRenderStreamStereoRendering, Warning, TEXT("Couldn't assign '%s' camera. Viewport '%s' not found"), *InCameraId, *InViewportId);
		return;
	}

	// Update if found
	DesiredViewport->SetCameraId(InCameraId);

	UE_LOG(LogRenderStreamStereoRendering, Log, TEXT("Camera '%s' was assigned to '%s' viewport"), *InCameraId, *InViewportId);
}

void FRenderStreamStereoRenderDevice::SetStartPostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& StartPostProcessingSettings)
{
	for (int ViewportIndex = 0; ViewportIndex < RenderViewports.Num(); ViewportIndex++)
	{
		if (RenderViewports[ViewportIndex].GetId() == ViewportID)
		{
			ViewportStartPostProcessingSettings.Emplace(ViewportIndex, StartPostProcessingSettings);
			break;
		}
	}
}

void FRenderStreamStereoRenderDevice::SetOverridePostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& OverridePostProcessingSettings, float BlendWeight)
{
	for (int ViewportIndex = 0; ViewportIndex < RenderViewports.Num(); ViewportIndex++)
	{
		if (RenderViewports[ViewportIndex].GetId() == ViewportID)
		{
			FOverridePostProcessingSettings OverrideSettings;
			OverrideSettings.BlendWeight = BlendWeight;
			OverrideSettings.PostProcessingSettings = OverridePostProcessingSettings;
			ViewportOverridePostProcessingSettings.Emplace(ViewportIndex, OverrideSettings);
			break;
		}
	}
}

void FRenderStreamStereoRenderDevice::SetFinalPostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& FinalPostProcessingSettings)
{
	for (int ViewportIndex = 0; ViewportIndex < RenderViewports.Num(); ViewportIndex++)
	{
		if (RenderViewports[ViewportIndex].GetId() == ViewportID)
		{
			ViewportFinalPostProcessingSettings.Emplace(ViewportIndex, FinalPostProcessingSettings);
			break;
		}
	}
}

bool FRenderStreamStereoRenderDevice::GetViewportRect(const FString& InViewportID, FIntRect& OutRect)
{
	// look in render viewports
	FDisplayClusterRenderViewport* DesiredViewport = RenderViewports.FindByPredicate([InViewportID](const FDisplayClusterRenderViewport& ItemViewport)
		{
			return InViewportID.Equals(ItemViewport.GetId(), ESearchCase::IgnoreCase);
		});

	if (!DesiredViewport)
	{
		return false;
	}

	OutRect = DesiredViewport->GetRect();

	return true;
}

bool FRenderStreamStereoRenderDevice::GetViewportProjectionPolicy(const FString& InViewportID, TSharedPtr<IDisplayClusterProjectionPolicy>& OutProjectionPolicy)
{
	// Ok, we have a request for a particular viewport. Let's find it.
	FDisplayClusterRenderViewport* const DesiredViewport = RenderViewports.FindByPredicate([InViewportID](const FDisplayClusterRenderViewport& ItemViewport)
		{
			return InViewportID.Compare(ItemViewport.GetId(), ESearchCase::IgnoreCase) == 0;
		});

	// Request data if found
	if (DesiredViewport)
	{
		OutProjectionPolicy = DesiredViewport->GetProjectionPolicy();
		return true;
	}

	return false;
}

bool FRenderStreamStereoRenderDevice::GetViewportContext(const FString& InViewportID, int ViewIndex, FDisplayClusterRenderViewContext& OutViewContext)
{
	// Ok, we have a request for a particular viewport context. Let's find it.
	FDisplayClusterRenderViewport* const DesiredViewport = RenderViewports.FindByPredicate([InViewportID](const FDisplayClusterRenderViewport& ItemViewport)
		{
			return InViewportID.Compare(ItemViewport.GetId(), ESearchCase::IgnoreCase) == 0;
		});

	// Request data if found
	if (DesiredViewport)
	{
		OutViewContext = DesiredViewport->GetContext(ViewIndex);
		return true;
	}

	return false;
}

bool FRenderStreamStereoRenderDevice::SetBufferRatio(const FString& InViewportID, float InBufferRatio)
{
	// Ok, we have a request for a particular viewport. Let's find it.
	FDisplayClusterRenderViewport* const DesiredViewport = RenderViewports.FindByPredicate([InViewportID](const FDisplayClusterRenderViewport& ItemViewport)
		{
			return InViewportID.Equals(ItemViewport.GetId(), ESearchCase::IgnoreCase);
		});

	// Update if found
	if (!DesiredViewport)
	{
		return false;
	}

	UE_LOG(LogRenderStreamStereoRendering, Log, TEXT("Set buffer ratio %f for viewport '%s'"), InBufferRatio, *InViewportID);
	DesiredViewport->SetBufferRatio(InBufferRatio);
	return true;
}

bool FRenderStreamStereoRenderDevice::GetBufferRatio(const FString& InViewportID, float& OutBufferRatio) const
{
	// Ok, we have a request for a particular viewport. Let's find it.
	const FDisplayClusterRenderViewport*  DesiredViewport = RenderViewports.FindByPredicate([InViewportID](const FDisplayClusterRenderViewport& ItemViewport)
		{
			return InViewportID.Equals(ItemViewport.GetId(), ESearchCase::IgnoreCase);
		});

	// Request data if found
	if (!DesiredViewport)
	{
		return false;
	}

	OutBufferRatio = DesiredViewport->GetBufferRatio();
	UE_LOG(LogRenderStreamStereoRendering, Log, TEXT("Viewport '%s' has buffer ratio %f"), *InViewportID, OutBufferRatio);
	return true;
}

bool FRenderStreamStereoRenderDevice::SetBufferRatio(int32 ViewportIdx, float InBufferRatio)
{
	if (!RenderViewports.IsValidIndex(ViewportIdx))
	{
		return false;
	}

	UE_LOG(LogRenderStreamStereoRendering, Log, TEXT("Set buffer ratio %f for viewport index '%d'"), InBufferRatio, ViewportIdx);
	RenderViewports[ViewportIdx].SetBufferRatio(InBufferRatio);
	return true;
}

bool FRenderStreamStereoRenderDevice::GetBufferRatio(int32 ViewportIdx, float& OutBufferRatio) const
{
	if (!RenderViewports.IsValidIndex(ViewportIdx))
	{
		return false;
	}

	OutBufferRatio = RenderViewports[ViewportIdx].GetBufferRatio();
	UE_LOG(LogRenderStreamStereoRendering, Log, TEXT("Viewport with index %d has buffer ratio %f"), ViewportIdx, OutBufferRatio);
	return true;
}



const FDisplayClusterRenderViewport* FRenderStreamStereoRenderDevice::GetRenderViewport(int32 ViewportIdx) const
{
	if (!RenderViewports.IsValidIndex(ViewportIdx))
	{
		return nullptr;
	}

	return &(RenderViewports[ViewportIdx]);
}

const void FRenderStreamStereoRenderDevice::GetRenderViewports(TArray<FDisplayClusterRenderViewport>& OutViewports) const
{
	OutViewports = RenderViewports;
}

uint32 FRenderStreamStereoRenderDevice::GetViewsAmountPerViewport() const
{
	return ViewsAmountPerViewport;
}

bool FRenderStreamStereoRenderDevice::IsStereoEnabled() const
{
	return m_isStereoEnabled;
}

bool FRenderStreamStereoRenderDevice::IsStereoEnabledOnNextFrame() const
{
	return IsStereoEnabled();
}

bool FRenderStreamStereoRenderDevice::EnableStereo(bool stereo)
{
	m_isStereoEnabled = stereo;
	return m_isStereoEnabled;
}

FMatrix FRenderStreamStereoRenderDevice::GetStereoProjectionMatrix(const EStereoscopicPass StereoPassType) const
{
	
	
	const int ViewportIndex = GetViewIndexForPass(StereoPassType) / ViewsAmountPerViewport;
	const int ViewIdx = GetViewIndexForPass(StereoPassType) % ViewsAmountPerViewport;

	// Current viewport data
	FMatrix outMatrix;
	FDisplayClusterRenderViewport& RenderViewport = RenderViewports[ViewportIndex];
	if (RenderViewports[ViewportIndex].GetProjectionPolicy()->GetProjectionMatrix(0, outMatrix))
	{
		return outMatrix;
	}
	
	else
	{
		UE_LOG(LogRenderStreamStereoRendering, Warning, TEXT("Unable to get projection matrix, using default instead."));

		const float HalfFov = FMath::DegreesToRadians(90.f) / 2.f;
		const float InWidth = 1920;
		const float InHeight = 1080;
		const float XS = 1.0f / FMath::Tan(HalfFov);
		const float YS = InWidth / FMath::Tan(HalfFov) / InHeight;
		const float NearZ = GNearClippingPlane;

		return FMatrix(
			FPlane(XS, 0.0f, 0.0f, 0.0f),
			FPlane(0.0f, YS, 0.0f, 0.0f),
			FPlane(0.0f, 0.0f, 0.0f, 1.0f),
			FPlane(0.0f, 0.0f, NearZ, 0.0f));
	}
}

void FRenderStreamStereoRenderDevice::AdjustViewRect(EStereoscopicPass StereoPass, int32& X, int32& Y, uint32& SizeX, uint32& SizeY) const
{
    const int ViewportIndex = GetViewIndexForPass(StereoPass) / ViewsAmountPerViewport;
    const uint32 ViewIndex = GetViewIndexForPass(StereoPass) % ViewsAmountPerViewport;

    // Current viewport data
    FDisplayClusterRenderViewport& RenderViewport = RenderViewports[ViewportIndex];

    // Provide the Engine with a viewport rectangle
    const FIntRect& ViewportRect = RenderViewport.GetRect();
    X = ViewportRect.Min.X;
    Y = ViewportRect.Min.Y;
    SizeX = ViewportRect.Width();
    SizeY = ViewportRect.Height();

    // Update view context
    FDisplayClusterRenderViewContext& ViewContext = RenderViewport.GetContext(ViewIndex);
    ViewContext.RenderTargetRect = FIntRect(X, Y, X + SizeX, Y + SizeY);

	UE_LOG(LogRenderStreamStereoRendering, Verbose, TEXT("AdjustViewRect: Origin: [%d, %d] Size: [%d, %d]"), X, Y, SizeX, SizeY)
}
void FRenderStreamStereoRenderDevice::SetFinalViewRect(const EStereoscopicPass StereoPass, const FIntRect& FinalViewRect)
{
}

void FRenderStreamStereoRenderDevice::InitCanvasFromView(FSceneView* InView, UCanvas* Canvas)
{
}

IStereoRenderTargetManager* FRenderStreamStereoRenderDevice::GetRenderTargetManager()
{
	return this;
}

int32 FRenderStreamStereoRenderDevice::GetDesiredNumberOfViews(bool bStereoRequested) const
{
	return bStereoRequested ? 2 : 1;
}

EStereoscopicPass FRenderStreamStereoRenderDevice::GetViewPassForIndex(bool bStereoRequested, uint32 ViewIndex) const
{
	if (!bStereoRequested)
		return EStereoscopicPass::eSSP_FULL;

	switch (ViewIndex)
	{
	case 0:
		return EStereoscopicPass::eSSP_LEFT_EYE;
	case 1:
		return EStereoscopicPass::eSSP_RIGHT_EYE;
	case 2:
		return EStereoscopicPass::eSSP_LEFT_EYE_SIDE;
	case 3:
		return EStereoscopicPass::eSSP_RIGHT_EYE_SIDE;
	default:
		return EStereoscopicPass::eSSP_FULL;
	}
}

uint32 FRenderStreamStereoRenderDevice::GetViewIndexForPass(EStereoscopicPass StereoPassType) const
{
	switch (StereoPassType)
	{
	case eSSP_FULL:
	case eSSP_LEFT_EYE:
		return 0;
	case eSSP_RIGHT_EYE:
		return 1;
	case eSSP_LEFT_EYE_SIDE:
		return 2;
	case eSSP_RIGHT_EYE_SIDE:
		return 3;
	default:
		check(0);
		return -1;
	}
}

bool IsPrimary(EStereoscopicPass Pass)
{
	return Pass == EStereoscopicPass::eSSP_FULL || Pass == EStereoscopicPass::eSSP_LEFT_EYE;
}

bool FRenderStreamStereoRenderDevice::DeviceIsAPrimaryView(const FSceneView& View)
{
    return true;// DeviceIsAPrimaryPass(View.StereoPass);
}

bool FRenderStreamStereoRenderDevice::DeviceIsAPrimaryPass(EStereoscopicPass Pass)
{
    return true;// IsPrimary(Pass);
}

bool FRenderStreamStereoRenderDevice::DeviceIsASecondaryView(const FSceneView& View)
{
    return false; //!DeviceIsAPrimaryView(View);
}

bool FRenderStreamStereoRenderDevice::DeviceIsASecondaryPass(EStereoscopicPass Pass)
{
    return false;// !DeviceIsAPrimaryPass(Pass);
}

bool FRenderStreamStereoRenderDevice::DeviceIsAnAdditionalView(const FSceneView& View)
{
    return false;// View.StereoPass > EStereoscopicPass::eSSP_RIGHT_EYE;
}

bool FRenderStreamStereoRenderDevice::DeviceIsAnAdditionalPass(EStereoscopicPass Pass)
{
    return false;// Pass > EStereoscopicPass::eSSP_RIGHT_EYE;
}



bool FRenderStreamStereoRenderDevice::ShouldUseSeparateRenderTarget() const
{
	return true;
}


void FRenderStreamStereoRenderDevice::UpdateViewport(bool bUseSeparateRenderTarget, const FViewport& Viewport, SViewport* ViewportWidget)
{
	check(IsInGameThread());

	// Store viewport
	if (!MainViewport)
	{
		// UE viewport
		MainViewport = (FViewport*)&Viewport;

		// Create texture share for render viewports by config line flag
		//@todo move to right place. add on\off
		{
			// Share viewports to external apps
			for (int ViewportIndex = 0; ViewportIndex < RenderViewports.Num(); ViewportIndex++)
			{
				if (RenderViewports[ViewportIndex].IsShared())
				{
					static ITextureShare& TextureShareAPI = ITextureShare::Get();

					//@todo: add custom sync setup
					FTextureShareSyncPolicy SyncPolicy;

					FString ShareName = RenderViewports[ViewportIndex].GetId();
					EStereoscopicPass PassType = GetViewPassForIndex(IsStereoEnabled(), ViewportIndex);

					// Create shared resource for external app
					if (!TextureShareAPI.CreateShare(ShareName, SyncPolicy, ETextureShareProcess::Server))
					{
						UE_LOG(LogRenderStreamStereoRendering, Error, TEXT("Failed create viewport share '%s'"), *ShareName);
					}
					else
					{
						// Find viewport stereoscopic pass
						int ResourceViewportIndex = RenderViewports.Num() - 1;

						// Initialize render callbacks
						TSharedPtr<ITextureShareItem> ShareItem;
						if (TextureShareAPI.GetShare(ShareName, ShareItem))
						{
							if (TextureShareAPI.LinkSceneContextToShare(ShareItem, PassType, true))
							{
								// Map viewport rect to stereoscopic pass
								TextureShareAPI.SetBackbufferRect(PassType, &RenderViewports[ViewportIndex].GetRect());
								// Begin share session
								ShareItem->BeginSession();
							}
							else
							{
								TextureShareAPI.ReleaseShare(ShareName);
								UE_LOG(LogRenderStreamStereoRendering, Error, TEXT("failed link scene conext for share '%s'"), *ShareName);
							}
						}
					}
				}
			}
		}
	}

	// Pass UpdateViewport to all PP operations
	TMap<FString, IDisplayClusterRenderManager::FDisplayClusterPPInfo> PPOperationsMap = IDisplayCluster::Get().GetRenderMgr()->GetRegisteredPostprocessOperations();
	for (auto& it : PPOperationsMap)
	{
		it.Value.Operation->PerformUpdateViewport(Viewport, RenderViewports);
	}
}

void FRenderStreamStereoRenderDevice::CalculateRenderTargetSize(const FViewport& Viewport, uint32& InOutSizeX, uint32& InOutSizeY)
{
	check(IsInGameThread());

	InOutSizeX = Viewport.GetSizeXY().X;
	InOutSizeY = Viewport.GetSizeXY().Y;

	UE_LOG(LogRenderStreamStereoRendering, Verbose, TEXT("Render target size: [%d x %d]"), InOutSizeX, InOutSizeY);

	check(InOutSizeX > 0 && InOutSizeY > 0);

}

bool FRenderStreamStereoRenderDevice::NeedReAllocateViewportRenderTarget(const FViewport& Viewport)
{
	check(IsInGameThread());

	// Get current RT size
	const FIntPoint rtSize = Viewport.GetRenderTargetTextureSizeXY();

	// Get desired RT size
	uint32 newSizeX = 0;
	uint32 newSizeY = 0;
	CalculateRenderTargetSize(Viewport, newSizeX, newSizeY);

	// Here we conclude if need to re-allocate
	const bool Result = (newSizeX != rtSize.X || newSizeY != rtSize.Y);

	UE_LOG(LogRenderStreamStereoRendering, Verbose, TEXT("Is reallocate viewport render target needed: %d"), Result ? 1 : 0);

	if (Result)
	{
		UE_LOG(LogRenderStreamStereoRendering, Verbose, TEXT("Need to re-allocate render target: cur %d:%d, new %d:%d"), rtSize.X, rtSize.Y, newSizeX, newSizeY);
	}

	return Result;
}


// Use this function to set the position of the additional views relative to the original view (LEFT_EYE).
void FRenderStreamStereoRenderDevice::CalculateStereoViewOffset(const EStereoscopicPass StereoPassType, FRotator& ViewRotation, const float InWorldToMeters, FVector& ViewLocation)
{
	
    UE_LOG(LogRenderStreamStereoRendering, Verbose, TEXT("StereoViewOffset for pass: %d"), StereoPassType);
	int ViewIndex = GetViewIndexForPass(StereoPassType) % ViewsAmountPerViewport;
	int ViewportIndex = GetViewIndexForPass(StereoPassType) / ViewsAmountPerViewport;


	float CfgNCP = 1.f;
	FVector ViewOffset;

	if (!RenderViewports[ViewportIndex].GetProjectionPolicy()->CalculateView(ViewIndex, ViewLocation, ViewRotation, ViewOffset, InWorldToMeters, CfgNCP, CfgNCP))
	{
		UE_LOG(LogRenderStreamStereoRendering, Warning, TEXT("Couldn't compute view parameters for Viewport %s(%d), ViewIdx: %d"), *RenderViewports[ViewportIndex].GetId(), ViewportIndex, int(ViewIndex));
	}

}


void FRenderStreamStereoRenderDevice::AddViewport(
	const FString& InViewportId,
	const FIntPoint& InViewportLocation,
	const FIntPoint& InViewportSize,
	TSharedPtr<IDisplayClusterProjectionPolicy> InProjPolicy,
	const FString& InCameraId,
	float InBufferRatio /* = 1.f */,
	int GPUIndex /*= INDEX_NONE */,
	bool bAllowCrossGPUTransfer /*= true*/,
	bool bIsShared /*= false*/)
{
	FScopeLock Lock(&InternalsSyncScope);

	// Check viewport ID
	if (InViewportId.IsEmpty())
	{
		UE_LOG(LogRenderStreamStereoRendering, Warning, TEXT("Wrong viewport ID"));
		return;
	}

	// Check if a viewport with the same ID already exists
	const bool bAlreadyExists = (nullptr != RenderViewports.FindByPredicate([InViewportId](const FDisplayClusterRenderViewport& ItemViewport)
		{
			return ItemViewport.GetId().Equals(InViewportId, ESearchCase::IgnoreCase);
		}));

	// ID must be unique
	if (bAlreadyExists)
	{
		UE_LOG(LogRenderStreamStereoRendering, Warning, TEXT("Viewport '%s' already exists"), *InViewportId);
		return;
	}

	// Create viewport
	FIntRect ViewportRect = FIntRect(InViewportLocation, InViewportLocation + InViewportSize);
	FDisplayClusterRenderViewport NewViewport(InViewportId, ViewportRect, InProjPolicy, EDisplayClusterEyeType::COUNT, InCameraId, InBufferRatio, bAllowCrossGPUTransfer, GPUIndex, bIsShared);

	// Make sure everything is good and the projection policy instance is initialized properly
	if (InProjPolicy.IsValid() && InProjPolicy->HandleAddViewport(InViewportSize, ViewsAmountPerViewport))
	{
		UE_LOG(LogRenderStreamStereoRendering, Verbose, TEXT("A corresponded projection policy object has initialized the viewport '%s'"), *InViewportId);
		// Store viewport instance
		RenderViewports.Add(NewViewport);
	}
}

const FDisplayClusterRenderViewport* FRenderStreamStereoRenderDevice::GetRenderViewport(const FString& ViewportId) const
{
    // Ok, we have a request for a particular viewport. Let's find it.
    const FDisplayClusterRenderViewport* DesiredViewport = RenderViewports.FindByPredicate([ViewportId](const FDisplayClusterRenderViewport& ItemViewport)
        {
            return ViewportId.Equals(ItemViewport.GetId(), ESearchCase::IgnoreCase);
        });

    return DesiredViewport;
}

FRHICustomPresent* FRenderStreamStereoRenderDevice::CreatePresentationObject(FViewport* const Viewport, TSharedPtr<IDisplayClusterRenderSyncPolicy>& SyncPolicy)
{
	return nullptr;
}

