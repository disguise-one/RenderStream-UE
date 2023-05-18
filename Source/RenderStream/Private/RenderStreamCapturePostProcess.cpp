#pragma once

#include "RenderStreamCapturePostProcess.h"


#include "DisplayClusterConfigurationTypes_Base.h"
#include "FrameStream.h"
#include "IDisplayCluster.h"
#include "RenderStream.h"
#include "RenderStreamProjectionPolicy.h"
#include "Render/Viewport/IDisplayClusterViewportManager.h"
#include "Render/Viewport/IDisplayClusterViewportProxy.h"

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
        auto& Info = Module->GetViewportInfo(ViewportId);
        RenderStreamLink::CameraResponseData frameResponse;
        {
            std::lock_guard<std::mutex> guard(Info.m_frameResponsesLock);
            if (Info.m_frameResponsesMap.count(Info.FrameInfo.FrameNumber)) // Check current frame data exists
            {
                frameResponse = Info.m_frameResponsesMap[Info.FrameInfo.FrameNumber];
                FVector pos;
                pos.X = FUnitConversion::Convert(float(frameResponse.camera.z), EUnit::Meters, FRenderStreamModule::distanceUnit());
                pos.Y = FUnitConversion::Convert(float(frameResponse.camera.x), EUnit::Meters, FRenderStreamModule::distanceUnit());
                pos.Z = FUnitConversion::Convert(float(frameResponse.camera.y), EUnit::Meters, FRenderStreamModule::distanceUnit());
                UE_LOG(LogRenderStreamPostProcess, Log, TEXT("Handling frame %d"), Info.FrameInfo.FrameNumber);
                UE_LOG(LogRenderStreamPostProcess, Log, TEXT("Render data: (%f, %f, %f) (%f, %f, %f)"),
                    Info.FrameInfo.Location.X, Info.FrameInfo.Location.Y, Info.FrameInfo.Location.Z,
                    Info.FrameInfo.Rotation.Pitch, Info.FrameInfo.Rotation.Yaw, Info.FrameInfo.Rotation.Roll);
                UE_LOG(LogRenderStreamPostProcess, Log, TEXT("DTrack data: (%f, %f, %f) (%f, %f, %f)"),
                    pos.X, pos.Y, pos.Z,
                    frameResponse.camera.rx, frameResponse.camera.ry, frameResponse.camera.rz);
                Info.m_frameResponsesMap.erase(Info.FrameInfo.FrameNumber);
            }
            else
            {
                UE_LOG(LogRenderStreamPostProcess, Log, TEXT("Failed to handle frame %d"), Info.FrameInfo.FrameNumber);
                return;
            }
        }

        TArray<FRHITexture*> Resources;
        TArray<FIntRect> Rects;
        // NOTE: If you get a black screen on the stream when updating the plugin to a new unreal version try changing the EDisplayClusterViewportResourceType enum.
        ViewportProxy->GetResourcesWithRects_RenderThread(EDisplayClusterViewportResourceType::InputShaderResource, Resources, Rects);
        check(Resources.Num() == 1);
        check(Rects.Num() == 1);
        Stream->SendFrame_RenderingThread(RHICmdList, frameResponse, Resources[0], Rects[0]);
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
