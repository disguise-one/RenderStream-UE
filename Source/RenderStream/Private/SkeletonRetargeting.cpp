#include "SkeletonRetargeting.h"

#include "CoreMinimal.h"
#include "Math/UnitConversion.h"

namespace RenderStreamRetargeting
{

// Build parent index array for a source layout by matching joint IDs.
static TArray<int32> BuildSourceParentIndices(const RenderStreamLink::FSkeletalLayout& Layout)
{
    const int32 N = Layout.joints.Num();
    TArray<int32> Parents;
    Parents.SetNum(N);
    for (int32 i = 0; i < N; ++i)
    {
        const auto& Joint = Layout.joints[i];
        Parents[i] = Layout.joints.IndexOfByPredicate(
            [&Joint](const RenderStreamLink::SkeletonJointDesc& Other)
            { return Other.id == Joint.parentId; });
    }
    return Parents;
}

FTransform ConvertD3TransformToUE(const RenderStreamLink::Transform& T)
{
    // d3(x,y,z) -> UE(z,x,y) in centimeters
    const FVector Pos(
        FUnitConversion::Convert(T.z, EUnit::Meters, EUnit::Centimeters),
        FUnitConversion::Convert(T.x, EUnit::Meters, EUnit::Centimeters),
        FUnitConversion::Convert(T.y, EUnit::Meters, EUnit::Centimeters));
    const FQuat Rotation(T.rz, T.rx, T.ry, T.rw);

    // Apply 90° yaw to align d3 Y-forward with UE X-forward
    const FTransform ToSkeletonSpace(FQuat::MakeFromRotator(FRotator(0, 90, 0)));
    return ToSkeletonSpace * FTransform(Rotation, Pos) * ToSkeletonSpace.Inverse();
}

void InitialiseRetargeting(
    const TArray<FRetargetMeshBone>&         MeshBones,
    const RenderStreamLink::FSkeletalLayout& Layout,
    const TMap<FName, int32>&                SourceNameToMeshIndex,
    const TSet<FName>&                       SkipOrientationCorrectionNames,
    bool                                     bAlignBoneLengths,
    FRetargetInitData&                       OutInitData)
{
    const int32 MeshBoneCount   = MeshBones.Num();
    const int32 SourceBoneCount = Layout.joints.Num();
    OutInitData.MeshBoneCount   = MeshBoneCount;

    // --- Phase 1: Build bone mappings ---

    OutInitData.SourceParentIndices = BuildSourceParentIndices(Layout);

    TArray<int32> SourceNumberOfChildren;
    SourceNumberOfChildren.Init(0, SourceBoneCount);
    for (int32 i = 0; i < SourceBoneCount; ++i)
        if (OutInitData.SourceParentIndices[i] != INDEX_NONE)
            SourceNumberOfChildren[OutInitData.SourceParentIndices[i]]++;

    TArray<int32> MeshToSourceIndex;
    MeshToSourceIndex.Init(INDEX_NONE, MeshBoneCount);
    OutInitData.SourceToMeshIndex.Init(INDEX_NONE, SourceBoneCount);

    TArray<FTransform> SourceInitialPose;
    SourceInitialPose.SetNum(SourceBoneCount);
    for (int32 i = 0; i < SourceBoneCount; ++i)
    {
        SourceInitialPose[i] = ConvertD3TransformToUE(Layout.joints[i].transform);
        if (const int32* Found = SourceNameToMeshIndex.Find(FName(Layout.jointNames[i])))
        {
            MeshToSourceIndex[*Found] = i;
            OutInitData.SourceToMeshIndex[i] = *Found;
        }
    }

    OutInitData.SkipOrientationCorrectionSourceIndices.Empty();
    for (int32 i = 0; i < SourceBoneCount; ++i)
        if (SkipOrientationCorrectionNames.Contains(FName(Layout.jointNames[i])))
            OutInitData.SkipOrientationCorrectionSourceIndices.Add(i);

    // Nearest mapped ancestor for each source bone
    OutInitData.SourceMappedParentIndex.Init(INDEX_NONE, SourceBoneCount);
    for (int32 i = 0; i < SourceBoneCount; ++i)
    {
        int32 Walk = OutInitData.SourceParentIndices[i];
        while (Walk != INDEX_NONE)
        {
            if (OutInitData.SourceToMeshIndex[Walk] != INDEX_NONE)
            {
                OutInitData.SourceMappedParentIndex[i] = Walk;
                break;
            }
            Walk = OutInitData.SourceParentIndices[Walk];
        }
    }

    // --- Phase 2: Pre-compute mesh world transforms ---

    OutInitData.MeshToSourceSpaceTransforms.Init(FTransform::Identity, SourceBoneCount);
    OutInitData.LocalInitialOrientationDifferences.Init(FQuat::Identity, SourceBoneCount);
    OutInitData.SourceInitialPoseRotations.Init(FQuat::Identity, SourceBoneCount);
    OutInitData.RootBoneTransform = FTransform::Identity;

    TArray<FTransform> MeshWorldTransforms;
    MeshWorldTransforms.SetNum(MeshBoneCount);
    for (int32 i = 0; i < MeshBoneCount; ++i)
    {
        MeshWorldTransforms[i] = MeshBones[i].LocalTransform;
        const int32 p = MeshBones[i].ParentIndex;
        if (p != INDEX_NONE && p < MeshBoneCount)
            MeshWorldTransforms[i] = MeshWorldTransforms[i] * MeshWorldTransforms[p];
    }

    // Accumulate source-space offset from a bone up to a mapped ancestor,
    // walking through unmapped intermediate bones.
    auto AccumulateSourceOffset = [&](int32 ChildSource, int32 AncestorSource) -> FVector
    {
        FVector Offset = SourceInitialPose[ChildSource].GetTranslation();
        int32 Walk = OutInitData.SourceParentIndices[ChildSource];
        while (Walk != AncestorSource)
        {
            Offset = SourceInitialPose[Walk].GetRotation().RotateVector(Offset)
                + SourceInitialPose[Walk].GetTranslation();
            Walk = OutInitData.SourceParentIndices[Walk];
        }
        return Offset;
    };

    // --- Phase 3: Multi-child translation offsets ---
    // A single rotation can't align all children of a multi-child parent, so
    // per-child translation offsets place each child at its source rest position.

    OutInitData.MultiChildTranslationOffsets.Init(FVector::ZeroVector, SourceBoneCount);
    TSet<int32> CorrectedMeshBones;

    for (int32 SourceParent = 0; SourceParent < SourceBoneCount; ++SourceParent)
    {
        if (SourceNumberOfChildren[SourceParent] <= 1)
            continue;
        const int32 ParentMesh = OutInitData.SourceToMeshIndex[SourceParent];
        if (ParentMesh == INDEX_NONE)
            continue;

        const FQuat ParentWorldRot = MeshWorldTransforms[ParentMesh].GetRotation();
        const FVector ParentWorldPos = MeshWorldTransforms[ParentMesh].GetTranslation();

        for (int32 Child = 0; Child < SourceBoneCount; ++Child)
        {
            if (OutInitData.SourceParentIndices[Child] != SourceParent)
                continue;
            const int32 ChildMesh = OutInitData.SourceToMeshIndex[Child];
            if (ChildMesh == INDEX_NONE)
                continue;
            const int32 MappedParent = OutInitData.SourceMappedParentIndex[Child];
            if (MappedParent == INDEX_NONE)
                continue;

            const FVector SrcOffset = AccumulateSourceOffset(Child, MappedParent);
            const FVector TargetLocal = ParentWorldRot.UnrotateVector(SrcOffset);
            const FVector Correction =
                TargetLocal - MeshBones[ChildMesh].LocalTransform.GetTranslation();

            if (!Correction.IsNearlyZero())
            {
                OutInitData.MultiChildTranslationOffsets[Child] = Correction;
                MeshWorldTransforms[ChildMesh].SetTranslation(ParentWorldPos + SrcOffset);
                CorrectedMeshBones.Add(ChildMesh);
            }
        }
    }

    // Recompute world transforms for descendants of corrected bones
    for (int32 m = 0; m < MeshBoneCount; ++m)
    {
        if (CorrectedMeshBones.Contains(m))
            continue;
        const int32 p = MeshBones[m].ParentIndex;
        if (p != INDEX_NONE && p < MeshBoneCount)
            MeshWorldTransforms[m] = MeshBones[m].LocalTransform * MeshWorldTransforms[p];
    }

    // --- Phase 4: Find root bone transform ---

    for (int32 m = 0; m < MeshBoneCount; ++m)
    {
        const int32 s = MeshToSourceIndex[m];
        if (s == INDEX_NONE || OutInitData.SourceParentIndices[s] >= 0)
            continue;
        const int32 p = MeshBones[m].ParentIndex;
        if (p != INDEX_NONE && p < MeshBoneCount)
        {
            OutInitData.RootBoneTransform = MeshWorldTransforms[p].Inverse();
            OutInitData.RootBoneTransform.SetScale3D(FVector::OneVector);
        }
        break;
    }

    // --- Phase 5: Orientation corrections ---
    // For each bone, compute the world-space orientation difference (WOD)
    // between source and mesh rest poses, then convert to a local correction.

    TArray<FQuat> WorldOrientDiff;
    WorldOrientDiff.Init(FQuat::Identity, MeshBoneCount);

    for (int32 MeshIndex = 0; MeshIndex < MeshBoneCount; ++MeshIndex)
    {
        const int32 SourceIndex = MeshToSourceIndex[MeshIndex];
        if (SourceIndex == INDEX_NONE)
            continue;

        const int32 SourceParentIndex = OutInitData.SourceParentIndices[SourceIndex];
        if (SourceParentIndex == INDEX_NONE)
            continue;

        const int32 MappedParent = OutInitData.SourceMappedParentIndex[SourceIndex];
        if (MappedParent == INDEX_NONE)
            continue;

        const int32 ParentMeshIndex = OutInitData.SourceToMeshIndex[MappedParent];
        if (ParentMeshIndex == INDEX_NONE || ParentMeshIndex >= MeshBoneCount)
            continue;

        OutInitData.SourceInitialPoseRotations[SourceIndex] =
            SourceInitialPose[SourceIndex].GetRotation();

        // Skip correction: preserve parent→child direction from mesh rest-pose
        if (OutInitData.SkipOrientationCorrectionSourceIndices.Contains(MappedParent))
        {
            WorldOrientDiff[MeshIndex] = WorldOrientDiff[ParentMeshIndex];
            continue;
        }

        const FVector SourceOffset = AccumulateSourceOffset(SourceIndex, MappedParent);
        const FVector MeshOffset =
            MeshWorldTransforms[MeshIndex].GetTranslation() -
            MeshWorldTransforms[ParentMeshIndex].GetTranslation();

        // Single-child parent with non-zero offset: compute exact correction.
        // Multi-child parents and zero-offset bones: inherit from parent.
        const bool bComputeWOD = SourceNumberOfChildren[SourceParentIndex] <= 1
            && SourceOffset != FVector::ZeroVector;

        if (bComputeWOD)
        {
            WorldOrientDiff[MeshIndex] = FQuat::FindBetween(MeshOffset, SourceOffset);

            // Convert world-space delta to parent's local space
            const FQuat ParentWorldRot = MeshWorldTransforms[ParentMeshIndex].GetRotation();
            const FQuat ParentLocalRot = MeshBones[ParentMeshIndex].LocalTransform.GetRotation();
            const FQuat GrandparentWorldRot = ParentWorldRot * ParentLocalRot.Inverse();
            const FQuat DeltaWOD =
                WorldOrientDiff[ParentMeshIndex].Inverse() * WorldOrientDiff[MeshIndex];

            OutInitData.LocalInitialOrientationDifferences[MappedParent] =
                GrandparentWorldRot.Inverse() * DeltaWOD * GrandparentWorldRot;
        }
        else
        {
            WorldOrientDiff[MeshIndex] = WorldOrientDiff[ParentMeshIndex];
        }
    }

    // --- Phase 6: MeshToSource rotations from corrected rest-pose ---
    // MeshToSource must equal the world rotation that BuildRetargetedPose
    // produces at rest, so that the conjugation MeshToSource^{-1} * P * MeshToSource
    // correctly transforms pose rotations into each bone's local frame.
    {
        TArray<FQuat> CorrectedRestWorldRot;
        CorrectedRestWorldRot.Init(FQuat::Identity, MeshBoneCount);

        for (int32 m = 0; m < MeshBoneCount; ++m)
        {
            const int32 s = MeshToSourceIndex[m];
            FQuat CorrectedLocalRot = MeshBones[m].LocalTransform.GetRotation();

            if (s != INDEX_NONE && OutInitData.SourceParentIndices[s] >= 0)
                CorrectedLocalRot = OutInitData.LocalInitialOrientationDifferences[s] * CorrectedLocalRot;

            const int32 p = MeshBones[m].ParentIndex;
            CorrectedRestWorldRot[m] = (p != INDEX_NONE && p < MeshBoneCount)
                ? CorrectedRestWorldRot[p] * CorrectedLocalRot
                : CorrectedLocalRot;

            if (s != INDEX_NONE)
                OutInitData.MeshToSourceSpaceTransforms[s].SetRotation(CorrectedRestWorldRot[m]);
        }
    }

    // --- Phase 7: Bone length ratios ---
    // ratio = 1 + (source_dist - mesh_dist) / bone_local_length
    OutInitData.BoneLengthRatios.Init(1.0f, SourceBoneCount);
    if (bAlignBoneLengths)
    {
        const TArray<FTransform> SourceWorldTransforms = ComputeWorldTransforms(
            SourceInitialPose, OutInitData.SourceParentIndices);

        for (int32 i = 0; i < SourceBoneCount; ++i)
        {
            const int32 MeshIndex = OutInitData.SourceToMeshIndex[i];
            if (MeshIndex == INDEX_NONE)
                continue;

            const int32 MappedParent = OutInitData.SourceMappedParentIndex[i];
            if (MappedParent == INDEX_NONE)
                continue;

            // Skip: preserves both direction and length from mesh rest-pose
            if (OutInitData.SkipOrientationCorrectionSourceIndices.Contains(MappedParent))
                continue;

            const int32 ParentMeshIndex = OutInitData.SourceToMeshIndex[MappedParent];
            if (ParentMeshIndex == INDEX_NONE)
                continue;

            const float SourceLength = FVector::Dist(
                SourceWorldTransforms[i].GetTranslation(),
                SourceWorldTransforms[MappedParent].GetTranslation());
            const float MeshLength = FVector::Dist(
                MeshWorldTransforms[MeshIndex].GetTranslation(),
                MeshWorldTransforms[ParentMeshIndex].GetTranslation());
            const float BoneLocalLength =
                MeshBones[MeshIndex].LocalTransform.GetTranslation().Size();

            if (BoneLocalLength > KINDA_SMALL_NUMBER)
                OutInitData.BoneLengthRatios[i] =
                    1.0f + (SourceLength - MeshLength) / BoneLocalLength;
        }
    }
}

void BuildRetargetedPose(
    const RenderStreamLink::FSkeletalPose& Pose,
    const FRetargetInitData&               InitData,
    TArray<FTransform>&                    InOutMeshBoneTransforms)
{
    const int32 SourceBoneCount = Pose.joints.Num();
    check(InitData.SourceToMeshIndex.Num() == SourceBoneCount);

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
        if (InitData.SourceToMeshIndex[i] == INDEX_NONE)
            continue;
        const int32 MappedParent = InitData.SourceMappedParentIndex[i];
        if (MappedParent == INDEX_NONE)
            continue;

        // Collect unmapped bones from direct parent up to mapped ancestor
        TArray<int32, TInlineAllocator<8>> UnmappedChain;
        int32 Walk = InitData.SourceParentIndices[i];
        while (Walk != MappedParent && Walk != INDEX_NONE)
        {
            UnmappedChain.Add(Walk);
            Walk = InitData.SourceParentIndices[Walk];
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
        const int32 MeshIndex = InitData.SourceToMeshIndex[i];
        if (MeshIndex == INDEX_NONE)
            continue;

        if (InitData.SourceParentIndices[i] < 0)
        {
            // Root: translation only (rotation/world-position handled by actor)
            InOutMeshBoneTransforms[MeshIndex].SetTranslation(
                InitData.RootBoneTransform.GetTranslation());
            continue;
        }

        const FTransform& PoseTransform = PoseTransforms[i];
        const FQuat CombinedPoseRotation =
            (UnmappedPoseRot[i] * PoseTransform.GetRotation()).GetNormalized();

        // Conjugate pose rotation into mesh bone's local frame
        const FQuat& MeshToSource = InitData.MeshToSourceSpaceTransforms[i].GetRotation();
        const FQuat SourceRotation =
            MeshToSource.Inverse() * InitData.SourceInitialPoseRotations[i] *
            CombinedPoseRotation * MeshToSource;

        InOutMeshBoneTransforms[MeshIndex].SetRotation(
            (InitData.LocalInitialOrientationDifferences[i] *
             InOutMeshBoneTransforms[MeshIndex].GetRotation() *
             SourceRotation).GetNormalized());

        // Position: bone length alignment + multi-child offset + pose translation
        FVector MeshPosition = InOutMeshBoneTransforms[MeshIndex].GetTranslation();
        MeshPosition *= InitData.BoneLengthRatios[i];
        MeshPosition += InitData.MultiChildTranslationOffsets[i];

        const int32 MappedParent = InitData.SourceMappedParentIndex[i];
        const FQuat ParentMeshToSource = (MappedParent != INDEX_NONE)
            ? InitData.MeshToSourceSpaceTransforms[MappedParent].GetRotation()
            : FQuat::Identity;
        const FVector SourcePosition =
            (FTransform(InitData.SourceInitialPoseRotations[i]) *
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
        const int32 p = ParentIndices[i];
        if (p != INDEX_NONE && p < N)
            World[i] = World[i] * World[p];
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
