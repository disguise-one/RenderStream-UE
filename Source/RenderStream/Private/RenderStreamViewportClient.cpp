#include "RenderStreamViewportClient.h"

#include "Camera/CameraActor.h"
#include "RenderStreamChannelDefinition.h"
#include "RenderStreamProjectionPolicy.h"
#include "Render/Device/DisplayClusterRenderViewport.h"
#include "Render/Device/IDisplayClusterRenderDevice.h"
#include "IDisplayCluster.h"
#include "Render/IDisplayClusterRenderManager.h"
#include "RenderStreamProjectionPolicy.h"

/// DisplayClusterViewportClient.cpp copy-pasta
#include "SceneView.h"
#include "Engine/Canvas.h"
#include "SceneViewExtension.h"
#include "IHeadMountedDisplay.h"
#include "IXRTrackingSystem.h"
#include "BufferVisualizationData.h"
#include "Engine/Engine.h"
#include "Engine/Console.h"
#include "Engine/LocalPlayer.h"
#include "UnrealEngine.h"
#include "EngineUtils.h"

#include "AudioDeviceManager.h"
#include "AudioDevice.h"
#include "Audio/AudioDebug.h"

#include "GameFramework/PlayerController.h"
#include "Debug/DebugDrawService.h"
#include "ContentStreaming.h"
#include "EngineModule.h"
#include "FXSystem.h"
#include "GameFramework/HUD.h"
#include "SubtitleManager.h"
#include "Components/LineBatchComponent.h"
#include "GameFramework/GameUserSettings.h"
#include "Framework/Application/SlateApplication.h"

#include "LegacyScreenPercentageDriver.h"
#include "DynamicResolutionState.h"
#include "EngineStats.h"

#include "Render/Device/IDisplayClusterRenderDevice.h"
#include "Render/Device/DisplayClusterRenderViewport.h"

#include "DisplayClusterEnums.h"
#include "DisplayClusterSceneViewExtensions.h"
//#include "Misc/DisplayClusterGlobals.h"
/// DisplayClusterViewportClient.cpp copy-pasta

#include "CustomStaticScreenPercentage.h"

URenderStreamViewportClient::URenderStreamViewportClient(FVTableHelper& Helper)
    : Super(Helper)
{}

URenderStreamViewportClient::~URenderStreamViewportClient() {}

/// DisplayClusterViewportClient.cpp copy-pasta

/** Util to find named canvas in transient package, and create if not found */
static UCanvas* GetCanvasByName(FName CanvasName)
{
    // Cache to avoid FString/FName conversions/compares
    static TMap<FName, UCanvas*> CanvasMap;
    UCanvas** FoundCanvas = CanvasMap.Find(CanvasName);
    if (!FoundCanvas)
    {
        UCanvas* CanvasObject = FindObject<UCanvas>(GetTransientPackage(), *CanvasName.ToString());
        if (!CanvasObject)
        {
            CanvasObject = NewObject<UCanvas>(GetTransientPackage(), CanvasName);
            CanvasObject->AddToRoot();
        }

        CanvasMap.Add(CanvasName, CanvasObject);
        return CanvasObject;
    }

    return *FoundCanvas;
}

void URenderStreamViewportClient::Draw(FViewport* InViewport, FCanvas* SceneCanvas)
{
    ////////////////////////////////
    // For any operation mode other than 'Cluster' we use default UGameViewportClient::Draw pipeline
    /// !!!! disguise customizations
    //const bool bIsNDisplayClusterMode = (GEngine->StereoRenderingDevice.IsValid() && GDisplayCluster->GetOperationMode() == EDisplayClusterOperationMode::Cluster);
    //if (!bIsNDisplayClusterMode)
    //{
    //    return UGameViewportClient::Draw(InViewport, SceneCanvas);
    //}
    /// !!!! disguise customizations

    // Get nDisplay stereo device
    IDisplayClusterRenderDevice* const DCRenderDevice = static_cast<IDisplayClusterRenderDevice* const>(GEngine->StereoRenderingDevice.Get());
    if (!DCRenderDevice)
    {
        return UGameViewportClient::Draw(InViewport, SceneCanvas);
    }

    ////////////////////////////////
    // Otherwise we use our own version of the UGameViewportClient::Draw which is basically
    // a simpler version of the original one but with multiple ViewFamilies support

    // Valid SceneCanvas is required.  Make this explicit.
    check(SceneCanvas);

    OnBeginDraw().Broadcast();

    const bool bStereoRendering = GEngine->IsStereoscopic3D(InViewport);
    FCanvas* DebugCanvas = InViewport->GetDebugCanvas();

    // Create a temporary canvas if there isn't already one.
    static FName CanvasObjectName(TEXT("CanvasObject"));
    UCanvas* CanvasObject = GetCanvasByName(CanvasObjectName);
    CanvasObject->Canvas = SceneCanvas;

    // Create temp debug canvas object
    FIntPoint DebugCanvasSize = InViewport->GetSizeXY();
    if (bStereoRendering && GEngine->XRSystem.IsValid() && GEngine->XRSystem->GetHMDDevice())
    {
        DebugCanvasSize = GEngine->XRSystem->GetHMDDevice()->GetIdealDebugCanvasRenderTargetSize();
    }

    static FName DebugCanvasObjectName(TEXT("DebugCanvasObject"));
    UCanvas* DebugCanvasObject = GetCanvasByName(DebugCanvasObjectName);
    DebugCanvasObject->Init(DebugCanvasSize.X, DebugCanvasSize.Y, NULL, DebugCanvas);

    if (DebugCanvas)
    {
        DebugCanvas->SetScaledToRenderTarget(bStereoRendering);
        DebugCanvas->SetStereoRendering(bStereoRendering);
    }
    if (SceneCanvas)
    {
        SceneCanvas->SetScaledToRenderTarget(bStereoRendering);
        SceneCanvas->SetStereoRendering(bStereoRendering);
    }

    // Force path tracing view mode, and extern code set path tracer show flags
    const bool bForcePathTracing = InViewport->GetClient()->GetEngineShowFlags()->PathTracing;
    if (bForcePathTracing)
    {
        EngineShowFlags.SetPathTracing(true);
        ViewModeIndex = VMI_PathTracing;
    }

    const int32 NumViews = DCRenderDevice->GetDesiredNumberOfViews(bStereoRendering);
    const int32 NumViewsPerFamily = 1;
    const int32 NumFamilies = NumViews / NumViewsPerFamily;

    UWorld* const MyWorld = GetWorld();
    APlayerController* const PlayerController = GEngine->GetFirstLocalPlayerController(GetWorld());
    ULocalPlayer* FirstLocalPlayer = nullptr;
    if (PlayerController)
    {
        FirstLocalPlayer = PlayerController->GetLocalPlayer();
    }

    if (!PlayerController || !FirstLocalPlayer)
    {
        return Super::Draw(InViewport, SceneCanvas);
    }

    /// !!!! disguise customizations
    IDisplayClusterRenderManager* RenderMgr = IDisplayCluster::Get().GetRenderMgr();
    const FRenderStreamProjectionPolicyFactory* RenderStreamFactory = static_cast<FRenderStreamProjectionPolicyFactory*>(RenderMgr->GetProjectionPolicyFactory(FRenderStreamProjectionPolicyFactory::RenderStreamPolicyType).Get());
    /// !!!! disguise customizations

    for (int32 ViewFamilyIdx = 0; ViewFamilyIdx < NumFamilies; ++ViewFamilyIdx)
    {
        // Create the view family for rendering the world scene to the viewport's render target
        FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(InViewport, MyWorld->Scene, EngineShowFlags)
            .SetRealtimeUpdate(true)
            .SetAdditionalViewFamily(ViewFamilyIdx > 0));

#if WITH_EDITOR
        if (GIsEditor)
        {
            // Force enable view family show flag for HighDPI derived's screen percentage.
            ViewFamily.EngineShowFlags.ScreenPercentage = true;
        }

        UpdateDebugViewModeShaders();
#endif

        // Gather Scene View Extensions
        {
            // Scene View Extension activation with ViewportId granularity only works if you have one ViewFamily per ViewportId
            check(NumViewsPerFamily == 1);

            // If not in Mono, the number of Views may be a factor of the number of ViewportIds.
            const uint32 ViewsAmountPerViewport = DCRenderDevice->GetViewsAmountPerViewport();
            check(ViewsAmountPerViewport > 0);

            const int32 ViewportIdx = (ViewFamilyIdx * NumViewsPerFamily) / ViewsAmountPerViewport;
            const FDisplayClusterRenderViewport* RenderViewport = DCRenderDevice->GetRenderViewport(ViewportIdx);
            check(RenderViewport);

            const FString ViewportId = RenderViewport->GetId();
            FDisplayClusterSceneViewExtensionContext ViewExtensionContext(InViewport, ViewportId);

            ViewFamily.ViewExtensions = GEngine->ViewExtensions->GatherActiveExtensions(ViewExtensionContext);
        }

        for (auto ViewExt : ViewFamily.ViewExtensions)
        {
            ViewExt->SetupViewFamily(ViewFamily);
        }

        ViewFamily.ViewMode = EViewModeIndex(ViewModeIndex);
        EngineShowFlagOverride(ESFIM_Game, ViewFamily.ViewMode, ViewFamily.EngineShowFlags, false);

        if (ViewFamily.EngineShowFlags.VisualizeBuffer && AllowDebugViewmodes())
        {
            // Process the buffer visualization console command
            FName NewBufferVisualizationMode = NAME_None;
            static IConsoleVariable* ICVar = IConsoleManager::Get().FindConsoleVariable(FBufferVisualizationData::GetVisualizationTargetConsoleCommandName());
            if (ICVar)
            {
                static const FName OverviewName = TEXT("Overview");
                FString ModeNameString = ICVar->GetString();
                FName ModeName = *ModeNameString;
                if (ModeNameString.IsEmpty() || ModeName == OverviewName || ModeName == NAME_None)
                {
                    NewBufferVisualizationMode = NAME_None;
                }
                else
                {
                    if (GetBufferVisualizationData().GetMaterial(ModeName) == NULL)
                    {
                        // Mode is out of range, so display a message to the user, and reset the mode back to the previous valid one
                        UE_LOG(LogConsoleResponse, Warning, TEXT("Buffer visualization mode '%s' does not exist"), *ModeNameString);
                        NewBufferVisualizationMode = GetCurrentBufferVisualizationMode();
                        // todo: cvars are user settings, here the cvar state is used to avoid log spam and to auto correct for the user (likely not what the user wants)
                        ICVar->Set(*NewBufferVisualizationMode.GetPlainNameString(), ECVF_SetByCode);
                    }
                    else
                    {
                        NewBufferVisualizationMode = ModeName;
                    }
                }
            }

            if (NewBufferVisualizationMode != GetCurrentBufferVisualizationMode())
            {
                SetCurrentBufferVisualizationMode(NewBufferVisualizationMode);
            }
        }

        const FDisplayClusterRenderViewport* RenderViewport = DCRenderDevice->GetRenderViewport(ViewFamilyIdx * NumViewsPerFamily);

        TMap<ULocalPlayer*, FSceneView*> PlayerViewMap;

        FAudioDeviceHandle RetrievedAudioDevice = MyWorld->GetAudioDevice();
        TArray<FSceneView*> Views;

        for (int32 ViewIdx = 0; ViewIdx < NumViewsPerFamily; ++ViewIdx)
        {
            const int32 DCViewIdx = ViewFamilyIdx * NumViewsPerFamily + ViewIdx;

            // Calculate the player's view information.
            FVector		ViewLocation;
            FRotator	ViewRotation;

            EStereoscopicPass PassType = bStereoRendering ? GEngine->StereoRenderingDevice->GetViewPassForIndex(bStereoRendering, DCViewIdx) : eSSP_FULL;

            /// !!!! disguise customizations
            ULocalPlayer* LocalPlayer = FirstLocalPlayer;

            const TSharedPtr<FRenderStreamProjectionPolicy> Policy = RenderStreamFactory->GetPolicyBySceneViewFamily(ViewFamilyIdx);
            if (APlayerController* PolicyController = UGameplayStatics::GetPlayerControllerFromID(World, Policy->GetPlayerControllerID()))
            {
                LocalPlayer = PolicyController->GetLocalPlayer();
            }
            /// !!!! disguise customizations

            FSceneView* View = LocalPlayer->CalcSceneView(&ViewFamily, ViewLocation, ViewRotation, InViewport, nullptr, PassType);

            if (View)
            {
                Views.Add(View);
                if (RenderViewport)
                {
                    // Support MGPU viewport mapping
                    if (RenderViewport->GetGPUIndex() >= 0)
                    {
                        View->bOverrideGPUMask = true;
                        View->GPUMask = FRHIGPUMask::FromIndex(RenderViewport->GetGPUIndex());
                    }

                    // Control CrossGPU transfer for this viewport
                    View->bAllowCrossGPUTransfer = RenderViewport->IsCrossGPUTransferAllowed();
                }

                // We don't allow instanced stereo currently
                View->bIsInstancedStereoEnabled = false;
                View->bShouldBindInstancedViewUB = false;

                if (View->Family->EngineShowFlags.Wireframe)
                {
                    // Wireframe color is emissive-only, and mesh-modifying materials do not use material substitution, hence...
                    View->DiffuseOverrideParameter = FVector4(0.f, 0.f, 0.f, 0.f);
                    View->SpecularOverrideParameter = FVector4(0.f, 0.f, 0.f, 0.f);
                }
                else if (View->Family->EngineShowFlags.OverrideDiffuseAndSpecular)
                {
                    View->DiffuseOverrideParameter = FVector4(GEngine->LightingOnlyBrightness.R, GEngine->LightingOnlyBrightness.G, GEngine->LightingOnlyBrightness.B, 0.0f);
                    View->SpecularOverrideParameter = FVector4(.1f, .1f, .1f, 0.0f);
                }
                else if (View->Family->EngineShowFlags.LightingOnlyOverride)
                {
                    View->DiffuseOverrideParameter = FVector4(GEngine->LightingOnlyBrightness.R, GEngine->LightingOnlyBrightness.G, GEngine->LightingOnlyBrightness.B, 0.0f);
                    View->SpecularOverrideParameter = FVector4(0.f, 0.f, 0.f, 0.f);
                }
                else if (View->Family->EngineShowFlags.ReflectionOverride)
                {
                    View->DiffuseOverrideParameter = FVector4(0.f, 0.f, 0.f, 0.f);
                    View->SpecularOverrideParameter = FVector4(1, 1, 1, 0.0f);
                    View->NormalOverrideParameter = FVector4(0, 0, 1, 0.0f);
                    View->RoughnessOverrideParameter = FVector2D(0.0f, 0.0f);
                }

                if (!View->Family->EngineShowFlags.Diffuse)
                {
                    View->DiffuseOverrideParameter = FVector4(0.f, 0.f, 0.f, 0.f);
                }

                if (!View->Family->EngineShowFlags.Specular)
                {
                    View->SpecularOverrideParameter = FVector4(0.f, 0.f, 0.f, 0.f);
                }

                View->CurrentBufferVisualizationMode = GetCurrentBufferVisualizationMode();

                View->CameraConstrainedViewRect = View->UnscaledViewRect;


                {
                    // Save the location of the view.
                    LocalPlayer->LastViewLocation = ViewLocation;

                    PlayerViewMap.Add(LocalPlayer, View);

                    // Update the listener.
                    if (RetrievedAudioDevice && PlayerController != NULL)
                    {
                        bool bUpdateListenerPosition = true;

                        // If the main audio device is used for multiple PIE viewport clients, we only
                        // want to update the main audio device listener position if it is in focus
                        if (GEngine)
                        {
                            FAudioDeviceManager* AudioDeviceManager = GEngine->GetAudioDeviceManager();

                            // If there is more than one world referencing the main audio device
                            if (AudioDeviceManager->GetNumMainAudioDeviceWorlds() > 1)
                            {
                                uint32 MainAudioDeviceID = GEngine->GetMainAudioDeviceID();
                                if (AudioDevice->DeviceID == MainAudioDeviceID && !HasAudioFocus())
                                {
                                    bUpdateListenerPosition = false;
                                }
                            }
                        }

                        if (bUpdateListenerPosition)
                        {
                            FVector Location;
                            FVector ProjFront;
                            FVector ProjRight;
                            PlayerController->GetAudioListenerPosition(Location, ProjFront, ProjRight);

                            FTransform ListenerTransform(FRotationMatrix::MakeFromXY(ProjFront, ProjRight));

                            // Allow the HMD to adjust based on the head position of the player, as opposed to the view location
                            if (GEngine->XRSystem.IsValid() && GEngine->StereoRenderingDevice.IsValid() && GEngine->StereoRenderingDevice->IsStereoEnabled())
                            {
                                const FVector Offset = GEngine->XRSystem->GetAudioListenerOffset();
                                Location += ListenerTransform.TransformPositionNoScale(Offset);
                            }

                            ListenerTransform.SetTranslation(Location);
                            ListenerTransform.NormalizeRotation();

                            uint32 ViewportIndex = PlayerViewMap.Num() - 1;
                            RetrievedAudioDevice->SetListener(MyWorld, ViewportIndex, ListenerTransform, (View->bCameraCut ? 0.f : MyWorld->GetDeltaSeconds()));

                            FVector OverrideAttenuation;
                            if (PlayerController->GetAudioListenerAttenuationOverridePosition(OverrideAttenuation))
                            {
                                RetrievedAudioDevice->SetListenerAttenuationOverride(ViewportIndex, OverrideAttenuation);
                            }
                            else
                            {
                                RetrievedAudioDevice->ClearListenerAttenuationOverride(ViewportIndex);
                            }
                        }
                    }

#if RHI_RAYTRACING
                    View->SetupRayTracedRendering();
#endif

                }

                // Add view information for resource streaming. Allow up to 5X boost for small FOV.
                const float StreamingScale = 1.f / FMath::Clamp<float>(View->LODDistanceFactor, .2f, 1.f);
                IStreamingManager::Get().AddViewInformation(View->ViewMatrices.GetViewOrigin(), View->UnscaledViewRect.Width(), View->UnscaledViewRect.Width() * View->ViewMatrices.GetProjectionMatrix().M[0][0], StreamingScale);
                MyWorld->ViewLocationsRenderedLastFrame.Add(View->ViewMatrices.GetViewOrigin());
            }
        }

#if CSV_PROFILER
        UpdateCsvCameraStats(PlayerViewMap);
#endif

        /// !!!! disguise customizations
        FinalizeViewFamily(ViewFamilyIdx, &ViewFamily, PlayerViewMap);
        /// !!!! disguise customizations

        // Force screen percentage show flag to be turned off if not supported.
        if (!ViewFamily.SupportsScreenPercentage())
        {
            ViewFamily.EngineShowFlags.ScreenPercentage = false;
        }

        // Set up secondary resolution fraction for the view family.
        if (!bStereoRendering && ViewFamily.SupportsScreenPercentage())
        {
            float CustomSecondaryScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SecondaryScreenPercentage.GameViewport"), false)->GetFloat();
            if (CustomSecondaryScreenPercentage > 0.0)
            {
                // Override secondary resolution fraction with CVar.
                ViewFamily.SecondaryViewFraction = FMath::Min(CustomSecondaryScreenPercentage / 100.0f, 1.0f);
            }
            else
            {
                // Automatically compute secondary resolution fraction from DPI.
                ViewFamily.SecondaryViewFraction = GetDPIDerivedResolutionFraction();
            }

            check(ViewFamily.SecondaryViewFraction > 0.0f);
        }
        
        checkf(ViewFamily.GetScreenPercentageInterface() == nullptr,
            TEXT("Some code has tried to set up an alien screen percentage driver, that could be wrong if not supported very well by the RHI."));

        // Setup main view family with screen percentage interface by dynamic resolution if screen percentage is enabled.
#if WITH_DYNAMIC_RESOLUTION
        if (ViewFamily.EngineShowFlags.ScreenPercentage)
        {
            FDynamicResolutionStateInfos DynamicResolutionStateInfos;
            GEngine->GetDynamicResolutionCurrentStateInfos(DynamicResolutionStateInfos);

            // Do not allow dynamic resolution to touch the view family if not supported to ensure there is no possibility to ruin
            // game play experience on platforms that does not support it, but have it enabled by mistake.
            if (DynamicResolutionStateInfos.Status == EDynamicResolutionStatus::Enabled)
            {
                GEngine->EmitDynamicResolutionEvent(EDynamicResolutionStateEvent::BeginDynamicResolutionRendering);
                GEngine->GetDynamicResolutionState()->SetupMainViewFamily(ViewFamily);
            }
            else if (DynamicResolutionStateInfos.Status == EDynamicResolutionStatus::DebugForceEnabled)
            {
                GEngine->EmitDynamicResolutionEvent(EDynamicResolutionStateEvent::BeginDynamicResolutionRendering);
                ViewFamily.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(
                    ViewFamily,
                    DynamicResolutionStateInfos.ResolutionFractionApproximation,
                    false,
                    DynamicResolutionStateInfos.ResolutionFractionUpperBound));
            }

#if CSV_PROFILER
            if (DynamicResolutionStateInfos.ResolutionFractionApproximation >= 0.0f)
            {
                CSV_CUSTOM_STAT_GLOBAL(DynamicResolutionPercentage, DynamicResolutionStateInfos.ResolutionFractionApproximation * 100.0f, ECsvCustomStatOp::Set);
            }
#endif
        }
#endif

        if (GCustomStaticScreenPercentage && ViewFamily.ViewMode == EViewModeIndex::VMI_Lit)
        {
            GCustomStaticScreenPercentage->SetupMainGameViewFamily(ViewFamily);
            if (ViewFamily.GetTemporalUpscalerInterface() != nullptr)
            {
                for (FSceneView* View : Views)
                {
                    View->PrimaryScreenPercentageMethod = EPrimaryScreenPercentageMethod::TemporalUpscale;
                }
            }
        }
        // If a screen percentage interface was not set by dynamic resolution, then create one matching legacy behavior.
        if (ViewFamily.GetScreenPercentageInterface() == nullptr)
        {
            // In case of stereo, we set the same buffer ratio for both left and right views (taken from left)
            float CustomBufferRatio = RenderViewport ? RenderViewport->GetBufferRatio() : 1;

            bool AllowPostProcessSettingsScreenPercentage = false;
            float GlobalResolutionFraction = 1.0f;
            float SecondaryScreenPercentage = 1.0f;

            if (ViewFamily.EngineShowFlags.ScreenPercentage)
            {
                // Allow FPostProcessSettings::ScreenPercentage.
                AllowPostProcessSettingsScreenPercentage = true;

                // Get global view fraction set by r.ScreenPercentage.
                GlobalResolutionFraction = FLegacyScreenPercentageDriver::GetCVarResolutionFraction() * CustomBufferRatio;

                // We need to split the screen percentage if below 0.5 because TAA upscaling only works well up to 2x.
                if (GlobalResolutionFraction < 0.5f)
                {
                    SecondaryScreenPercentage = 2.0f * GlobalResolutionFraction;
                    GlobalResolutionFraction = 0.5f;
                }
            }

            ViewFamily.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(
                ViewFamily, GlobalResolutionFraction, AllowPostProcessSettingsScreenPercentage));

            ViewFamily.SecondaryViewFraction = SecondaryScreenPercentage;
        }
        else if (bStereoRendering)
        {
            // Change screen percentage method to raw output when doing dynamic resolution with VR if not using TAA upsample.
            for (FSceneView* View : Views)
            {
                if (View->PrimaryScreenPercentageMethod == EPrimaryScreenPercentageMethod::SpatialUpscale)
                {
                    View->PrimaryScreenPercentageMethod = EPrimaryScreenPercentageMethod::RawOutput;
                }
            }
        }

        ViewFamily.bIsHDR = GetWindow().IsValid() ? GetWindow().Get()->GetIsHDR() : false;

        // Draw the player views.
        if (!bDisableWorldRendering && PlayerViewMap.Num() > 0 && FSlateApplication::Get().GetPlatformApplication()->IsAllowedToRender()) //-V560
        {
            GetRendererModule().BeginRenderingViewFamily(SceneCanvas, &ViewFamily);
        }
        else
        {
            GetRendererModule().PerFrameCleanupIfSkipRenderer();

            // Make sure RHI resources get flushed if we're not using a renderer
            ENQUEUE_RENDER_COMMAND(UGameViewportClient_FlushRHIResources)(
                [](FRHICommandListImmediate& RHICmdList)
                {
                    RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);
                });
        }
    }

    // Beyond this point, only UI rendering independent from dynamic resolution.
    GEngine->EmitDynamicResolutionEvent(EDynamicResolutionStateEvent::EndDynamicResolutionRendering);

    // Update level streaming.
    MyWorld->UpdateLevelStreaming();

    // Remove temporary debug lines.
    if (MyWorld->LineBatcher != nullptr)
    {
        MyWorld->LineBatcher->Flush();
    }

    if (MyWorld->ForegroundLineBatcher != nullptr)
    {
        MyWorld->ForegroundLineBatcher->Flush();
    }

    // Draw FX debug information.
    if (MyWorld->FXSystem)
    {
        MyWorld->FXSystem->DrawDebug(SceneCanvas);
    }

    {
        //ensure canvas has been flushed before rendering UI
        SceneCanvas->Flush_GameThread();

        OnDrawn().Broadcast();

        // Allow the viewport to render additional stuff
        PostRender(DebugCanvasObject);
    }

    // Grab the player camera location and orientation so we can pass that along to the stats drawing code.
    FVector PlayerCameraLocation = FVector::ZeroVector;
    FRotator PlayerCameraRotation = FRotator::ZeroRotator;
    PlayerController->GetPlayerViewPoint(PlayerCameraLocation, PlayerCameraRotation);

    if (DebugCanvas)
    {
        // Reset the debug canvas to be full-screen before drawing the console
        // (the debug draw service above has messed with the viewport size to fit it to a single player's subregion)
        DebugCanvasObject->Init(DebugCanvasSize.X, DebugCanvasSize.Y, NULL, DebugCanvas);

        DrawStatsHUD(MyWorld, InViewport, DebugCanvas, DebugCanvasObject, DebugProperties, PlayerCameraLocation, PlayerCameraRotation);

        if (GEngine->IsStereoscopic3D(InViewport))
        {
#if 0 //!UE_BUILD_SHIPPING
            // TODO: replace implementation in OculusHMD with a debug renderer
            if (GEngine->XRSystem.IsValid())
            {
                GEngine->XRSystem->DrawDebug(DebugCanvasObject);
            }
#endif
        }

        // Render the console absolutely last because developer input is was matter the most.
        if (ViewportConsole)
        {
            ViewportConsole->PostRender_Console(DebugCanvasObject);
        }
    }

    OnEndDraw().Broadcast();
}

/// DisplayClusterViewportClient.cpp copy-pasta

void URenderStreamViewportClient::FinalizeViewFamily(int32 ViewFamilyIdx, FSceneViewFamily* ViewFamily, const TMap<ULocalPlayer*, FSceneView*>& PlayerViewMap)
{
    IDisplayClusterRenderDevice* const DCRenderDevice = static_cast<IDisplayClusterRenderDevice* const>(GEngine->StereoRenderingDevice.Get());

    for (const FSceneView* const& ViewConst : ViewFamily->Views)
    {
        TSet<FPrimitiveComponentId> Collection;
        FSceneView* View = const_cast<FSceneView*>(ViewConst);

        IDisplayClusterRenderManager* RenderMgr = IDisplayCluster::Get().GetRenderMgr();
        const FRenderStreamProjectionPolicyFactory* Factory = static_cast<FRenderStreamProjectionPolicyFactory*>(RenderMgr->GetProjectionPolicyFactory(FRenderStreamProjectionPolicyFactory::RenderStreamPolicyType).Get());
        const TSharedPtr<FRenderStreamProjectionPolicy> Policy = Factory->GetPolicyBySceneViewFamily(ViewFamilyIdx);
        if (!Policy)
            continue;

        const ACameraActor* Camera = Policy->GetTemplateCamera();
        const URenderStreamChannelDefinition* Definition = Camera ? Camera->FindComponentByClass<URenderStreamChannelDefinition>() : nullptr;
        if (Definition != nullptr)
        {
            // This only really works if we have a single view per view family which is currently the case in nDisplay.
            ViewFamily->EngineShowFlags = Definition->ShowFlags;
            const TArray<TWeakObjectPtr<AActor>> Actors = Definition->DefaultVisibility == EVisibilty::Visible ? Definition->Hidden : Definition->Visible;
            for (const TWeakObjectPtr<AActor> Actor : Actors)
            {
                if (Actor.IsValid())
                {
                    Actor->ForEachComponent<UPrimitiveComponent>(false, [&Collection](const UPrimitiveComponent* PrimativeComponent)
                    {
                        Collection.Add(PrimativeComponent->ComponentId);
                    });
                }
            }

            if (Definition->DefaultVisibility == EVisibilty::Visible)
                View->HiddenPrimitives = Collection;
            else
                View->ShowOnlyPrimitives = Collection;
        }
    }

    FinalizeViews(ViewFamily, PlayerViewMap);
}
