#pragma once

#include "Cluster/IDisplayClusterClusterSyncObject.h"
#include "RenderStreamLink.h"

class FRenderStreamSyncFrameData : public IDisplayClusterClusterSyncObject
{
public:
    /** IDisplayClusterClusterSyncObject implementation */
    virtual bool IsActive() const override;
    virtual FString GetSyncId() const override;
    virtual bool IsDirty() const override { return true; };
    virtual void ClearDirty() override {};
    virtual FString SerializeToString() const override;
    virtual bool DeserializeFromString(const FString& Ar) override;

    bool Map(FArchive& Ar);

    void ControllerReceive();     // Controller receives from RenderStream, calls Apply.

protected:
    void FollowerReceive() const; // Follower receives from master, validates with RenderStream, calls Apply.
    void Apply() const;           // Applies changes from RS API to the engine, locally.
    void QuitNow() const;         // Exit the application due to a RenderStream-requested quit.

public:
    bool m_isQuitting = false;
    bool m_frameDataValid = false;
    RenderStreamLink::FrameData m_frameData;
    bool m_streamsChanged = false;
    double LastTrackedTime = std::numeric_limits<double>::quiet_NaN();
    double AwaitTime = 0;
    mutable double ReceiveTime = 0;
};
