#include "RenderStreamStatusWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/HorizontalBox.h"
#include "Components/VerticalBox.h"
#include "Components/Border.h"
#include "Components/Spacer.h"
#include "Components/Image.h"
#include "Blueprint/WidgetTree.h"
#include "Interfaces/IPluginManager.h"

URenderStreamStatusWidget::URenderStreamStatusWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Get logo texture
	auto plugin = IPluginManager::Get().FindPlugin(TEXT(RS_PLUGIN_NAME));
	if (plugin)
	{
		FString assetDir = plugin->GetMountedAssetPath();
		ConstructorHelpers::FObjectFinder<UTexture2D> finder(*(assetDir + "Icon128"));
		m_logoTex = finder.Object;
	}
}


bool URenderStreamStatusWidget::Initialize()
{
	
	bool initStatus = Super::Initialize();

	if (WidgetTree)
	{
		// Create border
		UBorder* border = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		if (border)
		{
			border->SetBrushColor(backgroundColor);
			border->SetPadding(paddingSize);
			border->SetRenderOpacity(contentOpacity);

			// Create horizontal box
			UHorizontalBox* horizBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			if (horizBox)
			{
				// Create d3 logo
				UImage* logo = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
				if (logo && m_logoTex)
				{
					logo->SetBrushFromTexture(m_logoTex);
					logo->SetDesiredSizeOverride({ float(logoSize), float(logoSize) });
					horizBox->AddChild(logo);
				}

				// Create spacer
				USpacer* spacer = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
				if (spacer)
				{
					spacer->SetSize({ float(paddingSize), 1.f });
					horizBox->AddChild(spacer);
				}

				// Create vertical box
				UVerticalBox* vertBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
				if (vertBox)
				{
					// Create title text
					UTextBlock* titleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
					if (titleText)
					{
						titleText->SetText(FText::FromString("RenderStream Status"));
						titleText->SetColorAndOpacity(FSlateColor(FColor(1.0, 1.0, 1.0)));
						titleText->Font.Size = fontSize;
						vertBox->AddChild(titleText);
					}

					// Create output status text
					m_outputStatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
					m_outputStatusText->Font.Size = fontSize;
					if (m_outputStatusText)
						vertBox->AddChild(m_outputStatusText);

					// Create input status text
					m_inputStatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
					m_inputStatusText->Font.Size = fontSize;
					if (m_inputStatusText)
						vertBox->AddChild(m_inputStatusText);

					horizBox->AddChild(vertBox);
				}
				border->AddChild(horizBox);
			}
			WidgetTree->Modify();
			WidgetTree->RootWidget = border;
		}

		// Set widget position
		SetPositionInViewport(widgetPosition);

		// Set initial status
		updateStatus();
	}

	return initStatus;

}

void URenderStreamStatusWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	updateStatus();
}

void URenderStreamStatusWidget::updateStatus()
{
	// Set status text
	FRenderStreamStatus& status = RenderStreamStatus();
	if (status.changed)
	{
		m_outputStatusText->SetText(FText::FromString(status.outputText));
		m_outputStatusText->SetColorAndOpacity(status.outputColor);
		m_inputStatusText->SetText(FText::FromString(status.inputText));
		m_inputStatusText->SetColorAndOpacity(status.inputColor);
		status.changed = false;
	}
}
