// Fill out your copyright notice in the Description page of Project Settings.

#include "RenderStreamSceneViewExtension.h"
#include "Renderer/Private/SceneTextures.h"
#include "CoreGlobals.h"

DEFINE_LOG_CATEGORY(LogRenderStreamViewExtension);


FRenderStreamSceneViewExtension::FRenderStreamSceneViewExtension(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister)
{
    m_depthEnabled = true;
    
    IsActiveThisFrameFunctions.Empty();
    FSceneViewExtensionIsActiveFunctor IsActiveFunctor;
    IsActiveFunctor.IsActiveFunction = [=](const ISceneViewExtension* SceneViewExtension, const FSceneViewExtensionContext& Context)
    {
        return TOptional<bool>(m_depthEnabled);
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

void FRenderStreamSceneViewExtension::PostRenderBasePassDeferred_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView, const FRenderTargetBindingSlots& RenderTargets, TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures)
{
    auto depth = SceneTextures->GetContents()->SceneDepthTexture;
    GraphBuilder.QueueTextureExtraction(depth, &m_depthIntermediate);
}



bool FRenderStreamSceneViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
    return m_depthEnabled;
}
