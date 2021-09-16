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
    static constexpr auto RenderStreamPolicyType = TEXT("renderstream");

    FRenderStreamProjectionPolicy(const FString& ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy);
    virtual ~FRenderStreamProjectionPolicy();

    //////////////////////////////////////////////////////////////////////////////////////////////
    // IDisplayClusterProjectionPolicy
    //////////////////////////////////////////////////////////////////////////////////////////////
    virtual bool HandleStartScene(class IDisplayClusterViewport* InViewport) override;
    virtual void HandleEndScene(class IDisplayClusterViewport* InViewport) override;

    const FString& GetId() const override { return ProjectionPolicyId; }
    const FString GetTypeId() const override { return RenderStreamPolicyType; }

    bool IsConfigurationChanged(const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy) const override { return false; }
    
    bool CalculateView(class IDisplayClusterViewport* InViewport, const uint32 InContextNum, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP) override;
    bool GetProjectionMatrix(class IDisplayClusterViewport* InViewport, const uint32 InContextNum, FMatrix& OutPrjMatrix) override;
    
    const TMap<FString, FString>& GetParameters() const
    {
        return Parameters;
    }
    
protected:
    FString ProjectionPolicyId;
    TMap<FString, FString> Parameters;
    
    float NCP;
    float FCP;
};


/**
 * Implements projection policy factory for the 'renderstream' policy
 */
class FRenderStreamProjectionPolicyFactory
    : public IDisplayClusterProjectionPolicyFactory
{
public:
    using BasePolicyPtr = TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe>;
    using PolicyPtr = TSharedPtr<FRenderStreamProjectionPolicy, ESPMode::ThreadSafe>;

    FRenderStreamProjectionPolicyFactory();
    virtual ~FRenderStreamProjectionPolicyFactory();

public:
    //////////////////////////////////////////////////////////////////////////////////////////////
    // IDisplayClusterProjectionPolicyFactory
    //////////////////////////////////////////////////////////////////////////////////////////////
    virtual BasePolicyPtr Create(const FString& ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy) override;
};
