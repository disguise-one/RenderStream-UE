#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"

#include "RenderStream.h"
#include "RenderStreamStatus.h"

#include "RenderStreamStatusWidget.generated.h"

/**
 * 
 */
UCLASS()
class URenderStreamStatusWidget : public UUserWidget
{

	GENERATED_BODY()

public:

	URenderStreamStatusWidget(const FObjectInitializer& ObjectInitializer);

	bool Initialize() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;


private:
	void updateStatus();

	UTextBlock* m_outputStatusText = nullptr;
	UTextBlock* m_inputStatusText = nullptr;
	UTexture2D* m_logoTex = nullptr;
	FRenderStreamModule* m_module = nullptr;

	// Formatting parameters
	static constexpr int fontSize = 12;
	static constexpr int logoSize = 50;
	static constexpr int paddingSize = 10;
	static constexpr float contentOpacity = 0.75;
	const FLinearColor backgroundColor = { 0, 0, 0, 0.5 };
	const FVector2D widgetPosition = { 10, 10 };

};
