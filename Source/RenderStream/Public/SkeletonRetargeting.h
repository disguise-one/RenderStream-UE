#pragma once

#include "CoreMinimal.h"
#include "RenderStreamLink.h"

// The skeleton retargeting code here maps data from a Designer source skeleton to a UE
// skeletal mesh. The UE skeleton may have a different bone layout to the source skeleton.
// Corresponding joints are selected in the RenderStream Skeleton Source AnimNode.
// The retargeting code transforms the joint data into the correct coordinate system.
// It also corrects for differences in the layout and initial pose of the two skeletons
// So that the world-space bone positions and orientations are the same.
// It runs in two phases:
// 1. InitialiseRetargeting which runs once to initialise fixed mappings between the
//    Source and Mesh skeletons.
// 2. BuildRetargetedPose which runs on each frame to convert each set of joint pose
//    data to the corresponding UE skeleton joint poses.

// A single bone in the UE mesh skeleton, used as input to InitialiseRetargeting.
struct FRetargetMeshBone
{
    FTransform LocalTransform;  // rest-pose local transform
    int32      ParentIndex;     // INDEX_NONE for root
};

// Resolved mapping from a source (d3) bone to a mesh (UE) bone.
// Built by the AnimNode from FBoneMapping after resolving FBoneReference to an index.
struct FSourceBoneMapping
{
    int32 MeshIndex = INDEX_NONE;            // compact pose index of the mesh bone, or INDEX_NONE if unmapped
    bool bSkipOrientationCorrection = false; // when true, children preserve mesh rest-pose direction and length
};

// Per-source-bone retargeting data, computed by InitialiseRetargeting.
struct FSourceBoneRetargetData
{
    // Topology
    int32   ParentIndex = INDEX_NONE;            // parent in source hierarchy
    int32   MappedParentIndex = INDEX_NONE;      // nearest mapped ancestor in source hierarchy
    int32   MeshIndex = INDEX_NONE;              // corresponding mesh bone index
    bool    bSkipOrientationCorrection = false;  // children preserve mesh rest-pose direction and length

    // Retargeting corrections
    FQuat   MeshToSourceRotation = FQuat::Identity;            // corrected rest-pose world rotation for pose conjugation
    FQuat   LocalOrientationCorrection = FQuat::Identity;      // local-space correction aligning mesh to source direction
    FQuat   InitialPoseRotation = FQuat::Identity;             // source rest-pose local rotation in UE space
    float   BoneLengthRatio = 1.0f;                            // scale factor for bone length alignment
    FVector MultiChildTranslationOffset = FVector::ZeroVector; // per-child offset for multi-child parents
};

// Pre-computed retargeting data produced by InitialiseRetargeting and consumed
// each frame by BuildRetargetedPose.
struct FRetargetInitData
{
    int32                            MeshBoneCount = 0;                        // total bones in the mesh skeleton
    FTransform                       RootBoneTransform = FTransform::Identity; // inverse of mesh root's parent world transform
    TArray<FSourceBoneRetargetData>  SourceBones;                              // one per source bone in the d3 layout
};

namespace RenderStreamRetargeting
{
    // Convert a d3 skeleton joint transform to UE space
    RENDERSTREAM_API FTransform ConvertD3TransformToUE(const RenderStreamLink::Transform& T);

    // Core initialisation pass
    RENDERSTREAM_API void InitialiseRetargeting(
        const TArray<FRetargetMeshBone>&            MeshBones,
        const RenderStreamLink::FSkeletalLayout&    Layout,
        const TMap<FName, FSourceBoneMapping>&      BoneMapping,
        bool                                        bAlignBoneLengths,
        FRetargetInitData&                          OutInitData);

    // Core pose-building pass
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
