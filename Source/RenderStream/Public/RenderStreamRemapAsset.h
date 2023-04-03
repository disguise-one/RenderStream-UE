// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "LiveLinkRetargetAsset.h"
#include "RenderStreamRemapAsset.generated.h"

UENUM(BlueprintType)
enum RenderStreamBoneNameEquivalents
{
    EquivalentNone        UMETA(DisplayName = "None"),

    EquivalentPelvis      UMETA(DisplayName = "Pelvis"),
    EquivalentSpine       UMETA(DisplayName = "Spine"),
    EquivalentNeck        UMETA(DisplayName = "Neck"),

    EquivalentLThigh      UMETA(DisplayName = "Left Thigh"),
    EquivalentLCalf       UMETA(DisplayName = "Left Calf"),
    EquivalentLFoot       UMETA(DisplayName = "Left Foot"),
    EquivalentLFootBall   UMETA(DisplayName = "Left Foot Ball"),

    EquivalentLUpperArm   UMETA(DisplayName = "Left Upper Arm"),
    EquivalentLLowerArm   UMETA(DisplayName = "Left Lower Arm"),
    EquivalentLHand       UMETA(DisplayName = "Left Hand"),

    EquivalentRThigh      UMETA(DisplayName = "Right Thigh"),
    EquivalentRCalf       UMETA(DisplayName = "Right Calf"),
    EquivalentRFoot       UMETA(DisplayName = "Right Foot"),
    EquivalentRFootBall   UMETA(DisplayName = "Right Foot Ball"),

    EquivalentRUpperArm   UMETA(DisplayName = "Right Upper Arm"),
    EquivalentRLowerArm   UMETA(DisplayName = "Right Lower Arm"),
    EquivalentRHand       UMETA(DisplayName = "Right Hand"),
};

/**
 * 
 */
UCLASS(Blueprintable)
class RENDERSTREAM_API URenderStreamRemapAsset : public ULiveLinkRetargetAsset
{
    GENERATED_UCLASS_BODY()
    
    virtual ~URenderStreamRemapAsset() override {}

    //~ Begin UObject Interface
    virtual void BeginDestroy() override;
    //~ End UObject Interface

    //~ Begin ULiveLinkRetargetAsset interface
    //~ End ULiveLinkRetargetAsset interface

    UFUNCTION(BlueprintCallable, Category = "Live Link Remap")
    TEnumAsByte<RenderStreamBoneNameEquivalents> GetBoneNameEquivalent(const FName& SourceBoneName) const;

    /** Blueprint Implementable function for getting a remapped bone name from the original */
    UFUNCTION(BlueprintNativeEvent, Category = "Live Link Remap")
    FName GetRemappedBoneName(FName BoneName) const;

    FName GetMeshBoneName(const FName& SourceBoneName);
    bool IsRootBone(const FName& SourceBoneName);

protected:
    virtual RenderStreamBoneNameEquivalents GetBoneNameEquivalent_Internal(const FName& SourceBoneName) const;

private:

    void OnBlueprintClassCompiled(UBlueprint* TargetBlueprint);

    // Name mapping between source bone name and transformed bone name
    // (returned from GetRemappedBoneName)
    TMap<FName, FName> BoneNameMap;

#if WITH_EDITOR
    /** Blueprint.OnCompiled delegate handle */
    FDelegateHandle OnBlueprintCompiledDelegate;
#endif

};
