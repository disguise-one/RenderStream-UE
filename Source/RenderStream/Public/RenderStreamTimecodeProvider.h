#pragma once

#include "Engine/TimecodeProvider.h"

#include "RenderStreamTimecodeProvider.generated.h"

/**
 * Read timecode from the RenderStream connection.
 */
UCLASS(EditInlineNew, Blueprintable)
class RENDERSTREAM_API URenderStreamTimecodeProvider : public UTimecodeProvider
{
	GENERATED_UCLASS_BODY()

public:
	//~ UTimecodeProvider interface
	virtual FQualifiedFrameTime GetQualifiedFrameTime() const override;
    virtual ETimecodeProviderSynchronizationState GetSynchronizationState() const override;

	/** This Provider became the Engine's Provider. */
	bool Initialize(class UEngine* InEngine) override { return true; };

	/** This Provider stopped being the Engine's Provider. */
    void Shutdown(class UEngine* InEngine) override {};

private:
	mutable FQualifiedFrameTime LastTime;
};