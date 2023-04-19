// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "SceneViewExtension.h"

DECLARE_LOG_CATEGORY_EXTERN(LogRenderStreamViewExtension, Log, All);

class FRenderStreamSceneViewExtension : public FSceneViewExtensionBase
{
public:

    FRenderStreamSceneViewExtension(const FAutoRegister& AutoRegister, const FString& ID);
    ~FRenderStreamSceneViewExtension();

    static TSharedPtr<FRenderStreamSceneViewExtension, ESPMode::ThreadSafe> Create(const FString& Id);

    // dummy overrides
    virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) {}
    virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) {}
    virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) {}
    virtual void PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) {}
    virtual void PreRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) {}
    
    // Hook into the rendering pass and enqueue an extraction of the scene depth.
    // We do it here as we have access to the scene textures.
    virtual void PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs) override;

    TRefCountPtr<IPooledRenderTarget> getExtractedDepth() { return m_depthIntermediate; }

    bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;

private:
    bool m_depthEnabled;
    TRefCountPtr<IPooledRenderTarget> m_depthIntermediate;
    FString m_viewportId;
};
