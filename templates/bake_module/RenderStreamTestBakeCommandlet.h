#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RenderStreamTestBakeCommandlet.generated.h"

// Builds the fixed RenderStream test scene (cube + camera channel + exposed level-blueprint
// variables) and saves it, which triggers the RenderStream schema/channel-cache bake.
// Run headless with:  UnrealEditor-Cmd.exe <uproject> -run=RenderStreamTestBake -unattended -nullrhi
UCLASS()
class URenderStreamTestBakeCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    URenderStreamTestBakeCommandlet();
    virtual int32 Main(const FString& Params) override;
};