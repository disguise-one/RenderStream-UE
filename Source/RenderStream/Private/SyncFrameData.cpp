#include "SyncFrameData.h"

#include "RenderStreamLink.h"
#include "RenderStream.h"
#include "RenderStreamStats.h"
#include "RenderStreamEventHandler.h"

bool FRenderStreamSyncFrameData::IsActive() const
{
    return RenderStreamLink::instance().isAvailable();
}

FString FRenderStreamSyncFrameData::GetSyncId() const
{
    static const FString SyncId = TEXT("FRenderStreamSyncFrameData");
    return SyncId;
}

FString FRenderStreamSyncFrameData::SerializeToString() const
{
    TArray<uint8> TempBytes;
    FMemoryWriter Ar(TempBytes, /*bIsPersistent=*/ true);
    const_cast<FRenderStreamSyncFrameData*>(this)->Map(Ar);
    return BytesToString(TempBytes.GetData(), TempBytes.Num());
}

bool FRenderStreamSyncFrameData::DeserializeFromString(const FString& Str)
{
    TArray<uint8> TempBytes;
    TempBytes.AddUninitialized(Str.Len());
    StringToBytes(Str, TempBytes.GetData(), Str.Len());

    FMemoryReader Ar(TempBytes);
    if (Map(Ar))
    {
        FollowerReceive();
        return true;
    }

    return false;
}

bool FRenderStreamSyncFrameData::Map(FArchive& Ar)
{
    //Master is saving, slaves are loading
    const bool bIsSaving = Ar.IsSaving();

    int rsMajorVersion = RENDER_STREAM_VERSION_MAJOR;
    int rsMinorVersion = RENDER_STREAM_VERSION_MINOR;
    static const int DATA_VERSION = 3;
    int v = DATA_VERSION;
    Ar << rsMajorVersion;
    Ar << rsMinorVersion;
    if (!bIsSaving)
    {
        if (rsMajorVersion != RENDER_STREAM_VERSION_MAJOR ||
            rsMinorVersion != RENDER_STREAM_VERSION_MINOR)
        {
            UE_LOG(LogRenderStream, Error, TEXT("nDisplay master is running unsupported RenderStream library, expected version %i.%i, got version %i.%i"), RENDER_STREAM_VERSION_MAJOR, RENDER_STREAM_VERSION_MINOR, rsMajorVersion, rsMinorVersion);
            return false;
        }
        if (v != DATA_VERSION)
        {
            UE_LOG(LogRenderStream, Error, TEXT("nDisplay master is running unsupported plugin, expected data version %i, got data version %i"), DATA_VERSION, v);
            return false;
        }
    }
    Ar << m_isQuitting;
    Ar << m_frameDataValid;
    Ar.Serialize(&m_frameData, sizeof(RenderStreamLink::FrameData));
    Ar << m_streamsChanged;
    if (bIsSaving)
    {
        // Reset flag after send
        m_streamsChanged = false;
    }

    return true;
}

void FRenderStreamSyncFrameData::ControllerReceive()
{
    if (m_isQuitting)
    {
        // Process deferred quit status from last frame (deferred due to ndisplay sync)
        QuitNow();
        return;
    }

    TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("FRenderStreamSyncFrameData::ControllerReceive()"));
    SCOPE_CYCLE_COUNTER(STAT_AwaitFrame);
    const double StartTime = FPlatformTime::Seconds();
    const RenderStreamLink::RS_ERROR Ret = RenderStreamLink::instance().rs_awaitFrameData(500, &m_frameData);

    if (Ret == RenderStreamLink::RS_ERROR_STREAMS_CHANGED)
    {
        // Update the streams
        FRenderStreamModule* Module = FRenderStreamModule::Get();
        check(Module);
        Module->PopulateStreamPool();
        m_streamsChanged = true;

        // We need to actually get frame data, go back.
        ControllerReceive();
    }
    else if (Ret == RenderStreamLink::RS_ERROR_QUIT)
    {
        m_frameDataValid = false;
        m_isQuitting = true; // notify ndisplay followers that the application should quit.
        // we must defer the processing of this quit message until the next frame, because otherwise ndisplay won't synchronise this.
    }
    else if (Ret != RenderStreamLink::RS_ERROR_SUCCESS)
    {
        if (Ret == RenderStreamLink::RS_ERROR_TIMEOUT)
        {
            RenderStreamLink::instance().rs_setNewStatusMessage("Not requested");
        }
        else
        {
            UE_LOG(LogRenderStream, Error, TEXT("Error awaiting frame data error %d"), Ret);
        }
        m_frameDataValid = false; // TODO: Mark timecode as invalid only after some multiple of the expected incoming framerate.
    }
    else
    {
        if (!m_frameDataValid)
        {
            RenderStreamLink::instance().rs_setNewStatusMessage("");
        }

        // Force a fixed time-step on the controller, followers will sync it via nDisplay
        float DeltaSeconds;
        if (FMath::IsNaN(LastTrackedTime))
            DeltaSeconds = static_cast<float>(m_frameData.frameRateDenominator) / m_frameData.frameRateNumerator;
        else
            DeltaSeconds = m_frameData.tTracked - LastTrackedTime;

        if (DeltaSeconds <= 0.f)
        {
            UE_LOG(LogRenderStream, Error, TEXT("Negative delta time! tTracked: %f LastTrackedTime: %f"), m_frameData.tTracked, LastTrackedTime);
            DeltaSeconds = static_cast<float>(m_frameData.frameRateDenominator) / m_frameData.frameRateNumerator;
        }

        LastTrackedTime = m_frameData.tTracked;

        FApp::SetUseFixedTimeStep(true);
        FApp::SetFixedDeltaTime(DeltaSeconds);

        m_frameDataValid = true;
        Apply();
    }

    AwaitTime = (FPlatformTime::Seconds() - StartTime) * 1000.f;
}

void FRenderStreamSyncFrameData::FollowerReceive() const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("FRenderStreamSyncFrameData::FollowerReceive()"));
    SCOPE_CYCLE_COUNTER(STAT_ReceiveFrame);
    const double StartTime = FPlatformTime::Seconds();
    RenderStreamLink::instance().rs_setFollower(1);

    if (m_streamsChanged)
    {
        // Update the streams
        FRenderStreamModule* Module = FRenderStreamModule::Get();
        check(Module);
        Module->PopulateStreamPool();
    }

    if (m_isQuitting)
    {
        // get the quit status direct from RenderStream, so it can notify everyone that we heard.
        while (RenderStreamLink::instance().rs_beginFollowerFrame(DBL_MAX) != RenderStreamLink::RS_ERROR_QUIT)
        {
            UE_LOG(LogRenderStream, Warning, TEXT("Waiting for quit status from RenderStream"));
        }
        QuitNow();
        return;
    }
    else if (m_frameDataValid)
    {
        // We have been given the frameData the controller node is using for this synchronised frame.
        // We must now let RenderStream know this is the frame we are processing, so that RS APIs give the correct data.
        RenderStreamLink::RS_ERROR err = RenderStreamLink::instance().rs_beginFollowerFrame(m_frameData.tTracked);
        if (err == RenderStreamLink::RS_ERROR_QUIT)
        {
            QuitNow();
            return;
        }

        // Write into the engine for this node.
        Apply();
    }

    ReceiveTime = (FPlatformTime::Seconds() - StartTime) * 1000.f;
}

void FRenderStreamSyncFrameData::Apply() const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("FRenderStreamSyncFrameData::Apply()"));
    FRenderStreamModule* Module = FRenderStreamModule::Get();
    Module->ApplyScene(m_frameData.scene);
    Module->ApplyCameras(m_frameData);
}

void FRenderStreamSyncFrameData::QuitNow() const
{
    RenderStreamLink::instance().rs_setNewStatusMessage("");
    UE_LOG(LogRenderStream, Log, TEXT("Quitting due to RenderStream request"));
    FPlatformMisc::RequestExit(false);
}
