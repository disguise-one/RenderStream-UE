// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "SceneViewExtension.h"

DECLARE_LOG_CATEGORY_EXTERN(LogRenderStreamViewExtension, Log, All);

class FRenderStreamSceneViewExtension : public FSceneViewExtensionBase
{
public:

    FRenderStreamSceneViewExtension(const FAutoRegister& AutoRegister);
    ~FRenderStreamSceneViewExtension();

    static TSharedPtr<FRenderStreamSceneViewExtension, ESPMode::ThreadSafe> Create();

    // dummy overrides
    virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) {}
    virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) {}
    virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) {}
    virtual void PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) {}
    virtual void PreRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) {}
    
    // meat and potatoes
    virtual void PostRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily);
    virtual void PostRenderBasePass_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) override;
	
    static void copyToIntermediateBuffer_RenderThread(FRHICommandListImmediate& RHICmdList, FTexture2DRHIRef& Src, FTexture2DRHIRef& Dst);
    
    FTexture2DRHIRef getExtractedDepth();

public:
    FTexture2DRHIRef m_intermediateDepth;

};
