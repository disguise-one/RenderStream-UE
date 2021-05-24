#include "RenderStreamChannelVisibility.h"

#include "Camera/CameraActor.h"

FChannelVisibilityEntry::FChannelVisibilityEntry()
    : Camera(nullptr)
    , Visible(true)
{}

FChannelVisibilityEntry::FChannelVisibilityEntry(const FChannelVisibilityEntry& Other)
    : Camera(Other.Camera)
    , Visible(Other.Visible)
{}

bool FChannelVisibilityEntry::operator==(const FChannelVisibilityEntry & Other) const
{
    return Equals(Other);
}

bool FChannelVisibilityEntry::Equals(const FChannelVisibilityEntry & Other) const
{
    return Other.Camera == Camera && Other.Visible == Visible;
}

URenderStreamChannelVisibility::URenderStreamChannelVisibility() {}
