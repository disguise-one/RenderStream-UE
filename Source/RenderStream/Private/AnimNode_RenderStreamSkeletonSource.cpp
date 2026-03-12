#include "AnimNode_RenderStreamSkeletonSource.h"
#include "RenderStream.h"
#include "SkeletonRetargeting.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimTrace.h"

#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Animation/SkeletalMeshActor.h"

#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/Skeleton.h"

#include "Engine/World.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNodeBase.h"

static TArray<FName> GetExpectedBonesForLayout(ERenderStreamSkeletonLayout Layout)
{
    TArray<FName> ExpectedBones;

    switch (Layout)
    {
    case ERenderStreamSkeletonLayout::Captury:
        ExpectedBones = {
            "Hips", "Spine", "Spine1", "Spine2", "Spine3", "Spine4",
            "Neck", "Head", "LeftShoulder", "LeftArm", "LeftForeArm",
            "LeftHand", "RightShoulder", "RightArm", "RightForeArm",
            "RightHand", "LeftUpLeg", "LeftLeg", "LeftFoot", "LeftToeBase",
            "RightUpLeg", "RightLeg", "RightFoot", "RightToeBase",
            "LeftHandThumb1", "LeftHandThumb2", "LeftHandThumb3",
            "LeftHandIndex1", "LeftHandIndex2", "LeftHandIndex3",
            "LeftHandMiddle1", "LeftHandMiddle2", "LeftHandMiddle3",
            "LeftHandRing1", "LeftHandRing2", "LeftHandRing3",
            "LeftHandPinky1", "LeftHandPinky2", "LeftHandPinky3",
            "RightHandThumb1", "RightHandThumb2", "RightHandThumb3",
            "RightHandIndex1", "RightHandIndex2", "RightHandIndex3",
            "RightHandMiddle1", "RightHandMiddle2", "RightHandMiddle3",
            "RightHandRing1", "RightHandRing2", "RightHandRing3",
            "RightHandPinky1", "RightHandPinky2", "RightHandPinky3"
        };
        break;

    case ERenderStreamSkeletonLayout::Default:
    default:
        ExpectedBones = {
            "Pelvis", "Spine", "Chest", "Neck",
            "LeftClavicle", "LeftShoulder", "LeftElbow", "LeftWrist",
            "LeftHip", "LeftKnee", "LeftAnkle",
            "RightClavicle", "RightShoulder", "RightElbow", "RightWrist",
            "RightHip", "RightKnee", "RightAnkle"
        };
        break;
    }

    return ExpectedBones;
}

void FAnimNode_RenderStreamSkeletonSource::OnLayoutChanged(const USkeleton* TargetSkeleton)
{
    // Save the current visible list into the master cache
    // This captures any edits the user just made before switching layouts
    for (const FBoneMapping& Mapping : BoneNameMap)
    {
        if (Mapping.SourceBone != NAME_None)
        {
            FBoneCacheEntry Entry;
            Entry.BoneName = Mapping.Bone.BoneName;
            Entry.bSkipOrientationCorrection = Mapping.bSkipOrientationCorrection;
            MasterBoneCache.Add(Mapping.SourceBone, Entry);
        }
    }

    // Build a set of skeleton bone names for auto-matching
    TSet<FName> SkeletonBoneNames;
    if (TargetSkeleton)
    {
        const FReferenceSkeleton& RefSkel = TargetSkeleton->GetReferenceSkeleton();
        for (int32 i = 0; i < RefSkel.GetNum(); ++i)
        {
            SkeletonBoneNames.Add(RefSkel.GetBoneName(i));
        }
    }

    // Clear the visible list for the new layout
    BoneNameMap.Empty();

    // Get the expected bones for the new layout
    TArray<FName> ExpectedBones = GetExpectedBonesForLayout(SkeletonLayout);

    // Populate the visible list
    for (const FName& Bone : ExpectedBones)
    {
        FBoneMapping NewMapping;
        NewMapping.SourceBone = Bone;

        // If we have a cached mapping for this bone, use it!
        if (const FBoneCacheEntry* CachedEntry = MasterBoneCache.Find(Bone))
        {
            NewMapping.Bone.BoneName = CachedEntry->BoneName;
            NewMapping.bSkipOrientationCorrection = CachedEntry->bSkipOrientationCorrection;
        }
        else if (SkeletonBoneNames.Contains(Bone))
        {
            // Auto-match: source bone name exists in the target skeleton
            NewMapping.Bone.BoneName = Bone;
        }
        else
        {
            NewMapping.Bone.BoneName = NAME_None;
        }

        BoneNameMap.Add(NewMapping);
    }
}

FAnimNode_RenderStreamSkeletonSource::FAnimNode_RenderStreamSkeletonSource()
{
    // Initialize with the default layout map on creation
    OnLayoutChanged();
}

FAnimNode_RenderStreamSkeletonSource::~FAnimNode_RenderStreamSkeletonSource()
{
    const FRenderStreamModule* Module = FRenderStreamModule::Get();
    if (Module)
    {
        Module->OnActorSpawnedDelegate.Remove(OnActorSpawnedHandle);
    }
    
}

void FAnimNode_RenderStreamSkeletonSource::CacheSkeletonActors(const FName& ParamName)
{
    // Find and cache any skeleton actors using this animation node
    // Note that skeletal mesh components can be added to basic actors, not just skeletalmeshactors

    if (!GWorld)
    {
        UE_LOG(LogRenderStream, Warning, TEXT("Error initialising skeleton %s. No GWorld."), *ParamName.ToString());
        return;
    }

    TArray<AActor*> FoundActors;
    UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), FoundActors);

    for (AActor* Actor : FoundActors)
    {
        AddIfCorrespondingSkeletonActor(Actor);
    }
}

void FAnimNode_RenderStreamSkeletonSource::AddIfCorrespondingSkeletonActor(AActor* SkeletonActor)
{
    if (!SkeletonActor)
        return;

    // Get any skeletal mesh components from the actor
    TArray<USkeletalMeshComponent*> SkeletalMeshComponents;
    SkeletonActor->GetComponents(SkeletalMeshComponents, false);

    if (SkeletalMeshComponents.IsEmpty())
        return;

    USkeleton* ThisSkeleton = nullptr;
    const UAnimBlueprintGeneratedClass* BPClass = dynamic_cast<const UAnimBlueprintGeneratedClass*>(GetAnimClassInterface());
    const FString SkeletonName = GetSkeletonParamName().ToString();

    // Get the skeleton corresponding to this AnimGraph
    if (!BPClass)
    {
        UE_LOG(LogRenderStream, Warning, TEXT("Error initialising skeleton %s. Couldn't find blueprint class"), *SkeletonName);
        return;
    }

    const UClass* ThisAnimClass = IAnimClassInterface::GetActualAnimClass(BPClass);
    if (!ThisAnimClass)
    {
        UE_LOG(LogRenderStream, Warning, TEXT("Error initialising skeleton %s. Couldn't find anim class"), *SkeletonName);
        return;
    }

    // Check if the actor is using this skeleton
    for (const USkeletalMeshComponent* SkeletalMeshComponent : SkeletalMeshComponents)
    {
        if (SkeletalMeshComponent)
        {
            TSubclassOf<UAnimInstance> AnimClass = SkeletalMeshComponent->AnimClass;
            if (AnimClass == ThisAnimClass)
            {
                TWeakObjectPtr<AActor> SkeletonWeakPtr(SkeletonActor);
                if (SkeletonWeakPtr.IsValid() &&
                    std::find(SkeletonActors.begin(), SkeletonActors.end(), SkeletonWeakPtr) == SkeletonActors.end())
                {
                    UE_LOG(LogRenderStream, Log, TEXT("Found actor %s for skeleton %s"), *SkeletonActor->GetActorNameOrLabel(), *SkeletonName);
                    SkeletonActors.push_back(SkeletonWeakPtr);
                }
            }
        }
    }
}

void FAnimNode_RenderStreamSkeletonSource::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
    BasePose.Initialize(Context);
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
        SkeletonActors.clear();
        CacheSkeletonActors(ParamName);
        SkeletonActorsCached = true;

        // Add delegate to pick up any actors spawned after this point
        const FRenderStreamModule* Module = FRenderStreamModule::Get();
        if (!Module)
        {
            UE_LOG(LogRenderStream, Warning, TEXT("Error initialising skeleton %s. No Renderstream module found"), *ParamName.ToString());
            return;
        }
        OnActorSpawnedHandle = Module->OnActorSpawnedDelegate.AddRaw(this, &FAnimNode_RenderStreamSkeletonSource::AddIfCorrespondingSkeletonActor);
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
        FUnitConversion::Convert(Pose->rootPosition.Z, EUnit::Meters, EUnit::Centimeters),
        FUnitConversion::Convert(Pose->rootPosition.X, EUnit::Meters, EUnit::Centimeters),
        FUnitConversion::Convert(Pose->rootPosition.Y, EUnit::Meters, EUnit::Centimeters));
    const FQuat RootRotation = FQuat(Pose->rootOrientation.Z, Pose->rootOrientation.X, Pose->rootOrientation.Y, Pose->rootOrientation.W)
        * FQuat::MakeFromRotator(FRotator(0, 90, 0));  // Apply 90 degree yaw to account for skeleton default orientation

    // Check skeleton actors have been cached
    if (SkeletonActors.empty())
    {
        UE_LOG(LogRenderStream, Warning, TEXT("Error applying skeleton data for %s. No corresponding skeletal mesh actors found"), *ParamName.ToString());
    }

    // Apply pose to any cached skeleton actors
    for (const TWeakObjectPtr<AActor> SkeletonActor : SkeletonActors)
    {
        if (SkeletonActor.IsValid())
        {
            USceneComponent* SceneComponent = SkeletonActor->K2_GetRootComponent();
            if (SceneComponent)
            {
                SceneComponent->SetRelativeRotation(RootRotation);
                if (ScaleRootOffsets)
                    SceneComponent->SetRelativeLocation(SceneComponent->GetRelativeScale3D() * RootPos);
                else
                    SceneComponent->SetRelativeLocation(RootPos);
            }
        }
    }
}

void FAnimNode_RenderStreamSkeletonSource::Update_AnyThread(const FAnimationUpdateContext& Context)
{
    BasePose.Update(Context);

    GetEvaluateGraphExposedInputs().Execute(Context);

    TRACE_ANIM_NODE_VALUE(Context, TEXT("SkeletonParamName"), GetSkeletonParamName());
}

void FAnimNode_RenderStreamSkeletonSource::Evaluate_AnyThread(FPoseContext& Output)
{
    BasePose.Evaluate(Output);

    const FRenderStreamModule* Module = FRenderStreamModule::Get();

    if (!Module)
        return;

    const FName ParamName = GetSkeletonParamName();
    const RenderStreamLink::FSkeletalPose* Pose = Module->GetSkeletalPose(ParamName);

    // Check if bone count has changed
    if (PoseInitialised && Output.Pose.GetNumBones() != CachedInitData.MeshBoneCount)
    {
        PoseInitialised = false;
        UE_LOG(LogRenderStream, Log, TEXT("%s: Number of bones has changed from %d to %d. Reinitialising"),
            *ParamName.ToString(), CachedInitData.MeshBoneCount, Output.Pose.GetNumBones());
    }

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

void FAnimNode_RenderStreamSkeletonSource::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
    Super::CacheBones_AnyThread(Context);
    BasePose.CacheBones(Context);

    // Initialize all our bone references against the current skeleton
    for (FBoneMapping& Mapping : BoneNameMap)
    {
        Mapping.Bone.Initialize(Context.AnimInstanceProxy->GetRequiredBones());
    }
}

void FAnimNode_RenderStreamSkeletonSource::GatherDebugData(FNodeDebugData& DebugData)
{
    FString DebugLine = FString::Printf(TEXT("RenderStreamSkeletonSource - SkeletonParamName: %s"), *GetSkeletonParamName().ToString());
    DebugData.AddDebugItem(DebugLine);
    BasePose.GatherDebugData(DebugData);
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
    const FName SkeletonName = GetSkeletonParamName();
    const int32 NumBones = OutPose.GetNumBones();

    // Build mesh bones array from compact pose
    TArray<FRetargetMeshBone> MeshBones;
    MeshBones.SetNum(NumBones);
    for (int32 i = 0; i < NumBones; ++i)
    {
        const FCompactPoseBoneIndex CPIdx(i);
        MeshBones[i].LocalTransform = OutPose[CPIdx];
        const FCompactPoseBoneIndex ParentCPIdx = OutPose.GetParentBoneIndex(CPIdx);
        MeshBones[i].ParentIndex = ParentCPIdx.GetInt();
    }

    // Build source-name → mesh-index map and skip-correction set from BoneNameMap
    TMap<FName, int32> NameToIdx;
    TSet<FName> SkipCorrectionNames;
    for (const FBoneMapping& Mapping : BoneNameMap)
    {
        if (Mapping.SourceBone == NAME_None)
            continue;
        const FCompactPoseBoneIndex CPIdx = Mapping.Bone.GetCompactPoseIndex(BoneContainerRef);
        if (CPIdx != INDEX_NONE)
            NameToIdx.Add(Mapping.SourceBone, CPIdx.GetInt());
        if (Mapping.bSkipOrientationCorrection)
            SkipCorrectionNames.Add(Mapping.SourceBone);
    }

    RenderStreamRetargeting::InitialiseRetargeting(MeshBones, Layout, NameToIdx, SkipCorrectionNames, CachedInitData);

    UE_LOG(LogRenderStream, Log, TEXT("%s: Initialised pose with %d bones"),
        *SkeletonName.ToString(), CachedInitData.MeshBoneCount);
}

void FAnimNode_RenderStreamSkeletonSource::BuildPoseFromAnimationData(const RenderStreamLink::FSkeletalPose& Pose, FCompactPose& OutPose)
{
    const int32 NumBones = OutPose.GetNumBones();

    // Snapshot local transforms from compact pose
    TArray<FTransform> LocalTransforms;
    LocalTransforms.SetNum(NumBones);
    for (int32 i = 0; i < NumBones; ++i)
        LocalTransforms[i] = OutPose[FCompactPoseBoneIndex(i)];

    RenderStreamRetargeting::BuildRetargetedPose(Pose, CachedInitData, LocalTransforms);

    // Write retargeted transforms back to compact pose
    for (int32 i = 0; i < NumBones; ++i)
        OutPose[FCompactPoseBoneIndex(i)] = LocalTransforms[i];

    UE_LOG(LogRenderStream, Verbose, TEXT("%s: Applied Live Link pose data to %d poses"),
        *GetSkeletonParamName().ToString(), Pose.joints.Num());
}

bool FAnimNode_RenderStreamSkeletonSource::IsRootBone(int32 SourceIndex)
{
    return CachedInitData.SourceParentIndices[SourceIndex] < 0;
}