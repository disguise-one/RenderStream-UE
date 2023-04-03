#pragma once

#include "AnimGraphNode_Base.h"
#include "AnimNode_RenderStreamSkeletonSource.h"
#include "AnimGraphNode_RenderStreamSkeletonSource.generated.h"

UCLASS()
class RENDERSTREAM_API UAnimGraphNode_RenderStreamSkeletonSource : public UAnimGraphNode_Base
{
    GENERATED_BODY()

public:

    virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
    virtual FText GetTooltipText() const override;
    virtual FText GetMenuCategory() const;

public:

    UPROPERTY(EditAnywhere, Category = Settings)
        FAnimNode_RenderStreamSkeletonSource Node;

};