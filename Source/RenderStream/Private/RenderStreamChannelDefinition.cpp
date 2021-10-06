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
        return FindCameraInScene();
    }

    const TSharedPtr<TArray<TWeakObjectPtr<ACameraActor>>>* ActorsPtrPtr = ChannelActorMap.Find(Channel);
    if (ActorsPtrPtr == nullptr || !(*ActorsPtrPtr) || (*ActorsPtrPtr)->Num() == 0)
    {
        return FindCameraInScene();
    }

    return (*ActorsPtrPtr)->Last();
}

URenderStreamChannelDefinition::URenderStreamChannelDefinition()
    : DefaultVisibility(EVisibilty::Visible)
    , ShowFlags(EShowFlagInitMode::ESFIM_Game)
    , Registered(false)
{
    MeshReconstruction = CreateDefaultSubobject<UProceduralMeshComponent>("MeshReconstruction");
    MeshReconstruction->bUseAsyncCooking = true;
    Hidden.Add(MeshReconstruction);
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
    return DefaultVisibility == EVisibilty::Visible
        ? Hidden.Contains(Actor)
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

void URenderStreamChannelDefinition::UnregisterCamera()
{
    if (Registered)
    {
        ACameraActor* Owner = Cast<ACameraActor>(GetOwner());
        if (Owner)
        {
            auto& Array = FindOrAdd(ChannelActorMap, Owner->GetName());
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

        auto& Array = FindOrAdd(ChannelActorMap, Owner->GetName());
        Array.Add(Owner);
        Registered = true;
    }
    else
    {
        UE_LOG(LogRenderStreamChannelDefinition, Error, TEXT("Unable to add, Channel definition component not on camera actor."));
    }

    ////debug whatever bullshit
    //MeshVertices.Add(FVector(0, -100, 0)); //lower left - 0
    //MeshVertices.Add(FVector(0, -100, 100)); //upper left - 1
    //MeshVertices.Add(FVector(0, 100, 0)); //lower right - 2 
    //MeshVertices.Add(FVector(0, 100, 100)); //upper right - 3

    //MeshVertices.Add(FVector(100, -100, 0)); //lower front left - 4
    //MeshVertices.Add(FVector(100, -100, 100)); //upper front left - 5

    //MeshVertices.Add(FVector(100, 100, 100)); //upper front right - 6
    //MeshVertices.Add(FVector(100, 100, 0)); //lower front right - 7

    ////Back face of cube
    //AddTriangleByIndex(0, 2, 3);
    //AddTriangleByIndex(3, 1, 0);

    ////Left face of cube
    //AddTriangleByIndex(0, 1, 4);
    //AddTriangleByIndex(4, 1, 5);

    ////Front face of cube
    //AddTriangleByIndex(4, 5, 7);
    //AddTriangleByIndex(7, 5, 6);

    ////Right face of cube
    //AddTriangleByIndex(7, 6, 3);
    //AddTriangleByIndex(3, 2, 7);

    ////Top face
    //AddTriangleByIndex(1, 3, 5);
    //AddTriangleByIndex(6, 5, 3);

    ////bottom face
    //AddTriangleByIndex(2, 0, 4);
    //AddTriangleByIndex(4, 7, 2);

    //TArray<FLinearColor> VertexColors;

    //MeshReconstruction->CreateMeshSection_LinearColor(0, MeshVertices, MeshTriangles, TArray<FVector>(), TArray<FVector2D>(), VertexColors, TArray<FProcMeshTangent>(), true);
}

void URenderStreamChannelDefinition::EndPlay(const EEndPlayReason::Type Reason)
{
    Super::EndPlay(Reason);
    UnregisterCamera();
}
