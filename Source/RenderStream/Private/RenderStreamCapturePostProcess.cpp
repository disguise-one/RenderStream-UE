#pragma once

#include "RenderStreamCapturePostProcess.h"
#include "SceneRenderTargetParameters.h"

#include "DisplayClusterConfigurationTypes_Base.h"
#include "FrameStream.h"
#include "IDisplayCluster.h"
#include "RenderStream.h"
#include "RenderStreamProjectionPolicy.h"
#include "Render/Viewport/IDisplayClusterViewportManager.h"
#include "Render/Viewport/IDisplayClusterViewportProxy.h"

#include "SceneRendering.h"

class UCameraComponent;
class UWorld;
class FRenderStreamModule;

DEFINE_LOG_CATEGORY(LogRenderStreamPostProcess);

FString FRenderStreamCapturePostProcess::Type = TEXT("renderstream_capture");

FRenderStreamCapturePostProcess::FRenderStreamCapturePostProcess(const FString& PostProcessId, const struct FDisplayClusterConfigurationPostprocess* InConfigurationPostProcess)
    : Id(PostProcessId)
{
    if (!ResolvedSceneColorCallbackHandle.IsValid())
    {
        if (IRendererModule* RendererModule = FModuleManager::GetModulePtr<IRendererModule>(TEXT("Renderer")))
        {
            ResolvedSceneColorCallbackHandle = RendererModule->GetResolvedSceneColorCallbacks().AddRaw(this, &FRenderStreamCapturePostProcess::OnResolvedSceneColor_RenderThread);
        }
    }
}

FRenderStreamCapturePostProcess::~FRenderStreamCapturePostProcess() 
{
    if (ResolvedSceneColorCallbackHandle.IsValid())
    {
        if (IRendererModule* RendererModule = FModuleManager::GetModulePtr<IRendererModule>(TEXT("Renderer")))
        {
            RendererModule->GetResolvedSceneColorCallbacks().Remove(ResolvedSceneColorCallbackHandle);
        }

        ResolvedSceneColorCallbackHandle.Reset();
    }

}

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

    for (auto Viewport : InViewportManager->GetViewports())
    {
        m_extractedDepth.Add(Viewport->GetId(), TRefCountPtr<IPooledRenderTarget>());
        m_depthIds.Add(Viewport->GetId());
    }

    m_maxDepthBuffers = m_extractedDepth.Num();
    m_depthIndex = 0;

    const URenderStreamSettings* settings = GetDefault<URenderStreamSettings>();
    const bool encodeDepth = settings->AlphaEncoding == ERenderStreamAlphaEncoding::Depth;
    UE_LOG(LogRenderStreamPostProcess, Log, TEXT("Using Alpha Encoding %d"), settings->AlphaEncoding);
    m_EncodeDepth = true;
    
    return true;
}

void FRenderStreamCapturePostProcess::HandleEndScene(IDisplayClusterViewportManager* InViewportManager) 
{

}

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

        check(m_extractedDepth[ViewportId]);
        if(m_EncodeDepth)
            Stream->SendFrameWithDepth_RenderingThread(RHICmdList, frameResponse, Resources[0], m_extractedDepth[ViewportId]->GetRHI(), Rects[0]);
        else
            Stream->SendFrame_RenderingThread(RHICmdList, frameResponse, Resources[0], Rects[0]);
    }

    // Uncomment this to restore client display
    // InViewportProxy->ResolveResources(RHICmdList, EDisplayClusterViewportResourceType::InputShaderResource, InViewportProxy->GetOutputResourceType());
}

void FRenderStreamCapturePostProcess::OnResolvedSceneColor_RenderThread(FRDGBuilder& GraphBuilder, const FSceneTextures& SceneTextures)
{
    check(IsInRenderingThread());

    // Hack that relies on the viewports being rendered in order
    // TODO need to find a way of associating SceneTexture extraction to nDisplay Viewports - SceneViewExtension?
    // Access Violation on shutdown is being caused by https://d3technologies.atlassian.net/browse/RSP-186
    GraphBuilder.QueueTextureExtraction(SceneTextures.Depth.Resolve, &m_extractedDepth[m_depthIds[m_depthIndex++]]);
    if (m_depthIndex >= m_maxDepthBuffers) 
        m_depthIndex = 0;
}

void FRenderStreamCapturePostProcess::OnPostOpaqueDelegateCallback(FPostOpaqueRenderParameters& RenderParameters)
{
    //UE_LOG(LogTemp, Log, TEXT("%p"), RenderParameters.Uid);
    
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
