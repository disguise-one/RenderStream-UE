#pragma once

#include "Animation/AnimNodeBase.h"

#include "CoreMinimal.h"
#include "LiveLinkRetargetAsset.h"
#include "LiveLinkTypes.h"

#include "AnimNode_RenderStreamSkeletonSource.generated.h"

class ILiveLinkClient;

USTRUCT(BlueprintType)
struct RENDERSTREAM_API FAnimNode_RenderStreamSkeletonSource : public FAnimNode_Base
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Input)
        FPoseLink BasePose;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SourceData, meta = (PinShownByDefault))
        FLiveLinkSubjectName LiveLinkSubjectName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, NoClear, Category = Retarget, meta = (NeverAsPin))
        TSubclassOf<ULiveLinkRetargetAsset> RetargetAsset;

    UPROPERTY(transient)
        TObjectPtr<ULiveLinkRetargetAsset> CurrentRetargetAsset;

public:
    FAnimNode_RenderStreamSkeletonSource();

    virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
    virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
    virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
    virtual void Evaluate_AnyThread(FPoseContext& Output) override;
    virtual bool HasPreUpdate() const { return true; }
    virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;
    virtual void GatherDebugData(FNodeDebugData& DebugData) override;

    //bool Serialize(FArchive& Ar);

protected:
    virtual void OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance) override;

private:

    ILiveLinkClient* LiveLinkClient_AnyThread;

    // Delta time from update so that it can be passed to retargeter
    float CachedDeltaTime;

};

