#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CubeRainSpawner.generated.h"

class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTextureRenderTarget2D;
class AStaticMeshActor;

// Rains rigid-body cube actors, driven by the sub-level's exposed parameters (read from the
// level's RenderStream blueprint actor each tick): SubLevelParticleSize / SubLevelParticleIntensity /
// SubLevelParticleSpeed / SubLevelEnabled / SubLevelTexture. Physics bodies, so weight and
// momentum apply. Cubes are recycled once they fall away (there is no floor).
UCLASS()
class ACubeRainSpawner : public AActor
{
    GENERATED_BODY()

public:
    ACubeRainSpawner();
    virtual void Tick(float DeltaSeconds) override;

private:
    void SpawnCube(float Size, float Speed);

    UPROPERTY(Transient) TObjectPtr<UStaticMesh> CubeMesh;
    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> BaseMaterial;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> CubeMID;
    UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> BoundTexture;
    UPROPERTY(Transient) TArray<TObjectPtr<AStaticMeshActor>> Spawned;

    float Accumulator = 0.f;
};