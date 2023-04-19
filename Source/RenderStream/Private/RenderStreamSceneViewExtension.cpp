// Fill out your copyright notice in the Description page of Project Settings.

#include "RenderStreamSceneViewExtension.h"
#include "Renderer/Private/SceneTextures.h"
#include "CoreGlobals.h"
#include "FrameStream.h"
#include "RenderStream.h"
#include "Renderer/Private/PostProcess/PostProcessing.h"

DEFINE_LOG_CATEGORY(LogRenderStreamViewExtension);


FRenderStreamSceneViewExtension::FRenderStreamSceneViewExtension(const FAutoRegister& AutoRegister, const FString& ID) : FSceneViewExtensionBase(AutoRegister)
{
    m_depthEnabled = true;
    m_viewportId = ID;

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

TSharedPtr<FRenderStreamSceneViewExtension, ESPMode::ThreadSafe> FRenderStreamSceneViewExtension::Create(const FString& Id)
{
    auto ext =  FSceneViewExtensions::NewExtension<FRenderStreamSceneViewExtension>(Id);
    return ext;
}


void FRenderStreamSceneViewExtension::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs)
{
    auto depth = Inputs.SceneTextures->GetContents()->SceneDepthTexture;
    GraphBuilder.QueueTextureExtraction(depth, &m_depthIntermediate, ERDGResourceExtractionFlags::None);

}



bool FRenderStreamSceneViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
    return m_depthEnabled;
}
