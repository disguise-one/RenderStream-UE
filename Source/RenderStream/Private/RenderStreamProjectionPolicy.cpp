#include "RenderStreamProjectionPolicy.h"

#include "Misc/DisplayClusterHelpers.h"

#include "IDisplayCluster.h"
#include "Render/Viewport/IDisplayClusterViewport.h"
#include "Render/Viewport/IDisplayClusterViewportProxy.h"
#include "Config/IDisplayClusterConfigManager.h"
#include "DisplayClusterConfigurationTypes.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "CinematicCamera/Public/CineCameraComponent.h"

#include "Math/UnitConversion.h"
#include "RenderStream.h"
#include "Kismet/GameplayStatics.h"

#include "RenderStreamSettings.h"
#include "FrameStream.h"

#include "RenderStreamChannelDefinition.h"
#include "..\Public\RenderStreamProjectionPolicy.h"

DEFINE_LOG_CATEGORY(LogRenderStreamPolicy);

FRenderStreamProjectionPolicy::FRenderStreamProjectionPolicy(const FString& _ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy)
    : ProjectionPolicyId(_ProjectionPolicyId)
    , Parameters(InConfigurationProjectionPolicy->Parameters)
    , NCP(0)
    , FCP(0)
{
}

FRenderStreamProjectionPolicy::~FRenderStreamProjectionPolicy() {}

bool FRenderStreamProjectionPolicy::HandleStartScene(class IDisplayClusterViewport* Viewport)
{
    if (GIsEditor)
        return false;

    const FString ViewportId = Viewport->GetId();
    FRenderStreamModule* Module = FRenderStreamModule::Get();
    check(Module);

    auto Stream = Module->StreamPool->GetStream(ViewportId);
    if (Stream)
    {
        // Reconfigure stream when scene changes
        Module->ConfigureStream(Stream);
    }

    return true;
}

void FRenderStreamProjectionPolicy::HandleEndScene(class IDisplayClusterViewport* Viewport)
{
    FRenderStreamModule* Module = FRenderStreamModule::Get();
    check(Module);

    Module->GetViewportInfo(Viewport->GetId()).Camera = nullptr;
}

bool FRenderStreamProjectionPolicy::CalculateView(class IDisplayClusterViewport* InViewport, const uint32 InContextNum, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float InNCP, const float InFCP)
{
    check(IsInGameThread());
    
    FRenderStreamModule* Module = FRenderStreamModule::Get();
    check(Module);

    auto& Info = Module->GetViewportInfo(InViewport->GetId());
    UCameraComponent* AssignedCamera = Info.Camera.IsValid() ? Info.Camera->GetCameraComponent() : nullptr;

    InOutViewLocation = (AssignedCamera ? AssignedCamera->GetComponentLocation() : FVector::ZeroVector);
    InOutViewRotation = (AssignedCamera ? AssignedCamera->GetComponentRotation() : FRotator::ZeroRotator);

    // Store culling data
    NCP = InNCP;
    FCP = InFCP;

    return true;
}

bool FRenderStreamProjectionPolicy::GetProjectionMatrix(class IDisplayClusterViewport* InViewport, const uint32 InContextNum, FMatrix& OutPrjMatrix)
{
    check(IsInGameThread());

    FRenderStreamModule* Module = FRenderStreamModule::Get();
    check(Module);

    auto const& ViewportId = InViewport->GetId();
    auto& Info = Module->GetViewportInfo(ViewportId);
    UCameraComponent* AssignedCamera = Info.Camera.IsValid() ? Info.Camera->GetCameraComponent() : nullptr;

    if (!AssignedCamera)
    {
        return false;
    }

    FMatrix PrjMatrix;
    if (AssignedCamera->ProjectionMode == ECameraProjectionMode::Orthographic)
    {
        const float OrthoWidth = 0.5f * AssignedCamera->OrthoWidth;
        const float OrthoHeight = OrthoWidth / AssignedCamera->AspectRatio;
        const float ZScale = 1.f / (AssignedCamera->OrthoFarClipPlane - AssignedCamera->OrthoNearClipPlane);
        const float ZOffset = -AssignedCamera->OrthoNearClipPlane;
        PrjMatrix = FReversedZOrthoMatrix(OrthoWidth, OrthoHeight, ZScale, ZOffset);
    }
    else
    {
        const float FieldOfViewH = FMath::DegreesToRadians(AssignedCamera->FieldOfView);
        const float FieldOfViewV = 2 * FMath::Atan(FMath::Tan((FieldOfViewH / 2.0f)) * (1 / AssignedCamera->AspectRatio));

        const float l = -FMath::Tan(0.5f * FieldOfViewH);
        const float r = FMath::Tan(0.5f * FieldOfViewH);
        const float t = FMath::Tan(0.5f * FieldOfViewV);
        const float b = -FMath::Tan(0.5f * FieldOfViewV);

        InViewport->CalculateProjectionMatrix(InContextNum, NCP * l, NCP * r, NCP * t, NCP * b, NCP, FCP, false);
        PrjMatrix = InViewport->GetContexts()[InContextNum].ProjectionMatrix;
    }

    // Center shift
    FVector centerShift = { 0.f, 0.f, 0.f };
    {
        std::lock_guard<std::mutex> guard(Info.m_frameResponsesLock);
        uint64 frameCounter = IsDX11() ? GFrameCounter : static_cast<uint64>(GFrameNumber);
        if (Info.m_frameResponsesMap.count(frameCounter)) // Check current frame data exists
        {
            // first frame can have no frame response.
            const RenderStreamLink::CameraResponseData& thisFrameResponse = Info.m_frameResponsesMap[frameCounter];
            centerShift = { thisFrameResponse.camera.cx, thisFrameResponse.camera.cy, 0.f };
        }
    }

    auto Stream = Module->StreamPool->GetStream(ViewportId);
    // Clipping
    FTransform clippingTransform;
    RenderStreamLink::ProjectionClipping Clipping = { 0.f, 1.f, 0.f, 1.f };  // Default clipping in case no streams
    if (Stream)
        Clipping = Stream->Clipping();
    FVector clippingScale = { 1.f / (Clipping.right - Clipping.left), -1.f / (Clipping.top - Clipping.bottom), 1.f };
    FVector clippingOffset = (FVector(1.f - (Clipping.right + Clipping.left), -1.f + (Clipping.top + Clipping.bottom), 0.f) + centerShift) * clippingScale;
    clippingTransform.SetTranslationAndScale3D(clippingOffset, clippingScale);
    FMatrix clippingMatrix = clippingTransform.ToMatrixWithScale();

    OutPrjMatrix = PrjMatrix * clippingMatrix;

    return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// FRenderStreamProjectionPolicyFactory
//////////////////////////////////////////////////////////////////////////////////////////////
FRenderStreamProjectionPolicyFactory::FRenderStreamProjectionPolicyFactory()
{
}

FRenderStreamProjectionPolicyFactory::~FRenderStreamProjectionPolicyFactory()
{
}

FRenderStreamProjectionPolicyFactory::BasePolicyPtr FRenderStreamProjectionPolicyFactory::Create(const FString& ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy)
{
    UE_LOG(LogRenderStreamPolicy, Log, TEXT("Instantiating projection policy <%s>..."), *InConfigurationProjectionPolicy->Type);

    if (!InConfigurationProjectionPolicy->Type.Compare(FRenderStreamProjectionPolicy::RenderStreamPolicyType, ESearchCase::IgnoreCase))
    {
        PolicyPtr Result = MakeShareable(new FRenderStreamProjectionPolicy(ProjectionPolicyId, InConfigurationProjectionPolicy));
        return StaticCastSharedPtr<IDisplayClusterProjectionPolicy>(Result);
    }

    return nullptr;
}

