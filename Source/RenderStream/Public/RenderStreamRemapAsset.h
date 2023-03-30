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
    virtual void BuildPoseFromAnimationData(float DeltaTime, const FLiveLinkSkeletonStaticData* InSkeletonData, const FLiveLinkAnimationFrameData* InFrameData, FCompactPose& OutPose) override;
    virtual void BuildPoseAndCurveFromBaseData(float DeltaTime, const FLiveLinkBaseStaticData* InBaseStaticData, const FLiveLinkBaseFrameData* InBaseFrameData, FCompactPose& OutPose, FBlendedCurve& OutCurve) override;
    //~ End ULiveLinkRetargetAsset interface

    void SetInitialPose(const TArray<FTransform>& Pose) { InitialPose = Pose; }

    UFUNCTION(BlueprintCallable, Category = "Live Link Remap")
    TEnumAsByte<RenderStreamBoneNameEquivalents> GetBoneNameEquivalent(const FName& SourceBoneName) const;

    /** Blueprint Implementable function for getting a remapped bone name from the original */
    UFUNCTION(BlueprintNativeEvent, Category = "Live Link Remap")
    FName GetRemappedBoneName(FName BoneName) const;

    /** Blueprint Implementable function for getting a remapped curve name from the original */
    UFUNCTION(BlueprintNativeEvent, Category = "Live Link Remap")
    FName GetRemappedCurveName(FName CurveName) const;

    /** Blueprint Implementable function for remapping, adding or otherwise modifying the curve element data from Live Link. This is run after GetRemappedCurveName */
    UFUNCTION(BlueprintNativeEvent, Category = "Live Link Remap")
    void RemapCurveElements(UPARAM(ref)TMap<FName, float>& CurveItems) const;

protected:
    virtual RenderStreamBoneNameEquivalents GetBoneNameEquivalent_Internal(const FName& SourceBoneName) const;

private:

    void OnBlueprintClassCompiled(UBlueprint* TargetBlueprint);

    void InitialiseAnimationData(const FLiveLinkSkeletonStaticData* InSkeletonData, const FLiveLinkAnimationFrameData* InFrameData, const FCompactPose& OutPose);

    // Name mapping between source bone name and transformed bone name
    // (returned from GetRemappedBoneName)
    TMap<FName, FName> BoneNameMap;

    // Name mapping between source curve name and transformed curve name
    // (returned from GetRemappedCurveName)
    TMap<FName, FName> CurveNameMap;

#if WITH_EDITOR
    /** Blueprint.OnCompiled delegate handle */
    FDelegateHandle OnBlueprintCompiledDelegate;
#endif

    TArray<FTransform> InitialPose;
    TArray<FQuat> WorldInitialOrientationDifferences;
    TArray<FQuat> WorldInitialOrientationDifferences2;
    TArray<FQuat> LocalInitialOrientationDifferences;
    TArray<FQuat> InitialPoseOrientations;
    TArray<FCompactPoseBoneIndex> SourceToMeshIndex;
    
    bool Initialised;
};
