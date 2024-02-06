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
    
}

FRenderStreamCapturePostProcess::~FRenderStreamCapturePostProcess() 
{

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

    IRendererModule* RendererModule = FModuleManager::GetModulePtr<IRendererModule>(TEXT("Renderer"));

    ViewportManager = InViewportManager;

    if (!StreamsChangedDelegateHandle.IsValid())
    {
        StreamsChangedDelegateHandle = Module->OnStreamsChangedDelegate.AddRaw(this, &FRenderStreamCapturePostProcess::RebuildDepthExtractionTable);
    }
    
    if (!DisplayClusterPostBackBufferUpdateHandle.IsValid())
    {
        DisplayClusterPostBackBufferUpdateHandle = IDisplayCluster::Get().GetCallbacks().OnDisplayClusterPostBackbufferUpdate_RenderThread().AddRaw(this, &FRenderStreamCapturePostProcess::OnDisplayClusterPostBackbufferUpdate_RenderThread);
    }

    if (!PostOpaqueDelegateHandle.IsValid() && RendererModule)
    {
        RendererModule->RegisterPostOpaqueRenderDelegate(FPostOpaqueRenderDelegate::CreateRaw(this, &FRenderStreamCapturePostProcess::OnPostOpaque_RenderThread));
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

    IRendererModule* RendererModule = FModuleManager::GetModulePtr<IRendererModule>(TEXT("Renderer"));

    if (PostOpaqueDelegateHandle.IsValid() && RendererModule)
    {
        RendererModule->RemovePostOpaqueRenderDelegate(PostOpaqueDelegateHandle);
        PostOpaqueDelegateHandle.Reset();
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

        FScopeLock lock(&m_extractedDepthLock);
        FRHITexture* depthPtr = nullptr;
        if (m_EncodeDepth)
        {
            if (m_extractedDepth.Contains(ViewportId))
                depthPtr = m_extractedDepth[ViewportId]->GetRHI();
            else
                UE_LOG(LogRenderStreamPostProcess, Error, TEXT("No extracted depth available for '%s : %s' with id '%s'"), *Stream->Name(), *Stream->Channel(), *ViewportId);
        }

        Stream->SendFrame_RenderingThread(RHICmdList, frameResponse, Resources[0],
            depthPtr, Rects[0], depthPtr ? m_extractedDepthTAAJitter[ViewportId] : FVector2D(0.f, 0.f)
        );
    }

    // Uncomment this to restore client display
    // InViewportProxy->ResolveResources(RHICmdList, EDisplayClusterViewportResourceType::InputShaderResource, InViewportProxy->GetOutputResourceType());
}

void FRenderStreamCapturePostProcess::HandleBeginNewFrame(IDisplayClusterViewportManager* InViewportManager, FDisplayClusterRenderFrame& InOutRenderFrame)
{
    ViewportIdOrdering OrderThisFrame;

    bool shouldRebuildExtractionTable = false;

    for (auto&& Target : InOutRenderFrame.RenderTargets)
    {
        for (auto&& Family : Target.ViewFamilies)
        {
            for (auto&& View : Family.Views)
            {
                check(View.Viewport);
                OrderThisFrame.Add(View.Viewport->GetId());
                if (!m_extractedDepth.Contains(OrderThisFrame.Last()))
                {
                    shouldRebuildExtractionTable = true;
                    UE_LOG(LogRenderStreamPostProcess, Error, TEXT("View %s has no corresponding depth extraction buffer"), *OrderThisFrame.Last());
                }
            }
        }
    }

    if (OrderThisFrame.IsEmpty())
        UE_LOG(LogRenderStreamPostProcess, Error, TEXT("No Viewports listed in OrderThisFrame"));
    
    Algo::Reverse(OrderThisFrame);
    ViewportIdOrderPerFrame.Enqueue(OrderThisFrame);
    
    if (shouldRebuildExtractionTable)
        RebuildDepthExtractionTable();
}

void FRenderStreamCapturePostProcess::RebuildDepthExtractionTable()
{
    check(ViewportManager);

    FRenderStreamModule* Module = FRenderStreamModule::Get();
    check(Module);

    FScopeLock lock(&m_extractedDepthLock);

    m_extractedDepth.Empty();
    m_depthIds.Empty();
    m_extractedDepthTAAJitter.Empty();

    bool anyViewportExtractsDepth = false;

    UE_LOG(LogRenderStreamPostProcess, Log, TEXT("Rebuilding DepthExtractionTable..."));

    for (auto Viewport : ViewportManager->GetViewports())
    {
        const FString& ViewportId = Viewport->GetId();
        m_extractedDepth.Add(ViewportId, TRefCountPtr<IPooledRenderTarget>());
        m_depthIds.Add(ViewportId);
        m_extractedDepthTAAJitter.Add(ViewportId, FVector2D(0, 0));
        
        if (const auto KnownViewport = Module->ViewportInfos.Find(ViewportId); KnownViewport)
        {
            anyViewportExtractsDepth |= (*KnownViewport)->ShouldExtractDepth;
        }
        UE_LOG(LogRenderStreamPostProcess, Log, TEXT("Adding Viewport %s"), *ViewportId);
    }

    m_EncodeDepth = anyViewportExtractsDepth;
    UE_LOG(LogRenderStreamPostProcess, Log, TEXT("DepthExtraction Active: %d"), m_EncodeDepth);
}

void FRenderStreamCapturePostProcess::OnPostOpaque_RenderThread(FPostOpaqueRenderParameters& PostOpaqueParameters)
{
    check(IsInRenderingThread());

    // Access Violation on shutdown is being caused by https://d3technologies.atlassian.net/browse/RSP-186
    ViewportIdOrdering* OrderThisFrame = ViewportIdOrderPerFrame.Peek();
    if (!OrderThisFrame)
        return;

    const FString CurrentViewportId = OrderThisFrame->Pop(false);
    
    FScopeLock lock(&m_extractedDepthLock);
    if (m_extractedDepth.Contains(CurrentViewportId))
    {
        m_extractedDepthTAAJitter[CurrentViewportId] = PostOpaqueParameters.View->TemporalJitterPixels;
        PostOpaqueParameters.GraphBuilder->QueueTextureExtraction(PostOpaqueParameters.DepthTexture, &m_extractedDepth[CurrentViewportId]);
    }
    else
    {
        UE_LOG(LogRenderStreamPostProcess, Error, TEXT("Viewport %s is not in ExtractedDepthTable! No extraction queued"), *CurrentViewportId);
    }
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
