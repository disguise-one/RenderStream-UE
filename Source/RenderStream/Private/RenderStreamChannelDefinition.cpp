#include "RenderStreamChannelDefinition.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameEngine.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY(LogRenderStreamChannelDefinition);

TMap<FString, TSharedPtr<TArray<TWeakObjectPtr<ACameraActor>>>> URenderStreamChannelDefinition::ChannelActorMap;

namespace
{
    UWorld* GetWorldContext()
    {
        UWorld* World = nullptr;
#if WITH_EDITOR
        if (GIsEditor)
        {
            for (const FWorldContext& Context : GEngine->GetWorldContexts())
            {
                if (Context.WorldType == EWorldType::PIE)
                {
                    World = Context.World();
                    break;
                }
            }
        }
        else
#endif
        {
            UGameEngine* GameEngine = CastChecked<UGameEngine>(GEngine);
            World = GameEngine->GetGameWorld();
        }

        return World;
    }

    ACameraActor* FindCameraInScene()
    {
        UWorld* World = GetWorldContext();
        if (!World)
            return nullptr;

        TArray<AActor*> FoundActors;
        UGameplayStatics::GetAllActorsOfClass(World, ACameraActor::StaticClass(), FoundActors);

        for (AActor* Actor : FoundActors)
        {
            bool IsAutoCamera = false;
            for (FConstCameraActorIterator CameraIterator = World->GetAutoActivateCameraIterator(); CameraIterator; ++CameraIterator)
            {
                ACameraActor* CameraActor = CameraIterator->Get();
                if (CameraActor == Actor)
                {
                    IsAutoCamera = true;
                    break;
                }
            }

            if (!IsAutoCamera)
                return Cast<ACameraActor>(Actor);
        }
        return nullptr;
    }

    TArray<TWeakObjectPtr<ACameraActor>>& FindOrAdd(TMap<FString, TSharedPtr<TArray<TWeakObjectPtr<ACameraActor>>>>& Map, const FString& Value)
    {
        const auto Ptr = Map.Find(Value);
        if (Ptr == nullptr)
        {
            Map.Add(Value, MakeShareable(new TArray<TWeakObjectPtr<ACameraActor>>()));
            return *Map.Find(Value)->Get();
        }

        return *Ptr->Get();
    }
}

uint32 URenderStreamChannelDefinition::GetChannelCameraNum(const FString& Channel)
{
    if (Channel.IsEmpty())
    {
        return 0;
    }

    const auto Actors = ChannelActorMap.Find(Channel);
    if (Actors == nullptr)
    {
        return 0;
    }

    return (*Actors)->Num();
}

TWeakObjectPtr<ACameraActor> URenderStreamChannelDefinition::GetChannelCamera(const FString& Channel)
{
    if (Channel.IsEmpty())
    {
        UE_LOG(LogRenderStreamChannelDefinition, Warning, TEXT("Channel is empty, returning first camera found in scene."));
        return FindCameraInScene();
    }

    const TSharedPtr<TArray<TWeakObjectPtr<ACameraActor>>>* ActorsPtrPtr = ChannelActorMap.Find(Channel);
    if (ActorsPtrPtr == nullptr || !(*ActorsPtrPtr) || (*ActorsPtrPtr)->Num() == 0)
    {
        UE_LOG(LogRenderStreamChannelDefinition, Warning, TEXT("Channel not found in ChannelActorMap, returning first camera found in scene."));
        return FindCameraInScene();
    }

    return (*ActorsPtrPtr)->Last();
}

URenderStreamChannelDefinition::URenderStreamChannelDefinition()
    : DefaultVisibility(EChannelVisibilty::Visible)
    , ShowFlags(EShowFlagInitMode::ESFIM_Game)
    , Registered(false)
{
}

void URenderStreamChannelDefinition::ResetDefaultVisibility(AActor* Actor)
{
    Visible.Remove(Actor);
    Hidden.Remove(Actor);
}

void URenderStreamChannelDefinition::SetVisibility(AActor* Actor, bool IsVisible)
{
    if (IsVisible)
    {
        Visible.Add(Actor);
        Hidden.Remove(Actor);
    }
    else
    {
        Visible.Remove(Actor);
        Hidden.Add(Actor);
    }
}

bool URenderStreamChannelDefinition::GetVisibility(AActor* Actor) const
{
    return DefaultVisibility == EChannelVisibilty::Visible
        ? !Hidden.Contains(Actor)
        : Visible.Contains(Actor);
}

TArray<ACameraActor*> URenderStreamChannelDefinition::GetInstancedCameras()
{
    TArray<ACameraActor*> ValidCameras;
    TArray<TWeakObjectPtr<ACameraActor>> InvalidCameras;
    for (TWeakObjectPtr<ACameraActor> Camera : InstancedCameras)
    {
        if (!Camera.IsValid())
            InvalidCameras.Add(Camera);
        else
            ValidCameras.Add(Camera.Get());
    }

    for (TWeakObjectPtr<ACameraActor> Camera : InvalidCameras)
        InstancedCameras.Remove(Camera);

    return ValidCameras;
}

FString URenderStreamChannelDefinition::GetChannelName() const
{
    FString Name = GetOwner()->GetActorNameOrLabel();
    Name.LeftChopInline(Name.Find("_UAID_", ESearchCase::CaseSensitive, ESearchDir::FromEnd), true);
    return Name;
}

void URenderStreamChannelDefinition::UnregisterCamera()
{
    if (Registered)
    {
        ACameraActor* Owner = Cast<ACameraActor>(GetOwner());
        if (Owner)
        {
            auto& Array = FindOrAdd(ChannelActorMap, GetChannelName());
            Array.Remove(Owner);
        }
        else
        {
            UE_LOG(LogRenderStreamChannelDefinition, Error, TEXT("Unable to remove, Channel definition component not on camera actor."));
        }

        Registered = false;
    }
}

void URenderStreamChannelDefinition::UpdateShowFlags()
{
    for (FEngineShowFlagsSetting ShowFlagSetting : ShowFlagSettings)
    {
        int32 SettingIndex = ShowFlags.FindIndexByName(*(ShowFlagSetting.ShowFlagName));
        if (SettingIndex != INDEX_NONE)
        {
            ShowFlags.SetSingleFlag(SettingIndex, ShowFlagSetting.Enabled);
        }
    }
}

void UpdateVisibilitySet(TSet<TWeakObjectPtr<AActor>>& Destination, TSet<TWeakObjectPtr<AActor>>& OldSet, const TSet<TSoftObjectPtr<AActor>>& NewSet)
{
    Destination = Destination.Difference(OldSet);
    OldSet.Empty();
    for (auto Entry : NewSet)
    {
        if (Entry.IsValid())
        {
            AActor* Actor = Entry.Get();
            OldSet.Add(Actor);
            Destination.Add(Actor);
        }
    }
}

void URenderStreamChannelDefinition::OnRegister()
{
    Super::OnRegister();
    UpdateShowFlags();
}

void URenderStreamChannelDefinition::BeginPlay()
{
    Super::BeginPlay();
    ACameraActor* Owner = Cast<ACameraActor>(GetOwner());
    if (Owner)
    {
        auto Component = Owner->FindComponentByClass<UCameraComponent>();
        if (Component)
            Component->SetConstraintAspectRatio(false);

        auto& Array = FindOrAdd(ChannelActorMap, GetChannelName());
        Array.Add(Owner);
        Registered = true;
    }
    else
    {
        UE_LOG(LogRenderStreamChannelDefinition, Error, TEXT("Unable to add, Channel definition component not on camera actor."));
    }
}

void URenderStreamChannelDefinition::EndPlay(const EEndPlayReason::Type Reason)
{
    UnregisterCamera();
    Super::EndPlay(Reason);
}

void URenderStreamChannelDefinition::AddCameraInstance(TWeakObjectPtr<ACameraActor> Camera)
{
    check(Camera.IsValid());
    URenderStreamChannelDefinition* Definition = Camera->FindComponentByClass<URenderStreamChannelDefinition>();
    Definition->IsInstance = true;
    InstancedCameras.Add(Camera);
    OnCameraInstanced.Broadcast(Camera.Get());
}
