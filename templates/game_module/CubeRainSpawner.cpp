#include "CubeRainSpawner.h"

#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/Level.h"
#include "Engine/World.h"

namespace
{
    float ReadFloatParam(const AActor* Actor, const FName Name, float Default)
    {
        if (Actor)
        {
            if (const FDoubleProperty* P = FindFProperty<FDoubleProperty>(Actor->GetClass(), Name))
                return (float)P->GetPropertyValue_InContainer(Actor);
            if (const FFloatProperty* P = FindFProperty<FFloatProperty>(Actor->GetClass(), Name))
                return P->GetPropertyValue_InContainer(Actor);
        }
        return Default;
    }

    bool ReadBoolParam(const AActor* Actor, const FName Name, bool Default)
    {
        if (Actor)
            if (const FBoolProperty* P = FindFProperty<FBoolProperty>(Actor->GetClass(), Name))
                return P->GetPropertyValue_InContainer(Actor);
        return Default;
    }

    UObject* ReadObjectParam(const AActor* Actor, const FName Name)
    {
        if (Actor)
            if (const FObjectProperty* P = FindFProperty<FObjectProperty>(Actor->GetClass(), Name))
                return P->GetObjectPropertyValue_InContainer(Actor);
        return nullptr;
    }
}

ACubeRainSpawner::ACubeRainSpawner()
{
    PrimaryActorTick.bCanEverTick = true;
    // A root component is required or the actor has no transform - GetActorLocation() would
    // return the origin and the cubes would spawn at (0,0,0) instead of the spawner's location.
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ACubeRainSpawner::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (!CubeMesh)
        CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!BaseMaterial)
        BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Face.M_Face"));

    const ALevelScriptActor* LSA = GetLevel() ? GetLevel()->GetLevelScriptActor() : nullptr;
    const float Size      = ReadFloatParam(LSA, TEXT("SubLevelParticleSize"), 1.f);
    const float Intensity = ReadFloatParam(LSA, TEXT("SubLevelParticleIntensity"), 1.f);
    const float Speed     = ReadFloatParam(LSA, TEXT("SubLevelParticleSpeed"), 1.f);
    const bool  bEnabled  = ReadBoolParam(LSA, TEXT("SubLevelEnabled"), true);
    UTextureRenderTarget2D* Texture = Cast<UTextureRenderTarget2D>(ReadObjectParam(LSA, TEXT("SubLevelTexture")));

    // (Re)build the cube material instance when the exposed texture changes.
    if (BaseMaterial && Texture && Texture != BoundTexture)
    {
        CubeMID = UMaterialInstanceDynamic::Create(BaseMaterial, this);
        CubeMID->SetTextureParameterValue(TEXT("Texture"), Texture);
        BoundTexture = Texture;
    }

    if (bEnabled && Intensity > 0.f)
    {
        Accumulator += DeltaSeconds * 30.f * Intensity;   // ~30/s at intensity 1
        while (Accumulator >= 1.f)
        {
            SpawnCube(Size, Speed);
            Accumulator -= 1.f;
        }
    }

    // No floor, so destroy cubes once they have fallen away.
    for (int32 i = Spawned.Num() - 1; i >= 0; --i)
    {
        AStaticMeshActor* Cube = Spawned[i];
        if (!Cube || Cube->GetActorLocation().Z < -1000.f)
        {
            if (Cube)
                Cube->Destroy();
            Spawned.RemoveAt(i);
        }
    }
}

void ACubeRainSpawner::SpawnCube(float Size, float Speed)
{
    UWorld* World = GetWorld();
    if (!World || !CubeMesh)
        return;

    const FVector Location = GetActorLocation() + FVector(
        FMath::FRandRange(-150.f, 150.f),
        FMath::FRandRange(-150.f, 150.f),
        FMath::FRandRange(0.f, 100.f));
    const FRotator Rotation(FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f));

    FActorSpawnParameters SpawnParams;
    SpawnParams.OverrideLevel = GetLevel();
    AStaticMeshActor* Cube = World->SpawnActor<AStaticMeshActor>(Location, Rotation, SpawnParams);
    if (!Cube)
        return;

    UStaticMeshComponent* Mesh = Cube->GetStaticMeshComponent();
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetStaticMesh(CubeMesh);
    Mesh->SetWorldScale3D(FVector(FMath::Max(Size, 0.01f) * 0.2f));   // size 1 -> 0.2 -> ~20-unit cube
    if (CubeMID)
        Mesh->SetMaterial(0, CubeMID);
    Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
    Mesh->SetSimulatePhysics(true);
    Mesh->SetPhysicsLinearVelocity(FVector(0.f, 0.f, -FMath::Max(Speed, 0.f) * 300.f));

    Spawned.Add(Cube);
}