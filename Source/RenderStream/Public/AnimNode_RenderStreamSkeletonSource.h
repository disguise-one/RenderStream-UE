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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = MapsAndSets)
        TMap<FName, FName> BoneNameMap;
public:
    FAnimNode_RenderStreamSkeletonSource();
    ~FAnimNode_RenderStreamSkeletonSource();

    virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
    virtual void Evaluate_AnyThread(FPoseContext& Output) override;
    virtual bool HasPreUpdate() const { return true; }
    virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;
    virtual void GatherDebugData(FNodeDebugData& DebugData) override;

protected:

    void CacheSkeletonActors(const FName& ParamName);
    void AddIfCorrespondingSkeletonActor(ASkeletalMeshActor* SkeletalMeshActor);
    void ApplyRootPose(const FName& ParamName);

    FName GetSkeletonParamName();

    void InitialiseAnimationData(const RenderStreamLink::FSkeletalLayout& Layout, const FCompactPose& OutPose);
    void BuildPoseFromAnimationData(const RenderStreamLink::FSkeletalPose& Pose, FCompactPose& OutPose);

    static bool IsRootBone(const FName& SourceBoneName);

private:
    std::vector<TWeakObjectPtr<ASkeletalMeshActor>> SkeletonActors;
    bool SkeletonActorsCached;
    FDelegateHandle OnSkeletonSpawnedHandle;

    // Cached pose info
    TArray<FName> SourceBoneNames;
    TArray<int32> SourceParentIndices;
    TArray<FQuat> MeshToSourceSpaceRotations;
    TArray<FQuat> LocalInitialOrientationDifferences;
    TArray<FQuat> SourceInitialPoseRotations;
    TArray<FCompactPoseBoneIndex> SourceToMeshIndex;
    FTransform RootBoneTransform;
    bool PoseInitialised;
};

