#pragma once

#include "Containers/UnrealString.h"
#include "Styling/SlateColor.h"

#define RSSTATUS_RED FSlateColor(FColor( 1.0, 0.0, 0.0 ))
#define RSSTATUS_GREEN FSlateColor(FColor( 0.0, 1.0, 0.0 ))
#define RSSTATUS_ORANGE FSlateColor(FColor( 1.0, 0.5, 0.0 ))

class FRenderStreamStatus
{
public:
    FString outputText;
    FSlateColor outputColor;
    FString inputText;
    FSlateColor inputColor;
    bool changed = true;

    void InputOutput(const FString& topText, const FString& bottomText, const FSlateColor& color);  // Set single status spanning both lines
    void Output(const FString& text, const FSlateColor& color);  // Set output (top) status
    void Input(const FString& text, const FSlateColor& color);  // Set input (bottom) status
};

FRenderStreamStatus& RenderStreamStatus();
