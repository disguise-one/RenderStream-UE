#pragma once
#include "RenderStreamLink.h"
#include "RenderStreamSettings.h"

#include "RHI.h"
#include "RHIResources.h"

class FRHICommandListImmediate;

class FFrameStream
{
public:
    FFrameStream();
    ~FFrameStream();

    void SendFrame_RenderingThread(FRHICommandListImmediate & RHICmdList, 
                                   RenderStreamLink::CameraResponseData& FrameData,
                                   FRHITexture2D* InSourceTexture,
                                   const FIntRect& ViewportRect);

    bool Setup(const FString& Name, const FIntPoint& Resolution, const FString& Channel, const RenderStreamLink::ProjectionClipping& Clipping, RenderStreamLink::StreamHandle Handle, RenderStreamLink::RSPixelFormat Fmt);
    void Update(const FIntPoint& Resolution, const FString& Channel, const RenderStreamLink::ProjectionClipping& Clipping, RenderStreamLink::StreamHandle Handle, RenderStreamLink::RSPixelFormat Fmt);

    const FString& Name() const { return m_streamName;}
    const FString& Channel() const { return m_channel; }
    const RenderStreamLink::ProjectionClipping& Clipping() const { return m_clipping; }
    FIntPoint Resolution() const { return m_resolution; }
    RenderStreamLink::StreamHandle Handle() const { return m_handle; }

private:
    FString m_streamName;
    FString m_channel;
    RenderStreamLink::ProjectionClipping m_clipping;
    FTextureRHIRef m_bufTexture;
    FIntPoint m_resolution;
    RenderStreamLink::StreamHandle m_handle;
};