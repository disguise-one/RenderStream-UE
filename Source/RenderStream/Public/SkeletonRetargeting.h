#pragma once

#include "CoreMinimal.h"
#include "RenderStreamLink.h"

struct FRetargetMeshBone
{
    FTransform LocalTransform;  // rest-pose local transform
    int32      ParentIndex;     // INDEX_NONE for root
};

struct FRetargetInitData
{
    int32                 MeshBoneCount;
    TArray<FTransform>    MeshToSourceSpaceTransforms;
    TArray<FQuat>         LocalInitialOrientationDifferences;
    TArray<FQuat>         SourceInitialPoseRotations;
    TArray<int32>         SourceToMeshIndex;      // plain int32, not FCompactPoseBoneIndex
    TArray<int32>         SourceParentIndices;
    TArray<int32>         SourceMappedParentIndex; // nearest source ancestor that IS mapped (-1 if none)
    TSet<int32>           SkipOrientationCorrectionSourceIndices;
    FTransform            RootBoneTransform;
};

namespace RenderStreamRetargeting
{
    // Coordinate conversion (extracted from ToUnrealTransform free function)
    RENDERSTREAM_API FTransform ConvertD3TransformToUE(const RenderStreamLink::Transform& T);

    // Core initialisation pass (extracted from InitialiseAnimationData)
    RENDERSTREAM_API void InitialiseRetargeting(
        const TArray<FRetargetMeshBone>&         MeshBones,
        const RenderStreamLink::FSkeletalLayout& Layout,
        const TMap<FName, int32>&                SourceNameToMeshIndex,
        const TSet<FName>&                       SkipOrientationCorrectionNames,
        FRetargetInitData&                       OutInitData);

    // Core pose-building pass (extracted from BuildPoseFromAnimationData)
    RENDERSTREAM_API void BuildRetargetedPose(
        const RenderStreamLink::FSkeletalPose& Pose,
        const FRetargetInitData&               InitData,
        TArray<FTransform>&                    InOutMeshBoneTransforms);

    // Utility: compute world-space transforms by walking the mesh hierarchy
    RENDERSTREAM_API TArray<FTransform> ComputeWorldTransforms(
        const TArray<FTransform>& LocalTransforms,
        const TArray<int32>&      ParentIndices);

    // Utility: compute world-space positions by walking the mesh hierarchy
    RENDERSTREAM_API TArray<FVector> ComputeWorldPositions(
        const TArray<FTransform>& LocalTransforms,
        const TArray<int32>&      ParentIndices);

    // Test oracle: compute expected world transforms from source data in d3 coordinates.
    RENDERSTREAM_API TArray<FTransform> ComputeExpectedTransformsFromSource(
        const RenderStreamLink::FSkeletalLayout& Layout,
        const RenderStreamLink::FSkeletalPose&   Pose);

    // Test oracle: compute expected world positions from source data in d3 coordinates.
    RENDERSTREAM_API TArray<FVector> ComputeExpectedPositionsFromSource(
        const RenderStreamLink::FSkeletalLayout& Layout,
        const RenderStreamLink::FSkeletalPose&   Pose);
}
