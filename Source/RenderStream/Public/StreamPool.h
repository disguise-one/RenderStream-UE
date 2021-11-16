#pragma once
#include "Containers/Array.h"
#include "Containers/Map.h"

#include "RenderStreamLink.h"

class FFrameStream;

using FFrameStreamPtr = TSharedPtr<FFrameStream, ESPMode::ThreadSafe>;

class FStreamPool
{
public:
    // add a stream to the pool for anything to get
    bool AddNewStreamToPool(const FString& StreamName, const FIntPoint& Resolution, const FString& Channel, const RenderStreamLink::ProjectionClipping& Clipping, RenderStreamLink::StreamHandle Handle, RenderStreamLink::RSPixelFormat Fmt);

    // get the stream by name
    FFrameStreamPtr GetStream(const FString& desiredStreamName);

    // passing a UID for the id, allocate a stream for that objects use
    // the stream is allocated for only that object to use
    FFrameStreamPtr AllocateStreamFor(const FString& desiredStreamName, uint32 id);

    // passing a UID for the id, return an allocated stream to the pool
    void ReturnStreamFor(uint32 uid);

    // get the allocated streams which are all considered "active"
    const TMap<uint32, FFrameStreamPtr>& GetActiveStreams() const;

    const TArray<FFrameStreamPtr>& GetAllStreams() const { return m_pool; }

    uint32_t PoolCount() const;
    uint32_t StreamCount() const;

private:
    TArray<FFrameStreamPtr> m_pool;
    TMap<uint32, FFrameStreamPtr> m_allocated;
};