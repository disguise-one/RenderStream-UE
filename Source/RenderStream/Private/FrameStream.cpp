#include "FrameStream.h"
#include "RenderStream.h"

#include "RSUCHelpers.inl"

FFrameStream::FFrameStream()
    : m_streamName(""), m_bufTexture(nullptr), m_handle(0) {}

FFrameStream::~FFrameStream()
{
}

void FFrameStream::SendFrame_RenderingThread(FRHICommandListImmediate& RHICmdList, RenderStreamLink::CameraResponseData& FrameData, FRHITexture2D* SourceTexture, const FIntRect& ViewportRect)
{
    float ULeft = (float)ViewportRect.Min.X / (float)SourceTexture->GetSizeX();
    float URight = (float)ViewportRect.Max.X / (float)SourceTexture->GetSizeX();
    float VTop = (float)ViewportRect.Min.Y / (float)SourceTexture->GetSizeY();
    float VBottom = (float)ViewportRect.Max.Y / (float)SourceTexture->GetSizeY();
    RSUCHelpers::SendFrame(m_handle, m_bufTexture, RHICmdList, FrameData, SourceTexture, SourceTexture->GetSizeXY(), { ULeft, URight }, { VTop, VBottom });
}

bool FFrameStream::Setup(const FString& name, const FIntPoint& Resolution, const FString& Channel, const RenderStreamLink::ProjectionClipping& Clipping, RenderStreamLink::StreamHandle Handle, RenderStreamLink::RSPixelFormat fmt)
{
    if (m_handle != 0)
        return false; // already have a stream handle call stop first

    m_handle = Handle;
    m_channel = Channel;
    m_clipping = Clipping;
    m_resolution = Resolution;
    m_streamName = name;

    if (!RSUCHelpers::CreateStreamResources(m_bufTexture, m_resolution, fmt))
        return false; // helper method logs on failure

    if (m_handle == 0) {
        UE_LOG(LogRenderStream, Error, TEXT("Unable to create stream"));
        return false;
    }
    UE_LOG(LogRenderStream, Log, TEXT("Created stream '%s'"), *m_streamName);
    
    return true;
}

void FFrameStream::Update(const FIntPoint& Resolution, const FString& Channel, const RenderStreamLink::ProjectionClipping& Clipping, RenderStreamLink::StreamHandle Handle, RenderStreamLink::RSPixelFormat Fmt)
{
    // Todo: Do we need to destroy the handle?
    m_handle = 0;
    Setup(m_streamName, Resolution, Channel, Clipping, Handle, Fmt);
}