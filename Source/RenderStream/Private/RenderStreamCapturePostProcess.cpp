#pragma once

#include "RenderStreamCapturePostProcess.h"


#include "DisplayClusterConfigurationTypes_Base.h"
#include "FrameStream.h"
#include "IDisplayCluster.h"
#include "RenderStream.h"
#include "RenderStreamProjectionPolicy.h"
#include "Render/Viewport/IDisplayClusterViewportManager.h"
#include "Render/Viewport/IDisplayClusterViewportProxy.h"

#include "Engine/World.h"

#include "OpenColorIORendering.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ScreenPass.h"

class UCameraComponent;
class UWorld;
class FRenderStreamModule;

DEFINE_LOG_CATEGORY(LogRenderStreamPostProcess);

FString FRenderStreamCapturePostProcess::Type = TEXT("renderstream_capture");

FRenderStreamCapturePostProcess::FRenderStreamCapturePostProcess(const FString& PostProcessId, const struct FDisplayClusterConfigurationPostprocess* InConfigurationPostProcess)
    : Id(PostProcessId)
{}

FRenderStreamCapturePostProcess::~FRenderStreamCapturePostProcess() {}

bool FRenderStreamCapturePostProcess::IsConfigurationChanged(const FDisplayClusterConfigurationPostprocess* InConfigurationPostprocess) const
{
    return false;
}

// Once we have access to FDisplayClusterProjectionModule or FDisplayClusterProjectionCameraPolicy
// we can do the work done in FRenderStreamProjectionPolicy HandleStartScene and HandleEndScene here.
bool FRenderStreamCapturePostProcess::HandleStartScene(IDisplayClusterViewportManager* InViewportManager)
{
    if (!IsInCluster()) {
        return false;
    }

    FRenderStreamModule* Module = FRenderStreamModule::Get();
    check(Module);

    Module->LoadSchemas(*GWorld);
    return true;
}

void FRenderStreamCapturePostProcess::HandleEndScene(IDisplayClusterViewportManager* InViewportManager) {}

void FRenderStreamCapturePostProcess::PerformPostProcessViewAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const IDisplayClusterViewportProxy* ViewportProxy) const
{
    if (!IsInCluster() || ViewportProxy == nullptr)
    {
        return;
    }

    auto ViewportId = ViewportProxy->GetId();
    FRenderStreamModule* Module = FRenderStreamModule::Get();
    check(Module);

    auto Stream = Module->StreamPool->GetStream(ViewportId);
    // We can't create a stream on the render thread, so our only option is to not do anything if the stream doesn't exist here.
    if (Stream)
    {
        auto Size = ViewportProxy->GetRenderSettings_RenderThread().Rect.Size();
        if (Size.GetMin() <= 0)
        {
            auto Resolution = Stream->Resolution();
            UE_LOG(LogRenderStream, Error, TEXT("Viewport of zero size detected in '%s : %s' with id '%s' %dx%d, expected size %dx%d"),
                *Stream->Name(), *Stream->Channel(), *ViewportId, Size.X, Size.Y, Resolution.X, Resolution.Y);
            return;
        }

        auto& Info = Module->GetViewportInfo(ViewportId);
        RenderStreamLink::CameraResponseData frameResponse;
        {
            std::lock_guard<std::mutex> guard(Info.m_frameResponsesLock);
            if (Info.m_frameResponsesMap.count(GFrameCounterRenderThread)) // Check current frame data exists
            {
                frameResponse = Info.m_frameResponsesMap[GFrameCounterRenderThread];
                Info.m_frameResponsesMap.erase(GFrameCounterRenderThread);
            }
            else
            {
                // default values to avoid any math assertions in debug dlls
                frameResponse.camera.nearZ = 0.1f;
                frameResponse.camera.farZ = 1.f;
                frameResponse.camera.sensorX = 1.f;
                frameResponse.camera.sensorY = 1.f;
                frameResponse.camera.focalLength = 1.f;
            }
        }

        TArray<FRHITexture*> Resources;
        TArray<FIntRect> Rects;
        // NOTE: If you get a black screen on the stream when updating the plugin to a new unreal version try changing the EDisplayClusterViewportResourceType enum.
        EDisplayClusterViewportResourceType resourceType = EDisplayClusterViewportResourceType::InputShaderResource;
        const FString policyType = ViewportProxy->GetProjectionPolicy_RenderThread()->GetType();
        if (policyType != FRenderStreamProjectionPolicy::RenderStreamPolicyType)
        {
            resourceType = EDisplayClusterViewportResourceType::AdditionalTargetableResource;
        }
        ViewportProxy->GetResourcesWithRects_RenderThread(resourceType, Resources, Rects);
        if (Resources.Num() != 1 || Rects.Num() != 1)
        {
            UE_LOG(LogRenderStream, Error, TEXT("Missing viewport output in '%s : %s' with id '%s'"), *Stream->Name(), *Stream->Channel(), *ViewportId);
            return;
        }

        // Apply OCIO manually if resources are cached
        if (Module->CachedOCIOResources.IsValid())
        {
            FTextureRHIRef& OutputTex = Module->OCIOOutputTextures.FindOrAdd(ViewportId);
            FIntRect OutputRect;
            ApplyOCIOTransform(RHICmdList, Resources[0], Rects[0], OutputTex, OutputRect);

            Stream->SendFrame_RenderingThread(RHICmdList, frameResponse, OutputTex, OutputRect);
        }
        else
        {
            Stream->SendFrame_RenderingThread(RHICmdList, frameResponse, Resources[0], Rects[0]);
        }
    }

    // Uncomment this to restore client display
    // InViewportProxy->ResolveResources(RHICmdList, EDisplayClusterViewportResourceType::InputShaderResource, InViewportProxy->GetOutputResourceType());
}

void FRenderStreamCapturePostProcess::ApplyOCIOTransform(FRHICommandListImmediate& RHICmdList, FRHITexture* Resource, const FIntRect& Rect, FTextureRHIRef& OutputTex, FIntRect& OutputRect) const
{
    FRenderStreamModule* Module = FRenderStreamModule::Get();
    check(Module);

    const FIntPoint OutputSize(Rect.Width(), Rect.Height());

    if (!OutputTex.IsValid() ||
        OutputTex->GetSizeX() != (uint32)OutputSize.X ||
        OutputTex->GetSizeY() != (uint32)OutputSize.Y)
    {
        const FRHITextureCreateDesc Desc =
            FRHITextureCreateDesc::Create2D(TEXT("RSCaptureOCIOOutput"))
            .SetExtent(OutputSize.X, OutputSize.Y)
            .SetFormat(PF_FloatRGBA)
            .SetFlags(ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::ShaderResource)
            .SetClearValue(FClearValueBinding::Black);
        OutputTex = RHICreateTexture(Desc);
    }

    FRDGBuilder GraphBuilder(RHICmdList);

    FRDGTextureRef ShaderInput = GraphBuilder.RegisterExternalTexture(
        CreateRenderTarget(Resource, TEXT("RSCaptureOCIOInput")));
    FRDGTextureRef ShaderOutput = GraphBuilder.RegisterExternalTexture(
        CreateRenderTarget(OutputTex, TEXT("RSCaptureOCIOOutput")));

    FScreenPassTexture Input(ShaderInput, Rect);
    OutputRect = FIntRect(FIntPoint::ZeroValue, OutputSize);
    FScreenPassRenderTarget Output(ShaderOutput, OutputRect, ERenderTargetLoadAction::EClear);

    // In UE5.6+ if r.DefaultBackBufferPixelFormat=3 then the EngineDisplayGamma is set to 1.0, which causes InternalRenderTarget to get a linear encoding
    // This a mismatch from the InputShaderResource  which it resolves to. It does a conversion (pow(input, 1/2.2)) before the copy
    // The OCIO shader applies pow(input, gamma) before the transform so we make it 2.2 to undo the previous transform
    // r.DefaultBackBufferPixelFormat is hardcoded in d3 to always be set to 3 so we don't need to worry about supporting other values
    const float OCIOGamma = 2.2f;

    FOpenColorIORendering::AddPass_RenderThread(
        GraphBuilder,
        FScreenPassViewInfo(),
        Module->CachedOCIOFeatureLevel,
        Input,
        Output,
        Module->CachedOCIOResources,
        OCIOGamma);

    GraphBuilder.Execute();
}

FRenderStreamPostProcessFactory::BasePostProcessPtr FRenderStreamPostProcessFactory::Create(
    const FString& PostProcessId, const struct FDisplayClusterConfigurationPostprocess* InConfigurationPostProcess)
{
    UE_LOG(LogRenderStreamPostProcess, Log, TEXT("Instantiating post process <%s>..."), *InConfigurationPostProcess->Type);

    if (!InConfigurationPostProcess->Type.Compare(FRenderStreamPostProcessFactory::RenderStreamPostProcessType, ESearchCase::IgnoreCase))
    {
        PostProcessPtr Result = MakeShareable(new FRenderStreamCapturePostProcess(PostProcessId, InConfigurationPostProcess));
        return StaticCastSharedPtr<IDisplayClusterPostProcess>(Result);
    }

    return nullptr;
}
