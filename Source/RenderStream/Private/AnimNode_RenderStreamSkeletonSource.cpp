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
#include "Kismet/KismetMathLibrary.h"
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
                const FQuat RotationOffset = RootPoseUE.GetRotation() * FQuat::MakeFromRotator(FRotator(0, 90, 0));  // Apply 90 degree yaw to account for skeleton default orientation
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

    if (!Layout || !Pose)
        return;

    ProcessSkeletonData(Layout, Pose, LiveLinkStatic, LiveLinkFrame);

    FLiveLinkSubjectFrameData SubjectFrameData;

    check(CurrentRetargetAsset);
    BuildPoseFromAnimationData(CachedDeltaTime, &LiveLinkStatic, &LiveLinkFrame, Output.Pose);
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
    FLiveLinkSkeletonStaticData& LiveLinkStatic, FLiveLinkAnimationFrameData& LiveLinkFrame)
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


void FAnimNode_RenderStreamSkeletonSource::InitialiseAnimationData(const FLiveLinkSkeletonStaticData* InSkeletonData, const FLiveLinkAnimationFrameData* InFrameData, const FCompactPose& OutPose)
{
    const FBoneContainer& BoneContainerRef = OutPose.GetBoneContainer();
    const int32 MeshBoneCount = OutPose.GetNumBones();

    const TArray<FName>& SourceBoneNames = InSkeletonData->GetBoneNames();
    const TArray<int32>& SourceParentIndices = InSkeletonData->GetBoneParents();
    const int32 SourceBoneCount = SourceBoneNames.Num();

    SourceToMeshIndex.Init(FCompactPoseBoneIndex(INDEX_NONE), SourceBoneCount);
    TArray<int32> SourceNumberOfChildren; SourceNumberOfChildren.Init(0, SourceBoneCount);
    TArray<int32> MeshToSourceIndex; MeshToSourceIndex.Init(INDEX_NONE, MeshBoneCount);

    // Loop through source bone names and find mapping to mesh bones
    // Find remapped bone names and cache them for fast subsequent retrieval.
    // NB source bones may not be in hierarchy order
    for (int SourceIndex = 0; SourceIndex < SourceBoneCount; SourceIndex++)
    {
        // Find equivalent mesh bone name for current source bone
        const FName& SourceBoneName = SourceBoneNames[SourceIndex];
        const FName MeshBoneName = CurrentRetargetAsset->GetMeshBoneName(SourceBoneName);

        const int32 ReferenceMeshIndex = BoneContainerRef.GetPoseBoneIndexForBoneName(MeshBoneName);
        const FCompactPoseBoneIndex MeshIndex = BoneContainerRef.MakeCompactPoseIndex(FMeshPoseBoneIndex(ReferenceMeshIndex));
        const int32 SourceParentBoneIndex = SourceParentIndices[SourceIndex];

        if (MeshIndex != INDEX_NONE)
        {
            MeshToSourceIndex[MeshIndex.GetInt()] = SourceIndex;
            SourceToMeshIndex[SourceIndex] = MeshIndex;
        }
        if (SourceParentBoneIndex != INDEX_NONE)
        {
            SourceNumberOfChildren[SourceParentBoneIndex] += 1;
        }

    }
    UE_LOG(LogRenderStream, Verbose, TEXT("%s: Cached %d remapped bone names from static skeleton data "),
        *CurrentRetargetAsset->GetName(), SourceBoneCount);

    // We now go through and calculate any differences between the initial pose of the mesh, and the initial pose of the source data
    // Then we can account for these offsets when applying the live source frame data to the skeleton
    // The pose differences can be categorised as:
    // - Initial orientation differences: The difference between the orientations implied by the directions of the position offsets
    //   of the bones in the initial pose, when the initial pose rotations are all set to zero
    // - Initial rotation differences: The difference between the rotations of the joints in the initial poses. For this we only need
    //   to consider the source initial rotations, as when we apply rotations to the bones we overwrite any initial rotation in the mesh pose.

    // Vectors initialised here and used when applying live frame data to bones 
    MeshToSourceInitialOrientations.Init(FQuat::Identity, MeshBoneCount);
    LocalInitialOrientationDifferences.Init(FQuat::Identity, MeshBoneCount);
    SourceInitialPoseRotations.Init(FQuat::Identity, MeshBoneCount);

    // Temporary vectors used to initialise the persistent vectors above
    TArray<FVector> MeshBoneWorldPositions; MeshBoneWorldPositions.Init(FVector::ZeroVector, MeshBoneCount);
    TArray<FQuat> MeshBoneWorldRotations; MeshBoneWorldRotations.Init(FQuat::Identity, MeshBoneCount);
    TArray<FQuat> WorldInitialOrientationDifferences;  WorldInitialOrientationDifferences.Init(FQuat::Identity, MeshBoneCount);

    // Loop over mesh bones (from OutPose input)
    // Mesh bones should be in hierarchy order
    for (int32 MeshIndex = 0; MeshIndex < MeshBoneCount; ++MeshIndex)
    {
        const FCompactPoseBoneIndex CPMeshIndex(MeshIndex);

        // Calculate world positions of bones in initial mesh pose
        FVector Position = OutPose[CPMeshIndex].GetLocation();
        FQuat Rotation = OutPose[CPMeshIndex].GetRotation();

        const FCompactPoseBoneIndex MeshParentIndex = OutPose.GetParentBoneIndex(CPMeshIndex);
        if ((MeshParentIndex != INDEX_NONE) && (MeshParentIndex < MeshBoneCount))
        {
            Position = Position + MeshBoneWorldPositions[MeshParentIndex.GetInt()];  // Find world positions with no rotations applied
            Rotation = Rotation * MeshBoneWorldRotations[MeshParentIndex.GetInt()];
        }

        MeshBoneWorldPositions[MeshIndex] = Position;
        MeshBoneWorldRotations[MeshIndex] = Rotation;

        // Find difference in starting orientation between mesh pose and static source data
        const int32 SourceIndex = MeshToSourceIndex[MeshIndex];
        if (SourceIndex == INDEX_NONE)
            continue;

        const int32 SourceParentIndex = SourceParentIndices[SourceIndex];
        if (SourceParentIndex == INDEX_NONE)
            continue;

        const int32 ParentMeshIndex = SourceToMeshIndex[SourceParentIndex].GetInt();

        if ((ParentMeshIndex == INDEX_NONE) || (ParentMeshIndex >= MeshBoneCount))
            continue;

        // Don't appy offset if parent has > 1 children
        // We could maybe find the average of all child offsets, and calculate orientation from that at the end
        if (SourceNumberOfChildren[SourceParentIndex] > 1)
        {
            WorldInitialOrientationDifferences[MeshIndex] = WorldInitialOrientationDifferences[ParentMeshIndex];
            continue;
        }

        // Find source initial pose rotation
        const FName& SourceBoneName = SourceBoneNames[SourceIndex];
        const FQuat InitialRotation = InitialPose[SourceIndex].GetRotation();
        SourceInitialPoseRotations[MeshIndex] = InitialRotation;

        // Find root bone transform
        if (CurrentRetargetAsset->IsRootBone(SourceBoneName))
        {
            RootBoneTransform.SetLocation(Position);
            RootBoneTransform.SetRotation(Rotation);
        }

        // Find offset between mesh joint and the SOURCE pose's parent (in case source contains fewer bones than mesh)
        // And use this to calculate the initial orientation of the mesh pose bone
        const FVector MeshInitialOffset = MeshBoneWorldPositions[MeshIndex] - MeshBoneWorldPositions[ParentMeshIndex];
        float MeshRollAngle = UKismetMathLibrary::Atan2(MeshInitialOffset.Z, MeshInitialOffset.X);
        const FQuat MeshInitialOrientation = FQuat::MakeFromEuler(FVector(0.f, FMath::RadiansToDegrees(MeshRollAngle), 0.f));

        // Find difference in initial orientation between mesh and source pose
        const FVector SourceInitialOffset = InitialPose[SourceIndex].GetTranslation();
        if (SourceInitialOffset == FVector(0.f, 0.f, 0.f))
        {
            WorldInitialOrientationDifferences[MeshIndex] = WorldInitialOrientationDifferences[ParentMeshIndex];
        }
        else
        {
            // Calculate the initial orientation of the bone in the source pose
            float SourceRollAngle = UKismetMathLibrary::Atan2(SourceInitialOffset.Z, SourceInitialOffset.X);
            const FQuat SourceInitialOrientation = FQuat::MakeFromEuler(FVector(0.f, FMath::RadiansToDegrees(SourceRollAngle), 0.f));

            // Calculate orientation differences between the source and mesh poses
            // World orientation difference is the global difference in orientations of the bones
            // Local orientation difference is  the relative offset to the parent bone required to achieve the correct global orientations
            WorldInitialOrientationDifferences[MeshIndex] = SourceInitialOrientation * MeshInitialOrientation.Inverse();
            LocalInitialOrientationDifferences[ParentMeshIndex] = WorldInitialOrientationDifferences[MeshIndex] * WorldInitialOrientationDifferences[ParentMeshIndex].Inverse();
        }
        MeshToSourceInitialOrientations[ParentMeshIndex] = WorldInitialOrientationDifferences[MeshIndex];
    }

    UE_LOG(LogRenderStream, Log, TEXT("%s: Initialised pose with %d bones"),
        *CurrentRetargetAsset->GetName(), MeshBoneCount);
}

void FAnimNode_RenderStreamSkeletonSource::BuildPoseFromAnimationData(float DeltaTime, const FLiveLinkSkeletonStaticData* InSkeletonData, const FLiveLinkAnimationFrameData* InFrameData, FCompactPose& OutPose)
{
    const TArray<FName>& SourceBoneNames = InSkeletonData->GetBoneNames();
    const int32 SourceBoneCount = SourceBoneNames.Num();

    // Initialise data if required
    if (!PoseInitialised)
    {
        InitialiseAnimationData(InSkeletonData, InFrameData, OutPose);
        PoseInitialised = true;
    }

    // Loop over source pose data and apply to mesh bones
    for (int32 SourceIndex = 0; SourceIndex < SourceBoneCount; SourceIndex++)
    {
        const FCompactPoseBoneIndex MeshIndex = SourceToMeshIndex[SourceIndex];

        if (MeshIndex != INDEX_NONE)
        {
            const FName& SourceBoneName = SourceBoneNames[SourceIndex];
            const FTransform& SourceBoneTransform = InFrameData->Transforms[SourceIndex];

            // Root position and rotation are set in the actor transform.
            // Apply the inverse of the root bone transform here to ensure the root stays at zero
            if (CurrentRetargetAsset->IsRootBone(SourceBoneName))
            {
                FTransform RootInverseTransform = RootBoneTransform.Inverse();
                OutPose[MeshIndex].SetLocation(RootInverseTransform.GetTranslation());
                OutPose[MeshIndex].SetRotation(RootInverseTransform.GetRotation());
            }
            else
            {
                // TODO apply position?
                OutPose[MeshIndex].SetRotation(
                    LocalInitialOrientationDifferences[MeshIndex.GetInt()] *         // 5. Apply local rotations to account for difference in initial orientations
                    MeshToSourceInitialOrientations[MeshIndex.GetInt()].Inverse() *  // 4. Get back to mesh initial orientation space
                    SourceInitialPoseRotations[MeshIndex.GetInt()] *                 // 3. Apply source initial pose rotation
                    SourceBoneTransform.GetRotation() *                              // 2. Apply local bone rotations in source zero-rotation pose space
                    MeshToSourceInitialOrientations[MeshIndex.GetInt()]);            // 1. Transform from mesh to source initial orientation space (from pos, ignoring initial rotation)
            }
        }
    }
    UE_LOG(LogRenderStream, Verbose, TEXT("%s: Applied Live Link pose data to %d poses for frame %d"),
        *CurrentRetargetAsset->GetName(), SourceBoneCount, InFrameData->FrameId);
}