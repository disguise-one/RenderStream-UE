#include "AnimGraphNode_RenderStreamSkeletonSource.h"

#define LOCTEXT_NAMESPACE "RenderStream"

FText UAnimGraphNode_RenderStreamSkeletonSource::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
    return LOCTEXT("NodeTitle", "RenderStream Skeleton Pose");
}

FText UAnimGraphNode_RenderStreamSkeletonSource::GetTooltipText() const
{
    return LOCTEXT("NodeTooltip", "Retrieves the current skeleton pose supplied by disguise Renderstream");
}

FText UAnimGraphNode_RenderStreamSkeletonSource::GetMenuCategory() const
{
    return LOCTEXT("NodeCategory", "RenderStream");
}