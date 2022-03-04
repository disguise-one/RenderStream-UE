// Fill out your copyright notice in the Description page of Project Settings.

#include "RenderStreamSceneViewExtension.h"
#include "Renderer/Private/PostProcess/SceneRenderTargets.h"

DEFINE_LOG_CATEGORY(LogRenderStreamViewExtension);

FRenderStreamSceneViewExtension::FRenderStreamSceneViewExtension(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister)
{
    IsActiveThisFrameFunctions.Empty();
    FSceneViewExtensionIsActiveFunctor IsActiveFunctor;
    IsActiveFunctor.IsActiveFunction = [](const ISceneViewExtension* SceneViewExtension, const FSceneViewExtensionContext& Context)
    {
        return TOptional<bool>(true);
    };
    IsActiveThisFrameFunctions.Add(IsActiveFunctor);
}

FRenderStreamSceneViewExtension::~FRenderStreamSceneViewExtension()
{
}

TSharedPtr<FRenderStreamSceneViewExtension, ESPMode::ThreadSafe> FRenderStreamSceneViewExtension::Create()
{
    auto ext =  FSceneViewExtensions::NewExtension<FRenderStreamSceneViewExtension>();
    return ext;
}

void FRenderStreamSceneViewExtension::PostRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily)
{
    
    FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
    FTexture2DRHIRef depthTex = SceneContext.GetSceneDepthTexture();
    copyToIntermediateBuffer_RenderThread(RHICmdList, depthTex, m_intermediateDepth);

    UE_LOG(LogRenderStreamViewExtension, Verbose, TEXT("Copied SceneDepth texture on PostRenderViewFamily_RenderThread"));
    
}

void FRenderStreamSceneViewExtension::PostRenderBasePass_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView)
{
    // Here is where GBuffer info should be extracted

}

/*static*/ void FRenderStreamSceneViewExtension::copyToIntermediateBuffer_RenderThread(FRHICommandListImmediate& RHICmdList, FTexture2DRHIRef& Src, FTexture2DRHIRef& Dst)
{
    if (!Dst.IsValid() || Dst->GetSizeXY() != Src->GetSizeXY())
    {
        FRHIResourceCreateInfo createInfo;
        Dst = RHICreateTexture2D(Src->GetSizeX(),
            Src->GetSizeY(),
            Src->GetFormat(),
            Src->GetNumMips(),
            Src->GetNumSamples(),
            Src->GetFlags(),
            createInfo);
        FString name(Src->GetName().ToString() + " intermediate");
        Dst->SetName(FName(name));
        UE_LOG(LogRenderStreamViewExtension, Verbose, TEXT("Intermediate texture created for %s"), *Src->GetName().ToString());
    }

    if (RHICmdList.IsOutsideRenderPass())
    {
        FRHICopyTextureInfo copyInfo;
        uint32 sizeX = Src->GetSizeX();
        uint32 sizeY = Src->GetSizeY();
        copyInfo.SourcePosition = FIntVector(0, 0, 0);
        copyInfo.DestPosition = FIntVector(0, 0, 0);
        copyInfo.Size = FIntVector(sizeX, sizeY, 1);
        RHICmdList.CopyTexture(Src, Dst, copyInfo);
    }
    else
    {
        UE_LOG(LogRenderStreamViewExtension, Verbose, TEXT("Copying %s into %s"), *Src->GetName().ToString(), *Dst->GetName().ToString());
        FResolveParams params;
        uint32 sizeX = Src->GetSizeX();
        uint32 sizeY = Src->GetSizeY();
        params.Rect = FResolveRect(FIntRect(FIntPoint(0, 0), FIntPoint(sizeX, sizeY)));
        params.DestRect = FResolveRect(FIntRect(FIntPoint(0, 0), FIntPoint(sizeX, sizeY)));
        RHICmdList.CopyToResolveTarget(Src, Dst, params);
    }
}
FTexture2DRHIRef FRenderStreamSceneViewExtension::getExtractedDepth()
{
    return m_intermediateDepth.IsValid() ? m_intermediateDepth : nullptr;
}

bool FRenderStreamSceneViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
    return true;
}
