#include "RenderStreamChannelVisibility.h"

#include "Camera/CameraActor.h"

FChannelVisibilityEntry::FChannelVisibilityEntry()
    : Camera(nullptr)
    , Visible(true)
{}

FChannelVisibilityEntry::FChannelVisibilityEntry(const FChannelVisibilityEntry& Other)
    : Camera(Other.Camera)
    , Visible(Other.Visible)
{}

bool FChannelVisibilityEntry::operator==(const FChannelVisibilityEntry & Other) const
{
    return Equals(Other);
}

bool FChannelVisibilityEntry::Equals(const FChannelVisibilityEntry & Other) const
{
    return Other.Camera == Camera && Other.Visible == Visible;
}

URenderStreamChannelVisibility::URenderStreamChannelVisibility() {}

void URenderStreamChannelVisibility::ResolveEntries()
{
    AActor* Owner = GetOwner();
    for (const auto& Entry : Entries)
    {
        if (Entry.Camera.IsValid())
        {
            ACameraActor* Camera = Entry.Camera.Get();
            URenderStreamChannelDefinition* Definition = Camera->FindComponentByClass<URenderStreamChannelDefinition>();
            if (Definition)
            {
                TArray<TWeakObjectPtr<AActor>>& Array = Entry.Visible ? Definition->Visible : Definition->Hidden;
                Array.Add(Owner);
                ResolvedEntries.Add(Entry);
            }
        }
    }
}

void URenderStreamChannelVisibility::RemoveEntries()
{
    AActor* Owner = GetOwner();
    for (const FChannelVisibilityEntry& Entry : ResolvedEntries)
    {
        if (Entry.Camera.IsValid())
        {
            URenderStreamChannelDefinition* Definition = Entry.Camera->FindComponentByClass<URenderStreamChannelDefinition>();
            if (Definition)
            {
                TArray<TWeakObjectPtr<AActor>>& Array = Entry.Visible ? Definition->Visible : Definition->Hidden;
                Array.RemoveAll([Owner](TWeakObjectPtr<AActor>& Element) { return Element == Owner; });
            }
        }
    }

    ResolvedEntries.Empty();
}

void URenderStreamChannelVisibility::BeginPlay()
{
    Super::BeginPlay();
    ResolveEntries();
}

void URenderStreamChannelVisibility::EndPlay(const EEndPlayReason::Type Reason)
{
    Super::EndPlay(Reason);
    RemoveEntries();
}

#if WITH_EDITOR
void URenderStreamChannelVisibility::PostEditChangeProperty(FPropertyChangedEvent& e)
{
    const FName PropertyName = (e.Property != nullptr) ? e.Property->GetFName() : NAME_None;
    if (PropertyName == GET_MEMBER_NAME_CHECKED(URenderStreamChannelVisibility, Entries))
        Refresh();

    Super::PostEditChangeProperty(e);
}

void URenderStreamChannelVisibility::PostEditChangeChainProperty(FPropertyChangedChainEvent& e)
{
    // TODO: If this never changes the number of items in the array, then we can probably optimize this.
    const FName PropertyName = (e.Property != nullptr) ? e.Property->GetFName() : NAME_None;
    if (PropertyName == GET_MEMBER_NAME_CHECKED(URenderStreamChannelVisibility, Entries))
        Refresh();

    Super::PostEditChangeProperty(e);
}

void URenderStreamChannelVisibility::PostEditUndo()
{
    Refresh();
    Super::PostEditUndo();
}
#endif

FChannelVisibilityEntry& URenderStreamChannelVisibility::FindOrAddEntry(const TSoftObjectPtr<ACameraActor> Camera)
{
    FChannelVisibilityEntry* Entry = FindEntry(Camera);
    if (!Entry)
    {
        Entries.Emplace();
        Entry = &Entries.Last();
        Entry->Camera = Camera;
    }

    return *Entry;
}

FChannelVisibilityEntry* URenderStreamChannelVisibility::FindEntry(const TSoftObjectPtr<ACameraActor> Camera)
{
    return Entries.FindByPredicate([&Camera](const FChannelVisibilityEntry& Entry) -> bool {
        return Entry.Camera == Camera;
    });
}

void URenderStreamChannelVisibility::Refresh()
{
    RemoveEntries();
    ResolveEntries();
}

void URenderStreamChannelVisibility::ResetDefaultVisibility(const TSoftObjectPtr<ACameraActor> Camera)
{
    Entries.RemoveAll([&Camera](const FChannelVisibilityEntry& Entry) -> bool {
        return Entry.Camera == Camera;
    });
    Refresh();
}

void URenderStreamChannelVisibility::SetVisibility(const TSoftObjectPtr<ACameraActor> Camera, const bool Visible)
{
    FChannelVisibilityEntry& Entry = FindOrAddEntry(Camera);
    Entry.Visible = Visible;
    Refresh();
}

bool URenderStreamChannelVisibility::GetVisibility(const TSoftObjectPtr<ACameraActor> Camera)
{
    const FChannelVisibilityEntry* Entry = FindEntry(Camera);
    if (Entry)
        return Entry->Visible;

    const auto Component = Camera->FindComponentByClass<URenderStreamChannelDefinition>();
    if (Component)
        return Component->DefaultVisibility == EVisibilty::Visible;

    // If we are using a default with no channel definition everything is always visible.
    return true;
}
