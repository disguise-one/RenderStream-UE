#include "RenderStreamValidation.h"
#include "RenderStreamChannelCacheAsset.h"

#include "UObject/Class.h"
#include "Engine/RendererSettings.h"
#include "Engine/PostProcessVolume.h"
#include "Logging/TokenizedMessage.h"
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"
#include "Misc/MapErrors.h"


void FRenderStreamValidation::ValidateProjectSettings()
{
    FMessageLog RSV("RenderStreamValidation");

    const URendererSettings* RenderSettings = GetDefault<URendererSettings>();

    // Texture streaming
    if (RenderSettings->bTextureStreaming)
        RSV.Warning()->AddToken(FTextToken::Create(FText::FromString("Texture streaming enabled in project settings. May cause pixellated content")));

    // Screen space global illumination
    if (RenderSettings->bSSGI)
        RSV.Warning()->AddToken(FTextToken::Create(FText::FromString("Screen-space global illumination enabled in project settings. May cause seams if using clustered rendering")));
}

FRenderStreamChannelInfo FRenderStreamValidation::GetChannelInfo(URenderStreamChannelDefinition* ChannelDefinition, const ULevel* Level)
{
    FRenderStreamChannelInfo Info;
    if (ChannelDefinition)
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

void FRenderStreamValidation::ValidateChannelInfo(const FRenderStreamChannelInfo& Info)
{
    FMessageLog RSV("RenderStreamValidation");

    const URendererSettings* RenderSettings = GetDefault<URendererSettings>();
    const FPostProcessSettings& PPS = Info.PostProcessSettings;
    const FEngineShowFlags& ShowFlags = Info.ShowFlags;

    // Features which require padding/overlap for clustered rendering

    // Motion blur
    bool motionBlurEnabled = ShowFlags.MotionBlur                                                // Enabled in RenderStreamChannelDefinition
        && ((PPS.bOverride_MotionBlurAmount && PPS.MotionBlurAmount > 0.f)                       // Overriden and enabled in camera
            || (!PPS.bOverride_MotionBlurAmount && RenderSettings->bDefaultFeatureMotionBlur));  // Not overridden in camera - enabled in project settings
    if (motionBlurEnabled)
        RSV.Warning()->AddToken(FTextToken::Create(FText::FromString("Channel " + Info.Name + " has motion blur enabled. May require padding / overlap if using clustered rendering.It can be disabled in the Renderstream channel definition.")));

    // Bloom
    bool bloomEnabled = ShowFlags.Bloom                                                  // Enabled in RenderStreamChannelDefinition
        && ((PPS.bOverride_BloomIntensity && PPS.BloomIntensity > 0.f)                   // Overriden and enabled in camera
            || (!PPS.bOverride_BloomIntensity && RenderSettings->bDefaultFeatureBloom)); // Not overridden in camera - enabled in project settings
    if (bloomEnabled)
        RSV.Warning()->AddToken(FTextToken::Create(FText::FromString("Channel " + Info.Name + " has bloom enabled. May require padding/overlap if using clustered rendering. It can be disabled in the Renderstream channel definition.")));

    // Screen-space AO
    bool ssaoEnabled = ShowFlags.ScreenSpaceAO                                                                  // Enabled in RenderStreamChannelDefinition
        && ((PPS.bOverride_AmbientOcclusionIntensity && PPS.AmbientOcclusionIntensity > 0.f)                    // Overriden and enabled in camera
            || (!PPS.bOverride_AmbientOcclusionIntensity && RenderSettings->bDefaultFeatureAmbientOcclusion));  // Not overridden in camera - enabled in project settings
    if (ssaoEnabled)
        RSV.Warning()->AddToken(FTextToken::Create(FText::FromString("Channel " + Info.Name + " has screen-space AO enabled. May require padding/overlap if using clustered rendering. It can be disabled in the Renderstream channel definition.")));

    // Temporal anti-aliasing
    if (ShowFlags.AntiAliasing && ShowFlags.TemporalAA                                         // Enabled in RenderStreamChannelDefinition
        && RenderSettings->DefaultFeatureAntiAliasing == EAntiAliasingMethod::AAM_TemporalAA)  // Enabled in project settings
        RSV.Warning()->AddToken(FTextToken::Create(FText::FromString("Channel " + Info.Name + " has temporal anti-aliasing enabled. May require padding/overlap if using clustered rendering. It can be disabled in the Renderstream channel definition.")));

    // Features which don't work well for clustered rendering

    // Eye adaptation
    bool eyeAdaptationEnabled = ShowFlags.EyeAdaptation                                                                                // Enabled in RenderStreamChannelDefinition
        && RenderSettings->bDefaultFeatureAutoExposure                                                                                 // Enabled in project settings (no corresponding setting in camera)
        && ((PPS.bOverride_AutoExposureMethod && PPS.AutoExposureMethod != EAutoExposureMethod::AEM_Manual)                            // Overriden and not set to manual in camera
            || (!PPS.bOverride_AutoExposureMethod && RenderSettings->DefaultFeatureAutoExposure != EAutoExposureMethod::AEM_Manual));  // Not overridden in camera - not set to manual in project settings
    if (eyeAdaptationEnabled)
        RSV.Warning()->AddToken(FTextToken::Create(FText::FromString("Channel " + Info.Name + " has eye adaptation (auto-exposure) enabled. May cause seams if using clustered rendering. It can be disabled in the Renderstream channel definition.")));

    // Lens flares
    bool lensFlaresEnabled = ShowFlags.LensFlares                                                 // Enabled in RenderStreamChannelDefinition
        && ((PPS.bOverride_LensFlareIntensity && PPS.LensFlareIntensity > 0.f)                    // Overriden and enabled in camera
            || (!PPS.bOverride_LensFlareIntensity && RenderSettings->bDefaultFeatureLensFlare));  // Not overridden in camera - enabled in project settings
    if (lensFlaresEnabled)
        RSV.Warning()->AddToken(FTextToken::Create(FText::FromString("Channel " + Info.Name + " has lens flares enabled. May cause seams if using clustered rendering. It can be disabled in the Renderstream channel definition.")));

    // Screen-space reflections (no project setting)
    bool ssrEnabled = ShowFlags.ScreenSpaceReflections                                                  // Enabled in RenderStreamChannelDefinition
        && (PPS.bOverride_ScreenSpaceReflectionIntensity && PPS.ScreenSpaceReflectionIntensity > 0.f);  // Overriden and enabled in camera
    if (ssrEnabled)
        RSV.Warning()->AddToken(FTextToken::Create(FText::FromString("Channel " + Info.Name + " has screen-space reflections enabled. May cause seams if using clustered rendering. It can be disabled in the Renderstream channel definition.")));

    // Scene color fringe (no project setting)
    bool colorFringeEnabled = ShowFlags.SceneColorFringe                           // Enabled in RenderStreamChannelDefinition
        && (PPS.bOverride_SceneFringeIntensity && PPS.SceneFringeIntensity > 0.f); // Overriden and enabled in camera
    if (colorFringeEnabled)
        RSV.Warning()->AddToken(FTextToken::Create(FText::FromString("Channel " + Info.Name + " has scene color fringe (chromatic aberration) enabled. May cause seams if using clustered rendering. It can be disabled in the Renderstream channel definition.")));

    // Tone curve (no project setting, ExpandGamut also related)
    bool toneCurveEnabled = ShowFlags.ToneCurve                          // Enabled in RenderStreamChannelDefinition
        && (PPS.bOverride_ToneCurveAmount && PPS.ToneCurveAmount > 0.f)  // Overriden and enabled in camera
        && (PPS.bOverride_ExpandGamut && PPS.ExpandGamut > 0.f);         // Overriden and enabled in camera
    if (toneCurveEnabled)
        RSV.Warning()->AddToken(FTextToken::Create(FText::FromString("Channel " + Info.Name + " has tone curve enabled. May cause seams if using clustered rendering. It can be disabled in the Renderstream channel definition.")));

    // Features which aren't recommended for other reasons

    // Vignette (no project setting)
    bool vignetteEnabled = ShowFlags.Vignette                                 // Enabled in RenderStreamChannelDefinition
        && (PPS.bOverride_VignetteIntensity && PPS.VignetteIntensity > 0.f);  // Overriden and enabled in camera
    if (vignetteEnabled)
        RSV.Warning()->AddToken(FTextToken::Create(FText::FromString("Channel " + Info.Name + " has vignette enabled. This may result in double vignette application when captured with a real camera. It can be disabled in the Renderstream channel definition.")));
}

void FRenderStreamValidation::RunValidation(const TArray<URenderStreamChannelCacheAsset*>& Caches)
{
    ValidateProjectSettings();

    for (const URenderStreamChannelCacheAsset* Cache : Caches)
    {
        if (!Cache)
            continue;

        for (const FString& ChannelName : Cache->Channels)
            ValidateChannelInfo(Cache->ChannelInfoMap[ChannelName]);
    }
}
