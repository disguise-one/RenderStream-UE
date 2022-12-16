// Fill out your copyright notice in the Description page of Project Settings.


#include "RenderStreamRemapAsset.h"
#include "RenderStream.h"

#include "BonePose.h"
#include "Engine/Blueprint.h"
#include "LiveLinkTypes.h"
#include "Roles/LiveLinkAnimationTypes.h"

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
    if (!Initialised)
    {
        // MeshBone Count
        const int32 MeshBoneCount = BoneContainerRef.GetNumBones();

        ReferenceWorldRotations.Init(FQuat::Identity, MeshBoneCount);
        ReferenceWorldPositions.Init(FVector::ZeroVector, MeshBoneCount);
        const TArray<FTransform>& MeshBoneRefPose = BoneContainerRef.GetRefPoseArray();
        for (int32 Index = 0; Index < MeshBoneCount; Index++)
        {
            FQuat Rotation = MeshBoneRefPose[Index].GetRotation();
            FVector Position = MeshBoneRefPose[Index].GetLocation();

            int32 ParentIndex = BoneContainerRef.GetParentBoneIndex(Index);
            if ((ParentIndex != INDEX_NONE) && (ParentIndex < MeshBoneCount))
            {
                Rotation = ReferenceWorldRotations[ParentIndex] * Rotation;
                Position = ReferenceWorldRotations[ParentIndex] * Position + ReferenceWorldPositions[ParentIndex];
            }
            else
            {
                FTransform T0 = FTransform(Rotation, Position);
                Rotation = T0.GetRotation();
                Position = T0.GetLocation();
            }

            ReferenceWorldRotations[Index] = Rotation;
            ReferenceWorldPositions[Index] = Position;
        }

        // Compact Pose Bone Count
        // The BoneCount in LiveLink preview panel is greater than or equal at runtime.
        const int32 BoneCount = OutPose.GetNumBones();

        TArray<FQuat> CompactRefPoseRotation;
        TArray<FVector> CompactRefPoseLocation;

        CompactRefPoseRotation.Init(FQuat::Identity, BoneCount);
        CompactRefPoseLocation.Init(FVector::ZeroVector, BoneCount);

        for (int32 Index = 0; Index < BoneCount; ++Index)
        {
            FCompactPoseBoneIndex CPIndex(Index);

            FQuat Rotation = OutPose[CPIndex].GetRotation();
            FVector Position = OutPose[CPIndex].GetLocation();

            FCompactPoseBoneIndex CPParentBoneIndex = BoneContainerRef.GetParentBoneIndex(CPIndex);
            if ((CPParentBoneIndex != INDEX_NONE) && (CPParentBoneIndex < BoneCount))
            {
                Rotation = CompactRefPoseRotation[CPParentBoneIndex.GetInt()] * Rotation;
                Position = CompactRefPoseRotation[CPParentBoneIndex.GetInt()] * Position + CompactRefPoseLocation[CPParentBoneIndex.GetInt()];
            }
            else
            {
                FTransform T0 = FTransform(Rotation, Position); // * GlobalDeltaTransform;
                Rotation = T0.GetRotation();
                Position = T0.GetLocation();
            }

            CompactRefPoseRotation[Index] = Rotation;
            CompactRefPoseLocation[Index] = Position;

            int LocalMeshBoneIndex = BoneContainerRef.MakeMeshPoseIndex(CPIndex).GetInt();
            ReferenceWorldRotations[LocalMeshBoneIndex] = CompactRefPoseRotation[Index];
            ReferenceWorldPositions[LocalMeshBoneIndex] = CompactRefPoseLocation[Index];
        }

        UE_LOG(LogRenderStream, Log, TEXT("%s: Initialised pose with %d bones and %d mesh bones"),
            *GetName(), BoneCount, MeshBoneCount);
        Initialised = true;
    }

    const TArray<FName>& SourceBoneNames = InSkeletonData->GetBoneNames();
    const TArray<int32>& SourceParentBoneNames = InSkeletonData->GetBoneParents();

    TArray<FName, TMemStackAllocator<>> TransformedBoneNames;
    TransformedBoneNames.Reserve(SourceBoneNames.Num());
    
    // Find remapped bone names and cache them for fast subsequent retrieval.
    for (const FName& SrcBoneName : SourceBoneNames)
    {
        FName* TargetBoneName = BoneNameMap.Find(SrcBoneName);
        FName NewName;
        if (TargetBoneName == nullptr)
        {
            /* User will create a blueprint child class and implement this function using a switch statement. */
            NewName = GetRemappedBoneName(SrcBoneName);
            TransformedBoneNames.Add(NewName);
            BoneNameMap.Add(SrcBoneName, NewName);
        }
        else
        {
            NewName = *TargetBoneName;
            TransformedBoneNames.Add(*TargetBoneName);
        }
    }
    UE_LOG(LogRenderStream, Verbose, TEXT("%s: Cached %d remapped bone names from static skeleton data "),
        *GetName(), TransformedBoneNames.Num());

    TArray<FCompactPoseBoneIndex, TMemStackAllocator<>> ModifiedPoses;
    ModifiedPoses.Reserve(TransformedBoneNames.Num());
    // Iterate over remapped bone names, find the index of that bone on the skeleton, and apply the Live Link pose data.
    for (int32 i = 0; i < TransformedBoneNames.Num(); i++)
    {
        FName BoneName = TransformedBoneNames[i];
        FName LogicalParentBoneName = SourceParentBoneNames[i] != INDEX_NONE ? TransformedBoneNames[SourceParentBoneNames[i]] : NAME_None;
        FTransform BoneTransform = InFrameData->Transforms[i];

        const int32 MeshBoneIndex = BoneContainerRef.GetPoseBoneIndexForBoneName(BoneName);
        if (MeshBoneIndex != INDEX_NONE)
        {
            const FCompactPoseBoneIndex CPBoneIndex = BoneContainerRef.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshBoneIndex));
            if (CPBoneIndex != INDEX_NONE)
            {
                const FName* RetargetSourceBoneName = BoneNameMap.FindKey(BoneName);

                // Only use position + rotation data for root. For all other bones, set rotation only.
                if (GetBoneNameEquivalent_Internal(*RetargetSourceBoneName) == EquivalentPelvis)
                {
                    OutPose[CPBoneIndex].SetLocation(BoneTransform.GetTranslation());
                    OutPose[CPBoneIndex].SetRotation(BoneTransform.GetRotation() * ReferenceWorldRotations[MeshBoneIndex]);
                    ModifiedPoses.Add(CPBoneIndex);
                }
                else
                {
                    if (CPBoneIndex != INDEX_NONE)
                    {
                        int32 RealParentMeshIndex = BoneContainerRef.GetParentBoneIndex(MeshBoneIndex);
                        int32 LogicParentMeshIndex = BoneContainerRef.GetPoseBoneIndexForBoneName(LogicalParentBoneName);

                        FQuat RotationInCS = BoneTransform.GetRotation() * ReferenceWorldRotations[MeshBoneIndex];

                        if (RealParentMeshIndex != INDEX_NONE)
                        {
                            if (LogicParentMeshIndex != INDEX_NONE)
                            {
                                FQuat TargetRotationInLogicParent = ReferenceWorldRotations[LogicParentMeshIndex].Inverse() * RotationInCS;
                                FQuat TargetRotationInRealParent = TargetRotationInLogicParent;

                                if (RealParentMeshIndex != LogicParentMeshIndex)
                                {
                                    FQuat RealParentRotationInLogicParent = ReferenceWorldRotations[LogicParentMeshIndex].Inverse() * ReferenceWorldRotations[RealParentMeshIndex];
                                    TargetRotationInRealParent = RealParentRotationInLogicParent.Inverse() * TargetRotationInLogicParent;
                                }

                                OutPose[CPBoneIndex].SetRotation(TargetRotationInRealParent);
                            }
                            else
                            {
                                FQuat TargetRotationInRealParent = ReferenceWorldRotations[RealParentMeshIndex].Inverse() * RotationInCS;
                                OutPose[CPBoneIndex].SetRotation(TargetRotationInRealParent);
                            }
                            ModifiedPoses.Add(CPBoneIndex);
                        }
                    }
                }
            }
        }
    }
    UE_LOG(LogRenderStream, Verbose, TEXT("%s: Applied Live Link pose data to %d poses for frame %d"),
        *GetName(), ModifiedPoses.Num(), InFrameData->FrameId);
}

void URenderStreamRemapAsset::BuildPoseAndCurveFromBaseData(float DeltaTime, const FLiveLinkBaseStaticData* InBaseStaticData, const FLiveLinkBaseFrameData* InBaseFrameData, FCompactPose& OutPose, FBlendedCurve& OutCurve)
{
    const TArray<FName>& SourceCurveNames = InBaseStaticData->PropertyNames;
    TArray<FName, TMemStackAllocator<>> TransformedCurveNames;
    TransformedCurveNames.Reserve(SourceCurveNames.Num());

    for (const FName& SrcCurveName : SourceCurveNames)
    {
        FName* TargetCurveName = CurveNameMap.Find(SrcCurveName);
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