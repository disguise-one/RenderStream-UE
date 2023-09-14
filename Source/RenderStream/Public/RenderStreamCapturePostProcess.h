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
	virtual const FString& GetType() const override { return Type; }
	virtual const TMap<FString, FString>& GetParameters() const override { return Parameters; }
	virtual bool IsPostProcessViewAfterWarpBlendRequired() const override { return true; }
	
	virtual bool IsConfigurationChanged(const struct FDisplayClusterConfigurationPostprocess* InConfigurationPostprocess) const override;
	virtual bool HandleStartScene(IDisplayClusterViewportManager* InViewportManager) override;
	virtual void HandleEndScene(IDisplayClusterViewportManager* InViewportManager) override;
	virtual void PerformPostProcessViewAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const IDisplayClusterViewportProxy* ViewportProxy) const override;

    virtual void HandleBeginNewFrame(IDisplayClusterViewportManager* InViewportManager, FDisplayClusterRenderFrame& InOutRenderFrame) override;

private:
    void RebuildDepthExtractionTable();

    void OnPostOpaque_RenderThread(FPostOpaqueRenderParameters& PostOpaqueParameters);
    
    void OnDisplayClusterPostBackbufferUpdate_RenderThread(FRHICommandListImmediate& CmdList, const IDisplayClusterViewportManagerProxy* ViewportProxyManager, FViewport* Viewport);

    FDelegateHandle StreamsChangedDelegateHandle;
    FDelegateHandle DisplayClusterPostBackBufferUpdateHandle;
    FDelegateHandle PostOpaqueDelegateHandle;
    TMap<FString, TRefCountPtr<IPooledRenderTarget>> m_extractedDepth;
    TMap<FString, FVector2D> m_extractedDepthTAAJitter;
    TArray<FString> m_depthIds;

	TMap<FString, FString> Parameters;
	FString Id;
	static FString Type;
    bool m_EncodeDepth = false;

    IDisplayClusterViewportManager* ViewportManager; // Not owned by this class

    using ViewportIdOrdering = TArray<FString>;
    TQueue<ViewportIdOrdering> ViewportIdOrderPerFrame;
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
