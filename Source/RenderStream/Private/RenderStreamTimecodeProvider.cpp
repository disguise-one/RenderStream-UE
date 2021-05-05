#include "RenderStreamTimecodeProvider.h"

#include "RenderStream.h"
#include "CoreMinimal.h"

URenderStreamTimecodeProvider::URenderStreamTimecodeProvider(const class FObjectInitializer& objectInitializer)
    : Super(objectInitializer)
{
}

FQualifiedFrameTime URenderStreamTimecodeProvider::GetQualifiedFrameTime() const
{
    const FRenderStreamSyncFrameData& syncData = FRenderStreamModule::Get()->m_syncFrame;
    if (syncData.m_frameDataValid)
    {
        // The rate that d3 is sending new time values. Each new localtime value is in 1/render rate increments
        const FFrameRate d3RenderRate(syncData.m_frameData.frameRateNumerator, syncData.m_frameData.frameRateDenominator);

        const FTimecode timecode(syncData.m_frameData.localTime, d3RenderRate, false);

        LastTime = FQualifiedFrameTime(timecode, d3RenderRate);
    }

    // Always return a valid time. If we have dropped a frame, or the d3 server has, we want to stay somewhere
    // sensible.
    return LastTime;
}

ETimecodeProviderSynchronizationState URenderStreamTimecodeProvider::GetSynchronizationState() const
{
    // Always return Synchronized. If we return anything else, unreal can snap back to 0.
    return ETimecodeProviderSynchronizationState::Synchronized;
}
