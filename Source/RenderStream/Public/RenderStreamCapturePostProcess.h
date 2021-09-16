#pragma once

#include "RenderStreamLink.h"
#include "Render/PostProcess/IDisplayClusterPostProcessFactory.h"
#include "Render/PostProcess/IDisplayClusterPostProcess.h"

DECLARE_LOG_CATEGORY_EXTERN(LogRenderStreamPostProcess, Log, All);

/**
 * 'renderstream' policy for disguise integration
 */
class FRenderStreamCapturePostProcess
    : public IDisplayClusterPostProcess
{
public:
    FRenderStreamCapturePostProcess(const FString& PostProcessId, const struct FDisplayClusterConfigurationPostprocess* InConfigurationPostProcess);
    virtual ~FRenderStreamCapturePostProcess() override;

public:
	virtual const FString& GetId() const override { return Id; }
	virtual int32 GetOrder() const override { return 0; }
	virtual const FString GetTypeId() const override { return "renderstream_capture"; }
	virtual const TMap<FString, FString>& GetParameters() const override { return Parameters; }
	virtual bool IsPostProcessViewAfterWarpBlendRequired() const override { return true; }
	
	virtual bool IsConfigurationChanged(const struct FDisplayClusterConfigurationPostprocess* InConfigurationPostprocess) const override;
	virtual bool HandleStartScene(IDisplayClusterViewportManager* InViewportManager) override;
	virtual void HandleEndScene(IDisplayClusterViewportManager* InViewportManager) override;
	virtual void PerformPostProcessViewAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const IDisplayClusterViewportProxy* ViewportProxy) const override;

private:
	TMap<FString, FString> Parameters;
	FString Id;
};

class FRenderStreamPostProcessFactory
    : public IDisplayClusterPostProcessFactory
{
public:
	using BasePostProcessPtr = TSharedPtr<IDisplayClusterPostProcess, ESPMode::ThreadSafe>;
	using PostProcessPtr = TSharedPtr<FRenderStreamCapturePostProcess, ESPMode::ThreadSafe>;

	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterPostProcessFactory
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual TSharedPtr<IDisplayClusterPostProcess, ESPMode::ThreadSafe> Create(const FString& PostProcessId, const struct FDisplayClusterConfigurationPostprocess* InConfigurationPostProcess);

    static constexpr auto RenderStreamPostProcessType = TEXT("renderstream_capture");
};
