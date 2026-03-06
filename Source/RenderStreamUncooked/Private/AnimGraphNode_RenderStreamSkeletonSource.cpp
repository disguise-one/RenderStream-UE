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

#if WITH_EDITOR
void UAnimGraphNode_RenderStreamSkeletonSource::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

    if (PropertyName == GET_MEMBER_NAME_CHECKED(FAnimNode_RenderStreamSkeletonSource, SkeletonLayout))
    {
        Node.OnLayoutChanged();
    }
}
#endif