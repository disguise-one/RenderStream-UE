#pragma once

#include "Animation/AnimNodeBase.h"

#include "CoreMinimal.h"
#include "RenderStreamLink.h"

#include "Engine/SkeletalMesh.h"

#include <vector>

#include "AnimNode_RenderStreamSkeletonSource.generated.h"

class ILiveLinkClient;
class ASkeletalMeshActor;

UENUM(BlueprintType)
enum class ERenderStreamSkeletonLayout : uint8
{
    Default     UMETA(DisplayName = "Default"),
    Captury     UMETA(DisplayName = "Captury")
};

USTRUCT(BlueprintInternalUseOnly)
struct RENDERSTREAM_API FAnimNode_RenderStreamSkeletonSource : public FAnimNode_Base
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Input, DisplayName = "Base Pose (Optional)")
        FPoseLink BasePose;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = MapsAndSets)
    ERenderStreamSkeletonLayout SkeletonLayout = ERenderStreamSkeletonLayout::Default;

    UPROPERTY(EditAnywhere, Category = MapsAndSets)
    TMap<FName, FBoneReference> BoneNameMap;

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

    void OnLayoutChanged();

protected:

    void CacheSkeletonActors(const FName& ParamName);
    void AddIfCorrespondingSkeletonActor(AActor* SkeletonActor);
    void ApplyRootPose(const FName& ParamName);

    FName GetSkeletonParamName();

    void InitialiseAnimationData(const RenderStreamLink::FSkeletalLayout& Layout, const FCompactPose& OutPose);
    void BuildPoseFromAnimationData(const RenderStreamLink::FSkeletalPose& Pose, FCompactPose& OutPose);

    bool IsRootBone(int32 SourceIndex);

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

    // Hidden cache that remembers every bone mapping ever set
    UPROPERTY()
    TMap<FName, FName> MasterBoneCache;
};

