#pragma once

#include "RenderStreamCapturePostProcess.h"


#include "DisplayClusterConfigurationTypes_Base.h"
#include "FrameStream.h"
#include "IDisplayCluster.h"
#include "RenderStream.h"
#include "RenderStreamProjectionPolicy.h"
#include "Render/Viewport/IDisplayClusterViewportManager.h"
#include "Render/Viewport/IDisplayClusterViewportProxy.h"

#include "RenderStreamSceneViewExtension.h"

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
    FRenderStreamModule* Module = FRenderStreamModule::Get();
    check(Module);

    Module->LoadSchemas(*GWorld);

    const URenderStreamSettings* settings = GetDefault<URenderStreamSettings>();
    const bool encodeDepth = settings->AlphaEncoding == ERenderStreamAlphaEncoding::Depth;
    UE_LOG(LogRenderStreamPostProcess, Log, TEXT("Using Alpha Encoding %d"), settings->AlphaEncoding);
    if (encodeDepth)
    {
        ViewExtension = FRenderStreamSceneViewExtension::Create();
    }
    return true;
}
void FRenderStreamCapturePostProcess::HandleEndScene(IDisplayClusterViewportManager* InViewportManager) {}

void FRenderStreamCapturePostProcess::PerformPostProcessViewAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const IDisplayClusterViewportProxy* ViewportProxy) const
{
    if (ViewportProxy == nullptr)
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
        ViewportProxy->GetResourcesWithRects_RenderThread(EDisplayClusterViewportResourceType::InputShaderResource, Resources, Rects);
        check(Resources.Num() == 1);
        check(Rects.Num() == 1);
        
        FRHITexture* depth = nullptr;
        if (ViewExtension && ViewExtension->getExtractedDepth().IsValid())
        {
            depth = ViewExtension->getExtractedDepth()->GetRHI();
            check(depth);
        }
        Stream->SendFrame_RenderingThread(RHICmdList, frameResponse, Resources[0], depth, Rects[0]);
    }

    // Uncomment this to restore client display
    // InViewportProxy->ResolveResources(RHICmdList, EDisplayClusterViewportResourceType::InputShaderResource, InViewportProxy->GetOutputResourceType());
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
