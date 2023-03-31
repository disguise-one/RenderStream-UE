#include "AnimNode_RenderStreamSkeletonSource.h"
#include "RenderStream.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimTrace.h"
#include "Features/IModularFeatures.h"
#include "ILiveLinkClient.h"
#include "LiveLinkCustomVersion.h"
#include "LiveLinkRemapAsset.h"
#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkAnimationTypes.h"

#include "Kismet/GameplayStatics.h"
#include "Animation/SkeletalMeshActor.h"

FAnimNode_RenderStreamSkeletonSource::FAnimNode_RenderStreamSkeletonSource()
    : RetargetAsset(URenderStreamRemapAsset::StaticClass())
    , CurrentRetargetAsset(nullptr)
    , LiveLinkClient_AnyThread(nullptr)
    , CachedDeltaTime(0.0f)
{
}

void FAnimNode_RenderStreamSkeletonSource::OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance)
{
    // Doesn't seem to be called

    CurrentRetargetAsset = nullptr;

    Super::OnInitializeAnimInstance(InProxy, InAnimInstance);
}

void FAnimNode_RenderStreamSkeletonSource::CacheSkeletonActors(const FName& ParamName)
{
    // Find and cache any skeleton actors using this animation node
    SkeletonActors.clear();
    USkeleton* ThisSkeleton = nullptr;
    const UAnimBlueprintGeneratedClass* BPClass = dynamic_cast<const UAnimBlueprintGeneratedClass*>(GetAnimClassInterface());
    FString SkeletonName = ParamName.ToString();

    if (!BPClass)
    {
        UE_LOG(LogRenderStream, Warning, TEXT("Error initialising skeleton <%s>. Couldn't find blueprint class"), *SkeletonName);
        return;
    }

    ThisSkeleton = BPClass->TargetSkeleton;
    if (!ThisSkeleton)
    {
        UE_LOG(LogRenderStream, Warning, TEXT("Error initialising skeleton <%s>. Blueprint class has no target skeleton"), *SkeletonName);
        return;
    }

    TArray<AActor*> FoundActors;
    UGameplayStatics::GetAllActorsOfClass(GWorld, ASkeletalMeshActor::StaticClass(), FoundActors);

    if (FoundActors.IsEmpty())
    {
        UE_LOG(LogRenderStream, Warning, TEXT("Error initialising skeleton <%s>. No skeletal mesh actors found"), *SkeletonName);
        return;
    }

    for (AActor* Actor : FoundActors)
    {
        if (ASkeletalMeshActor* SkeletalMeshActor = Cast<ASkeletalMeshActor>(Actor))
        {
            USkeletalMesh* SkeletalMesh = SkeletalMeshActor->ReplicatedMesh;
            if (SkeletalMesh)
            {
                USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
                if (Skeleton == ThisSkeleton)
                {
                    SkeletonActors.push_back(TWeakObjectPtr<ASkeletalMeshActor>(SkeletalMeshActor));
                }
            }
        }
    }

    if (SkeletonActors.empty())
    {
        UE_LOG(LogRenderStream, Warning, TEXT("Error initialising skeleton <%s>. No corresponding skeletal mesh actors found"), *SkeletonName);
    }
}

void FAnimNode_RenderStreamSkeletonSource::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
    BasePose.Initialize(Context);
}


FTransform ToUnrealTransform(const FVector& d3Pos, const FQuat& d3Rot)
{
    // Standard d3 to Unreal coordinate system transform
    const FVector pos(
        FUnitConversion::Convert(d3Pos.Z, EUnit::Meters, FRenderStreamModule::distanceUnit()),
        FUnitConversion::Convert(d3Pos.X, EUnit::Meters, FRenderStreamModule::distanceUnit()),
        FUnitConversion::Convert(d3Pos.Y, EUnit::Meters, FRenderStreamModule::distanceUnit()));
    const FQuat rotation(d3Rot.Z, d3Rot.X, d3Rot.Y, d3Rot.W);
    return FTransform(rotation, pos);
}

void FAnimNode_RenderStreamSkeletonSource::PreUpdate(const UAnimInstance* InAnimInstance)
{
    ILiveLinkClient* ThisFrameClient = nullptr;
    IModularFeatures& ModularFeatures = IModularFeatures::Get();
    if (ModularFeatures.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
    {
        ThisFrameClient = &IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
    }
    LiveLinkClient_AnyThread = ThisFrameClient;

    // Protection as a class graph pin does not honor rules on abstract classes and NoClear
    UClass* RetargetAssetPtr = RetargetAsset.Get();
    if (!RetargetAssetPtr || RetargetAssetPtr->HasAnyClassFlags(CLASS_Abstract))
    {
        RetargetAssetPtr = URenderStreamRemapAsset::StaticClass();
        RetargetAsset = RetargetAssetPtr;
    }

    if (!CurrentRetargetAsset || RetargetAssetPtr != CurrentRetargetAsset->GetClass())
    {
        CurrentRetargetAsset = NewObject<URenderStreamRemapAsset>(const_cast<UAnimInstance*>(InAnimInstance), RetargetAssetPtr);
        CurrentRetargetAsset->Initialize();
    }

    // Get the name of the exposed parameter
    const FName ParamName = GetSkeletonParamName();
    if (ParamName == FName())
        return;

    // Find and cache skeleton actors using this animnode
    if (!SkeletonActorsCached)
    {
        CacheSkeletonActors(ParamName);
        SkeletonActorsCached = true;
    }

    // Apply the root pose to the skeleton actors
    ApplyRootPose(ParamName);
}

void FAnimNode_RenderStreamSkeletonSource::ApplyRootPose(const FName& ParamName)
{
    const FRenderStreamModule* Module = FRenderStreamModule::Get();

    if (!Module)
        return;
    
    const RenderStreamLink::FSkeletalPose* Pose = Module->GetSkeletalPose(ParamName);

    if (!Pose)
        return;

    // Transform root pose to UE coordinate system
    const FTransform RootPoseUE = ToUnrealTransform(FVector(Pose->rootPosition.X, Pose->rootPosition.Y, Pose->rootPosition.Z),
        FQuat(Pose->rootOrientation.X, Pose->rootOrientation.Y, Pose->rootOrientation.Z, Pose->rootOrientation.W));

    // Apply pose to any cached skeleton actors
    for (const TWeakObjectPtr<ASkeletalMeshActor> SkeletonActor : SkeletonActors)
    {
        if (SkeletonActor.IsValid())
        {
            USceneComponent* SceneComponent = SkeletonActor->K2_GetRootComponent();
            if (SceneComponent)
            {
                const FQuat RotationOffset = FQuat::MakeFromRotator(FRotator(0, 90, 0)) * RootPoseUE.GetRotation();  // Apply 90 degree yaw to account for skeleton default orientation
                SceneComponent->SetRelativeRotation(RotationOffset);
                SceneComponent->SetRelativeLocation(RootPoseUE.GetLocation());
            }
        }
    }
}

void FAnimNode_RenderStreamSkeletonSource::Update_AnyThread(const FAnimationUpdateContext& Context)
{
    BasePose.Update(Context);

    GetEvaluateGraphExposedInputs().Execute(Context);

    // Accumulate Delta time from update
    CachedDeltaTime += Context.GetDeltaTime();

    TRACE_ANIM_NODE_VALUE(Context, TEXT("SkeletonParamName"), GetSkeletonParamName());
}

void FAnimNode_RenderStreamSkeletonSource::Evaluate_AnyThread(FPoseContext& Output)
{
    BasePose.Evaluate(Output);

    const FRenderStreamModule* Module = FRenderStreamModule::Get();

    if (!CurrentRetargetAsset || !Module)
    {
        return;
    }

    FName ParamName = GetSkeletonParamName();
    const RenderStreamLink::FSkeletalLayout* Layout = Module->GetSkeletalLayout(ParamName);
    const RenderStreamLink::FSkeletalPose* Pose = Module->GetSkeletalPose(ParamName);

    FLiveLinkSkeletonStaticData LiveLinkStatic;
    FLiveLinkAnimationFrameData LiveLinkFrame;
    TArray<FTransform> InitialPose;

    if (!Layout || !Pose)
        return;

    ProcessSkeletonData(Layout, Pose, LiveLinkStatic, LiveLinkFrame, InitialPose);

    FLiveLinkSubjectFrameData SubjectFrameData;

    check(CurrentRetargetAsset);
    CurrentRetargetAsset->SetInitialPose(InitialPose);
    CurrentRetargetAsset->BuildPoseFromAnimationData(CachedDeltaTime, &LiveLinkStatic, &LiveLinkFrame, Output.Pose);
    CurrentRetargetAsset->BuildPoseAndCurveFromBaseData(CachedDeltaTime, &LiveLinkStatic, &LiveLinkFrame, Output.Pose, Output.Curve);
    CachedDeltaTime = 0.f; // Reset so that if we evaluate again we don't "create" time inside of the retargeter
            
}

void FAnimNode_RenderStreamSkeletonSource::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
    Super::CacheBones_AnyThread(Context);
    BasePose.CacheBones(Context);
}

void FAnimNode_RenderStreamSkeletonSource::GatherDebugData(FNodeDebugData& DebugData)
{
    FString DebugLine = FString::Printf(TEXT("RenderStreamSkeletonSource - SkeletonParamName: %s"), *GetSkeletonParamName().ToString());

    DebugData.AddDebugItem(DebugLine);
    BasePose.GatherDebugData(DebugData);
}

void FAnimNode_RenderStreamSkeletonSource::ProcessSkeletonData(const RenderStreamLink::FSkeletalLayout* Layout, const RenderStreamLink::FSkeletalPose* Pose, 
    FLiveLinkSkeletonStaticData& LiveLinkStatic, FLiveLinkAnimationFrameData& LiveLinkFrame, TArray<FTransform>& InitialPose)
{
    if (!Layout || !Pose)
        return;

    // Unreal skeletons are defined with X sideways, rather than Y, so need to apply a 90 degree yaw
    const FTransform ToSkeletonSpace(FQuat::MakeFromRotator(FRotator(0, 90, 0)));

    int32 RootIdx = INDEX_NONE;

    // Static data
    LiveLinkStatic.BoneNames.SetNum(Layout->joints.Num());
    LiveLinkStatic.BoneParents.SetNum(Layout->joints.Num());
    InitialPose.SetNum(Layout->joints.Num());

    for (int32 i = 0; i < Layout->joints.Num(); ++i)
    {
        const RenderStreamLink::SkeletonJointDesc& Joint = Layout->joints[i];
        const FString& Name = Layout->jointNames[i];

        // Set bone names and parent indices
        LiveLinkStatic.BoneNames[i] = FName(Name);
        const int32 idx = Layout->joints.IndexOfByPredicate([&Joint](const auto& OtherJoint) { return OtherJoint.id == Joint.parentId; });
        LiveLinkStatic.BoneParents[i] = idx; // Root bone is indicated by negative index, which IndexOfByPredicate will return if not found
        if (idx == INDEX_NONE)
            RootIdx = i;

        // Set initial pose
        const FTransform JointPoseUE = ToUnrealTransform(FVector(Joint.transform.x, Joint.transform.y, Joint.transform.z),
            FQuat(Joint.transform.rx, Joint.transform.ry, Joint.transform.rz, Joint.transform.rw));
        InitialPose[i] = ToSkeletonSpace * JointPoseUE * ToSkeletonSpace.Inverse();
    }

    // Frame Data
    LiveLinkFrame.Transforms.SetNum(Pose->joints.Num());

    for (int32 i = 0; i < Pose->joints.Num(); ++i)
    {
        // Set live pose for each joint
        const RenderStreamLink::SkeletonJointPose& Joint = Pose->joints[i];
        const int32 idx = Layout->joints.IndexOfByPredicate([&Joint](const auto& LayoutJoint) { return LayoutJoint.id == Joint.id; });

        const FTransform JointPoseUE = ToUnrealTransform(FVector(Joint.transform.x, Joint.transform.y, Joint.transform.z),
            FQuat(Joint.transform.rx, Joint.transform.ry, Joint.transform.rz, Joint.transform.rw));
        LiveLinkFrame.Transforms[idx] = ToSkeletonSpace * JointPoseUE * ToSkeletonSpace.Inverse();
    }
}

FName FAnimNode_RenderStreamSkeletonSource::GetSkeletonParamName()
{
    const UAnimBlueprintGeneratedClass* BPClass = dynamic_cast<const UAnimBlueprintGeneratedClass*>(GetAnimClassInterface());

    if (BPClass)
    {
        if (const FRenderStreamModule* Module = FRenderStreamModule::Get(); Module)
        {
            const FName* SubjectName = Module->GetSkeletalParamName(FSoftObjectPath(BPClass->TargetSkeleton));
            if (SubjectName)
                return *SubjectName;
        }
    }
    return FName();
}