#include "FrameStream.h"
#include "RenderStream.h"

#include "RSUCHelpers.inl"

FFrameStream::FFrameStream()
    : m_streamName(""), m_bufTexture(nullptr), m_handle(0) {}

FFrameStream::~FFrameStream()
{
}

void FFrameStream::SendFrame_RenderingThread(FRHICommandListImmediate& RHICmdList, RenderStreamLink::CameraResponseData& FrameData, FRHITexture* InSourceTexture, FRHITexture* InDepthTexture, const FIntRect& ViewportRect)
{
    float ULeft = (float)ViewportRect.Min.X / (float)InSourceTexture->GetSizeX();
    float URight = (float)ViewportRect.Max.X / (float)InSourceTexture->GetSizeX();
    float VTop = (float)ViewportRect.Min.Y / (float)InSourceTexture->GetSizeY();
    float VBottom = (float)ViewportRect.Max.Y / (float)InSourceTexture->GetSizeY();

    RSUCHelpers::SendFrame(m_handle, m_bufTexture, m_depthBufTexture, RHICmdList, FrameData, InSourceTexture, InDepthTexture, InSourceTexture->GetSizeXY(), { ULeft, URight }, { VTop, VBottom });

}

bool FFrameStream::Setup(const FString& name, const FIntPoint& Resolution, const FString& Channel, const RenderStreamLink::ProjectionClipping& Clipping, RenderStreamLink::StreamHandle Handle, RenderStreamLink::RSPixelFormat fmt, bool requiresDepth)
{
    if (m_handle != 0)
        return false; // already have a stream handle call stop first

    m_handle = Handle;
    m_channel = Channel;
    m_clipping = Clipping;
    m_resolution = Resolution;
    m_streamName = name;
    m_requiresDepth = requiresDepth;

    if (!RSUCHelpers::CreateStreamResources(m_bufTexture, m_resolution, fmt))
        return false; // helper method logs on failure

    if (!RSUCHelpers::CreateStreamResources(m_depthBufTexture, m_resolution, RenderStreamLink::RSPixelFormat::RS_FMT_R32F))
        return false;

    if (m_handle == 0) {
        UE_LOG(LogRenderStream, Error, TEXT("Unable to create stream"));
        return false;
    }
    UE_LOG(LogRenderStream, Log, TEXT("Created stream '%s'"), *m_streamName);
    


    return true;
}

void FFrameStream::Update(const FIntPoint& Resolution, const FString& Channel, const RenderStreamLink::ProjectionClipping& Clipping, RenderStreamLink::StreamHandle Handle, RenderStreamLink::RSPixelFormat Fmt, bool requiresDepth)
{
    // Todo: Do we need to destroy the handle?
    m_handle = 0;
    Setup(m_streamName, Resolution, Channel, Clipping, Handle, Fmt, requiresDepth);
}