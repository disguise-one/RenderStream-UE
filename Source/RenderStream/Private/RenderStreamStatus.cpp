#include "RenderStreamStatus.h"

void FRenderStreamStatus::InputOutput(const FString& topText, const FString& bottomText, const FSlateColor& color)
{
    outputText = topText;
    inputText = bottomText;
    outputColor = color;
    inputColor = color;
    changed = true;
}

void FRenderStreamStatus::Output(const FString& text, const FSlateColor& color)
{
    outputText = text;
    outputColor = color;
    changed = true;
}

void FRenderStreamStatus::Input(const FString& text, const FSlateColor& color)
{
    inputText = text;
    inputColor = color;
    changed = true;
}

FRenderStreamStatus& RenderStreamStatus()
{
    static FRenderStreamStatus status;
    return status;
}
