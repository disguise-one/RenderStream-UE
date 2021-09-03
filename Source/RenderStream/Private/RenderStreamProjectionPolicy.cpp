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

TMap<FString, int32_t> FRenderStreamProjectionPolicy::Players;

static EUnit distanceUnit()
{
    // Unreal defaults to centimeters so we might as well do the same
    static EUnit ret = EUnit::Unspecified;
    if (ret == EUnit::Unspecified)
    {
        ret = EUnit::Centimeters;

        FString ValueReceived;
        if (!GConfig->GetString(TEXT("/Script/UnrealEd.EditorProjectAppearanceSettings"), TEXT("DistanceUnits"), ValueReceived, GEditorIni))
            return ret;

        TOptional<EUnit> currentUnit = FUnitConversion::UnitFromString(*ValueReceived);
        if (currentUnit.IsSet())
            ret = currentUnit.GetValue();
    }
    return ret;
}

FRenderStreamProjectionPolicy::FRenderStreamProjectionPolicy(const FString& _ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy)
    : ProjectionPolicyId(_ProjectionPolicyId)
    , ViewportId(_ProjectionPolicyId.Replace(TEXT("proj_"), TEXT("")))
    , Parameters(InConfigurationProjectionPolicy->Parameters)
    , NCP(0)
    , FCP(0)
{
}

FRenderStreamProjectionPolicy::~FRenderStreamProjectionPolicy()
{
}

//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicy
//////////////////////////////////////////////////////////////////////////////////////////////
const FString FRenderStreamProjectionPolicy::GetTypeId() const
{
    return FRenderStreamProjectionPolicyFactory::RenderStreamPolicyType;
}

bool FRenderStreamProjectionPolicy::IsConfigurationChanged(const FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy) const
{
    return false;
}

bool FRenderStreamProjectionPolicy::HandleStartScene(class IDisplayClusterViewport* InViewport)
{
    check(IsInGameThread());
    check(InViewport);

    FRenderStreamModule* Module = FRenderStreamModule::Get();
    check(Module);

    Module->LoadSchemas(*GWorld);
    
    int* playerId = Players.Find(GetViewportId());
    if (APlayerController* ExistingController = playerId ? UGameplayStatics::GetPlayerControllerFromID(GWorld, *playerId) : nullptr)
        Controller = ExistingController;
    else if (APlayerController* NewController = UGameplayStatics::CreatePlayer(GWorld))
    {
        Players.Add(GetViewportId(), UGameplayStatics::GetPlayerControllerID(NewController));
        Controller = NewController;
    }
    else
    {
        UE_LOG(LogRenderStream, Warning, TEXT("Could not set new view target for capturing."));
        return false;
    }

    Stream = Module->StreamPool->GetStream(GetViewportId());
    if (!Stream)
        Module->PopulateStreamPool();

    ConfigureCapture();

    return true;
}

void FRenderStreamProjectionPolicy::ConfigureCapture()
{
    check(IsInGameThread());

    FRenderStreamModule* Module = FRenderStreamModule::Get();
    check(Module);

    const IDisplayClusterConfigManager* const ConfigMgr = IDisplayCluster::Get().GetConfigMgr();
    check(ConfigMgr);

    const UDisplayClusterConfigurationViewport* Viewport = ConfigMgr->GetLocalViewport(GetViewportId());
    if (!Viewport)
    {
        UE_LOG(LogRenderStreamPolicy, Error, TEXT("Policy '%s' created without corresponding viewport"), *GetViewportId());
        return;
    }
    const FIntPoint Offset(Viewport->Region.X, Viewport->Region.Y);
    const FIntPoint Resolution(Viewport->Region.W, Viewport->Region.H);

    // Allocate the stream.
    Stream = Module->StreamPool->GetStream(GetViewportId());
    if (!Stream)
    {
        UE_LOG(LogRenderStreamPolicy, Warning, TEXT("Policy '%s' created for unknown stream"), *GetViewportId());
    }
    if (Stream && Stream->Resolution() != Resolution)
    {
        UE_LOG(LogRenderStreamPolicy, Error, TEXT("Policy '%s' created with incorrect resolution: %dx%d vs expected %dx%d"), *GetViewportId(), Resolution.X, Resolution.Y, Stream->Resolution().X, Stream->Resolution().Y);
        return;
    }

    const FString Channel = Stream ? Stream->Channel() : "";
    const TWeakObjectPtr<ACameraActor> ChannelCamera = URenderStreamChannelDefinition::GetChannelCamera(Channel);
    if (Template != ChannelCamera)
    {
        Template = ChannelCamera;
        if (Template.IsValid())
        {
            UE_LOG(LogRenderStreamPolicy, Log, TEXT("Channel '%s' currently mapped to camera '%s'"), *Channel, *ChannelCamera->GetName());

            URenderStreamChannelDefinition* Definition = Template->FindComponentByClass<URenderStreamChannelDefinition>();
            if (Definition)
            {
                const bool DefaultVisible = Definition->DefaultVisibility == EVisibilty::Visible;
                const TSet<TSoftObjectPtr<AActor>> Actors = DefaultVisible ? Definition->Hidden : Definition->Visible;
                const FString TypeString = DefaultVisible ? "visible" : "hidden";
                UE_LOG(LogRenderStreamPolicy, Log, TEXT("%d cameras registered to channel, filtering %d actors, default visibility '%s'"),
                    URenderStreamChannelDefinition::GetChannelCameraNum(Channel), Actors.Num(), *TypeString);
            }

            // Spawn the instance of the template camera needed for this policy / view.
            FActorSpawnParameters ActorSpawnParameters;
            ActorSpawnParameters.Template = Template.Get();
            Camera = Template->GetWorld()->SpawnActor<class ACameraActor>(Template->GetClass(), ActorSpawnParameters);
            if (URenderStreamChannelDefinition* ClonedDefinition = Camera->FindComponentByClass<URenderStreamChannelDefinition>())
                ClonedDefinition->UnregisterCamera();

            Camera->SetOwner(Template->GetOwner());
            Camera->AttachToActor(Template->GetAttachParentActor(), FAttachmentTransformRules::KeepWorldTransform);

            USceneComponent* RootComponent = Template->GetRootComponent();
            if (RootComponent)
                Camera->SetActorRelativeTransform(Template->GetRootComponent()->GetRelativeTransform());

            if (Definition)
                Definition->AddCameraInstance(Camera);

            if (Controller.IsValid())
                Controller->SetViewTargetWithBlend(Camera.Get());
            else
                UE_LOG(LogRenderStream, Warning, TEXT("Could not set new view target for capturing, no valid controller."));
        }
        else
            UE_LOG(LogRenderStreamPolicy, Log, TEXT("Channel '%s' currently not mapped to a camera"), *Channel);
    }
}

void FRenderStreamProjectionPolicy::UpdateStream(const FString& StreamName)
{
    // Do nothing if this projection policy already has a stream
    if (Stream)
        return;

    ProjectionPolicyId = StreamName;
    ConfigureCapture();
}

void FRenderStreamProjectionPolicy::ApplyCameraData(const RenderStreamLink::FrameData& frameData, const RenderStreamLink::CameraData& cameraData)
{
    // Each call must always have a frame response, because there will be a corresponding render call.
    {
        std::lock_guard<std::mutex> guard(m_frameResponsesLock);
        m_frameResponses.push_back({ frameData.tTracked, cameraData });
    }

    if (!Camera.IsValid() || cameraData.cameraHandle == 0)
        return;

    // Attach the instanced Camera to the Capture object for this view.
    USceneComponent* SceneComponent = Camera->K2_GetRootComponent();
    UCameraComponent* CameraComponent = Camera->GetCameraComponent();

    if (CameraComponent && cameraData.orthoWidth > 0.f)  // Use an orthographic camera
    {
        CameraComponent->ProjectionMode = ECameraProjectionMode::Orthographic;
        CameraComponent->OrthoWidth = FUnitConversion::Convert(float(cameraData.orthoWidth), EUnit::Meters, FRenderStreamModule::distanceUnit());
        CameraComponent->SetAspectRatio(cameraData.sensorX / cameraData.sensorY);
    }
    else if (UCineCameraComponent* CineCamera = dynamic_cast<UCineCameraComponent*>(CameraComponent))
    {
        CineCamera->Filmback.SensorWidth = cameraData.sensorX;
        CineCamera->Filmback.SensorHeight = cameraData.sensorY;
        CineCamera->CurrentFocalLength = cameraData.focalLength;
    }
    else if (CameraComponent)
    {
        float throwRatioH = cameraData.focalLength / cameraData.sensorX;
        float fovH = 2.f * FMath::Atan(0.5f / throwRatioH);
        CameraComponent->SetFieldOfView(fovH * 180.f / PI);
        CameraComponent->SetAspectRatio(cameraData.sensorX / cameraData.sensorY);
    }

    if (SceneComponent)
    {
        float _pitch = cameraData.rx;
        float _yaw = cameraData.ry;
        float _roll = cameraData.rz;
        FQuat rotationQuat = FQuat::MakeFromEuler(FVector(_roll, _pitch, _yaw));
        SceneComponent->SetRelativeRotation(rotationQuat);

        FVector pos;
        pos.X = FUnitConversion::Convert(float(cameraData.z), EUnit::Meters, FRenderStreamModule::distanceUnit());
        pos.Y = FUnitConversion::Convert(float(cameraData.x), EUnit::Meters, FRenderStreamModule::distanceUnit());
        pos.Z = FUnitConversion::Convert(float(cameraData.y), EUnit::Meters, FRenderStreamModule::distanceUnit());
        SceneComponent->SetRelativeLocation(pos);
    }
}

void FRenderStreamProjectionPolicy::HandleEndScene(IDisplayClusterViewport* InViewport)
{
    check(IsInGameThread());
    
    // Reset reference looked up by name in StartScene or set in StartCapture
    Camera = nullptr;
}

bool FRenderStreamProjectionPolicy::CalculateView(class IDisplayClusterViewport* InViewport, const uint32 InContextNum, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float InNCP, const float InFCP)
{
    check(IsInGameThread());

    UCameraComponent* AssignedCamera = Camera.IsValid() ? Camera->GetCameraComponent() : nullptr;

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

    UCameraComponent* AssignedCamera = Camera.IsValid() ? Camera->GetCameraComponent() : nullptr;

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

        // FIX: Currently nDisplay doesn't allow us to set a projection matrix on the viewport.
        UE_LOG(LogRenderStream, Error, TEXT("Unable to set ortho matrices into ndisplay, currently"));
        return false;
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
    }

    // Center shift
    FVector centerShift = { 0.f, 0.f, 0.f };
    {
        std::lock_guard<std::mutex> guard(m_frameResponsesLock);
        if (!m_frameResponses.empty())
        {
            // first frame can have no frame response.
            const RenderStreamLink::CameraResponseData& thisFrameResponse = m_frameResponses.back();
            centerShift = { thisFrameResponse.camera.cx, thisFrameResponse.camera.cy, 0.f };
        }
    }

    // Clipping
    FTransform clippingTransform;
    RenderStreamLink::ProjectionClipping Clipping = { 0.f, 1.f, 0.f, 1.f };  // Default clipping in case no streams
    if (Stream)
        Clipping = Stream->Clipping();
    FVector clippingScale = { 1.f / (Clipping.right - Clipping.left), -1.f / (Clipping.top - Clipping.bottom), 1.f };
    FVector clippingOffset = (FVector(1.f - (Clipping.right + Clipping.left), -1.f + (Clipping.top + Clipping.bottom), 0.f) + centerShift) * clippingScale;
    clippingTransform.SetTranslationAndScale3D(clippingOffset, clippingScale);
    FMatrix clippingMatrix = clippingTransform.ToMatrixWithScale();

    OutPrjMatrix = InViewport->GetContexts()[InContextNum].ProjectionMatrix * clippingMatrix;

    return true;
}

void FRenderStreamProjectionPolicy::ApplyWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const class IDisplayClusterViewportProxy* InViewportProxy)
{
    if (Stream)
    {
        RenderStreamLink::CameraResponseData frameResponse;
        {
            std::lock_guard<std::mutex> guard(m_frameResponsesLock);
            if (m_frameResponses.empty())
            {
                // First frame can have no response data, so do not send a response to nothing.
                return;
            }

            frameResponse = m_frameResponses.front();
            m_frameResponses.pop_front();
        }

        TArray<FRHITexture2D*> Resources;
        TArray<FIntRect> Rects;
        InViewportProxy->GetResourcesWithRects_RenderThread(EDisplayClusterViewportResourceType::InputShaderResource, Resources, Rects);
        check(Resources.Num() == 1);
        check(Rects.Num() == 1);
        Stream->SendFrame_RenderingThread(RHICmdList, frameResponse, Resources[0], Rects[0]);
    }

    // Uncomment this to restore client display
    // InViewportProxy->ResolveResources(RHICmdList, EDisplayClusterViewportResourceType::InputShaderResource, InViewportProxy->GetOutputResourceType());
}

const ACameraActor* FRenderStreamProjectionPolicy::GetTemplateCamera() const
{
    return Template.IsValid() ? Template.Get() : nullptr;
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

TArray<FRenderStreamProjectionPolicyFactory::PolicyPtr> FRenderStreamProjectionPolicyFactory::GetPolicies()
{
    return Policies;
}

FRenderStreamProjectionPolicyFactory::PolicyPtr FRenderStreamProjectionPolicyFactory::GetPolicyByViewport(const FString& ViewportId) const
{
    for (auto& It : Policies)
    {
        if (!ViewportId.Compare(It->GetViewportId(), ESearchCase::IgnoreCase))
        {
            return It;
        }
    }

    return nullptr;
}

FRenderStreamProjectionPolicyFactory::PolicyPtr FRenderStreamProjectionPolicyFactory::GetPolicyBySceneViewFamily(int32 ViewFamilyIdx) const
{
    check(ViewFamilyIdx < Policies.Num());

    return Policies[ViewFamilyIdx];
}


FRenderStreamProjectionPolicyFactory::BasePolicyPtr FRenderStreamProjectionPolicyFactory::Create(const FString& ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy)
{
    UE_LOG(LogRenderStreamPolicy, Log, TEXT("Instantiating projection policy <%s>..."), *InConfigurationProjectionPolicy->Type);

    if (!InConfigurationProjectionPolicy->Type.Compare(FRenderStreamProjectionPolicyFactory::RenderStreamPolicyType, ESearchCase::IgnoreCase))
    {
        PolicyPtr Result = MakeShareable(new FRenderStreamProjectionPolicy(ProjectionPolicyId, InConfigurationProjectionPolicy));
        Policies.Add(Result);
        return StaticCastSharedPtr<IDisplayClusterProjectionPolicy>(Result);
    }

    return nullptr;
}

