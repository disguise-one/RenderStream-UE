#include "RenderStreamCustomization.h"
#include "PropertyEditing.h"
#include "RenderStreamChannelDefinition.h"
#include "RenderStreamEditorModule.h"
#include "RenderStreamSettings.h"
#include "Components/SceneCaptureComponent.h"
#include "Engine/Selection.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Input/SComboBox.h"
#include "Kismet/GameplayStatics.h"
#include "Windows/WindowsPlatformApplicationMisc.h"

#define LOCTEXT_NAMESPACE "RenderStreamEditor"

namespace
{
    inline static bool SortAlphabeticallyByLocalizedText(const FString& ip1, const FString& ip2)
    {
        FText LocalizedText1;
        FEngineShowFlags::FindShowFlagDisplayName(ip1, LocalizedText1);

        FText LocalizedText2;
        FEngineShowFlags::FindShowFlagDisplayName(ip2, LocalizedText2);

        return LocalizedText1.ToString() < LocalizedText2.ToString();
    }

    class FDefinitionCustomization final : public IDetailCustomization
    {
    public:
        // IDetailCustomization interface
        virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
        void CustomizeShowFlagSettings(IDetailCategoryBuilder& CategoryBuilder, TSharedPtr<IPropertyHandle> InShowFlagSettingsProperty);
        void CustomizeVisibility(IDetailLayoutBuilder& DetailBuilder, TSharedPtr<IPropertyHandle> Property);

    private:
        ECheckBoxState OnGetDisplayCheckState(FString ShowFlagName) const;
        void OnShowFlagCheckStateChanged(ECheckBoxState InNewRadioState, FString FlagName);

        TSharedPtr<IPropertyHandle> ShowFlagSettingsProperty;
    };

    void FDefinitionCustomization::CustomizeShowFlagSettings(IDetailCategoryBuilder& CategoryBuilder, TSharedPtr<IPropertyHandle> InShowFlagSettingsProperty)
    {
        ShowFlagSettingsProperty = InShowFlagSettingsProperty;
        check(ShowFlagSettingsProperty->IsValidHandle());
        ShowFlagSettingsProperty->MarkHiddenByCustomization();

        TArray<TSharedRef<IPropertyHandle>> SceneCaptureCategoryDefaultProperties;
        CategoryBuilder.GetDefaultProperties(SceneCaptureCategoryDefaultProperties);
        for (TSharedRef<IPropertyHandle> Handle : SceneCaptureCategoryDefaultProperties)
            if (Handle->GetProperty() != ShowFlagSettingsProperty->GetProperty())
                CategoryBuilder.AddProperty(Handle);

        TArray<FEngineShowFlags::EShowFlag> ShowFlagsToAllowForCaptures;

        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_Atmosphere);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_BSP);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_Decals);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_Fog);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_Landscape);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_Particles);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_SkeletalMeshes);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_StaticMeshes);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_Translucency);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_Lighting);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_DeferredLighting);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_InstancedStaticMeshes);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_InstancedFoliage);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_InstancedGrass);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_Paper2DSprites);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_TextRender);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_AmbientOcclusion);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_DynamicShadows);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_SkyLighting);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_VolumetricFog);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_AmbientCubemap);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_DistanceFieldAO);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_LightFunctions);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_LightShafts);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_PostProcessing);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_ReflectionEnvironment);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_ScreenSpaceReflections);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_TexturedLightProfiles);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_AntiAliasing);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_TemporalAA);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_MotionBlur);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_Bloom);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_EyeAdaptation);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_Game);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_ToneCurve);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_Tonemapper);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_Vignette);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_LensFlares); 
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_SceneColorFringe);
        ShowFlagsToAllowForCaptures.Add(FEngineShowFlags::EShowFlag::SF_ScreenSpaceAO);

        // Create array of flag name strings for each group
        TArray< TArray<FString> > ShowFlagsByGroup;
        for (int32 GroupIndex = 0; GroupIndex < SFG_Max; ++GroupIndex)
            ShowFlagsByGroup.Add(TArray<FString>());

        // Add the show flags we want to expose to their group's array
        for (FEngineShowFlags::EShowFlag AllowedFlag : ShowFlagsToAllowForCaptures)
        {
            FString FlagName;
            FlagName = FEngineShowFlags::FindNameByIndex(AllowedFlag);
            if (!FlagName.IsEmpty())
            {
                EShowFlagGroup Group = FEngineShowFlags::FindShowFlagGroup(*FlagName);
                ShowFlagsByGroup[Group].Add(FlagName);
            }
        }

        // Sort the flags in their respective group alphabetically
        for (TArray<FString>& ShowFlagGroup : ShowFlagsByGroup)
            ShowFlagGroup.Sort(SortAlphabeticallyByLocalizedText);

        // Add each group
        for (int32 GroupIndex = 0; GroupIndex < SFG_Max; ++GroupIndex)
        {
            // Don't add a group if there are no flags allowed for it
            if (ShowFlagsByGroup[GroupIndex].Num() >= 1)
            {
                FText GroupName;
                FText GroupTooltip;
                switch (GroupIndex)
                {
                case SFG_Normal:
                    GroupName = LOCTEXT("CommonShowFlagHeader", "General Show Flags");
                    break;
                case SFG_Advanced:
                    GroupName = LOCTEXT("AdvancedShowFlagsMenu", "Advanced Show Flags");
                    break;
                case SFG_PostProcess:
                    GroupName = LOCTEXT("PostProcessShowFlagsMenu", "Post Processing Show Flags");
                    break;
                case SFG_Developer:
                    GroupName = LOCTEXT("DeveloperShowFlagsMenu", "Developer Show Flags");
                    break;
                case SFG_Visualize:
                    GroupName = LOCTEXT("VisualizeShowFlagsMenu", "Visualize Show Flags");
                    break;
                case SFG_LightTypes:
                    GroupName = LOCTEXT("LightTypesShowFlagsMenu", "Light Types Show Flags");
                    break;
                case SFG_LightingComponents:
                    GroupName = LOCTEXT("LightingComponentsShowFlagsMenu", "Lighting Components Show Flags");
                    break;
                case SFG_LightingFeatures:
                    GroupName = LOCTEXT("LightingFeaturesShowFlagsMenu", "Lighting Features Show Flags");
                    break;
                case SFG_CollisionModes:
                    GroupName = LOCTEXT("CollisionModesShowFlagsMenu", "Collision Modes Show Flags");
                    break;
                case SFG_Hidden:
                case SFG_Transient:
                    GroupName = LOCTEXT("HiddenShowFlagsMenu", "Hidden Show Flags");
                    break;
                default:
                    // Should not get here unless a new group is added without being updated here
                    GroupName = LOCTEXT("MiscFlagsMenu", "Misc Show Flags");
                    break;
                }

                const FName GroupFName = FName(*(GroupName.ToString()));
                IDetailGroup& Group = CategoryBuilder.AddGroup(GroupFName, GroupName, true);

                // Add each show flag for this group
                for (FString& FlagName : ShowFlagsByGroup[GroupIndex])
                {
                    bool bFlagHidden = false;
                    FText LocalizedText;
                    FEngineShowFlags::FindShowFlagDisplayName(FlagName, LocalizedText);

                    Group.AddWidgetRow()
                        .IsEnabled(true)
                        .NameContent()
                        [
                            SNew(STextBlock)
                            .Text(LocalizedText)
                        ]
                    .ValueContent()
                        [
                            SNew(SCheckBox)
                            .OnCheckStateChanged(this, &FDefinitionCustomization::OnShowFlagCheckStateChanged, FlagName)
                        .IsChecked(this, &FDefinitionCustomization::OnGetDisplayCheckState, FlagName)
                        ]
                    .FilterString(LocalizedText);
                }
            }
        }
    }

    void FDefinitionCustomization::CustomizeVisibility(IDetailLayoutBuilder& DetailBuilder, TSharedPtr<IPropertyHandle> Property)
    {
        check(Property->IsValidHandle());

        IDetailPropertyRow* IRow = DetailBuilder.EditDefaultProperty(Property);
        TSharedPtr<SWidget> Name;
        TSharedPtr<SWidget> Value;
        FDetailWidgetRow Row;
        IRow->GetDefaultWidgets(Name, Value, Row);
        // Sigh, why can't I just modify the row instead of recreating it...
        FDetailWidgetRow& NewRow = IRow->CustomWidget(true);
        NewRow.CopyMenuAction = Row.CopyMenuAction;
        NewRow.FilterTextString = Row.FilterTextString;
        NewRow.IsEnabledAttr = Row.IsEnabledAttr;
        NewRow.PasteMenuAction = Row.PasteMenuAction;
        NewRow.PropertyHandles = Row.PropertyHandles;
        NewRow.RowTagName = Row.RowTagName;
        NewRow.VisibilityAttr = Row.VisibilityAttr;

        NewRow.ValueWidget.Widget = Row.ValueWidget.Widget;
        NewRow.ValueWidget.HorizontalAlignment = Row.ValueWidget.HorizontalAlignment;
        NewRow.ValueWidget.VerticalAlignment = Row.ValueWidget.VerticalAlignment;
        NewRow.ValueWidget.MaxWidth = Row.ValueWidget.MaxWidth;
        NewRow.ValueWidget.MinWidth = Row.ValueWidget.MinWidth;

        NewRow.NameWidget.Widget = Row.NameWidget.Widget;
        NewRow.NameWidget.HorizontalAlignment = Row.NameWidget.HorizontalAlignment;
        NewRow.NameWidget.VerticalAlignment = Row.NameWidget.VerticalAlignment;
        NewRow.NameWidget.MaxWidth = Row.NameWidget.MaxWidth;
        NewRow.NameWidget.MinWidth = Row.NameWidget.MinWidth;

        NewRow.AddCustomContextMenuAction(
            FUIAction(
                FExecuteAction::CreateLambda([Property]()
                {
                    TSet<TSoftObjectPtr<AActor>> ActorsInSelection;
                    USelection* Selection = GEditor->GetSelectedActors();
                    for (int i = 0; i < Selection->Num(); ++i)
                    {
                        AActor* Actor = Cast<AActor>(Selection->GetSelectedObject(i));
                        if (Actor)
                            ActorsInSelection.Add(Actor);
                    }

                    TArray<void*> RawData;
                    Property->AccessRawData(RawData);
                    for (int32 ObjectIdx = 0; ObjectIdx < RawData.Num(); ++ObjectIdx)
                    {
                        void* Data = RawData[ObjectIdx];
                        check(Data);
                        TSet<TSoftObjectPtr<AActor>>& Actors = *static_cast<TSet<TSoftObjectPtr<AActor>>*>(Data);
                        Actors = Actors.Union(ActorsInSelection);
                    }
                })
            ), FText::FromString("Add from Selection")
        );
    }

    void FDefinitionCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
    {
        CustomizeShowFlagSettings(
            DetailBuilder.EditCategory("SceneCapture"),
            DetailBuilder.GetProperty("ShowFlagSettings", URenderStreamChannelDefinition::StaticClass())
        );

        CustomizeVisibility(
            DetailBuilder,
            DetailBuilder.GetProperty("Visible", URenderStreamChannelDefinition::StaticClass())
        );

        CustomizeVisibility(
            DetailBuilder,
            DetailBuilder.GetProperty("Hidden", URenderStreamChannelDefinition::StaticClass())
        );
    }

    static bool FindShowFlagSetting(
        TArray<FEngineShowFlagsSetting>& ShowFlagSettings,
        FString FlagName,
        FEngineShowFlagsSetting** ShowFlagSettingOut)
    {
        bool HasSetting = false;
        for (int32 ShowFlagSettingsIndex = 0; ShowFlagSettingsIndex < ShowFlagSettings.Num(); ++ShowFlagSettingsIndex)
        {
            if (ShowFlagSettings[ShowFlagSettingsIndex].ShowFlagName.Equals(FlagName))
            {
                HasSetting = true;
                *ShowFlagSettingOut = &(ShowFlagSettings[ShowFlagSettingsIndex]);
                break;
            }
        }
        return HasSetting;
    }

    ECheckBoxState FDefinitionCustomization::OnGetDisplayCheckState(FString ShowFlagName) const
    {
        TArray<const void*> RawData;
        ShowFlagSettingsProperty->AccessRawData(RawData);

        TArray<UObject*> OuterObjects;
        ShowFlagSettingsProperty->GetOuterObjects(OuterObjects);

        ECheckBoxState ReturnState = ECheckBoxState::Unchecked;
        bool bReturnStateSet = false;
        for (int32 ObjectIdx = 0; ObjectIdx < RawData.Num(); ++ObjectIdx)
        {
            const void* Data = RawData[ObjectIdx];
            check(Data);

            const TArray<FEngineShowFlagsSetting>& ShowFlagSettings = *static_cast<const TArray<FEngineShowFlagsSetting>*>(Data);
            const FEngineShowFlagsSetting* Setting = ShowFlagSettings.FindByPredicate([&ShowFlagName](const FEngineShowFlagsSetting& S) { return S.ShowFlagName == ShowFlagName; });
            ECheckBoxState ThisObjectState = ECheckBoxState::Unchecked;
            if (Setting)
            {
                ThisObjectState = Setting->Enabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
            }
            else
            {
                const UObject* SceneComp = OuterObjects[ObjectIdx];
                const URenderStreamChannelDefinition* SceneCompArchetype = SceneComp ? Cast<URenderStreamChannelDefinition>(SceneComp->GetArchetype()) : nullptr;
                const int32 SettingIndex = SceneCompArchetype ? SceneCompArchetype->ShowFlags.FindIndexByName(*ShowFlagName) : INDEX_NONE;
                if (SettingIndex != INDEX_NONE)
                {
                    ThisObjectState = SceneCompArchetype->ShowFlags.GetSingleFlag(SettingIndex) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                }
            }

            if (bReturnStateSet)
            {
                if (ThisObjectState != ReturnState)
                {
                    ReturnState = ECheckBoxState::Undetermined;
                    break;
                }
            }
            else
            {
                ReturnState = ThisObjectState;
                bReturnStateSet = true;
            }
        }

        return ReturnState;
    }

    void FDefinitionCustomization::OnShowFlagCheckStateChanged(ECheckBoxState InNewRadioState, FString FlagName)
    {
        if (InNewRadioState == ECheckBoxState::Undetermined)
        {
            return;
        }

        FScopedTransaction Transaction(FText::FromString("Change show flag of channel definition"));

        TArray<void*> RawData;
        ShowFlagSettingsProperty->AccessRawData(RawData);

        TArray<UObject*> OuterObjects;
        ShowFlagSettingsProperty->GetOuterObjects(OuterObjects);

        const bool bNewEnabledState = (InNewRadioState == ECheckBoxState::Checked) ? true : false;
        for (int32 ObjectIdx = 0; ObjectIdx < RawData.Num(); ++ObjectIdx)
        {
            void* Data = RawData[ObjectIdx];
            check(Data);

            UObject* Definition = OuterObjects[ObjectIdx];
            URenderStreamChannelDefinition* DefinitionArchetype = Definition ? Cast<URenderStreamChannelDefinition>(Definition->GetArchetype()) : nullptr;
            if (Definition)
                Definition->Modify();

            TArray<FEngineShowFlagsSetting>& ShowFlagSettings = *static_cast<TArray<FEngineShowFlagsSetting>*>(Data);

            FEngineShowFlagsSetting* Setting = ShowFlagSettings.FindByPredicate([&FlagName](const FEngineShowFlagsSetting& S) { return S.ShowFlagName == FlagName; });
            if (Setting)
            {
                // If the setting exists already, update it
                Setting->Enabled = bNewEnabledState;
            }
            else
            {
                // Otherwise create a new setting
                FEngineShowFlagsSetting NewFlagSetting;
                NewFlagSetting.ShowFlagName = FlagName;
                NewFlagSetting.Enabled = bNewEnabledState;
                ShowFlagSettings.Add(NewFlagSetting);
            }
        }
    }

    class FSettingsCustomization final : public IDetailCustomization
    {
    public:
        // IDetailCustomization interface
        virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
    };

    class SSceneSelectionCombo : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SSceneSelectionCombo) : _SceneSelection()
        {}
        SLATE_ARGUMENT(TArray<TWeakObjectPtr<UObject>>, SceneSelection)
        SLATE_END_ARGS()

        struct FSceneSelectionItem
        {
            FString Name;
            ERenderStreamSceneSelector Value;
        };

        typedef TSharedPtr<FSceneSelectionItem> FSceneSelectionItemType;

        void Construct(const FArguments& InArgs)
        {
            SceneSelection = InArgs._SceneSelection;

            auto CastEntry = Cast<URenderStreamSettings>(SceneSelection[0]);
            UEnum* Enum = StaticEnum<ERenderStreamSceneSelector>();
            // (Enum->NumEnums() - 1) because we don't want the MAX entry that is automatically added.
            for (int I = 0; I < (Enum->NumEnums() - 1); ++I)
            {
                FSceneSelectionItemType Item = MakeShared<FSceneSelectionItem>();
                Item->Name = Enum->GetNameStringByIndex(I);
                Item->Value = static_cast<ERenderStreamSceneSelector>(Enum->GetValueByIndex(I));
                Options.Add(Item);
                if (Item->Value == CastEntry->SceneSelector)
                    CurrentItem = Item;
            }

            if (!CurrentItem.IsValid())
                CurrentItem = Options[0];

            ChildSlot
                [
                    SNew(SComboBox<FSceneSelectionItemType>)
                    .OptionsSource(&Options)
                    .OnSelectionChanged(this, &SSceneSelectionCombo::OnSelectionChanged)
                    .OnGenerateWidget(this, &SSceneSelectionCombo::MakeWidgetForOption)
                    .InitiallySelectedItem(CurrentItem)
                [
                    SNew(STextBlock)
                    .Text(this, &SSceneSelectionCombo::GetCurrentItemLabel)
                ]
            ];
        }

        TSharedRef<SWidget> MakeWidgetForOption(FSceneSelectionItemType InOption)
        {
            return SNew(STextBlock).Text(FText::FromString(*InOption->Name));
        }

        void OnSelectionChanged(FSceneSelectionItemType NewValue, ESelectInfo::Type)
        {
            {
                FScopedTransaction Transaction(
                    FText::FromString("Change scene selection")
                );

                CurrentItem = NewValue;
                for (auto Entry : SceneSelection)
                {
                    auto CastEntry = Cast<URenderStreamSettings>(Entry);
                    CastEntry->Modify();
                    CastEntry->SceneSelector = NewValue->Value;
                    CastEntry->SaveConfig(CPF_Config, *CastEntry->GetDefaultConfigFilename());
                }
            }

            FRenderStreamEditorModule* EditorModule = FModuleManager::GetModulePtr<FRenderStreamEditorModule>("RenderStreamEditorModule");
            EditorModule->GenerateAssetMetadata();
        }

        FText GetCurrentItemLabel() const
        {
            if (CurrentItem.IsValid())
            {
                return FText::FromString(*CurrentItem->Name);
            }

            return LOCTEXT("InvalidComboEntryText", "<<Invalid option>>");
        }

        FSceneSelectionItemType CurrentItem;
        TArray<FSceneSelectionItemType> Options;
        TArray<TWeakObjectPtr<UObject>> SceneSelection;
    };

    void FSettingsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
    {
        const TSharedRef<IPropertyHandle> Property = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(URenderStreamSettings, SceneSelector));
        if (Property->IsValidHandle())
        {
            TArray<TWeakObjectPtr<UObject>> Objects;
            DetailBuilder.GetObjectsBeingCustomized(Objects);
            IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("Scene Selection");

            Property->MarkHiddenByCustomization();
            Property->MarkResetToDefaultCustomized();
            FDetailWidgetRow& SceneSelectionRow = Category.AddCustomRow(FText::FromString("Scene Selection"));

            SceneSelectionRow
                .NameContent()
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("Scene Selection", "Scene Selection"))
                    .Font(IDetailLayoutBuilder::GetDetailFont())
                ]
            .ValueContent()
                .HAlign(HAlign_Fill)
                [
                    SNew(SSceneSelectionCombo)
                    .SceneSelection(Objects)
                ];
        }
    }
}

TSharedRef<IDetailCustomization> MakeDefinitionCustomizationInstance()
{
    return MakeShareable(new FDefinitionCustomization);
}

TSharedRef<IDetailCustomization> MakeSettingsCustomizationInstance()
{
    return MakeShareable(new FSettingsCustomization);
}

#undef LOCTEXT_NAMESPACE
