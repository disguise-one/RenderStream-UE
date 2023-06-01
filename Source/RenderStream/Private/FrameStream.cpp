#include "FrameStream.h"
#include "RenderStream.h"

#include "RSUCHelpers.inl"

FFrameStream::FFrameStream()
    : m_streamName(""), m_bufTextures(), m_handle(0) { }

FFrameStream::~FFrameStream()
{
}

void FFrameStream::SendFrame_RenderingThread(FRHICommandListImmediate& RHICmdList, RenderStreamLink::CameraResponseData& FrameData, FRHITexture* SourceTexture, const FIntRect& ViewportRect)
{
    float ULeft = (float)ViewportRect.Min.X / (float)SourceTexture->GetSizeX();
    float URight = (float)ViewportRect.Max.X / (float)SourceTexture->GetSizeX();
    float VTop = (float)ViewportRect.Min.Y / (float)SourceTexture->GetSizeY();
    float VBottom = (float)ViewportRect.Max.Y / (float)SourceTexture->GetSizeY();

    if(m_bufTextures.empty())
        UE_LOG(LogRenderStream, Error, TEXT("No more texture buffer available in the frame stream queue."));

    FTextureRHIRef first = m_bufTextures.front(); // take the first ref
    m_bufTextures.pop(); // remove the first ref
    RSUCHelpers::SendFrame(m_handle, first, RHICmdList, FrameData, SourceTexture, SourceTexture->GetSizeXY(), { ULeft, URight }, { VTop, VBottom });
    m_bufTextures.push(first); // place what was the first ref at the end
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

    FTextureRHIRef texture1, texture2, texture3, texture4;
    if (!RSUCHelpers::CreateStreamResources(texture1, m_resolution, fmt) 
        || !RSUCHelpers::CreateStreamResources(texture2, m_resolution, fmt) 
        || !RSUCHelpers::CreateStreamResources(texture3, m_resolution, fmt) 
        || !RSUCHelpers::CreateStreamResources(texture4, m_resolution, fmt))
        return false; // helper method logs on failure

    m_bufTextures.push(texture1);
    m_bufTextures.push(texture2);
    m_bufTextures.push(texture3);
    m_bufTextures.push(texture4);

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