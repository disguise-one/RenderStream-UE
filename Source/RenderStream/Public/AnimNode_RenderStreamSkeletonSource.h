#pragma once

#include "Animation/AnimNodeBase.h"

#include "CoreMinimal.h"
#include "RenderStreamLink.h"

#include "AnimNode_RenderStreamSkeletonSource.generated.h"

class ILiveLinkClient;
class ASkeletalMeshActor;

USTRUCT(BlueprintInternalUseOnly)
struct RENDERSTREAM_API FAnimNode_RenderStreamSkeletonSource : public FAnimNode_Base
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Input, DisplayName = "Base Pose (Optional)")
        FPoseLink BasePose;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = MapsAndSets)
        TMap<FName, FName> BoneNameMap;

    // When ticked, the root offsets applied to the actor are scaled by the actor's scale
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
        bool ScaleRootOffsets;
public:
    FAnimNode_RenderStreamSkeletonSource();
    ~FAnimNode_RenderStreamSkeletonSource();

    virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
    virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;

    virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
    virtual void Evaluate_AnyThread(FPoseContext& Output) override;
    virtual bool HasPreUpdate() const { return true; }
    virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;
    virtual void GatherDebugData(FNodeDebugData& DebugData) override;

protected:

    void CacheSkeletonActors(const FName& ParamName);
    void AddIfCorrespondingSkeletonActor(AActor* SkeletonActor);
    void ApplyRootPose(const FName& ParamName);

    FName GetSkeletonParamName();

    void InitialiseAnimationData(const RenderStreamLink::FSkeletalLayout& Layout, const FCompactPose& OutPose);
    void BuildPoseFromAnimationData(const RenderStreamLink::FSkeletalPose& Pose, FCompactPose& OutPose);

    static bool IsRootBone(const FName& SourceBoneName);

private:
    std::vector<TWeakObjectPtr<AActor>> SkeletonActors;
    bool SkeletonActorsCached;
    FDelegateHandle OnActorSpawnedHandle;

    // Cached pose info
    TArray<FName> SourceBoneNames;
    TArray<int32> SourceParentIndices;
    TArray<FTransform> MeshToSourceSpaceTransforms;
    TArray<FQuat> LocalInitialOrientationDifferences;
    TArray<FQuat> SourceInitialPoseRotations;
    TArray<FCompactPoseBoneIndex> SourceToMeshIndex;
    FTransform RootBoneTransform;
    int32 MeshBoneCount;
    bool PoseInitialised;
};

