#include "AnimNode_RenderStreamSkeletonSource.h"
#include "RenderStream.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimTrace.h"

#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Animation/SkeletalMeshActor.h"

#include "Animation/AnimBlueprintGeneratedClass.h"

TMap<FName, FName> GetDefaultBoneNameMap()
{
    TMap<FName, FName> BoneMap;

    const std::vector<FName> ExpectedBones =
    {
        "R",
        "Spine",
        "Chest",
        "Neck",
        "L_Hip",
        "L_Knee",
        "L_Ankle",
        "L_Foot_Pinky",
        "L_Shoulder",
        "L_Elbow",
        "L_Wrist",
        "R_Hip",
        "R_Knee",
        "R_Ankle",
        "R_Foot_Pinky",
        "R_Shoulder",
        "R_Elbow",
        "R_Wrist",
        "L_Big_Toe",
        "L_Shoulder_Prism",
        "L_Hand1",
        "L_Hand2",
        "R_Big_Toe",
        "R_Shoulder_Prism",
        "R_Hand1",
        "R_Hand2",
        "L_Ear",
        "L_Eye",
        "R_Ear",
        "R_Eye"
    };

    for (const FName& Bone : ExpectedBones)
    {
        BoneMap.Add(Bone, Bone);
    }

    return BoneMap;
}

FAnimNode_RenderStreamSkeletonSource::FAnimNode_RenderStreamSkeletonSource()
{
    BoneNameMap = GetDefaultBoneNameMap();
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

void FAnimNode_RenderStreamSkeletonSource::PreUpdate(const UAnimInstance* InAnimInstance)
{
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
    const FVector RootPos(
        FUnitConversion::Convert(Pose->rootPosition.Z, EUnit::Meters, FRenderStreamModule::distanceUnit()),
        FUnitConversion::Convert(Pose->rootPosition.X, EUnit::Meters, FRenderStreamModule::distanceUnit()),
        FUnitConversion::Convert(Pose->rootPosition.Y, EUnit::Meters, FRenderStreamModule::distanceUnit()));
    const FQuat RootRotation = FQuat(Pose->rootOrientation.Z, Pose->rootOrientation.X, Pose->rootOrientation.Y, Pose->rootOrientation.W)
        * FQuat::MakeFromRotator(FRotator(0, 90, 0));  // Apply 90 degree yaw to account for skeleton default orientation

    // Apply pose to any cached skeleton actors
    for (const TWeakObjectPtr<ASkeletalMeshActor> SkeletonActor : SkeletonActors)
    {
        if (SkeletonActor.IsValid())
        {
            USceneComponent* SceneComponent = SkeletonActor->K2_GetRootComponent();
            if (SceneComponent)
            {
                SceneComponent->SetRelativeRotation(RootRotation);
                SceneComponent->SetRelativeLocation(RootPos);
            }
        }
    }
}

void FAnimNode_RenderStreamSkeletonSource::Update_AnyThread(const FAnimationUpdateContext& Context)
{
    GetEvaluateGraphExposedInputs().Execute(Context);

    TRACE_ANIM_NODE_VALUE(Context, TEXT("SkeletonParamName"), GetSkeletonParamName());
}

void FAnimNode_RenderStreamSkeletonSource::Evaluate_AnyThread(FPoseContext& Output)
{
    Output.ResetToRefPose();

    const FRenderStreamModule* Module = FRenderStreamModule::Get();

    if (!Module)
        return;

    FName ParamName = GetSkeletonParamName();
    const RenderStreamLink::FSkeletalPose* Pose = Module->GetSkeletalPose(ParamName);

    // Initialise data if required
    if (!PoseInitialised)
    {
        const RenderStreamLink::FSkeletalLayout* Layout = Module->GetSkeletalLayout(ParamName);
        if (Layout)
        {
            InitialiseAnimationData(*Layout, Output.Pose);
            PoseInitialised = true;
        }
    }

    // Apply latest pose to skeleton
    if (Pose && PoseInitialised)
        BuildPoseFromAnimationData(*Pose, Output.Pose);      
}

void FAnimNode_RenderStreamSkeletonSource::GatherDebugData(FNodeDebugData& DebugData)
{
    FString DebugLine = FString::Printf(TEXT("RenderStreamSkeletonSource - SkeletonParamName: %s"), *GetSkeletonParamName().ToString());
    DebugData.AddDebugItem(DebugLine);
}

FTransform ToUnrealTransform(const RenderStreamLink::Transform& transform)
{
    // Standard d3 to Unreal coordinate system transform
    const FVector pos(
        FUnitConversion::Convert(transform.z, EUnit::Meters, FRenderStreamModule::distanceUnit()),
        FUnitConversion::Convert(transform.x, EUnit::Meters, FRenderStreamModule::distanceUnit()),
        FUnitConversion::Convert(transform.y, EUnit::Meters, FRenderStreamModule::distanceUnit()));
    const FQuat rotation(transform.rz, transform.rx, transform.ry, transform.rw);
    const FTransform JointPoseUE(rotation, pos);

    // Unreal skeletons are defined with X sideways, rather than Y, so need to apply a 90 degree yaw
    const FTransform ToSkeletonSpace(FQuat::MakeFromRotator(FRotator(0, 90, 0)));
    return ToSkeletonSpace * JointPoseUE * ToSkeletonSpace.Inverse();
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

void FAnimNode_RenderStreamSkeletonSource::InitialiseAnimationData(const RenderStreamLink::FSkeletalLayout& Layout, const FCompactPose& OutPose)
{
    const FBoneContainer& BoneContainerRef = OutPose.GetBoneContainer();
    const int32 MeshBoneCount = OutPose.GetNumBones();
    const FName SkeletonName = GetSkeletonParamName();

    // Initialise bone info vectors
    const int32 SourceBoneCount = Layout.joints.Num();
    SourceBoneNames.SetNum(SourceBoneCount);
    SourceParentIndices.SetNum(SourceBoneCount);
    TArray<int32> SourceNumberOfChildren; SourceNumberOfChildren.Init(0, SourceBoneCount);
    TArray<int32> MeshToSourceIndex; MeshToSourceIndex.Init(INDEX_NONE, MeshBoneCount);
    SourceToMeshIndex.Init(FCompactPoseBoneIndex(INDEX_NONE), SourceBoneCount);
    TArray<FTransform> SourceInitialPose;  SourceInitialPose.SetNum(SourceBoneCount);

    // Loop through source layout and find mapping to mesh bones
    // Find remapped bone names and cache them for fast subsequent retrieval.
    // NB source bones may not be in hierarchy order
    for (int SourceIndex = 0; SourceIndex < SourceBoneCount; SourceIndex++)
    {
        // Get source bone info from layout
        const RenderStreamLink::SkeletonJointDesc& Joint = Layout.joints[SourceIndex];
        SourceBoneNames[SourceIndex] = FName(Layout.jointNames[SourceIndex]);
        const FName& SourceBoneName = SourceBoneNames[SourceIndex];
        const int32 SourceParentBoneIndex = Layout.joints.IndexOfByPredicate([&Joint](const auto& OtherJoint) { return OtherJoint.id == Joint.parentId; });
        SourceParentIndices[SourceIndex] = SourceParentBoneIndex;
        SourceInitialPose[SourceIndex] = ToUnrealTransform(Joint.transform);

        // Find equivalent mesh bone name and index for current source bone
        FCompactPoseBoneIndex MeshIndex(INDEX_NONE);
        if (BoneNameMap.Contains(SourceBoneName))
        {
            const FName MeshBoneName = BoneNameMap[SourceBoneName];
            const int32 ReferenceMeshIndex = BoneContainerRef.GetPoseBoneIndexForBoneName(MeshBoneName);
            MeshIndex = BoneContainerRef.MakeCompactPoseIndex(FMeshPoseBoneIndex(ReferenceMeshIndex));
        }

        // Populate mappings between indices and number of children
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
        *SkeletonName.ToString(), SourceBoneCount);

    // We now go through and calculate any differences between the initial pose of the mesh, and the initial pose of the source data
    // Then we can account for these offsets when applying the live source frame data to the skeleton
    // The pose differences can be categorised as:
    // - Initial orientation differences: The difference between the orientations implied by the directions of the position offsets
    //   of the bones in the initial pose, when the initial pose rotations are all set to zero
    // - Initial rotation differences: The difference between the rotations of the joints in the initial poses. For this we only need
    //   to consider the source initial rotations, as when we apply rotations to the bones we overwrite any initial rotation in the mesh pose.

    // Vectors initialised here and used when applying live frame data to bones 
    MeshToSourceSpaceRotations.Init(FQuat::Identity, SourceBoneCount);
    LocalInitialOrientationDifferences.Init(FQuat::Identity, SourceBoneCount);
    SourceInitialPoseRotations.Init(FQuat::Identity, SourceBoneCount);

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
        FVector Position = OutPose[CPMeshIndex].GetTranslation();
        FQuat Rotation = OutPose[CPMeshIndex].GetRotation();

        const FCompactPoseBoneIndex MeshParentIndex = OutPose.GetParentBoneIndex(CPMeshIndex);
        if ((MeshParentIndex != INDEX_NONE) && (MeshParentIndex < MeshBoneCount))
        {
            Position = MeshBoneWorldRotations[MeshParentIndex.GetInt()] * Position + MeshBoneWorldPositions[MeshParentIndex.GetInt()];  // Find world positions with no rotations applied
            Rotation = MeshBoneWorldRotations[MeshParentIndex.GetInt()] * Rotation;
        }

        MeshBoneWorldPositions[MeshIndex] = Position;
        MeshBoneWorldRotations[MeshIndex] = Rotation;

        const int32 SourceIndex = MeshToSourceIndex[MeshIndex];
        if (SourceIndex == INDEX_NONE)
            continue;
        else
            MeshToSourceSpaceRotations[SourceIndex] = Rotation;

        // Find root bone transform
        // Apply the inverse of the parents total position/rotation, so that root bone is at zero
        const FName& SourceBoneName = SourceBoneNames[SourceIndex];
        if (IsRootBone(SourceBoneName) && (MeshParentIndex != INDEX_NONE) && (MeshParentIndex < MeshBoneCount))
        {
            RootBoneTransform = FTransform(MeshBoneWorldRotations[MeshParentIndex.GetInt()], MeshBoneWorldPositions[MeshParentIndex.GetInt()]).Inverse();
        }

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
        const FQuat InitialRotation = SourceInitialPose[SourceIndex].GetRotation();
        SourceInitialPoseRotations[SourceIndex] = InitialRotation;

        // Find offset between mesh joint and the SOURCE pose's parent (in case source contains fewer bones than mesh)
        // And use this to calculate the initial orientation of the mesh pose bone
        const FVector MeshInitialOffset = MeshBoneWorldPositions[MeshIndex] - MeshBoneWorldPositions[ParentMeshIndex];
        float MeshRollAngle = UKismetMathLibrary::Atan2(MeshInitialOffset.Z, MeshInitialOffset.X);
        const FQuat MeshInitialOrientation = FQuat::MakeFromEuler(FVector(0.f, FMath::RadiansToDegrees(MeshRollAngle), 0.f));

        // Find difference in initial orientation between mesh and source pose
        const FVector SourceInitialOffset = SourceInitialPose[SourceIndex].GetTranslation();
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
            LocalInitialOrientationDifferences[SourceParentIndex] = MeshBoneWorldRotations[ParentMeshIndex].Inverse() * (WorldInitialOrientationDifferences[MeshIndex] * WorldInitialOrientationDifferences[ParentMeshIndex].Inverse()) * MeshBoneWorldRotations[ParentMeshIndex];
        }

        MeshToSourceSpaceRotations[SourceParentIndex] = WorldInitialOrientationDifferences[MeshIndex] * MeshToSourceSpaceRotations[SourceParentIndex];
    }

    UE_LOG(LogRenderStream, Log, TEXT("%s: Initialised pose with %d bones"),
        *SkeletonName.ToString(), MeshBoneCount);
}

void FAnimNode_RenderStreamSkeletonSource::BuildPoseFromAnimationData(const RenderStreamLink::FSkeletalPose& Pose, FCompactPose& OutPose)
{
    const int32 SourceBoneCount = Pose.joints.Num();
    check(SourceBoneNames.Num() == SourceBoneCount);

    // Loop over source pose data and apply to mesh bones
    for (int32 SourceIndex = 0; SourceIndex < SourceBoneCount; SourceIndex++)
    {
        const FCompactPoseBoneIndex MeshIndex = SourceToMeshIndex[SourceIndex];

        if (MeshIndex != INDEX_NONE)
        {
            const RenderStreamLink::SkeletonJointPose& Joint = Pose.joints[SourceIndex];
            const FName& SourceBoneName = SourceBoneNames[SourceIndex];

            if (IsRootBone(SourceBoneName))
            {
                // Set the root bone position so it is at zero
                // Root pose is applied directly to the SkeletalMeshActor transform
                OutPose[MeshIndex].SetTranslation(RootBoneTransform.GetTranslation());
                OutPose[MeshIndex].SetRotation(OutPose[MeshIndex].GetRotation());
            }
            else
            {
                // Get bone transform from source data, and rotate into mesh bone coordinate system
                const FTransform SourceBoneTransform = ToUnrealTransform(Joint.transform);

                // Apply rotations
                const FQuat& MeshToSource = MeshToSourceSpaceRotations[SourceIndex];  // Transform from space in which rotations are applied to UE mesh, to space in which rotations are applied in source data
                const FQuat SourceRotation = MeshToSource.Inverse() * SourceInitialPoseRotations[SourceIndex] * SourceBoneTransform.GetRotation() * MeshToSource;  // Rotation to apply from the source data
                const FQuat MeshRotation = OutPose[MeshIndex].GetRotation();  // Rotation to apply for the initial mesh pose
                const FQuat& InitialOrientationOffset = LocalInitialOrientationDifferences[SourceIndex];  // Rotation to apply to account for different initial orientations (e.g. A-pose vs T-pose)
                OutPose[MeshIndex].SetRotation(InitialOrientationOffset * MeshRotation * SourceRotation);

                // Apply position
                const int32 SourceParentIndex = SourceParentIndices[SourceIndex];
                const FQuat& MeshToSourceParent = MeshToSourceSpaceRotations[SourceParentIndex];  // Position is transformed into parent coordinate space
                const FVector SourcePosition = MeshToSourceParent.Inverse() * SourceBoneTransform.GetTranslation(); // Position to apply from the source data
                const FVector MeshPosition = OutPose[MeshIndex].GetTranslation();  // Position to apply for the initial mesh pose
                OutPose[MeshIndex].SetTranslation(MeshPosition + SourcePosition);
            }
        }
    }
    const FName SkeletonName = GetSkeletonParamName();
    UE_LOG(LogRenderStream, Verbose, TEXT("%s: Applied Live Link pose data to %d poses"),
        *SkeletonName.ToString(), SourceBoneCount);
}

/*static*/ bool FAnimNode_RenderStreamSkeletonSource::IsRootBone(const FName& SourceBoneName)
{
    return SourceBoneName == "R";
}