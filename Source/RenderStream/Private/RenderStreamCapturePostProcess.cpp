#pragma once

#include "RenderStreamCapturePostProcess.h"
#include "SceneRenderTargetParameters.h"

#include "DisplayClusterConfigurationTypes_Base.h"
#include "FrameStream.h"
#include "IDisplayCluster.h"
#include "IDisplayClusterCallbacks.h"
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
    if (!IsInCluster()) 
    {
        return false;
    }

    FRenderStreamModule* Module = FRenderStreamModule::Get();
    check(Module);

    Module->LoadSchemas(*GWorld);

    ViewportManager = InViewportManager;

    if (!StreamsChangedDelegateHandle.IsValid())
    {
        StreamsChangedDelegateHandle = Module->OnStreamsChangedDelegate.AddRaw(this, &FRenderStreamCapturePostProcess::RebuildDepthExtractionTable);
    }
    
    if (!DisplayClusterPostBackBufferUpdateHandle.IsValid())
    {
        DisplayClusterPostBackBufferUpdateHandle = IDisplayCluster::Get().GetCallbacks().OnDisplayClusterPostBackbufferUpdate_RenderThread().AddRaw(this, &FRenderStreamCapturePostProcess::OnDisplayClusterPostBackbufferUpdate_RenderThread);
    }

    RebuildDepthExtractionTable();
    
    return true;
}

void FRenderStreamCapturePostProcess::HandleEndScene(IDisplayClusterViewportManager* InViewportManager) 
{
    if (StreamsChangedDelegateHandle.IsValid())
    {
        if (FRenderStreamModule* Module = FRenderStreamModule::Get(); Module)
        {
            Module->OnStreamsChangedDelegate.Remove(StreamsChangedDelegateHandle);
        }

        StreamsChangedDelegateHandle.Reset();
    }

    if (IsInCluster() && DisplayClusterPostBackBufferUpdateHandle.IsValid())
    {
        IDisplayCluster::Get().GetCallbacks().OnDisplayClusterPostBackbufferUpdate_RenderThread().Remove(DisplayClusterPostBackBufferUpdateHandle);
    }
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

        if (m_EncodeDepth)
        {
            check(m_extractedDepth[ViewportId]);
        }

        Stream->SendFrame_RenderingThread(RHICmdList, frameResponse, Resources[0], 
            m_EncodeDepth ? m_extractedDepth[ViewportId]->GetRHI() : nullptr, 
            Rects[0]
        );
    }

    // Uncomment this to restore client display
    // InViewportProxy->ResolveResources(RHICmdList, EDisplayClusterViewportResourceType::InputShaderResource, InViewportProxy->GetOutputResourceType());
}

void FRenderStreamCapturePostProcess::HandleBeginNewFrame(IDisplayClusterViewportManager* InViewportManager, FDisplayClusterRenderFrame& InOutRenderFrame)
{
    ViewportIdOrdering OrderThisFrame;
    for (auto&& Target : InOutRenderFrame.RenderTargets)
    {
        for (auto&& Family : Target.ViewFamilies)
        {
            for (auto&& View : Family.Views)
            {
                check(View.Viewport);
                OrderThisFrame.Add(View.Viewport->GetId());
            }
        }
    }
    Algo::Reverse(OrderThisFrame);
    ViewportIdOrderPerFrame.Enqueue(OrderThisFrame);
}

void FRenderStreamCapturePostProcess::RebuildDepthExtractionTable()
{
    check(ViewportManager);

    FRenderStreamModule* Module = FRenderStreamModule::Get();
    check(Module);

    m_extractedDepth.Empty();
    m_depthIds.Empty();

    bool anyViewportExtractsDepth = false;

    for (auto Viewport : ViewportManager->GetViewports())
    {
        const FString& ViewportId = Viewport->GetId();
        m_extractedDepth.Add(ViewportId, TRefCountPtr<IPooledRenderTarget>());
        m_depthIds.Add(ViewportId);
        
        if (const auto KnownViewport = Module->ViewportInfos.Find(ViewportId); KnownViewport)
        {
            anyViewportExtractsDepth |= (*KnownViewport)->ShouldExtractDepth;
        }
    }

    m_EncodeDepth = anyViewportExtractsDepth;
}

void FRenderStreamCapturePostProcess::OnResolvedSceneColor_RenderThread(FRDGBuilder& GraphBuilder, const FSceneTextures& SceneTextures)
{
    check(IsInRenderingThread());

    // Access Violation on shutdown is being caused by https://d3technologies.atlassian.net/browse/RSP-186
    ViewportIdOrdering* OrderThisFrame = ViewportIdOrderPerFrame.Peek();
    if (!OrderThisFrame)
        return;

    const FString CurrentViewportId = OrderThisFrame->Pop(false);
    GraphBuilder.QueueTextureExtraction(SceneTextures.Depth.Resolve, &m_extractedDepth[CurrentViewportId]);
}

void FRenderStreamCapturePostProcess::OnDisplayClusterPostBackbufferUpdate_RenderThread(FRHICommandListImmediate& CmdList, const IDisplayClusterViewportManagerProxy* ViewportProxyManager, FViewport* Viewport)
{
    if (ViewportIdOrdering* OrderThisFrame = ViewportIdOrderPerFrame.Peek(); OrderThisFrame)
    {
        OrderThisFrame->Empty();
        ViewportIdOrderPerFrame.Pop();
    }
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
