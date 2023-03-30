// Fill out your copyright notice in the Description page of Project Settings.


#include "RenderStreamRemapAsset.h"
#include "RenderStream.h"

#include "BonePose.h"
#include "Engine/Blueprint.h"
#include "LiveLinkTypes.h"
#include "Roles/LiveLinkAnimationTypes.h"
#include "Engine/Classes/Kismet/KismetMathLibrary.h"

#pragma optimize("", off)

URenderStreamRemapAsset::URenderStreamRemapAsset(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
#if WITH_EDITOR
    UBlueprint* Blueprint = Cast<UBlueprint>(GetClass()->ClassGeneratedBy);
    if (Blueprint)
    {
        OnBlueprintCompiledDelegate = Blueprint->OnCompiled().AddUObject(this, &URenderStreamRemapAsset::OnBlueprintClassCompiled);
    }
#endif
}

void URenderStreamRemapAsset::BeginDestroy()
{
#if WITH_EDITOR
    if (OnBlueprintCompiledDelegate.IsValid())
    {
        if (UBlueprint* Blueprint = Cast<UBlueprint>(GetClass()->ClassGeneratedBy))
        {
            Blueprint->OnCompiled().Remove(OnBlueprintCompiledDelegate);
        }
        OnBlueprintCompiledDelegate.Reset();
    }
#endif

    Super::BeginDestroy();
}

void URenderStreamRemapAsset::OnBlueprintClassCompiled(UBlueprint* TargetBlueprint)
{
    BoneNameMap.Reset();
    CurveNameMap.Reset();
}

void MakeCurveMapFromFrame(const FCompactPose& InPose, const FLiveLinkBaseStaticData* InBaseStaticData, const FLiveLinkBaseFrameData* InFrameData, const TArray<FName, TMemStackAllocator<>>& TransformedCurveNames, TMap<FName, float>& OutCurveMap)
{
    OutCurveMap.Reset();
    OutCurveMap.Reserve(InFrameData->PropertyValues.Num());

    if (InBaseStaticData->PropertyNames.Num() == InFrameData->PropertyValues.Num())
    {
        for (int32 CurveIdx = 0; CurveIdx < InBaseStaticData->PropertyNames.Num(); ++CurveIdx)
        {
            const float PropertyValue = InFrameData->PropertyValues[CurveIdx];
            if (FMath::IsFinite(PropertyValue))
            {
                OutCurveMap.Add(TransformedCurveNames[CurveIdx]) = PropertyValue;
            }
        }
    }
}

void URenderStreamRemapAsset::BuildPoseFromAnimationData(float DeltaTime, const FLiveLinkSkeletonStaticData* InSkeletonData, const FLiveLinkAnimationFrameData* InFrameData, FCompactPose& OutPose)
{
    const FBoneContainer& BoneContainerRef = OutPose.GetBoneContainer();
    const int32 MeshBoneCount = OutPose.GetNumBones();

    if (!Initialised)
    {
        MeshBoneWorldRotations.Init(FQuat::Identity, MeshBoneCount);
        MeshBoneWorldPositions.Init(FVector::ZeroVector, MeshBoneCount);
        WorldInitialOrientationDifferences.Init(FQuat::Identity, MeshBoneCount);
        LocalInitialOrientationDifferences.Init(FQuat::Identity, MeshBoneCount);
        
        const TArray<FTransform>& MeshBoneRefPose = BoneContainerRef.GetRefPoseArray();

        // Loop over mesh joints (from OutPose input) and calculate world positions and rotations
        for (int32 MeshIndex = 0; MeshIndex < MeshBoneCount; ++MeshIndex)
        {
            FCompactPoseBoneIndex CPIndex(MeshIndex);

            FQuat Rotation = OutPose[CPIndex].GetRotation();
            FVector Position = OutPose[CPIndex].GetLocation();

            FCompactPoseBoneIndex CPParentBoneIndex = OutPose.GetParentBoneIndex(CPIndex);
            if ((CPParentBoneIndex != INDEX_NONE) && (CPParentBoneIndex < MeshBoneCount))
            {
                Rotation = MeshBoneWorldRotations[CPParentBoneIndex.GetInt()] * Rotation;
                Position = Position + MeshBoneWorldPositions[CPParentBoneIndex.GetInt()];  // Find world positions with no rotations applied
            }
            else
            {
                FTransform T0 = FTransform(Rotation, Position); // * GlobalDeltaTransform;
                Rotation = T0.GetRotation();
                Position = T0.GetLocation();
            }

            int LocalMeshBoneIndex = BoneContainerRef.MakeMeshPoseIndex(CPIndex).GetInt();
            MeshBoneWorldRotations[LocalMeshBoneIndex] = Rotation;
            MeshBoneWorldPositions[LocalMeshBoneIndex] = Position;
        }

        UE_LOG(LogRenderStream, Log, TEXT("%s: Initialised pose with %d bones"),
            *GetName(), MeshBoneCount);
        Initialised = true;
    }

    const TArray<FName>& SourceBoneNames = InSkeletonData->GetBoneNames();
    const TArray<int32>& SourceParentIndices = InSkeletonData->GetBoneParents();
    const int32 SourceBoneCount = SourceBoneNames.Num();

    TArray<FName, TMemStackAllocator<>> SourceIndexToMeshBoneName;
    SourceIndexToMeshBoneName.Reserve(SourceBoneCount);

    TArray<int32> SourceNumberOfChildren;
    SourceNumberOfChildren.Init(0, SourceBoneCount);

    TArray<int32> MeshToSourceIndex;
    MeshToSourceIndex.Init(INDEX_NONE, MeshBoneCount);
    
    // Loop through source bone names and find mapping to mesh bones
    // Find remapped bone names and cache them for fast subsequent retrieval.
    for (int i = 0; i < SourceBoneCount; i++)
    {
        // Find equivalent mesh bone name for current source bone
        const FName& SourceBoneName = SourceBoneNames[i];
        FName* MeshBoneNamePtr = BoneNameMap.Find(SourceBoneName);
        FName MeshBoneName;
        if (MeshBoneNamePtr == nullptr)
        {
            /* User will create a blueprint child class and implement this function using a switch statement. */
            MeshBoneName = GetRemappedBoneName(SourceBoneName);
            BoneNameMap.Add(SourceBoneName, MeshBoneName);
        }
        else
        {
            MeshBoneName = *MeshBoneNamePtr;
        }
        SourceIndexToMeshBoneName.Add(MeshBoneName);

        const int32 MeshBoneIndex = BoneContainerRef.GetPoseBoneIndexForBoneName(MeshBoneName);
        const FCompactPoseBoneIndex CPBoneIndex = BoneContainerRef.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshBoneIndex));

        const int32 SourceParentBoneIndex = SourceParentIndices[i];
        if (CPBoneIndex != INDEX_NONE)
        {
            MeshToSourceIndex[CPBoneIndex.GetInt()] = i;
        }
        if (SourceParentBoneIndex != INDEX_NONE)
        {
            SourceNumberOfChildren[SourceParentBoneIndex] += 1;
        }

    }
    UE_LOG(LogRenderStream, Verbose, TEXT("%s: Cached %d remapped bone names from static skeleton data "),
        *GetName(), SourceBoneCount);

    // Loop over mesh bones and find difference in starting orientation between mesh pose and static data
    for (int32 MeshIndex = 0; MeshIndex < MeshBoneCount; MeshIndex++)
    {
        const int32 SourceIndex = MeshToSourceIndex[MeshIndex];
        if (SourceIndex == INDEX_NONE)
            continue;

        const int32 SourceParentIndex = SourceParentIndices[SourceIndex];
        if (SourceParentIndex == INDEX_NONE)
            continue;

        const FName ParentBoneName = SourceIndexToMeshBoneName[SourceParentIndex];
        const int32 ReferenceParentMeshIndex = BoneContainerRef.GetPoseBoneIndexForBoneName(ParentBoneName);
        const int32 ParentMeshIndex = BoneContainerRef.MakeCompactPoseIndex(FMeshPoseBoneIndex(ReferenceParentMeshIndex)).GetInt();

        if ((ParentMeshIndex == INDEX_NONE) || (ParentMeshIndex >= MeshBoneCount))
            continue;

        // Don't appy offset if parent has > 1 children
        // We could maybe find the average of all child offsets, and calculate orientation from that at the end
        if (SourceNumberOfChildren[SourceParentIndex] > 1)
        {
            WorldInitialOrientationDifferences[MeshIndex] = WorldInitialOrientationDifferences[ParentMeshIndex];
            continue;
        }

        // Find offset between mesh joint and the SOURCE pose's parent (in case source contains fewers bones than mesh)
        const FVector MeshInitialOffset = MeshBoneWorldPositions[MeshIndex] - MeshBoneWorldPositions[ParentMeshIndex];
        //FQuat InitialRotation = FQuat::FindBetweenVectors(FVector(1.f, 0.f, 0.f), InitialOffset);
        float MeshAngle = UKismetMathLibrary::Atan2(MeshInitialOffset.Z, MeshInitialOffset.X);
        const FQuat MeshInitialRotation = FQuat::MakeFromEuler(FVector(0.f, FMath::RadiansToDegrees(MeshAngle), 0.f));

        // Find difference in initial orientation between mesh and source pose
        const FVector SourceInitialOffset = InitialPose[SourceIndex].GetTranslation();
        if (SourceInitialOffset == FVector(0.f, 0.f, 0.f))
        {
            WorldInitialOrientationDifferences[MeshIndex] = WorldInitialOrientationDifferences[ParentMeshIndex];
        }
        else
        {
            //InitialRotationStreamed = FQuat::FindBetweenVectors(FVector(1.f, 0.f, 0.f), InitialOffsetStreamed);
            float SourceAngle = UKismetMathLibrary::Atan2(SourceInitialOffset.Z, SourceInitialOffset.X);
            const FQuat SourceInitialRotation = FQuat::MakeFromEuler(FVector(0.f, FMath::RadiansToDegrees(SourceAngle), 0.f));
            WorldInitialOrientationDifferences[MeshIndex] = SourceInitialRotation * MeshInitialRotation.Inverse();
            LocalInitialOrientationDifferences[ParentMeshIndex] = WorldInitialOrientationDifferences[MeshIndex] * WorldInitialOrientationDifferences[ParentMeshIndex].Inverse();
        }
    }

    // Loop over source pose data and apply to mesh bones
    for (int32 SourceIndex = 0; SourceIndex < SourceBoneCount; SourceIndex++)
    {
        // TODO Cache SourceToMeshIndex (instead of bone names?)
        const FName MeshBoneName = SourceIndexToMeshBoneName[SourceIndex];
        const int32 ReferenceMeshIndex = BoneContainerRef.GetPoseBoneIndexForBoneName(MeshBoneName);
        const FCompactPoseBoneIndex MeshIndex = BoneContainerRef.MakeCompactPoseIndex(FMeshPoseBoneIndex(ReferenceMeshIndex));

        if (MeshIndex != INDEX_NONE)
        {
            const FName& SourceBoneName = SourceBoneNames[SourceIndex];
            const FTransform& SourceBoneTransform = InFrameData->Transforms[SourceIndex];

            // Only use position + rotation data for root. For all other bones, set rotation only.
            if (GetBoneNameEquivalent_Internal(SourceBoneName) == EquivalentPelvis)
            {
                OutPose[MeshIndex].SetLocation(SourceBoneTransform.GetTranslation());
                OutPose[MeshIndex].SetRotation(SourceBoneTransform.GetRotation());
            }
            else
            {
                OutPose[MeshIndex].SetRotation(SourceBoneTransform.GetRotation() * LocalInitialOrientationDifferences[MeshIndex.GetInt()]);
            }
        }
    }
    UE_LOG(LogRenderStream, Verbose, TEXT("%s: Applied Live Link pose data to %d poses for frame %d"),
        *GetName(), SourceBoneCount, InFrameData->FrameId);
}

void URenderStreamRemapAsset::BuildPoseAndCurveFromBaseData(float DeltaTime, const FLiveLinkBaseStaticData* InBaseStaticData, const FLiveLinkBaseFrameData* InBaseFrameData, FCompactPose& OutPose, FBlendedCurve& OutCurve)
{
    const TArray<FName>& SourceCurveNames = InBaseStaticData->PropertyNames;
    TArray<FName, TMemStackAllocator<>> TransformedCurveNames;
    TransformedCurveNames.Reserve(SourceCurveNames.Num());

    for (const FName& SrcCurveName : SourceCurveNames)
    {
        const FName* TargetCurveName = CurveNameMap.Find(SrcCurveName);
        if (TargetCurveName == nullptr)
        {
            FName NewName = GetRemappedCurveName(SrcCurveName);
            TransformedCurveNames.Add(NewName);
            CurveNameMap.Add(SrcCurveName, NewName);
        }
        else
        {
            TransformedCurveNames.Add(*TargetCurveName);
        }
    }

    TMap<FName, float> BPCurveValues;

    MakeCurveMapFromFrame(OutPose, InBaseStaticData, InBaseFrameData, TransformedCurveNames, BPCurveValues);

    RemapCurveElements(BPCurveValues);

    BuildCurveData(BPCurveValues, OutPose, OutCurve);
}

TEnumAsByte<RenderStreamBoneNameEquivalents> URenderStreamRemapAsset::GetBoneNameEquivalent(const FName& SourceBoneName) const
{
    return GetBoneNameEquivalent_Internal(SourceBoneName);
}

RenderStreamBoneNameEquivalents URenderStreamRemapAsset::GetBoneNameEquivalent_Internal(const FName& SourceBoneName) const
{
    if (SourceBoneName == "R")
     return EquivalentPelvis;
    if (SourceBoneName == "Spine")
     return EquivalentSpine;
    if (SourceBoneName == "Neck")
     return EquivalentNeck;

    if (SourceBoneName == "L_Hip")
     return EquivalentLThigh;
    if (SourceBoneName == "L_Knee")
     return EquivalentLCalf;
    if (SourceBoneName == "L_Ankle")
     return EquivalentLFoot;
    if (SourceBoneName == "L_Foot_Pinky")
     return EquivalentLFootBall;

    if (SourceBoneName == "L_Shoulder")
     return EquivalentLUpperArm;
    if (SourceBoneName == "L_Elbow")
     return EquivalentLLowerArm;
    if (SourceBoneName == "L_Wrist")
     return EquivalentLHand;

    if (SourceBoneName == "R_Hip")
     return EquivalentRThigh;
    if (SourceBoneName == "R_Knee")
     return EquivalentRCalf;
    if (SourceBoneName == "R_Ankle")
     return EquivalentRFoot;
    if (SourceBoneName == "R_Foot_Pinky")
     return EquivalentRFootBall;

    if (SourceBoneName == "R_Shoulder")
     return EquivalentRUpperArm;
    if (SourceBoneName == "R_Elbow")
     return EquivalentRLowerArm;
    if (SourceBoneName == "R_Wrist")
     return EquivalentRHand;

    if (SourceBoneName == "L_Big_Toe")
     return EquivalentNone;
    if (SourceBoneName == "L_Shoulder_prism")
     return EquivalentNone;
    if (SourceBoneName == "L_hand1")
     return EquivalentNone;
    if (SourceBoneName == "L_hand2")
     return EquivalentNone;

    if (SourceBoneName == "R_Big_Toe")
     return EquivalentNone;
    if (SourceBoneName == "R_Shoulder_prism")
     return EquivalentNone;
    if (SourceBoneName == "R_hand1")
     return EquivalentNone;
    if (SourceBoneName == "R_hand2")
     return EquivalentNone;

    if (SourceBoneName == "L_Ear")
     return EquivalentNone;
    if (SourceBoneName == "L_Eye")
     return EquivalentNone;

    if (SourceBoneName == "R_Ear")
     return EquivalentNone;
    if (SourceBoneName == "R_Eye")
     return EquivalentNone;

    if (SourceBoneName == "Chest")
     return EquivalentNone;

    return EquivalentNone;
}

FName URenderStreamRemapAsset::GetRemappedBoneName_Implementation(FName BoneName) const
{
    return BoneName;
}

FName URenderStreamRemapAsset::GetRemappedCurveName_Implementation(FName CurveName) const
{
    return CurveName;
}

void URenderStreamRemapAsset::RemapCurveElements_Implementation(TMap<FName, float>& CurveItems) const
{
}

#pragma optimize("", on)