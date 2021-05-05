#include "RenderStreamCustomization.h"
#include "RenderStreamChannelVisibility.h" // The class we're customizing
#include "PropertyEditing.h"
#include "RenderStreamEditorModule.h"
#include "RenderStreamSettings.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Input/SComboBox.h"
#include "Kismet/GameplayStatics.h"

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

    private:
        ECheckBoxState OnGetDisplayCheckState(FString ShowFlagName) const;
        void OnShowFlagCheckStateChanged(ECheckBoxState InNewRadioState, FString FlagName);

        TSharedPtr<IPropertyHandle> ShowFlagSettingsProperty;
    };

    void FDefinitionCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
    {
        IDetailCategoryBuilder& SceneCaptureCategoryBuilder = DetailBuilder.EditCategory("SceneCapture");

        ShowFlagSettingsProperty = DetailBuilder.GetProperty("ShowFlagSettings", URenderStreamChannelDefinition::StaticClass());
        check(ShowFlagSettingsProperty->IsValidHandle());
        ShowFlagSettingsProperty->MarkHiddenByCustomization();

        TArray<TSharedRef<IPropertyHandle>> SceneCaptureCategoryDefaultProperties;
        SceneCaptureCategoryBuilder.GetDefaultProperties(SceneCaptureCategoryDefaultProperties);
        for (TSharedRef<IPropertyHandle> Handle : SceneCaptureCategoryDefaultProperties)
        {
            if (Handle->GetProperty() != ShowFlagSettingsProperty->GetProperty())
            {
                SceneCaptureCategoryBuilder.AddProperty(Handle);
            }
        }

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

        // Create array of flag name strings for each group
        TArray< TArray<FString> > ShowFlagsByGroup;
        for (int32 GroupIndex = 0; GroupIndex < SFG_Max; ++GroupIndex)
        {
            ShowFlagsByGroup.Add(TArray<FString>());
        }

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
        {
            ShowFlagGroup.Sort(SortAlphabeticallyByLocalizedText);
        }

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
                IDetailGroup& Group = SceneCaptureCategoryBuilder.AddGroup(GroupFName, GroupName, true);

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

            const TArray<FEngineShowFlagsSetting>& ShowFlagSettings = *reinterpret_cast<const TArray<FEngineShowFlagsSetting>*>(Data);
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

            const int32 SettingIndex = DefinitionArchetype ? DefinitionArchetype->ShowFlags.FindIndexByName(*FlagName) : INDEX_NONE;
            const bool bDefaultValue = (SettingIndex != INDEX_NONE) ? DefinitionArchetype->ShowFlags.GetSingleFlag(SettingIndex) : false;

            TArray<FEngineShowFlagsSetting>& ShowFlagSettings = *static_cast<TArray<FEngineShowFlagsSetting>*>(Data);
            if (bNewEnabledState == bDefaultValue)
            {
                // Just remove settings that are the same as defaults. This lets the flags return to their default state
                ShowFlagSettings.RemoveAll([&FlagName](const FEngineShowFlagsSetting& Setting) { return Setting.ShowFlagName == FlagName; });
            }
            else
            {
                FEngineShowFlagsSetting* Setting = ShowFlagSettings.FindByPredicate([&FlagName](const FEngineShowFlagsSetting& S) { return S.ShowFlagName == FlagName; });
                if (Setting)
                {
                    // If the setting exists already for some reason, update it
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
    }

    class SVisibilityCombo final : public SCompoundWidget
    {
    public:
        enum class EOptions
        {
            Default,
            Visible,
            Hidden,
            Multiple
        };

        SLATE_BEGIN_ARGS(SVisibilityCombo): _Property(), _ChannelVisibility(), _CameraActor(nullptr), _Override(nullptr), _YOffset(0)
        {}
        SLATE_ARGUMENT(TSharedPtr<IPropertyHandle>, Property)
        SLATE_ARGUMENT(TArray<TWeakObjectPtr<UObject>>, ChannelVisibility)
        SLATE_ARGUMENT(ACameraActor*, CameraActor)
        SLATE_ARGUMENT(TSharedPtr<SVerticalBox>, Override)
        SLATE_ARGUMENT(int, YOffset)
        SLATE_END_ARGS()

        typedef EOptions FComboItemType;

        void Construct(const FArguments& InArgs)
        {
            Property = InArgs._Property;
            ChannelVisibility = InArgs._ChannelVisibility;
            CameraActor = InArgs._CameraActor;

            CurrentItem = EOptions::Multiple;
            for (auto Entry : ChannelVisibility)
            {
                FChannelVisibilityEntry* Value = Cast<URenderStreamChannelVisibility>(Entry)->FindEntry(CameraActor);
                FComboItemType Next;
                if (Value)
                    Next = Value->Visible ? EOptions::Visible : EOptions::Hidden;
                else
                    Next = EOptions::Default;

                if (CurrentItem == EOptions::Multiple)
                    CurrentItem = Next;

                if (CurrentItem != Next)
                {
                    CurrentItem = EOptions::Multiple;
                    break;
                }
            }

            TSharedPtr<SHorizontalBox> Data;
            ChildSlot[
                SAssignNew(Data, SHorizontalBox)
            ];

            Data->AddSlot()
                .FillWidth(2)
                .Padding(10, 5, 0, 5)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(CameraActor->GetName()))
                    .Font(IDetailLayoutBuilder::GetDetailFont())
                ];

            Data->AddSlot()
                .HAlign(HAlign_Right)
                .VAlign(VAlign_Center)
                .AutoWidth()
                .Padding(0, 0, 20, 0)
                .MaxWidth(16)
                [
                    SAssignNew(VisibilityIcon, SImage)
                        .Image_Raw(this, &SVisibilityCombo::GetVisiblityIcon)
                ];

            TSharedPtr<SHorizontalBox> Overrides;
            InArgs._Override->AddSlot()
                .VAlign(VAlign_Center)
                .HAlign(HAlign_Fill)
                [
                    SAssignNew(Overrides, SHorizontalBox)
                ];

            Overrides->AddSlot()
                .VAlign(VAlign_Center)
                .HAlign(HAlign_Center)
                .Padding(10, 5)
                [
                    SAssignNew(VisibleOverride, SCheckBox)
                    .OnCheckStateChanged_Lambda([this](const ECheckBoxState State) { this->OnCheckStateChanged(EOptions::Visible, State); })
                    .IsChecked_Lambda([this]() -> ECheckBoxState { return IsChecked(EOptions::Visible); })
                ];

            Overrides->AddSlot()
                .VAlign(VAlign_Center)
                .HAlign(HAlign_Center)
                .Padding(10, 5)
                [
                    SAssignNew(HiddenOverride, SCheckBox)
                    .OnCheckStateChanged_Lambda([this](const ECheckBoxState State) { this->OnCheckStateChanged(EOptions::Hidden, State); })
                    .IsChecked_Lambda([this]() -> ECheckBoxState { return IsChecked(EOptions::Hidden); })
                ];
        }

        const FSlateBrush* GetVisiblityIcon() const
        {
            if (CurrentItem == EOptions::Visible)
                return IconVisible;
            if (CurrentItem == EOptions::Hidden)
                return IconHidden;

            const URenderStreamChannelDefinition* Component = CameraActor->FindComponentByClass<URenderStreamChannelDefinition>();
            return Component->DefaultVisibility == EVisibilty::Visible ? IconVisible : IconHidden;
        }

        ECheckBoxState IsChecked(const EOptions Option) const
        {
            if (CurrentItem == Option)
                return ECheckBoxState::Checked;

            if (CurrentItem == EOptions::Multiple)
                return ECheckBoxState::Undetermined;

            return ECheckBoxState::Unchecked;
        }

        void OnCheckStateChanged(const EOptions Option, const ECheckBoxState State)
        {
            FScopedTransaction Transaction(
                FText::Format(
                    FText::FromString("Change visibility of object on channel {0}"),
                    FText::FromString(CameraActor->GetName())
                )
            );

            if (State == ECheckBoxState::Checked)
            {
                CurrentItem = Option;
                const bool Visible = Option == EOptions::Visible;
                const TSharedPtr<SCheckBox>& CheckBox = Visible ? HiddenOverride : VisibleOverride;
                CheckBox->SetIsChecked(false);
                for (auto Entry : ChannelVisibility)
                {
                    URenderStreamChannelVisibility* CastEntry = Cast<URenderStreamChannelVisibility>(Entry);
                    CastEntry->Modify();
                    CastEntry->FindOrAddEntry(CameraActor).Visible = Visible;
                }
            }
            else
            {
                CurrentItem = EOptions::Default;
                for (auto Entry : ChannelVisibility)
                {
                    auto CastEntry = Cast<URenderStreamChannelVisibility>(Entry);
                    CastEntry->Modify();

                    // Unreal has a check for references to elements inside the container so we need to create a copy here.
                    FChannelVisibilityEntry Value = CastEntry->FindOrAddEntry(CameraActor);
                    CastEntry->Entries.Remove(Value);
                }
            }

            VisibilityIcon->Invalidate(EInvalidateWidgetReason::Paint);
        }

        const FSlateBrush* IconVisible = FEditorStyle::GetBrush("Level.VisibleIcon16x");
        const FSlateBrush* IconHidden = FEditorStyle::GetBrush("Level.NotVisibleIcon16x");
        const FSlateBrush* IconUndetermined = FEditorStyle::GetBrush("NoBrush");

        TSharedPtr<IPropertyHandle> Property;
        FComboItemType CurrentItem;
        TSharedPtr<SImage> VisibilityIcon;
        TSharedPtr<SCheckBox> VisibleOverride;
        TSharedPtr<SCheckBox> HiddenOverride;

        ACameraActor* CameraActor;
        TArray<TWeakObjectPtr<UObject>> ChannelVisibility;
    };

    class FVisibilityCustomization final : public IDetailCustomization
    {
    public:
        // IDetailCustomization interface
        virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

    private:
        void CreateVisibilityCustomization(TArray<TWeakObjectPtr<UObject>> Objects, IDetailCategoryBuilder& Category, TSharedRef<IPropertyHandle> InVisibilityHandle) const;
    };

    void FVisibilityCustomization::CreateVisibilityCustomization(TArray<TWeakObjectPtr<UObject>> Objects, IDetailCategoryBuilder& Category, TSharedRef<IPropertyHandle> InVisibilityHandle) const
    {
        // Clean up invalid entries.
        for (TWeakObjectPtr<UObject> Object : Objects)
        {
            URenderStreamChannelVisibility* Visibility = Cast<URenderStreamChannelVisibility>(Object);
            for (int i = Visibility->Entries.Num() - 1; i >= 0; i--)
                if (!Visibility->Entries[i].Camera.IsValid())
                    Visibility->Entries.RemoveAt(i);
        }

        InVisibilityHandle->MarkHiddenByCustomization();
        InVisibilityHandle->MarkResetToDefaultCustomized();
        TSharedPtr<SHorizontalBox> VisibilityPanel;
        FDetailWidgetRow& VisibilityRow = Category.AddCustomRow(FText::FromString("Visibility"));
        VisibilityRow
            .NameContent()
            [
                SNew(STextBlock)
                .Text(LOCTEXT("Visibility", "Visibility"))
                .Font(IDetailLayoutBuilder::GetDetailFont())
            ]
            .ValueContent()
            .MaxDesiredWidth(0)
            .MinDesiredWidth(0)
            .HAlign(HAlign_Fill)
            [
                SAssignNew(VisibilityPanel, SHorizontalBox)
            ];

        TSharedPtr<SVerticalBox> VisiblityDataPanel;
        TSharedPtr<SVerticalBox> VisiblityOverridePanel;
        VisibilityPanel->AddSlot()
            .FillWidth(4)
            [
                SAssignNew(VisiblityDataPanel, SVerticalBox)
            ];

        VisibilityPanel->AddSlot()
            .AutoWidth()
            .MaxWidth(1)
            [
                SNew(SBorder)
            ];

        VisibilityPanel->AddSlot()
            .FillWidth(3)
            [
                SAssignNew(VisiblityOverridePanel, SVerticalBox)
            ];

        // Header
        VisiblityDataPanel->AddSlot()
            .VAlign(VAlign_Center)
            .HAlign(HAlign_Left)
            .Padding(5, 5)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("Camera", "Camera"))
                .Font(IDetailLayoutBuilder::GetDetailFontBold())
            ];

        TSharedPtr<SHorizontalBox> VisibilityLabels;
        VisiblityOverridePanel->AddSlot()
            .VAlign(VAlign_Center)
            .HAlign(HAlign_Fill)
            [
                SAssignNew(VisibilityLabels, SHorizontalBox)
            ];

        VisibilityLabels->AddSlot()
            .MaxWidth(0)
            .FillWidth(0)
            .Padding(0, 25);

        VisibilityLabels->AddSlot()
            .VAlign(VAlign_Center)
            .HAlign(HAlign_Center)
            .Padding(10, 5)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("Force\nVisible", "Force\nVisible"))
                .Font(IDetailLayoutBuilder::GetDetailFontBold())
            ];

        VisibilityLabels->AddSlot()
            .VAlign(VAlign_Center)
            .HAlign(HAlign_Center)
            .Padding(10, 5)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("Force\nHidden", "Force\nHidden"))
                .Font(IDetailLayoutBuilder::GetDetailFontBold())
            ];

        // Entries
        const UWorld* World = GEditor->GetEditorWorldContext().World();
        TArray<AActor*> Actors;
        UGameplayStatics::GetAllActorsOfClass(World, ACameraActor::StaticClass(), Actors);

        int i = 1;
        for (AActor* Actor : Actors)
        {
            URenderStreamChannelDefinition* Definition = Actor->FindComponentByClass<URenderStreamChannelDefinition>();
            if (Definition)
            {
                VisiblityDataPanel->AddSlot()
                    .VAlign(VAlign_Center)
                    .HAlign(HAlign_Fill)
                    [
                        SNew(SVisibilityCombo)
                        .Property(InVisibilityHandle)
                        .CameraActor(Cast<ACameraActor>(Actor))
                        .ChannelVisibility(Objects)
                        .Override(VisiblityOverridePanel)
                        .YOffset(i)
                    ];

                ++i;
            }
        }
    }

    void FVisibilityCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
    {
        const TSharedRef<IPropertyHandle> Property = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(URenderStreamChannelVisibility, Entries));
        if (Property->IsValidHandle())
        {
            TArray<TWeakObjectPtr<UObject>> Objects;
            DetailBuilder.GetObjectsBeingCustomized(Objects);
            IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("Visibility");
            CreateVisibilityCustomization(Objects, Category, Property);
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

TSharedRef<IDetailCustomization> MakeVisibilityCustomizationInstance()
{
    return MakeShareable(new FVisibilityCustomization);
}

TSharedRef<IDetailCustomization> MakeSettingsCustomizationInstance()
{
    return MakeShareable(new FSettingsCustomization);
}

#undef LOCTEXT_NAMESPACE
