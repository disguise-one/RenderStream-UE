#include "SkeletonRetargeting.h"

#include "CoreMinimal.h"
#include "Math/UnitConversion.h"

namespace RenderStreamRetargeting
{

// Build parent index array for a source layout by matching joint IDs.
static TArray<int32> BuildSourceParentIndices(const RenderStreamLink::FSkeletalLayout& Layout)
{
    const int32 N = Layout.joints.Num();

    // Build id-to-index map for O(N) lookup instead of O(N^2) linear search
    TMap<uint64, int32> IdToIndex;
    IdToIndex.Reserve(N);
    for (int32 i = 0; i < N; ++i)
        IdToIndex.Add(Layout.joints[i].id, i);

    TArray<int32> Parents;
    Parents.SetNum(N);
    for (int32 i = 0; i < N; ++i)
    {
        const int32* Found = IdToIndex.Find(Layout.joints[i].parentId);
        Parents[i] = Found ? *Found : INDEX_NONE;
    }
    return Parents;
}

// Accumulate the source-space offset from a child bone up to a mapped ancestor,
// walking through any unmapped intermediate bones in the source hierarchy.
static FVector AccumulateSourceOffset(
    const TArray<FTransform>&              SourceInitialPose,
    const TArray<FSourceBoneRetargetData>& SourceBones,
    int32                                  ChildSource,
    int32                                  AncestorSource)
{
    FVector Offset = SourceInitialPose[ChildSource].GetTranslation();
    int32 Walk = SourceBones[ChildSource].ParentIndex;
    while (Walk != AncestorSource && Walk != INDEX_NONE)
    {
        Offset = SourceInitialPose[Walk].GetRotation().RotateVector(Offset)
            + SourceInitialPose[Walk].GetTranslation();
        Walk = SourceBones[Walk].ParentIndex;
    }
    checkSlow(Walk != INDEX_NONE);
    return Offset;
}

// Extract local transforms and parent indices from the MeshBones array.
static void ExtractMeshArrays(
    const TArray<FRetargetMeshBone>& MeshBones,
    TArray<FTransform>&              OutLocalTransforms,
    TArray<int32>&                   OutParentIndices)
{
    const int32 N = MeshBones.Num();
    OutLocalTransforms.SetNum(N);
    OutParentIndices.SetNum(N);
    for (int32 i = 0; i < N; ++i)
    {
        OutLocalTransforms[i] = MeshBones[i].LocalTransform;
        OutParentIndices[i]   = MeshBones[i].ParentIndex;
    }
}

// ---------------------------------------------------------------------------
// Build source-to-mesh topology and convert rest poses to UE space.
// Populates OutInitData.SourceBones with parent indices, mesh indices,
// initial pose rotations, skip flags, and mapped parent indices.
// Also produces per-bone child counts, mesh-to-source index mapping,
// and the converted source initial pose transforms.
// ---------------------------------------------------------------------------
static void BuildTopologyAndMapping(
    const RenderStreamLink::FSkeletalLayout& Layout,
    const TMap<FName, FSourceBoneMapping>&   BoneMapping,
    int32                                    MeshBoneCount,
    FRetargetInitData&                       OutInitData,
    TArray<int32>&                           OutSourceChildCount,
    TArray<int32>&                           OutMeshToSourceIndex,
    TArray<FTransform>&                      OutSourceInitialPose)
{
    const int32 SourceBoneCount = Layout.joints.Num();
    const TArray<int32> SourceParentIndices = BuildSourceParentIndices(Layout);

    OutSourceChildCount.Init(0, SourceBoneCount);
    OutMeshToSourceIndex.Init(INDEX_NONE, MeshBoneCount);
    OutSourceInitialPose.SetNum(SourceBoneCount);

    for (int32 i = 0; i < SourceBoneCount; ++i)
    {
        FSourceBoneRetargetData& Bone = OutInitData.SourceBones[i];
        Bone.ParentIndex = SourceParentIndices[i];
        if (Bone.ParentIndex != INDEX_NONE)
            OutSourceChildCount[Bone.ParentIndex]++;

        OutSourceInitialPose[i] = ConvertD3TransformToUE(Layout.joints[i].transform);
        Bone.InitialPoseRotation = OutSourceInitialPose[i].GetRotation();

        if (const FSourceBoneMapping* Found = BoneMapping.Find(FName(Layout.jointNames[i])))
        {
            if (Found->MeshIndex != INDEX_NONE)
            {
                OutMeshToSourceIndex[Found->MeshIndex] = i;
                Bone.MeshIndex = Found->MeshIndex;
            }
            Bone.bSkipOrientationCorrection = Found->bSkipOrientationCorrection;
        }
    }

    // Find nearest mapped ancestor for each source bone
    for (int32 i = 0; i < SourceBoneCount; ++i)
    {
        int32 Walk = OutInitData.SourceBones[i].ParentIndex;
        while (Walk != INDEX_NONE)
        {
            if (OutInitData.SourceBones[Walk].MeshIndex != INDEX_NONE)
            {
                OutInitData.SourceBones[i].MappedParentIndex = Walk;
                break;
            }
            Walk = OutInitData.SourceBones[Walk].ParentIndex;
        }
    }
}

// ---------------------------------------------------------------------------
// Compute per-child translation offsets for multi-child parents.
// A single rotation can't align all children of a multi-child parent, so
// per-child offsets place each child at its source-space position.
// Also recomputes world transforms for descendants of corrected bones.
// ---------------------------------------------------------------------------
static void ComputeMultiChildOffsets(
    FRetargetInitData&                   OutInitData,
    const TArray<FTransform>&            SourceInitialPose,
    const TArray<int32>&                 SourceChildCount,
    const TArray<FRetargetMeshBone>&     MeshBones,
    const TArray<FTransform>&            MeshLocalTransforms,
    const TArray<int32>&                 MeshParentIndices,
    TArray<FTransform>&                  InOutMeshWorldTransforms)
{
    const int32 SourceBoneCount = OutInitData.SourceBones.Num();
    const int32 MeshBoneCount = MeshBones.Num();

    // Build children list for multi-child parents
    TMap<int32, TArray<int32>> MultiChildParentToChildren;
    for (int32 i = 0; i < SourceBoneCount; ++i)
    {
        const int32 ParentIdx = OutInitData.SourceBones[i].ParentIndex;
        if (ParentIdx != INDEX_NONE && SourceChildCount[ParentIdx] > 1)
            MultiChildParentToChildren.FindOrAdd(ParentIdx).Add(i);
    }

    TSet<int32> CorrectedMeshBones;

    for (const auto& Pair : MultiChildParentToChildren)
    {
        const int32 SourceParent = Pair.Key;
        const int32 ParentMesh = OutInitData.SourceBones[SourceParent].MeshIndex;
        if (ParentMesh == INDEX_NONE)
            continue;

        const FQuat ParentWorldRot = InOutMeshWorldTransforms[ParentMesh].GetRotation();
        const FVector ParentWorldPos = InOutMeshWorldTransforms[ParentMesh].GetTranslation();

        for (int32 Child : Pair.Value)
        {
            const int32 ChildMesh = OutInitData.SourceBones[Child].MeshIndex;
            if (ChildMesh == INDEX_NONE)
                continue;
            const int32 MappedParent = OutInitData.SourceBones[Child].MappedParentIndex;
            if (MappedParent == INDEX_NONE)
                continue;

            const FVector SrcOffset = AccumulateSourceOffset(
                SourceInitialPose, OutInitData.SourceBones, Child, MappedParent);
            const FVector TargetLocal = ParentWorldRot.UnrotateVector(SrcOffset);
            const FVector Correction =
                TargetLocal - MeshBones[ChildMesh].LocalTransform.GetTranslation();

            if (!Correction.IsNearlyZero())
            {
                OutInitData.SourceBones[Child].MultiChildTranslationOffset = Correction;
                InOutMeshWorldTransforms[ChildMesh].SetTranslation(ParentWorldPos + SrcOffset);
                CorrectedMeshBones.Add(ChildMesh);
            }
        }
    }

    // Recompute world transforms for descendants of corrected bones
    TSet<int32> RecomputedBones = CorrectedMeshBones;
    for (int32 MeshIdx = 0; MeshIdx < MeshBoneCount; ++MeshIdx)
    {
        if (CorrectedMeshBones.Contains(MeshIdx))
            continue;
        const int32 ParentIdx = MeshParentIndices[MeshIdx];
        if (ParentIdx != INDEX_NONE && ParentIdx < MeshBoneCount && RecomputedBones.Contains(ParentIdx))
        {
            InOutMeshWorldTransforms[MeshIdx] = MeshLocalTransforms[MeshIdx] * InOutMeshWorldTransforms[ParentIdx];
            RecomputedBones.Add(MeshIdx);
        }
    }
}

// ---------------------------------------------------------------------------
// Compute orientation corrections.
// For each mesh bone mapped to a source bone, compute the world-space
// orientation difference (WOD) between source and mesh rest poses, then
// convert to a local correction applied to the parent bone.
// Root bones store their root transform instead of computing a correction.
// ---------------------------------------------------------------------------
static void ComputeOrientationCorrections(
    FRetargetInitData&                   OutInitData,
    const TArray<FTransform>&            SourceInitialPose,
    const TArray<int32>&                 SourceChildCount,
    const TArray<FRetargetMeshBone>&     MeshBones,
    const TArray<int32>&                 MeshToSourceIndex,
    const TArray<FTransform>&            MeshWorldTransforms)
{
    const int32 MeshBoneCount = MeshBones.Num();

    TArray<FQuat> WorldOrientDiff;
    WorldOrientDiff.Init(FQuat::Identity, MeshBoneCount);

    for (int32 MeshIndex = 0; MeshIndex < MeshBoneCount; ++MeshIndex)
    {
        const int32 SourceIndex = MeshToSourceIndex[MeshIndex];
        if (SourceIndex == INDEX_NONE)
            continue;

        FSourceBoneRetargetData& Bone = OutInitData.SourceBones[SourceIndex];

        // Root bone: store root transform and skip orientation correction
        if (Bone.ParentIndex < 0)
        {
            const int32 ParentIdx = MeshBones[MeshIndex].ParentIndex;
            if (ParentIdx != INDEX_NONE && ParentIdx < MeshBoneCount)
            {
                OutInitData.RootBoneTransform = MeshWorldTransforms[ParentIdx].Inverse();
                OutInitData.RootBoneTransform.SetScale3D(FVector::OneVector);
            }
            continue;
        }

        const int32 MappedParent = Bone.MappedParentIndex;
        if (MappedParent == INDEX_NONE)
            continue;

        const int32 ParentMeshIndex = OutInitData.SourceBones[MappedParent].MeshIndex;
        if (ParentMeshIndex == INDEX_NONE || ParentMeshIndex >= MeshBoneCount)
            continue;

        // Skip correction: preserve parent->child direction from mesh rest-pose
        if (OutInitData.SourceBones[MappedParent].bSkipOrientationCorrection)
        {
            WorldOrientDiff[MeshIndex] = WorldOrientDiff[ParentMeshIndex];
            continue;
        }

        const FVector SourceOffset = AccumulateSourceOffset(
            SourceInitialPose, OutInitData.SourceBones, SourceIndex, MappedParent);
        const FVector MeshOffset =
            MeshWorldTransforms[MeshIndex].GetTranslation() -
            MeshWorldTransforms[ParentMeshIndex].GetTranslation();

        // Single-child parent with non-zero offset: compute exact correction.
        // TODO: implement per-child orientation correction for multi-child parents.
        // Currently only translation offsets are applied; orientation inherits from parent.
        // A single rotation can't correct all children since they share the same parent rotation.
        const bool bComputeWOD = SourceChildCount[Bone.ParentIndex] <= 1
            && SourceOffset != FVector::ZeroVector;

        if (bComputeWOD)
        {
            WorldOrientDiff[MeshIndex] = FQuat::FindBetweenVectors(MeshOffset, SourceOffset);

            // Convert world-space delta to parent's local space
            const FQuat ParentWorldRot = MeshWorldTransforms[ParentMeshIndex].GetRotation();
            const FQuat ParentLocalRot = MeshBones[ParentMeshIndex].LocalTransform.GetRotation();
            const FQuat GrandparentWorldRot = ParentWorldRot * ParentLocalRot.Inverse();
            const FQuat DeltaWOD =
                WorldOrientDiff[ParentMeshIndex].Inverse() * WorldOrientDiff[MeshIndex];

            OutInitData.SourceBones[MappedParent].LocalOrientationCorrection =
                GrandparentWorldRot.Inverse() * DeltaWOD * GrandparentWorldRot;
        }
        else
        {
            WorldOrientDiff[MeshIndex] = WorldOrientDiff[ParentMeshIndex];
        }
    }
}

// ---------------------------------------------------------------------------
// Build corrected rest-pose world rotations for pose conjugation.
// MeshToSourceRotation must equal the world rotation that BuildRetargetedPose
// produces at rest, so that the conjugation
// MeshToSource^{-1} * P * MeshToSource correctly transforms pose rotations.
// ---------------------------------------------------------------------------
static void ComputeMeshToSourceRotations(
    FRetargetInitData&               OutInitData,
    const TArray<FRetargetMeshBone>& MeshBones,
    const TArray<int32>&             MeshToSourceIndex)
{
    const int32 MeshBoneCount = MeshBones.Num();
    TArray<FQuat> CorrectedRestWorldRot;
    CorrectedRestWorldRot.Init(FQuat::Identity, MeshBoneCount);

    for (int32 MeshIdx = 0; MeshIdx < MeshBoneCount; ++MeshIdx)
    {
        const int32 SourceIdx = MeshToSourceIndex[MeshIdx];
        FQuat CorrectedLocalRot = MeshBones[MeshIdx].LocalTransform.GetRotation();

        if (SourceIdx != INDEX_NONE && OutInitData.SourceBones[SourceIdx].ParentIndex >= 0)
            CorrectedLocalRot = OutInitData.SourceBones[SourceIdx].LocalOrientationCorrection * CorrectedLocalRot;

        const int32 ParentIdx = MeshBones[MeshIdx].ParentIndex;
        CorrectedRestWorldRot[MeshIdx] = (ParentIdx != INDEX_NONE && ParentIdx < MeshBoneCount)
            ? CorrectedRestWorldRot[ParentIdx] * CorrectedLocalRot
            : CorrectedLocalRot;

        if (SourceIdx != INDEX_NONE)
            OutInitData.SourceBones[SourceIdx].MeshToSourceRotation = CorrectedRestWorldRot[MeshIdx];
    }
}

// ---------------------------------------------------------------------------
// Compute bone length ratios for positional alignment.
// ratio = 1 + (source_dist - mesh_dist) / bone_local_length
// Skipped for children of parents with bSkipOrientationCorrection.
// ---------------------------------------------------------------------------
static void ComputeBoneLengthRatios(
    FRetargetInitData&        OutInitData,
    const TArray<FTransform>& SourceInitialPose,
    const TArray<int32>&      SourceParentIndices,
    const TArray<FRetargetMeshBone>& MeshBones,
    const TArray<FTransform>& MeshWorldTransforms)
{
    const int32 SourceBoneCount = OutInitData.SourceBones.Num();
    const TArray<FTransform> SourceWorldTransforms = ComputeWorldTransforms(
        SourceInitialPose, SourceParentIndices);

    for (int32 i = 0; i < SourceBoneCount; ++i)
    {
        const FSourceBoneRetargetData& Bone = OutInitData.SourceBones[i];

        if (Bone.MeshIndex == INDEX_NONE)
            continue;
        if (Bone.MappedParentIndex == INDEX_NONE)
            continue;

        // Skip: preserves both direction and length from mesh rest-pose
        if (OutInitData.SourceBones[Bone.MappedParentIndex].bSkipOrientationCorrection)
            continue;

        const int32 ParentMeshIndex = OutInitData.SourceBones[Bone.MappedParentIndex].MeshIndex;
        if (ParentMeshIndex == INDEX_NONE)
            continue;

        const float SourceLength = FVector::Dist(
            SourceWorldTransforms[i].GetTranslation(),
            SourceWorldTransforms[Bone.MappedParentIndex].GetTranslation());
        const float MeshLength = FVector::Dist(
            MeshWorldTransforms[Bone.MeshIndex].GetTranslation(),
            MeshWorldTransforms[ParentMeshIndex].GetTranslation());
        const float BoneLocalLength =
            MeshBones[Bone.MeshIndex].LocalTransform.GetTranslation().Size();

        if (BoneLocalLength > KINDA_SMALL_NUMBER)
            OutInitData.SourceBones[i].BoneLengthRatio =
                FMath::Max(KINDA_SMALL_NUMBER, 1.0f + (SourceLength - MeshLength) / BoneLocalLength);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

FTransform ConvertD3TransformToUE(const RenderStreamLink::Transform& T)
{
    // d3(x,y,z) -> UE(z,x,y) in centimeters
    const FVector Pos(
        FUnitConversion::Convert(T.z, EUnit::Meters, EUnit::Centimeters),
        FUnitConversion::Convert(T.x, EUnit::Meters, EUnit::Centimeters),
        FUnitConversion::Convert(T.y, EUnit::Meters, EUnit::Centimeters));
    const FQuat Rotation(T.rz, T.rx, T.ry, T.rw);

    // Apply 90 yaw to align d3 Y-forward with UE X-forward
    const FTransform ToSkeletonSpace(FQuat::MakeFromRotator(FRotator(0, 90, 0)));
    return ToSkeletonSpace * FTransform(Rotation, Pos) * ToSkeletonSpace.Inverse();
}

void InitialiseRetargeting(
    const TArray<FRetargetMeshBone>&         MeshBones,
    const RenderStreamLink::FSkeletalLayout& Layout,
    const TMap<FName, FSourceBoneMapping>&   BoneMapping,
    bool                                     bAlignBoneLengths,
    FRetargetInitData&                       OutInitData)
{
    // Initialization proceeds in phases:
    // 1. Build source-to-mesh topology and convert rest poses to UE space
    // 2. Compute mesh world transforms from local transforms
    // 3. Compute per-child translation offsets for multi-child parents
    // 4. Compute orientation corrections (world-space layout differences)
    // 5. Build corrected rest-pose world rotations for pose conjugation
    // 6. (Optional) Compute bone length ratios for positional alignment

    const int32 MeshBoneCount   = MeshBones.Num();
    const int32 SourceBoneCount = Layout.joints.Num();
    OutInitData.MeshBoneCount   = MeshBoneCount;
    OutInitData.RootBoneTransform = FTransform::Identity;
    OutInitData.SourceBones.SetNum(SourceBoneCount);

    // Phase 1: Build topology and source-to-mesh mapping
    TArray<int32>      SourceChildCount;
    TArray<int32>      MeshToSourceIndex;
    TArray<FTransform> SourceInitialPose;
    BuildTopologyAndMapping(Layout, BoneMapping, MeshBoneCount, OutInitData,
        SourceChildCount, MeshToSourceIndex, SourceInitialPose);

    // Phase 2: Compute mesh world transforms
    TArray<FTransform> MeshLocalTransforms;
    TArray<int32>      MeshParentIndices;
    ExtractMeshArrays(MeshBones, MeshLocalTransforms, MeshParentIndices);
    TArray<FTransform> MeshWorldTransforms = ComputeWorldTransforms(MeshLocalTransforms, MeshParentIndices);

    // Phase 3: Multi-child translation offsets
    ComputeMultiChildOffsets(OutInitData, SourceInitialPose, SourceChildCount,
        MeshBones, MeshLocalTransforms, MeshParentIndices, MeshWorldTransforms);

    // Phase 4: Orientation corrections
    ComputeOrientationCorrections(OutInitData, SourceInitialPose, SourceChildCount,
        MeshBones, MeshToSourceIndex, MeshWorldTransforms);

    // Phase 5: Corrected rest-pose world rotations
    ComputeMeshToSourceRotations(OutInitData, MeshBones, MeshToSourceIndex);

    // Phase 6: Bone length ratios
    if (bAlignBoneLengths)
    {
        const TArray<int32> SourceParentIndices = BuildSourceParentIndices(Layout);
        ComputeBoneLengthRatios(OutInitData, SourceInitialPose, SourceParentIndices,
            MeshBones, MeshWorldTransforms);
    }
}

void BuildRetargetedPose(
    const RenderStreamLink::FSkeletalPose& Pose,
    const FRetargetInitData&               InitData,
    TArray<FTransform>&                    InOutMeshBoneTransforms)
{
    const int32 SourceBoneCount = Pose.joints.Num();
    if (InitData.SourceBones.Num() != SourceBoneCount)
        return;
    if (InOutMeshBoneTransforms.Num() < InitData.MeshBoneCount)
        return;

    // Convert all pose joints to UE space (cached to avoid double conversion)
    TArray<FTransform> PoseTransforms;
    PoseTransforms.SetNum(SourceBoneCount);
    for (int32 i = 0; i < SourceBoneCount; ++i)
        PoseTransforms[i] = ConvertD3TransformToUE(Pose.joints[i].transform);

    // For each mapped bone, accumulate pose rotations from unmapped ancestors
    // between it and its nearest mapped ancestor.
    TArray<FQuat> UnmappedPoseRot;
    UnmappedPoseRot.Init(FQuat::Identity, SourceBoneCount);

    for (int32 i = 0; i < SourceBoneCount; ++i)
    {
        const FSourceBoneRetargetData& Bone = InitData.SourceBones[i];
        if (Bone.MeshIndex == INDEX_NONE)
            continue;
        if (Bone.MappedParentIndex == INDEX_NONE)
            continue;

        // Collect unmapped bones from direct parent up to mapped ancestor
        TArray<int32, TInlineAllocator<8>> UnmappedChain;
        int32 Walk = Bone.ParentIndex;
        while (Walk != Bone.MappedParentIndex && Walk != INDEX_NONE)
        {
            UnmappedChain.Add(Walk);
            Walk = InitData.SourceBones[Walk].ParentIndex;
        }

        // Compose rotations in parent-to-child order (reverse of walk order)
        FQuat Accumulated = FQuat::Identity;
        for (int32 j = UnmappedChain.Num() - 1; j >= 0; --j)
            Accumulated = Accumulated * PoseTransforms[UnmappedChain[j]].GetRotation();
        UnmappedPoseRot[i] = Accumulated;
    }

    // Apply pose to each mapped bone
    for (int32 i = 0; i < SourceBoneCount; ++i)
    {
        const FSourceBoneRetargetData& Bone = InitData.SourceBones[i];
        const int32 MeshIndex = Bone.MeshIndex;
        if (MeshIndex == INDEX_NONE)
            continue;

        if (Bone.ParentIndex < 0)
        {
            // Root: translation only. Root rotation and world-position are handled
            // by ApplyRootPose() on the actor, not by the skeleton hierarchy.
            InOutMeshBoneTransforms[MeshIndex].SetTranslation(
                InitData.RootBoneTransform.GetTranslation());
            continue;
        }

        const FTransform& PoseTransform = PoseTransforms[i];
        const FQuat CombinedPoseRotation =
            (UnmappedPoseRot[i] * PoseTransform.GetRotation()).GetNormalized();

        // Conjugate pose rotation into mesh bone's local frame
        const FQuat SourceRotation =
            Bone.MeshToSourceRotation.Inverse() * Bone.InitialPoseRotation *
            CombinedPoseRotation * Bone.MeshToSourceRotation;

        InOutMeshBoneTransforms[MeshIndex].SetRotation(
            (Bone.LocalOrientationCorrection *
             InOutMeshBoneTransforms[MeshIndex].GetRotation() *
             SourceRotation).GetNormalized());

        // Position: bone length alignment + multi-child offset + pose translation
        FVector MeshPosition = InOutMeshBoneTransforms[MeshIndex].GetTranslation();
        MeshPosition *= Bone.BoneLengthRatio;
        MeshPosition += Bone.MultiChildTranslationOffset;

        // Transform pose translation from source-local space into mesh-local space,
        // accounting for the source rest-pose orientation and the parent's corrected world rotation.
        const FQuat ParentMeshToSource = (Bone.MappedParentIndex != INDEX_NONE)
            ? InitData.SourceBones[Bone.MappedParentIndex].MeshToSourceRotation
            : FQuat::Identity;
        const FVector SourcePosition =
            (FTransform(Bone.InitialPoseRotation) *
             FTransform(ParentMeshToSource).Inverse()).TransformVector(
                PoseTransform.GetTranslation());

        InOutMeshBoneTransforms[MeshIndex].SetTranslation(MeshPosition + SourcePosition);
    }
}

TArray<FTransform> ComputeWorldTransforms(
    const TArray<FTransform>& LocalTransforms,
    const TArray<int32>&      ParentIndices)
{
    const int32 N = LocalTransforms.Num();
    TArray<FTransform> World;
    World.SetNum(N);
    for (int32 i = 0; i < N; ++i)
    {
        World[i] = LocalTransforms[i];
        const int32 ParentIdx = ParentIndices[i];
        if (ParentIdx != INDEX_NONE && ParentIdx < N)
            World[i] = World[i] * World[ParentIdx];
    }
    return World;
}

TArray<FVector> ComputeWorldPositions(
    const TArray<FTransform>& LocalTransforms,
    const TArray<int32>&      ParentIndices)
{
    const TArray<FTransform> World = ComputeWorldTransforms(LocalTransforms, ParentIndices);
    TArray<FVector> Positions;
    Positions.SetNum(World.Num());
    for (int32 i = 0; i < World.Num(); ++i)
        Positions[i] = World[i].GetTranslation();
    return Positions;
}

TArray<FTransform> ComputeExpectedTransformsFromSource(
    const RenderStreamLink::FSkeletalLayout& Layout,
    const RenderStreamLink::FSkeletalPose&   Pose)
{
    const int32 N = Layout.joints.Num();
    const TArray<int32> ParentIndices = BuildSourceParentIndices(Layout);

    TArray<FTransform> LocalTransforms;
    LocalTransforms.SetNum(N);
    for (int32 i = 0; i < N; ++i)
    {
        FTransform RestLocal = ConvertD3TransformToUE(Layout.joints[i].transform);

        const auto* PoseJoint = Pose.joints.FindByPredicate(
            [&](const RenderStreamLink::SkeletonJointPose& PJ)
            { return PJ.id == Layout.joints[i].id; });

        if (PoseJoint)
        {
            FQuat PoseDeltaRot = ConvertD3TransformToUE(PoseJoint->transform).GetRotation();
            LocalTransforms[i] = FTransform(
                RestLocal.GetRotation() * PoseDeltaRot,
                RestLocal.GetTranslation(),
                RestLocal.GetScale3D());
        }
        else
        {
            LocalTransforms[i] = RestLocal;
        }
    }

    return ComputeWorldTransforms(LocalTransforms, ParentIndices);
}

TArray<FVector> ComputeExpectedPositionsFromSource(
    const RenderStreamLink::FSkeletalLayout& Layout,
    const RenderStreamLink::FSkeletalPose&   Pose)
{
    const TArray<FTransform> World = ComputeExpectedTransformsFromSource(Layout, Pose);
    TArray<FVector> Positions;
    Positions.SetNum(World.Num());
    for (int32 i = 0; i < World.Num(); ++i)
        Positions[i] = World[i].GetTranslation();
    return Positions;
}

} // namespace RenderStreamRetargeting
