#include "StreamPool.h"
#include "FrameStream.h"

bool FStreamPool::AddNewStreamToPool(const FString& StreamName, const FIntPoint& Resolution, const FString& Channel, const RenderStreamLink::ProjectionClipping& Clipping, RenderStreamLink::StreamHandle Handle, RenderStreamLink::RSPixelFormat Fmt)
{
    FFrameStreamPtr stream = MakeShared<FFrameStream, ESPMode::ThreadSafe>();
    if (!stream->Setup(StreamName, Resolution, Channel, Clipping, Handle, Fmt))
        return false;

    m_pool.Add(stream);
    return true;
}

FFrameStreamPtr FStreamPool::GetStream(const FString& desiredStreamName)
{
    auto It = m_pool.FindByPredicate([&desiredStreamName](const FFrameStreamPtr& stream) {
        return stream->Name().Compare(desiredStreamName, ESearchCase::IgnoreCase) == 0;
    });
    return It ? *It : nullptr;
}

FFrameStreamPtr FStreamPool::AllocateStreamFor(const FString& desiredStreamName, uint32 id)
{
    bool exists = m_allocated.Find(id) != nullptr;
    if (!exists && m_pool.Num() > 0)
    {
        if (FFrameStreamPtr stream = GetStream(desiredStreamName))
        {
            m_allocated.Add(id, stream);
            m_pool.Remove(stream);
            return m_allocated[id];
        }
    }
    return nullptr;
}

void FStreamPool::ReturnStreamFor(uint32 uid)
{
    auto found = m_allocated.Find(uid);
    if (found)
    {
        m_pool.Add(*found);
        m_allocated.Remove(uid);
    }
}

const TMap<uint32, FFrameStreamPtr>& FStreamPool::GetActiveStreams() const
{
    return m_allocated;
}

uint32_t FStreamPool::PoolCount() const
{
    return m_pool.Num();
}

uint32_t FStreamPool::StreamCount() const
{
    return PoolCount() + m_allocated.Num();
}
