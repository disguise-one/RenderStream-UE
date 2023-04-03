// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
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
class RENDERSTREAM_API URenderStreamRemapAsset : public UObject
{
    GENERATED_UCLASS_BODY()
    
    virtual void BeginDestroy() override;

    UFUNCTION(BlueprintCallable, Category = "RenderStream Remap")
    TEnumAsByte<RenderStreamBoneNameEquivalents> GetBoneNameEquivalent(const FName& SourceBoneName) const;

    /** Blueprint Implementable function for getting a remapped bone name from the original */
    UFUNCTION(BlueprintNativeEvent, Category = "RenderStream Remap")
    FName GetRemappedBoneName(FName BoneName) const;

    FName GetMeshBoneName(const FName& SourceBoneName);
    bool IsRootBone(const FName& SourceBoneName);

protected:
    virtual RenderStreamBoneNameEquivalents GetBoneNameEquivalent_Internal(const FName& SourceBoneName) const;

private:

    void OnBlueprintClassCompiled(UBlueprint* TargetBlueprint);

    TMap<FName, FName> BoneNameMap;

#if WITH_EDITOR
    /** Blueprint.OnCompiled delegate handle */
    FDelegateHandle OnBlueprintCompiledDelegate;
#endif

};
