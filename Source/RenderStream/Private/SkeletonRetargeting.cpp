#include "SkeletonRetargeting.h"

#include "CoreMinimal.h"
#include "Math/UnitConversion.h"

namespace RenderStreamRetargeting
{

FTransform ConvertD3TransformToUE(const RenderStreamLink::Transform& T)
{
    // Standard d3 to Unreal coordinate system transform: d3(x,y,z) -> UE(z,x,y)
    const FVector Pos(
        FUnitConversion::Convert(T.z, EUnit::Meters, EUnit::Centimeters),
        FUnitConversion::Convert(T.x, EUnit::Meters, EUnit::Centimeters),
        FUnitConversion::Convert(T.y, EUnit::Meters, EUnit::Centimeters));
    const FQuat Rotation(T.rz, T.rx, T.ry, T.rw);
    const FTransform JointPoseUE(Rotation, Pos);

    // Unreal skeletons are defined with X sideways rather than Y, so apply 90 degree yaw
    const FTransform ToSkeletonSpace(FQuat::MakeFromRotator(FRotator(0, 90, 0)));
    return ToSkeletonSpace * JointPoseUE * ToSkeletonSpace.Inverse();
}

void InitialiseRetargeting(
    const TArray<FRetargetMeshBone>&         MeshBones,
    const RenderStreamLink::FSkeletalLayout& Layout,
    const TMap<FName, int32>&                SourceNameToMeshIndex,
    const TSet<FName>&                       SkipOrientationCorrectionNames,
    FRetargetInitData&                       OutInitData)
{
    const int32 MeshBoneCount  = MeshBones.Num();
    const int32 SourceBoneCount = Layout.joints.Num();

    OutInitData.MeshBoneCount = MeshBoneCount;

    // Initialise bone info vectors
    OutInitData.SourceParentIndices.Init(INDEX_NONE, SourceBoneCount);
    TArray<int32> SourceNumberOfChildren; SourceNumberOfChildren.Init(0, SourceBoneCount);
    TArray<int32> MeshToSourceIndex;      MeshToSourceIndex.Init(INDEX_NONE, MeshBoneCount);
    OutInitData.SourceToMeshIndex.Init(INDEX_NONE, SourceBoneCount);
    TArray<FTransform> SourceInitialPose; SourceInitialPose.Init(FTransform::Identity, SourceBoneCount);

    // Loop through source layout and find mapping to mesh bones
    for (int32 SourceIndex = 0; SourceIndex < SourceBoneCount; SourceIndex++)
    {
        const RenderStreamLink::SkeletonJointDesc& Joint = Layout.joints[SourceIndex];
        const FName SourceBoneName(Layout.jointNames[SourceIndex]);

        const int32 SourceParentBoneIndex = Layout.joints.IndexOfByPredicate(
            [&Joint](const RenderStreamLink::SkeletonJointDesc& OtherJoint)
            { return OtherJoint.id == Joint.parentId; });

        OutInitData.SourceParentIndices[SourceIndex] = SourceParentBoneIndex;
        SourceInitialPose[SourceIndex] = ConvertD3TransformToUE(Joint.transform);

        int32 MeshIndex = INDEX_NONE;
        if (const int32* Found = SourceNameToMeshIndex.Find(SourceBoneName))
            MeshIndex = *Found;

        if (MeshIndex != INDEX_NONE)
        {
            MeshToSourceIndex[MeshIndex] = SourceIndex;
            OutInitData.SourceToMeshIndex[SourceIndex] = MeshIndex;
        }
        if (SourceParentBoneIndex != INDEX_NONE)
        {
            SourceNumberOfChildren[SourceParentBoneIndex] += 1;
        }
    }

    // Convert skip-correction names to source indices
    OutInitData.SkipOrientationCorrectionSourceIndices.Empty();
    for (int32 SourceIndex = 0; SourceIndex < SourceBoneCount; ++SourceIndex)
    {
        if (SkipOrientationCorrectionNames.Contains(FName(Layout.jointNames[SourceIndex])))
            OutInitData.SkipOrientationCorrectionSourceIndices.Add(SourceIndex);
    }

    // Compute SourceMappedParentIndex: for each source bone, walk up the source
    // parent chain to find the nearest ancestor that is mapped to a mesh bone.
    OutInitData.SourceMappedParentIndex.Init(INDEX_NONE, SourceBoneCount);
    for (int32 SourceIndex = 0; SourceIndex < SourceBoneCount; ++SourceIndex)
    {
        int32 Walk = OutInitData.SourceParentIndices[SourceIndex];
        while (Walk != INDEX_NONE)
        {
            if (OutInitData.SourceToMeshIndex[Walk] != INDEX_NONE)
            {
                OutInitData.SourceMappedParentIndex[SourceIndex] = Walk;
                break;
            }
            Walk = OutInitData.SourceParentIndices[Walk];
        }
    }

    // Initialise persistent per-bone arrays
    OutInitData.MeshToSourceSpaceTransforms.Init(FTransform::Identity, SourceBoneCount);
    OutInitData.LocalInitialOrientationDifferences.Init(FQuat::Identity, SourceBoneCount);
    OutInitData.SourceInitialPoseRotations.Init(FQuat::Identity, SourceBoneCount);

    // Temporary arrays
    TArray<FTransform> MeshBoneWorldTransforms; MeshBoneWorldTransforms.Init(FTransform::Identity, MeshBoneCount);
    TArray<FQuat> WorldInitialOrientationDifferences; WorldInitialOrientationDifferences.Init(FQuat::Identity, MeshBoneCount);

    // Loop over mesh bones (must be in hierarchy order)
    for (int32 MeshIndex = 0; MeshIndex < MeshBoneCount; ++MeshIndex)
    {
        // Calculate world transforms of bones in initial mesh pose
        MeshBoneWorldTransforms[MeshIndex] = MeshBones[MeshIndex].LocalTransform;
        const int32 MeshParentIndex = MeshBones[MeshIndex].ParentIndex;
        if (MeshParentIndex != INDEX_NONE && MeshParentIndex < MeshBoneCount)
        {
            MeshBoneWorldTransforms[MeshIndex] =
                MeshBoneWorldTransforms[MeshIndex] * MeshBoneWorldTransforms[MeshParentIndex];
        }

        // Set initial mesh-to-source space transform
        const int32 SourceIndex = MeshToSourceIndex[MeshIndex];
        if (SourceIndex == INDEX_NONE)
            continue;

        OutInitData.MeshToSourceSpaceTransforms[SourceIndex].SetRotation(MeshBoneWorldTransforms[MeshIndex].GetRotation());
        OutInitData.MeshToSourceSpaceTransforms[SourceIndex].SetScale3D(MeshBoneWorldTransforms[MeshIndex].GetScale3D());

        // Find root bone transform
        if (OutInitData.SourceParentIndices[SourceIndex] < 0 &&
            MeshParentIndex != INDEX_NONE && MeshParentIndex < MeshBoneCount)
        {
            OutInitData.RootBoneTransform = MeshBoneWorldTransforms[MeshParentIndex].Inverse();
            OutInitData.RootBoneTransform.SetScale3D(FVector::OneVector);
        }

        const int32 SourceParentIndex = OutInitData.SourceParentIndices[SourceIndex];
        if (SourceParentIndex == INDEX_NONE)
            continue;

        // Find the nearest mapped source ancestor's mesh index.
        // If the direct source parent is mapped, use it. Otherwise walk up.
        const int32 MappedParentSourceIndex = OutInitData.SourceMappedParentIndex[SourceIndex];
        if (MappedParentSourceIndex == INDEX_NONE)
            continue;

        const int32 ParentMeshIndex = OutInitData.SourceToMeshIndex[MappedParentSourceIndex];
        if (ParentMeshIndex == INDEX_NONE || ParentMeshIndex >= MeshBoneCount)
            continue;

        // Find source initial pose rotation (set for all non-root bones with valid parent)
        const FQuat InitialRotation = SourceInitialPose[SourceIndex].GetRotation();
        OutInitData.SourceInitialPoseRotations[SourceIndex] = InitialRotation;

        // Skip orientation correction when the mapped parent is marked.
        // Ticking bone X means "preserve X→child direction from mesh rest-pose."
        if (OutInitData.SkipOrientationCorrectionSourceIndices.Contains(MappedParentSourceIndex))
        {
            WorldInitialOrientationDifferences[MeshIndex] = WorldInitialOrientationDifferences[ParentMeshIndex];
            OutInitData.MeshToSourceSpaceTransforms[MappedParentSourceIndex].SetRotation(
                WorldInitialOrientationDifferences[MeshIndex] * MeshBoneWorldTransforms[ParentMeshIndex].GetRotation());
            OutInitData.MeshToSourceSpaceTransforms[SourceIndex].SetRotation(
                WorldInitialOrientationDifferences[MeshIndex] * MeshBoneWorldTransforms[MeshIndex].GetRotation());
            continue;
        }

        // Compute the accumulated source-space offset from the mapped parent to this bone,
        // walking through any unmapped intermediate bones.
        FVector SourceInitialOffset = SourceInitialPose[SourceIndex].GetTranslation();
        {
            int32 Walk = SourceParentIndex;
            while (Walk != MappedParentSourceIndex)
            {
                SourceInitialOffset = SourceInitialPose[Walk].GetRotation().RotateVector(SourceInitialOffset)
                    + SourceInitialPose[Walk].GetTranslation();
                Walk = OutInitData.SourceParentIndices[Walk];
            }
        }

        // Find offset between mesh joint and the mapped source parent's mesh bone
        const FVector MeshInitialOffset =
            MeshBoneWorldTransforms[MeshIndex].GetTranslation() -
            MeshBoneWorldTransforms[ParentMeshIndex].GetTranslation();

        // For multi-child check, use the direct source parent
        if (SourceNumberOfChildren[SourceParentIndex] > 1)
        {
            // Multi-child parent: can't rotate parent to satisfy all children.
            // Inherit parent's WOD to keep the chain consistent for downstream
            // single-child processing.
            WorldInitialOrientationDifferences[MeshIndex] = WorldInitialOrientationDifferences[ParentMeshIndex];
            continue;
        }

        // Single-child parent: compute orientation correction
        if (SourceInitialOffset == FVector(0.f, 0.f, 0.f))
        {
            WorldInitialOrientationDifferences[MeshIndex] = WorldInitialOrientationDifferences[ParentMeshIndex];
        }
        else
        {
            WorldInitialOrientationDifferences[MeshIndex] =
                FQuat::FindBetween(MeshInitialOffset, SourceInitialOffset);

            const FQuat ParentGlobalRotation = MeshBoneWorldTransforms[ParentMeshIndex].GetRotation();
            const FQuat ParentLocalRotation  = MeshBones[ParentMeshIndex].LocalTransform.GetRotation();
            const FQuat ParentParentGlobalRotation = ParentGlobalRotation * ParentLocalRotation.Inverse();
            const FQuat OrientationDifferenceDelta =
                WorldInitialOrientationDifferences[ParentMeshIndex].Inverse() *
                WorldInitialOrientationDifferences[MeshIndex];
            OutInitData.LocalInitialOrientationDifferences[MappedParentSourceIndex] =
                ParentParentGlobalRotation.Inverse() * OrientationDifferenceDelta * ParentParentGlobalRotation;

            OutInitData.MeshToSourceSpaceTransforms[MappedParentSourceIndex].SetRotation(
                WorldInitialOrientationDifferences[MeshIndex] * MeshBoneWorldTransforms[ParentMeshIndex].GetRotation());
        }

        OutInitData.MeshToSourceSpaceTransforms[SourceIndex].SetRotation(
            WorldInitialOrientationDifferences[MeshIndex] * MeshBoneWorldTransforms[MeshIndex].GetRotation());
    }
}

void BuildRetargetedPose(
    const RenderStreamLink::FSkeletalPose& Pose,
    const FRetargetInitData&               InitData,
    TArray<FTransform>&                    InOutMeshBoneTransforms)
{
    const int32 SourceBoneCount = Pose.joints.Num();
    check(InitData.SourceToMeshIndex.Num() == SourceBoneCount);

    // For each source bone, compute the accumulated pose rotation from all unmapped
    // ancestors between it and its nearest mapped ancestor. This is composed
    // with the bone's own pose rotation so mapped descendants feel the effect.
    // AccumulatedPoseRotation[i] = product of pose rotations from unmapped bones
    // between SourceMappedParentIndex[i] and i (exclusive of the mapped parent,
    // inclusive of unmapped intermediates, exclusive of i itself).
    TArray<FQuat> AccumulatedUnmappedPoseRotation;
    AccumulatedUnmappedPoseRotation.Init(FQuat::Identity, SourceBoneCount);

    // First pass: convert all pose joints to UE space
    TArray<FQuat> SourcePoseRotations;
    SourcePoseRotations.SetNum(SourceBoneCount);
    for (int32 i = 0; i < SourceBoneCount; ++i)
        SourcePoseRotations[i] = ConvertD3TransformToUE(Pose.joints[i].transform).GetRotation();

    // Second pass: for each mapped bone, walk from its direct source parent up to the
    // mapped ancestor, accumulating unmapped pose rotations.
    for (int32 SourceIndex = 0; SourceIndex < SourceBoneCount; ++SourceIndex)
    {
        if (InitData.SourceToMeshIndex[SourceIndex] == INDEX_NONE)
            continue; // only compute for mapped bones

        const int32 MappedParent = InitData.SourceMappedParentIndex[SourceIndex];
        if (MappedParent == INDEX_NONE)
            continue;

        // Walk from direct parent up to (but not including) the mapped ancestor,
        // collecting unmapped bone indices.
        TArray<int32, TInlineAllocator<8>> UnmappedChain;
        int32 Walk = InitData.SourceParentIndices[SourceIndex];
        while (Walk != MappedParent && Walk != INDEX_NONE)
        {
            UnmappedChain.Add(Walk);
            Walk = InitData.SourceParentIndices[Walk];
        }

        // Compose rotations from mapped parent downward (reverse order)
        FQuat Accumulated = FQuat::Identity;
        for (int32 j = UnmappedChain.Num() - 1; j >= 0; --j)
        {
            const int32 UnmappedIdx = UnmappedChain[j];
            Accumulated = Accumulated * SourcePoseRotations[UnmappedIdx];
        }
        AccumulatedUnmappedPoseRotation[SourceIndex] = Accumulated;
    }

    for (int32 SourceIndex = 0; SourceIndex < SourceBoneCount; SourceIndex++)
    {
        const int32 MeshIndex = InitData.SourceToMeshIndex[SourceIndex];
        if (MeshIndex == INDEX_NONE)
            continue;

        const RenderStreamLink::SkeletonJointPose& Joint = Pose.joints[SourceIndex];

        if (InitData.SourceParentIndices[SourceIndex] < 0) // root bone
        {
            // Root pose is applied directly to the SkeletalMeshActor transform
            InOutMeshBoneTransforms[MeshIndex].SetTranslation(InitData.RootBoneTransform.GetTranslation());
        }
        else
        {
            const FTransform SourceBoneTransform = ConvertD3TransformToUE(Joint.transform);

            // Compose this bone's pose rotation with accumulated unmapped ancestor rotations
            const FQuat CombinedPoseRotation =
                (AccumulatedUnmappedPoseRotation[SourceIndex] * SourceBoneTransform.GetRotation()).GetNormalized();

            {
                // Apply rotation with orientation correction
                const FQuat& MeshToSource = InitData.MeshToSourceSpaceTransforms[SourceIndex].GetRotation();
                const FQuat SourceRotation =
                    MeshToSource.Inverse() * InitData.SourceInitialPoseRotations[SourceIndex] *
                    CombinedPoseRotation * MeshToSource;
                const FQuat MeshRotation = InOutMeshBoneTransforms[MeshIndex].GetRotation();
                const FQuat& InitialOrientationOffset = InitData.LocalInitialOrientationDifferences[SourceIndex];
                InOutMeshBoneTransforms[MeshIndex].SetRotation(
                    (InitialOrientationOffset * MeshRotation * SourceRotation).GetNormalized());

                // Apply position
                const FVector MeshPosition = InOutMeshBoneTransforms[MeshIndex].GetTranslation();
                const int32 MappedParentSourceIndex = InitData.SourceMappedParentIndex[SourceIndex];
                const FQuat ParentMeshToSource = (MappedParentSourceIndex != INDEX_NONE)
                    ? InitData.MeshToSourceSpaceTransforms[MappedParentSourceIndex].GetRotation()
                    : FQuat::Identity;
                const FTransform SourceInitialTransform(InitData.SourceInitialPoseRotations[SourceIndex]);
                const FVector SourcePosition =
                    (SourceInitialTransform * FTransform(ParentMeshToSource).Inverse()).TransformVector(
                        SourceBoneTransform.GetTranslation());
                InOutMeshBoneTransforms[MeshIndex].SetTranslation(MeshPosition + SourcePosition);
            }
        }
    }
}

TArray<FTransform> ComputeWorldTransforms(
    const TArray<FTransform>& LocalTransforms,
    const TArray<int32>&      ParentIndices)
{
    const int32 N = LocalTransforms.Num();
    TArray<FTransform> WorldTransforms;
    WorldTransforms.SetNum(N);

    for (int32 i = 0; i < N; ++i)
    {
        WorldTransforms[i] = LocalTransforms[i];
        const int32 ParentIdx = ParentIndices[i];
        if (ParentIdx != INDEX_NONE && ParentIdx < N)
            WorldTransforms[i] = WorldTransforms[i] * WorldTransforms[ParentIdx];
    }
    return WorldTransforms;
}

TArray<FVector> ComputeWorldPositions(
    const TArray<FTransform>& LocalTransforms,
    const TArray<int32>&      ParentIndices)
{
    const TArray<FTransform> WorldTransforms = ComputeWorldTransforms(LocalTransforms, ParentIndices);

    TArray<FVector> Positions;
    Positions.SetNum(WorldTransforms.Num());
    for (int32 i = 0; i < WorldTransforms.Num(); ++i)
        Positions[i] = WorldTransforms[i].GetTranslation();
    return Positions;
}

TArray<FTransform> ComputeExpectedTransformsFromSource(
    const RenderStreamLink::FSkeletalLayout& Layout,
    const RenderStreamLink::FSkeletalPose&   Pose)
{
    const int32 N = Layout.joints.Num();

    // Build parent index array
    TArray<int32> ParentIndices;
    ParentIndices.SetNum(N);
    for (int32 i = 0; i < N; ++i)
    {
        const RenderStreamLink::SkeletonJointDesc& Joint = Layout.joints[i];
        ParentIndices[i] = Layout.joints.IndexOfByPredicate(
            [&Joint](const RenderStreamLink::SkeletonJointDesc& OtherJoint)
            { return OtherJoint.id == Joint.parentId; });
    }

    // Convert layout rest-pose local transforms to UE space, applying pose delta rotations
    TArray<FTransform> LocalTransforms;
    LocalTransforms.SetNum(N);
    for (int32 i = 0; i < N; ++i)
    {
        FTransform RestLocal = ConvertD3TransformToUE(Layout.joints[i].transform);

        const RenderStreamLink::SkeletonJointPose* PoseJoint = Pose.joints.FindByPredicate(
            [&](const RenderStreamLink::SkeletonJointPose& PJ) { return PJ.id == Layout.joints[i].id; });

        if (PoseJoint)
        {
            FTransform PoseDelta = ConvertD3TransformToUE(PoseJoint->transform);
            FQuat CombinedRotation = RestLocal.GetRotation() * PoseDelta.GetRotation();
            LocalTransforms[i] = FTransform(CombinedRotation, RestLocal.GetTranslation(), RestLocal.GetScale3D());
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
    const TArray<FTransform> WorldTransforms = ComputeExpectedTransformsFromSource(Layout, Pose);

    TArray<FVector> Positions;
    Positions.SetNum(WorldTransforms.Num());
    for (int32 i = 0; i < WorldTransforms.Num(); ++i)
        Positions[i] = WorldTransforms[i].GetTranslation();
    return Positions;
}

} // namespace RenderStreamRetargeting
