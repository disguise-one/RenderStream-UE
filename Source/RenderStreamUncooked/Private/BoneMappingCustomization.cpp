#include "BoneMappingCustomization.h"

#include "AnimNode_RenderStreamSkeletonSource.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/Skeleton.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

// ---------------------------------------------------------------------------
// IPropertyTypeCustomization — per-row layout
// ---------------------------------------------------------------------------

static USkeleton* GetSkeletonFromPropertyHandle(TSharedRef<IPropertyHandle> Handle)
{
    TArray<UObject*> OuterObjects;
    Handle->GetOuterObjects(OuterObjects);
    for (UObject* Obj : OuterObjects)
    {
        for (UObject* Current = Obj; Current; Current = Current->GetOuter())
        {
            if (UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Current))
            {
                return AnimBP->TargetSkeleton;
            }
        }
    }
    return nullptr;
}

void FBoneMappingCustomization::CustomizeHeader(
    TSharedRef<IPropertyHandle>      PropertyHandle,
    FDetailWidgetRow&                HeaderRow,
    IPropertyTypeCustomizationUtils& CustomizationUtils)
{
    // Get handles for all three fields
    SourceBoneHandle = PropertyHandle->GetChildHandle(
        GET_MEMBER_NAME_CHECKED(FBoneMapping, SourceBone));

    TSharedPtr<IPropertyHandle> BoneHandle = PropertyHandle->GetChildHandle(
        GET_MEMBER_NAME_CHECKED(FBoneMapping, Bone));
    BoneNameHandle = BoneHandle.IsValid()
        ? BoneHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FBoneReference, BoneName))
        : nullptr;
    TSharedPtr<IPropertyHandle> SkipHandle = PropertyHandle->GetChildHandle(
        GET_MEMBER_NAME_CHECKED(FBoneMapping, bSkipOrientationCorrection));

    // Build bone name list from the target skeleton
    BoneNameOptions.Empty();
    BoneNameOptions.Add(MakeShared<FString>(TEXT("None")));

    USkeleton* Skeleton = GetSkeletonFromPropertyHandle(PropertyHandle);
    if (Skeleton)
    {
        const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
        for (int32 i = 0; i < RefSkel.GetNum(); ++i)
        {
            BoneNameOptions.Add(MakeShared<FString>(RefSkel.GetBoneName(i).ToString()));
        }
    }

    // Find the currently selected item for initial highlight
    TSharedPtr<FString> InitialSelection;
    if (BoneNameHandle.IsValid())
    {
        FName CurrentName;
        BoneNameHandle->GetValue(CurrentName);
        FString CurrentStr = (CurrentName == NAME_None) ? TEXT("None") : CurrentName.ToString();
        for (const auto& Option : BoneNameOptions)
        {
            if (*Option == CurrentStr)
            {
                InitialSelection = Option;
                break;
            }
        }
    }

    HeaderRow
        .NameContent()
        [
            SourceBoneHandle.IsValid()
                ? SourceBoneHandle->CreatePropertyValueWidget()
                : SNullWidget::NullWidget
        ]
        .ValueContent()
        .MinDesiredWidth(350.f)
        [
            SNew(SHorizontalBox)

            // Bone picker combo box
            + SHorizontalBox::Slot()
            .FillWidth(1.f)
            [
                SNew(SComboBox<TSharedPtr<FString>>)
                .OptionsSource(&BoneNameOptions)
                .InitiallySelectedItem(InitialSelection)
                .OnGenerateWidget(this, &FBoneMappingCustomization::OnGenerateComboWidget)
                .OnSelectionChanged(this, &FBoneMappingCustomization::OnBoneSelectionChanged)
                .Content()
                [
                    SNew(STextBlock)
                    .Text(this, &FBoneMappingCustomization::GetCurrentBoneNameText)
                    .Font(IDetailLayoutBuilder::GetDetailFont())
                ]
            ]

            // "Skip" label
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(8.f, 0.f, 0.f, 0.f)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("Skip")))
                .Font(IDetailLayoutBuilder::GetDetailFont())
            ]

            // Skip checkbox
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(4.f, 0.f, 0.f, 0.f)
            .VAlign(VAlign_Center)
            [
                SkipHandle.IsValid()
                    ? SkipHandle->CreatePropertyValueWidget()
                    : SNullWidget::NullWidget
            ]
        ];
}

void FBoneMappingCustomization::CustomizeChildren(
    TSharedRef<IPropertyHandle>      PropertyHandle,
    IDetailChildrenBuilder&          ChildBuilder,
    IPropertyTypeCustomizationUtils& CustomizationUtils)
{
    // Everything is in the header row.
}

TSharedRef<SWidget> FBoneMappingCustomization::OnGenerateComboWidget(TSharedPtr<FString> InItem)
{
    return SNew(STextBlock)
        .Text(FText::FromString(InItem.IsValid() ? *InItem : TEXT("None")))
        .Font(IDetailLayoutBuilder::GetDetailFont());
}

void FBoneMappingCustomization::OnBoneSelectionChanged(
    TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo)
{
    if (BoneNameHandle.IsValid() && NewValue.IsValid())
    {
        FName NewBoneName = (*NewValue == TEXT("None")) ? NAME_None : FName(**NewValue);
        BoneNameHandle->SetValue(NewBoneName);
    }
}

FText FBoneMappingCustomization::GetCurrentBoneNameText() const
{
    if (BoneNameHandle.IsValid())
    {
        FName CurrentName;
        BoneNameHandle->GetValue(CurrentName);
        if (CurrentName != NAME_None)
            return FText::FromName(CurrentName);
    }
    return FText::FromString(TEXT("None"));
}
