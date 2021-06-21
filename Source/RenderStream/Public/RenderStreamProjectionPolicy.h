#pragma once

#include "RenderStreamLink.h"
#include "Render/Projection/IDisplayClusterProjectionPolicyFactory.h"
#include "Render/Projection/IDisplayClusterProjectionPolicy.h"
#include <deque>
#include <mutex>

class UCameraComponent;
class UWorld;
class FRenderStreamModule;

DECLARE_LOG_CATEGORY_EXTERN(LogRenderStreamPolicy, Log, All);

/**
 * 'renderstream' policy for disguise integration
 */
class FRenderStreamProjectionPolicy
    : public IDisplayClusterProjectionPolicy
{
public:
    FRenderStreamProjectionPolicy(const FString& ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy);
    virtual ~FRenderStreamProjectionPolicy();

    //////////////////////////////////////////////////////////////////////////////////////////////
    // IDisplayClusterProjectionPolicy
    //////////////////////////////////////////////////////////////////////////////////////////////
    const FString& GetId() const override { return ProjectionPolicyId; }
    const FString GetTypeId() const override;

    bool IsConfigurationChanged(const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy) const override;

    bool HandleStartScene(class IDisplayClusterViewport* InViewport) override;
    void HandleEndScene(class IDisplayClusterViewport* InViewport) override;

    bool IsWarpBlendSupported() override { return true; }
    void ApplyWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const class IDisplayClusterViewportProxy* InViewportProxy) override;

    bool CalculateView(class IDisplayClusterViewport* InViewport, const uint32 InContextNum, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP) override;
    bool GetProjectionMatrix(class IDisplayClusterViewport* InViewport, const uint32 InContextNum, FMatrix& OutPrjMatrix) override;

    //////////////////////////////////////////////////////////////////////////////////////////////
    // RenderStream specific
    //////////////////////////////////////////////////////////////////////////////////////////////
    const FString& GetViewportId() const { return ViewportId; }
    const ACameraActor* GetTemplateCamera() const;

    const TMap<FString, FString>& GetParameters() const
    {
        return Parameters;
    }

    void ApplyCameraData(const RenderStreamLink::FrameData& frameData, const RenderStreamLink::CameraData& cameraData);

    void ConfigureCapture();

    const int32_t GetPlayerControllerID() const { return PlayerControllerID; }

    void UpdateStream(const FString& StreamName);

    bool isInitialised = false;

protected:
    FString ProjectionPolicyId;
    FString ViewportId;
    TMap<FString, FString> Parameters;

    // Near/far clip planes
    float NCP;
    float FCP;
    // Capture settings
    TWeakObjectPtr<ACameraActor> Camera = nullptr;
    TWeakObjectPtr<ACameraActor> Template = nullptr;
    TSharedPtr<FFrameStream> Stream = nullptr;
    int32_t PlayerControllerID = INDEX_NONE;
    TWeakObjectPtr<APlayerController> Controller = nullptr;

    std::mutex m_frameResponsesLock;
    std::deque<RenderStreamLink::CameraResponseData> m_frameResponses;
};


/**
 * Implements projection policy factory for the 'renderstream' policy
 */
class FRenderStreamProjectionPolicyFactory
    : public IDisplayClusterProjectionPolicyFactory
{
public:
    FRenderStreamProjectionPolicyFactory();
    virtual ~FRenderStreamProjectionPolicyFactory();

public:
    //////////////////////////////////////////////////////////////////////////////////////////////
    // IDisplayClusterProjectionPolicyFactory
    //////////////////////////////////////////////////////////////////////////////////////////////
    virtual TSharedPtr<IDisplayClusterProjectionPolicy> Create(const FString& ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy) override;

    TArray<TSharedPtr<FRenderStreamProjectionPolicy>> GetPolicies();
    TSharedPtr<FRenderStreamProjectionPolicy>         GetPolicyByViewport(const FString& ViewportId) const;
    TSharedPtr<FRenderStreamProjectionPolicy>         GetPolicyBySceneViewFamily(int32 ViewFamilyIdx) const;

    static constexpr auto RenderStreamPolicyType = TEXT("renderstream");

private:
    TArray<TSharedPtr<FRenderStreamProjectionPolicy>> Policies;
};
