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

FName URenderStreamRemapAsset::GetMeshBoneName(const FName& SourceBoneName)
{
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
    return MeshBoneName;
}

bool URenderStreamRemapAsset::IsRootBone(const FName& SourceBoneName)
{
    return GetBoneNameEquivalent_Internal(SourceBoneName) == EquivalentPelvis;
}

#pragma optimize("", on)