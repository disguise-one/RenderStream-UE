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

FAnimNode_RenderStreamSkeletonSource::FAnimNode_RenderStreamSkeletonSource()
    : RetargetAsset(URenderStreamRemapAsset::StaticClass())
    , CurrentRetargetAsset(nullptr)
    , LiveLinkClient_AnyThread(nullptr)
    , CachedDeltaTime(0.0f)
{
}

void FAnimNode_RenderStreamSkeletonSource::OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance)
{
    CurrentRetargetAsset = nullptr;

    Super::OnInitializeAnimInstance(InProxy, InAnimInstance);
}

void FAnimNode_RenderStreamSkeletonSource::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
    BasePose.Initialize(Context);
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

    int32 RootIdx = INDEX_NONE;

    auto ToUnrealTransform = [](const FVector& S, const FQuat& R, const FVector& T, const FMatrix& YUpMatrix) {
        const FMatrix YUpMatrixInv(YUpMatrix.Inverse());

        FMatrix D3Mat = R.ToMatrix();
        D3Mat.SetOrigin(T);
        D3Mat.ScaleTranslation(S);

        return FTransform(YUpMatrix * D3Mat * YUpMatrixInv);
    };

    const FVector RootScale(FUnitConversion::Convert(1.f, EUnit::Meters, FRenderStreamModule::distanceUnit()));

    static const FMatrix YUpMats[] = {
        // Y up matrix for root pose transform and root bone transform
        FMatrix(FVector(0.0f, 0.0f, 1.0f)
            , FVector(1.0f, 0.0f, 0.0f)
            , FVector(0.0f, 1.0f, 0.0f)
            , FVector(0.0f, 0.0f, 0.0f)
        ),
        // Y up matrix for the relative transforms of the rest of the bones
        // Skeleton pose is defined in Unreal with X sideways
        FMatrix(FVector(1.0f, 0.0f, 0.0f)
            , FVector(0.0f, 0.0f, -1.0f)
            , FVector(0.0f, 1.0f, 0.0f)
            , FVector(0.0f, 0.0f, 0.0f)
        )
    };

    { // Static data
        LiveLinkStatic.BoneNames.SetNum(Layout->joints.Num());
        LiveLinkStatic.BoneParents.SetNum(Layout->joints.Num());
        InitialPose.SetNum(Layout->joints.Num());
        for (int32 i = 0; i < Layout->joints.Num(); ++i)
        {
            const RenderStreamLink::SkeletonJointDesc& Joint = Layout->joints[i];
            const FString& Name = Layout->jointNames[i];

            LiveLinkStatic.BoneNames[i] = FName(Name);
            const int32 idx = Layout->joints.IndexOfByPredicate([&Joint](const auto& OtherJoint) { return OtherJoint.id == Joint.parentId; });
            LiveLinkStatic.BoneParents[i] = idx; // Root bone is indicated by negative index, which IndexOfByPredicate will return if not found

            InitialPose[i] = ToUnrealTransform(RootScale
                , FQuat(Joint.transform.rx, Joint.transform.ry, Joint.transform.rz, Joint.transform.rw)
                , FVector(Joint.transform.x, Joint.transform.y, Joint.transform.z)
                , YUpMats[1]
            );

            if (idx == INDEX_NONE)
                RootIdx = i;
        }
    }

    { // Frame Data
        LiveLinkFrame.Transforms.SetNum(Pose->joints.Num());
        // calculate all transforms, for root joint and all others
        for (int32 i = 0; i < Pose->joints.Num(); ++i)
        {
            const RenderStreamLink::SkeletonJointPose& Joint = Pose->joints[i];

            const int32 idx = Layout->joints.IndexOfByPredicate([&Joint](const auto& LayoutJoint) { return LayoutJoint.id == Joint.id; });
            const int YUpMatIdx = idx == RootIdx ? 0 : 1;
            LiveLinkFrame.Transforms[idx] = ToUnrealTransform(RootScale
                , FQuat(Joint.transform.rx, Joint.transform.ry, Joint.transform.rz, Joint.transform.rw)
                , FVector(Joint.transform.x, Joint.transform.y, Joint.transform.z)
                , YUpMats[YUpMatIdx]
            );
        }
        // get correct scale and parent transform for root joint
        const FTransform PoseRootTransform = ToUnrealTransform(RootScale, FQuat(Pose->rootOrientation), FVector(Pose->rootPosition), YUpMats[0]);
        FTransform& RootBoneTransform = LiveLinkFrame.Transforms[RootIdx];
        // combined rotations, including 90 degree yaw
        FTransform FinalTransform = FTransform(PoseRootTransform.GetRotation() * RootBoneTransform.GetRotation());
        FinalTransform.ConcatenateRotation(FQuat::MakeFromRotator(FRotator(0, 90, 0)));
        // combined translation, unaffected by rotations
        FinalTransform.SetTranslation(PoseRootTransform.GetTranslation() + RootBoneTransform.GetTranslation());
        // update root joint transform
        RootBoneTransform = FinalTransform;
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