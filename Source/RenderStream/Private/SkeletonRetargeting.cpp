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
    const TMap<FName, FSourceBoneMapping>&   BoneMapping,
    bool                                     bAlignBoneLengths,
    FRetargetInitData&                       OutInitData)
{
    const int32 MeshBoneCount   = MeshBones.Num();
    const int32 SourceBoneCount = Layout.joints.Num();
    OutInitData.MeshBoneCount   = MeshBoneCount;
    OutInitData.RootBoneTransform = FTransform::Identity;
    OutInitData.SourceBones.SetNum(SourceBoneCount);

    // Build source bone topology, mapping, and initial pose
    const TArray<int32> SourceParentIndices = BuildSourceParentIndices(Layout);

    TArray<int32> SourceNumberOfChildren;
    SourceNumberOfChildren.Init(0, SourceBoneCount);
    TArray<int32> MeshToSourceIndex;
    MeshToSourceIndex.Init(INDEX_NONE, MeshBoneCount);
    TArray<FTransform> SourceInitialPose;
    SourceInitialPose.SetNum(SourceBoneCount);

    for (int32 i = 0; i < SourceBoneCount; ++i)
    {
        FSourceBoneRetargetData& Bone = OutInitData.SourceBones[i];
        Bone.ParentIndex = SourceParentIndices[i];
        if (Bone.ParentIndex != INDEX_NONE)
            SourceNumberOfChildren[Bone.ParentIndex]++;

        SourceInitialPose[i] = ConvertD3TransformToUE(Layout.joints[i].transform);
        Bone.InitialPoseRotation = SourceInitialPose[i].GetRotation();

        if (const FSourceBoneMapping* Found = BoneMapping.Find(FName(Layout.jointNames[i])))
        {
            if (Found->MeshIndex != INDEX_NONE)
            {
                MeshToSourceIndex[Found->MeshIndex] = i;
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

    // Pre-compute mesh bone world transforms
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
        int32 Walk = OutInitData.SourceBones[ChildSource].ParentIndex;
        while (Walk != AncestorSource)
        {
            Offset = SourceInitialPose[Walk].GetRotation().RotateVector(Offset)
                + SourceInitialPose[Walk].GetTranslation();
            Walk = OutInitData.SourceBones[Walk].ParentIndex;
        }
        return Offset;
    };

    // Multi-child translation offsets: a single rotation can't align all children
    // of a multi-child parent, so per-child offsets place each at its source position.

    TSet<int32> CorrectedMeshBones;

    for (int32 SourceParent = 0; SourceParent < SourceBoneCount; ++SourceParent)
    {
        if (SourceNumberOfChildren[SourceParent] <= 1)
            continue;
        const int32 ParentMesh = OutInitData.SourceBones[SourceParent].MeshIndex;
        if (ParentMesh == INDEX_NONE)
            continue;

        const FQuat ParentWorldRot = MeshWorldTransforms[ParentMesh].GetRotation();
        const FVector ParentWorldPos = MeshWorldTransforms[ParentMesh].GetTranslation();

        for (int32 Child = 0; Child < SourceBoneCount; ++Child)
        {
            if (OutInitData.SourceBones[Child].ParentIndex != SourceParent)
                continue;
            const int32 ChildMesh = OutInitData.SourceBones[Child].MeshIndex;
            if (ChildMesh == INDEX_NONE)
                continue;
            const int32 MappedParent = OutInitData.SourceBones[Child].MappedParentIndex;
            if (MappedParent == INDEX_NONE)
                continue;

            const FVector SrcOffset = AccumulateSourceOffset(Child, MappedParent);
            const FVector TargetLocal = ParentWorldRot.UnrotateVector(SrcOffset);
            const FVector Correction =
                TargetLocal - MeshBones[ChildMesh].LocalTransform.GetTranslation();

            if (!Correction.IsNearlyZero())
            {
                OutInitData.SourceBones[Child].MultiChildTranslationOffset = Correction;
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

    // Orientation corrections: for each mesh bone mapped to a source bone, compute
    // the world-space orientation difference (WOD) between source and mesh rest poses,
    // then convert to a local correction. Root bones store their root transform instead.
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
            const int32 p = MeshBones[MeshIndex].ParentIndex;
            if (p != INDEX_NONE && p < MeshBoneCount)
            {
                OutInitData.RootBoneTransform = MeshWorldTransforms[p].Inverse();
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

        // Skip correction: preserve parent→child direction from mesh rest-pose
        if (OutInitData.SourceBones[MappedParent].bSkipOrientationCorrection)
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
        const bool bComputeWOD = SourceNumberOfChildren[Bone.ParentIndex] <= 1
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

            OutInitData.SourceBones[MappedParent].LocalOrientationCorrection =
                GrandparentWorldRot.Inverse() * DeltaWOD * GrandparentWorldRot;
        }
        else
        {
            WorldOrientDiff[MeshIndex] = WorldOrientDiff[ParentMeshIndex];
        }
    }

    // MeshToSource rotations from corrected rest-pose: must equal the world rotation
    // that BuildRetargetedPose produces at rest, so that the conjugation
    // MeshToSource^{-1} * P * MeshToSource correctly transforms pose rotations.
    {
        TArray<FQuat> CorrectedRestWorldRot;
        CorrectedRestWorldRot.Init(FQuat::Identity, MeshBoneCount);

        for (int32 m = 0; m < MeshBoneCount; ++m)
        {
            const int32 s = MeshToSourceIndex[m];
            FQuat CorrectedLocalRot = MeshBones[m].LocalTransform.GetRotation();

            if (s != INDEX_NONE && OutInitData.SourceBones[s].ParentIndex >= 0)
                CorrectedLocalRot = OutInitData.SourceBones[s].LocalOrientationCorrection * CorrectedLocalRot;

            const int32 p = MeshBones[m].ParentIndex;
            CorrectedRestWorldRot[m] = (p != INDEX_NONE && p < MeshBoneCount)
                ? CorrectedRestWorldRot[p] * CorrectedLocalRot
                : CorrectedLocalRot;

            if (s != INDEX_NONE)
                OutInitData.SourceBones[s].MeshToSourceRotation = CorrectedRestWorldRot[m];
        }
    }

    // Bone length ratios: ratio = 1 + (source_dist - mesh_dist) / bone_local_length
    if (bAlignBoneLengths)
    {
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
    check(InitData.SourceBones.Num() == SourceBoneCount);

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
            // Root: translation only (rotation/world-position handled by actor)
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
