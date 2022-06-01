#include "RenderStreamValidation.h"
#include "RenderStreamChannelCacheAsset.h"
#include "RenderStreamEditorModule.h"

#include "UObject/Class.h"
#include "Engine/RendererSettings.h"
#include "Engine/PostProcessVolume.h"
#include "Logging/TokenizedMessage.h"
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"
#include "Misc/MapErrors.h"
#include "Camera/CameraComponent.h"
#include "LevelEditorSubsystem.h"

bool FRenderStreamValidation::ValidateProjectSettings()
{
    FMessageLog RSV("RenderStreamValidation");
    RSV.SuppressLoggingToOutputLog(true);
    bool IssuesFound = false;

    const URendererSettings* RenderSettings = GetDefault<URendererSettings>();

    // Texture streaming
    if (RenderSettings->bTextureStreaming)
    {
        IssuesFound = true;
        FOnActionTokenExecuted FixTextureStreaming;
        FixTextureStreaming.BindLambda([]() {
            URendererSettings* RenderSettings = GetMutableDefault<URendererSettings>();
            RenderSettings->bTextureStreaming = false;
            RenderSettings->SaveConfig();
            ForceRunValidation();
            });
        RSV.Warning()->AddToken(FTextToken::Create(FText::FromString("Texture streaming enabled in project settings. May cause pixellated content.")))
            ->AddToken(FActionToken::Create(FText::FromString("Fix"), FText::FromString("FixTextureStreaming"), FixTextureStreaming));
    }

    // Global illumination
    if (RenderSettings->DynamicGlobalIllumination != EDynamicGlobalIlluminationMethod::None)
    {
        IssuesFound = true;
        FOnActionTokenExecuted FixGI;
        FixGI.BindLambda([]() {
            URendererSettings* RenderSettings = GetMutableDefault<URendererSettings>();
            RenderSettings->DynamicGlobalIllumination = EDynamicGlobalIlluminationMethod::None;
            RenderSettings->SaveConfig();
            ForceRunValidation();
            });
        RSV.Warning()->AddToken(FTextToken::Create(FText::FromString("Global illumination enabled in project settings. May cause seams if using clustered rendering.")))
            ->AddToken(FActionToken::Create(FText::FromString("Fix"), FText::FromString("FixGI"), FixGI));
    }

    // Ray-tracing
    if (RenderSettings->bEnableRayTracing)
    {
        IssuesFound = true;
        FOnActionTokenExecuted FixRayTracing;
        FixRayTracing.BindLambda([]() {
            URendererSettings* RenderSettings = GetMutableDefault<URendererSettings>();
            RenderSettings->bEnableRayTracing = false;
            RenderSettings->SaveConfig();
            ForceRunValidation();
            });
        RSV.Warning()->AddToken(FTextToken::Create(FText::FromString("Ray tracing enabled in project settings. May cause seams if using clustered rendering")))
            ->AddToken(FActionToken::Create(FText::FromString("Fix"), FText::FromString("FixRayTracing"), FixRayTracing));
    }

    return IssuesFound;
}

FRenderStreamChannelInfo FRenderStreamValidation::GetChannelInfo(TWeakObjectPtr<URenderStreamChannelDefinition> ChannelDefinition, const ULevel* Level)
{
    FRenderStreamChannelInfo Info;
    if (ChannelDefinition.IsValid())
    {
        // Get the show flags from the channel definition
        ChannelDefinition->UpdateShowFlags();
        Info.ShowFlags = ChannelDefinition->ShowFlags;

        // Get post-processing settings from the camera
        ACameraActor* Camera = Cast<ACameraActor>(ChannelDefinition->GetOwner());
        if (Camera)
        {
            Info.PostProcessSettings = Camera->GetCameraComponent()->PostProcessSettings;
            Info.Name = TCHAR_TO_UTF8(*Camera->GetName());
        }

        // Check if any post-processing volumes in the level enable settings (camera settings take priority when overridden)
        for (const AActor* Actor : Level->Actors)
        {
            if (const APostProcessVolume* PPV = dynamic_cast<const APostProcessVolume*>(Actor))
            {
                if (!Info.PostProcessSettings.bOverride_MotionBlurAmount && PPV->Settings.bOverride_MotionBlurAmount && PPV->Settings.MotionBlurAmount > 0.f)  // Motion blur
                {
                    Info.PostProcessSettings.bOverride_MotionBlurAmount = true;
                    Info.PostProcessSettings.MotionBlurAmount = PPV->Settings.MotionBlurAmount;
                }
                if (!Info.PostProcessSettings.bOverride_BloomIntensity && PPV->Settings.bOverride_BloomIntensity && PPV->Settings.BloomIntensity > 0.f)  // Bloom
                {
                    Info.PostProcessSettings.bOverride_BloomIntensity = true;
                    Info.PostProcessSettings.BloomIntensity = PPV->Settings.BloomIntensity;
                }
                if (!Info.PostProcessSettings.bOverride_AmbientOcclusionIntensity && PPV->Settings.bOverride_AmbientOcclusionIntensity && PPV->Settings.AmbientOcclusionIntensity > 0.f)  // Screen-space AO
                {
                    Info.PostProcessSettings.bOverride_AmbientOcclusionIntensity = true;
                    Info.PostProcessSettings.AmbientOcclusionIntensity = PPV->Settings.AmbientOcclusionIntensity;
                }
                if (!Info.PostProcessSettings.bOverride_AutoExposureMethod && PPV->Settings.bOverride_AutoExposureMethod && PPV->Settings.AutoExposureMethod != EAutoExposureMethod::AEM_Manual)  // Eye adaptation
                {
                    Info.PostProcessSettings.bOverride_AutoExposureMethod = true;
                    Info.PostProcessSettings.AutoExposureMethod = PPV->Settings.AutoExposureMethod;
                }
                if (!Info.PostProcessSettings.bOverride_LensFlareIntensity && PPV->Settings.bOverride_LensFlareIntensity && PPV->Settings.LensFlareIntensity > 0.f)  // Lens flares
                {
                    Info.PostProcessSettings.bOverride_LensFlareIntensity = true;
                    Info.PostProcessSettings.LensFlareIntensity = PPV->Settings.LensFlareIntensity;
                }
                if (!Info.PostProcessSettings.bOverride_ScreenSpaceReflectionIntensity && PPV->Settings.bOverride_ScreenSpaceReflectionIntensity && PPV->Settings.ScreenSpaceReflectionIntensity > 0.f)  // Screen-space reflections
                {
                    Info.PostProcessSettings.bOverride_ScreenSpaceReflectionIntensity = true;
                    Info.PostProcessSettings.ScreenSpaceReflectionIntensity = PPV->Settings.ScreenSpaceReflectionIntensity;
                }
                if (!Info.PostProcessSettings.bOverride_SceneFringeIntensity && PPV->Settings.bOverride_SceneFringeIntensity && PPV->Settings.SceneFringeIntensity > 0.f)  // Scene color fringe
                {
                    Info.PostProcessSettings.bOverride_SceneFringeIntensity = true;
                    Info.PostProcessSettings.SceneFringeIntensity = PPV->Settings.SceneFringeIntensity;
                }
                if (!Info.PostProcessSettings.bOverride_ToneCurveAmount && PPV->Settings.bOverride_ToneCurveAmount && PPV->Settings.ToneCurveAmount > 0.f)  // Tone curve
                {
                    Info.PostProcessSettings.bOverride_ToneCurveAmount = true;
                    Info.PostProcessSettings.ToneCurveAmount = PPV->Settings.ToneCurveAmount;
                }
                if (!Info.PostProcessSettings.bOverride_ExpandGamut && PPV->Settings.bOverride_ExpandGamut && PPV->Settings.ExpandGamut > 0.f)  // ExpandGamut
                {
                    Info.PostProcessSettings.bOverride_ExpandGamut = true;
                    Info.PostProcessSettings.ExpandGamut = PPV->Settings.ExpandGamut;
                }
                if (!Info.PostProcessSettings.bOverride_VignetteIntensity && PPV->Settings.bOverride_VignetteIntensity && PPV->Settings.VignetteIntensity > 0.f)  // Vignette
                {
                    Info.PostProcessSettings.bOverride_VignetteIntensity = true;
                    Info.PostProcessSettings.VignetteIntensity = PPV->Settings.VignetteIntensity;
                } 
            }
        }

    }
    return Info;
}

void DisableShowFlag(const FRenderStreamChannelInfo& Info, const FString& LevelName, const FString& SettingName)
{
    FMessageLog RSV("RenderStreamValidation");

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        RSV.Error()->AddToken(FTextToken::Create(FText::FromString(FString::Printf(TEXT("Could not fix %s in channel %s. Invalid world."), *SettingName, *Info.Name))));
        return;
    }

    // Check if the level is loaded
    bool LevelLoaded = false;
    for (ULevel* Level : World->GetLevels())
    {
        if (Level && Level->GetPackage()->GetPathName() == LevelName)
            LevelLoaded = true;
    }
    for (const ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
    {
        if (StreamingLevel->IsLevelLoaded() && StreamingLevel->GetPackage()->GetPathName() == LevelName)
            LevelLoaded = true;
    }

    if (!LevelLoaded)
    {
        // Save current level and load the required level for the channel
		ULevelEditorSubsystem* LevelEditorSubsystem = GEditor ? GEditor->GetEditorSubsystem<ULevelEditorSubsystem>() : nullptr;
		if (!LevelEditorSubsystem)
		{
			RSV.Error()->AddToken(FTextToken::Create(FText::FromString(FString::Printf(TEXT("Could not fix %s in channel %s. Could not get level editor subsystem."), *SettingName, *Info.Name))));
			return;
		}
		LevelEditorSubsystem->SaveCurrentLevel();
        if (!LevelEditorSubsystem->LoadLevel(LevelName))
        {
            RSV.Error()->AddToken(FTextToken::Create(FText::FromString(FString::Printf(TEXT("Could not fix %s in channel %s. Unable to load level %s."), *SettingName, *Info.Name, *LevelName))));
            return;
        }
    }

    // Try to find the channel definition in the loaded levels
    TArray<AActor*> FoundCameras;
    UGameplayStatics::GetAllActorsOfClass(GWorld, ACameraActor::StaticClass(), FoundCameras);
    for (AActor* Camera : FoundCameras)
    {
        if (Camera && Camera->GetName() == Info.Name)
        {
            TWeakObjectPtr<URenderStreamChannelDefinition> Definition = Camera->FindComponentByClass<URenderStreamChannelDefinition>();
            if (Definition.IsValid())
            {
                Definition->Modify(true);  // Ensure the definition gets marked as dirty

                // Update or add show flag setting
                bool SettingFound = false;
                for (FEngineShowFlagsSetting& ShowFlagSetting : Definition->ShowFlagSettings)
                {
                    if (ShowFlagSetting.ShowFlagName == SettingName)
                    {
                        ShowFlagSetting.Enabled = false;
                        SettingFound = true;
                    }
                }
                if (!SettingFound)
                {
                    FEngineShowFlagsSetting ShowFlagSetting;
                    ShowFlagSetting.ShowFlagName = SettingName;
                    ShowFlagSetting.Enabled = false;
                    Definition->ShowFlagSettings.Add(ShowFlagSetting);
                }

                // Ensure show flags are up to date and re-run validation
                Definition->UpdateShowFlags();
                FRenderStreamValidation::ForceRunValidation();
                return;
            }
        }
    }

    RSV.Error()->AddToken(FTextToken::Create(FText::FromString(FString::Printf(TEXT("Could not fix %s. Channel definition %s not found in level %."), *SettingName, *Info.Name, *LevelName))));

}

bool FRenderStreamValidation::ValidateChannelInfo(const FRenderStreamChannelInfo& Info, const FString& Level)
{
    FMessageLog RSV("RenderStreamValidation");
    RSV.SuppressLoggingToOutputLog(true);
    bool IssuesFound = false;

    const URendererSettings* RenderSettings = GetDefault<URendererSettings>();
    const FPostProcessSettings& PPS = Info.PostProcessSettings;
    const FEngineShowFlags& ShowFlags = Info.ShowFlags;

    // Features which require padding/overlap for clustered rendering

    // Motion blur
    bool motionBlurEnabled = ShowFlags.MotionBlur                                                // Enabled in RenderStreamChannelDefinition
        && ((PPS.bOverride_MotionBlurAmount && PPS.MotionBlurAmount > 0.f)                       // Overriden and enabled in camera
            || (!PPS.bOverride_MotionBlurAmount && RenderSettings->bDefaultFeatureMotionBlur));  // Not overridden in camera - enabled in project settings
    if (motionBlurEnabled)
    {
        IssuesFound = true;
        FOnActionTokenExecuted FixMotionBlur;
        FixMotionBlur.BindLambda([&Info, Level]() { DisableShowFlag(Info, Level, "MotionBlur"); });
        RSV.Warning()->AddToken(FTextToken::Create(FText::FromString(FString::Printf(TEXT("Channel %s has motion blur enabled. May require padding/overlap if using clustered rendering."), *Info.Name))))
            ->AddToken(FActionToken::Create(FText::FromString("Fix"), FText::FromString("FixMotionBlur"), FixMotionBlur));
    }

    // Bloom
    bool bloomEnabled = ShowFlags.Bloom                                                  // Enabled in RenderStreamChannelDefinition
        && ((PPS.bOverride_BloomIntensity && PPS.BloomIntensity > 0.f)                   // Overriden and enabled in camera
            || (!PPS.bOverride_BloomIntensity && RenderSettings->bDefaultFeatureBloom)); // Not overridden in camera - enabled in project settings
    if (bloomEnabled)
    {
        IssuesFound = true;
        FOnActionTokenExecuted FixBloom;
        FixBloom.BindLambda([&Info, Level]() { DisableShowFlag(Info, Level, "Bloom"); });
        RSV.Warning()->AddToken(FTextToken::Create(FText::FromString(FString::Printf(TEXT("Channel %s has bloom enabled. May require padding/overlap if using clustered rendering."), *Info.Name))))
            ->AddToken(FActionToken::Create(FText::FromString("Fix"), FText::FromString("FixBloom"), FixBloom));
    }

    // Screen-space AO
    bool ssaoEnabled = ShowFlags.ScreenSpaceAO                                                                  // Enabled in RenderStreamChannelDefinition
        && ((PPS.bOverride_AmbientOcclusionIntensity && PPS.AmbientOcclusionIntensity > 0.f)                    // Overriden and enabled in camera
            || (!PPS.bOverride_AmbientOcclusionIntensity && RenderSettings->bDefaultFeatureAmbientOcclusion));  // Not overridden in camera - enabled in project settings
    if (ssaoEnabled)
    {
        IssuesFound = true;
        FOnActionTokenExecuted FixSSAO;
        FixSSAO.BindLambda([&Info, Level]() { DisableShowFlag(Info, Level, "ScreenSpaceAO"); });
        RSV.Warning()->AddToken(FTextToken::Create(FText::FromString(FString::Printf(TEXT("Channel %s has screen-space AO enabled. May require padding/overlap if using clustered rendering."), *Info.Name))))
            ->AddToken(FActionToken::Create(FText::FromString("Fix"), FText::FromString("FixSSAO"), FixSSAO));
    }

    // Temporal anti-aliasing
    bool taaEnabled = ShowFlags.AntiAliasing && ShowFlags.TemporalAA                           // Enabled in RenderStreamChannelDefinition
        && RenderSettings->DefaultFeatureAntiAliasing == EAntiAliasingMethod::AAM_TemporalAA;  // Enabled in project settings
    if (taaEnabled)
    {
        IssuesFound = true;
        FOnActionTokenExecuted FixTAA;
        FixTAA.BindLambda([&Info, Level]() { DisableShowFlag(Info, Level, "TemporalAA"); });
        RSV.Warning()->AddToken(FTextToken::Create(FText::FromString(FString::Printf(TEXT("Channel %s has temporal anti-aliasing enabled. May require padding/overlap if using clustered rendering."), *Info.Name))))
            ->AddToken(FActionToken::Create(FText::FromString("Fix"), FText::FromString("FixTAA"), FixTAA));
    }

    // Features which don't work well for clustered rendering

    // Eye adaptation
    bool eyeAdaptationEnabled = ShowFlags.EyeAdaptation                                                                                // Enabled in RenderStreamChannelDefinition
        && RenderSettings->bDefaultFeatureAutoExposure                                                                                 // Enabled in project settings (no corresponding setting in camera)
        && ((PPS.bOverride_AutoExposureMethod && PPS.AutoExposureMethod != EAutoExposureMethod::AEM_Manual)                            // Overriden and not set to manual in camera
            || (!PPS.bOverride_AutoExposureMethod && RenderSettings->DefaultFeatureAutoExposure != EAutoExposureMethod::AEM_Manual));  // Not overridden in camera - not set to manual in project settings
    if (eyeAdaptationEnabled)
    {
        IssuesFound = true;
        FOnActionTokenExecuted FixEyeAdaptation;
        FixEyeAdaptation.BindLambda([&Info, Level]() { DisableShowFlag(Info, Level, "EyeAdaptation"); });
        RSV.Warning()->AddToken(FTextToken::Create(FText::FromString(FString::Printf(TEXT("Channel %s has eye adaptation (auto-exposure) enabled. May cause seams if using clustered rendering."), *Info.Name))))
            ->AddToken(FActionToken::Create(FText::FromString("Fix"), FText::FromString("FixEyeAdaptation"), FixEyeAdaptation));
    }

    // Lens flares
    bool lensFlaresEnabled = ShowFlags.LensFlares                                                 // Enabled in RenderStreamChannelDefinition
        && ((PPS.bOverride_LensFlareIntensity && PPS.LensFlareIntensity > 0.f)                    // Overriden and enabled in camera
            || (!PPS.bOverride_LensFlareIntensity && RenderSettings->bDefaultFeatureLensFlare));  // Not overridden in camera - enabled in project settings
    if (lensFlaresEnabled)
    {
        IssuesFound = true;
        FOnActionTokenExecuted FixLensFlares;
        FixLensFlares.BindLambda([&Info, Level]() { DisableShowFlag(Info, Level, "LensFlares"); });
        RSV.Warning()->AddToken(FTextToken::Create(FText::FromString(FString::Printf(TEXT("Channel %s has lens flares enabled. May cause seams if using clustered rendering."), *Info.Name))))
            ->AddToken(FActionToken::Create(FText::FromString("Fix"), FText::FromString("FixLensFlares"), FixLensFlares));
    }

    // Screen-space reflections (no project setting)
    bool ssrEnabled = ShowFlags.ScreenSpaceReflections                                                  // Enabled in RenderStreamChannelDefinition
        && (PPS.bOverride_ScreenSpaceReflectionIntensity && PPS.ScreenSpaceReflectionIntensity > 0.f);  // Overriden and enabled in camera
    if (ssrEnabled)
    {
        IssuesFound = true;
        FOnActionTokenExecuted FixSSR;
        FixSSR.BindLambda([&Info, Level]() { DisableShowFlag(Info, Level, "ScreenSpaceReflections"); });
        RSV.Warning()->AddToken(FTextToken::Create(FText::FromString(FString::Printf(TEXT("Channel %s has screen-space reflections enabled. May cause seams if using clustered rendering."), *Info.Name))))
            ->AddToken(FActionToken::Create(FText::FromString("Fix"), FText::FromString("FixSSR"), FixSSR));
    }

    // Scene color fringe (no project setting)
    bool colorFringeEnabled = ShowFlags.SceneColorFringe                           // Enabled in RenderStreamChannelDefinition
        && (PPS.bOverride_SceneFringeIntensity && PPS.SceneFringeIntensity > 0.f); // Overriden and enabled in camera
    if (colorFringeEnabled)
    {
        IssuesFound = true;
        FOnActionTokenExecuted FixColorFringe;
        FixColorFringe.BindLambda([&Info, Level]() { DisableShowFlag(Info, Level, "SceneColorFringe"); });
        RSV.Warning()->AddToken(FTextToken::Create(FText::FromString(FString::Printf(TEXT("Channel %s has scene color fringe (chromatic aberration) enabled. May cause seams if using clustered rendering."), *Info.Name))))
            ->AddToken(FActionToken::Create(FText::FromString("Fix"), FText::FromString("FixColorFringe"), FixColorFringe));
    }

    // Tone curve (no project setting, ExpandGamut also related)
    bool toneCurveEnabled = ShowFlags.ToneCurve                           // Enabled in RenderStreamChannelDefinition
        && ((PPS.bOverride_ToneCurveAmount && PPS.ToneCurveAmount > 0.f)  // Overriden and enabled in camera
        || (PPS.bOverride_ExpandGamut && PPS.ExpandGamut > 0.f));         // Overriden and enabled in camera
    if (toneCurveEnabled)
    {
        IssuesFound = true;
        FOnActionTokenExecuted FixToneCurve;
        FixToneCurve.BindLambda([&Info, Level]() { DisableShowFlag(Info, Level, "ToneCurve"); });
        RSV.Warning()->AddToken(FTextToken::Create(FText::FromString(FString::Printf(TEXT("Channel %s has tone curve enabled. May cause seams if using clustered rendering."), *Info.Name))))
            ->AddToken(FActionToken::Create(FText::FromString("Fix"), FText::FromString("FixToneCurve"), FixToneCurve));
    }

    // Features which aren't recommended for other reasons

    // Vignette (no project setting)
    bool vignetteEnabled = ShowFlags.Vignette                                 // Enabled in RenderStreamChannelDefinition
        && (PPS.bOverride_VignetteIntensity && PPS.VignetteIntensity > 0.f);  // Overriden and enabled in camera
    if (vignetteEnabled)
    {
        IssuesFound = true;
        FOnActionTokenExecuted FixVignette;
        FixVignette.BindLambda([&Info, Level]() { DisableShowFlag(Info, Level, "Vignette"); });
        RSV.Warning()->AddToken(FTextToken::Create(FText::FromString(FString::Printf(TEXT("Channel %s has vignette enabled. This may result in double vignette application when captured with a real camera."), *Info.Name))))
            ->AddToken(FActionToken::Create(FText::FromString("Fix"), FText::FromString("FixVignette"), FixVignette));
    }

    return IssuesFound;
}

void FRenderStreamValidation::RunValidation(const TArray<URenderStreamChannelCacheAsset*>& Caches)
{
    bool IssuesFound = false;
    {
        FMessageLog RSV("RenderStreamValidation");
        RSV.SuppressLoggingToOutputLog(true);
        RSV.Info()->AddToken(FTextToken::Create(FText::FromString("Project settings:")));
    }
    IssuesFound |= ValidateProjectSettings();

    for (const URenderStreamChannelCacheAsset* Cache : Caches)
    {
        if (!Cache)
            continue;

        const FString LevelName = Cache->Level.GetAssetPathName().ToString();

        {
            FMessageLog RSV("RenderStreamValidation");
            RSV.SuppressLoggingToOutputLog(true);
            RSV.Info()->AddToken(FTextToken::Create(FText::FromString(FString::Printf(TEXT("Level %s:"), *LevelName))));
        }

        for (const FString& ChannelName : Cache->Channels)
        {
            if (Cache->ChannelInfoMap.Contains(ChannelName))
                IssuesFound |= ValidateChannelInfo(Cache->ChannelInfoMap[ChannelName], LevelName);
        }
    }

    if (IssuesFound)
        UE_LOG(LogRenderStreamEditor, Warning, TEXT("Potential issues found in project settings. Open Renderstream Validation tab in Message Log to view and fix."));

}

void FRenderStreamValidation::ForceRunValidation()
{
    FRenderStreamEditorModule& RenderStreamEditorModule = FModuleManager::LoadModuleChecked<FRenderStreamEditorModule>("RenderStreamEditor");
    RenderStreamEditorModule.GenerateAssetMetadata();
}