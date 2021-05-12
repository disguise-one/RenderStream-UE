#include "RenderStreamChannelCacheAsset.h"

RenderStreamLink::RemoteParameterType RenderStreamParameterTypeToLink(RenderStreamParameterType type)
{
    switch (type)
    {
    case RenderStreamParameterType::Float:
        return RenderStreamLink::RS_PARAMETER_NUMBER;
    case RenderStreamParameterType::Image:
        return RenderStreamLink::RS_PARAMETER_IMAGE;
    case RenderStreamParameterType::Pose:
        return RenderStreamLink::RS_PARAMETER_POSE;
    case RenderStreamParameterType::Transform:
        return RenderStreamLink::RS_PARAMETER_TRANSFORM;
    case RenderStreamParameterType::Text:
        return RenderStreamLink::RS_PARAMETER_TEXT;
    default:
        check(false);
        return RenderStreamLink::RS_PARAMETER_NUMBER;
    }
}