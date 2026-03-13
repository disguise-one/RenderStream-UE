#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "SkeletonRetargeting.h"

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

static TArray<FRetargetMeshBone> BuildMeshBones(
    const TArray<FVector>& UEOffsets,
    const TArray<int32>&   ParentIndices)
{
    TArray<FRetargetMeshBone> Result;
    Result.SetNum(UEOffsets.Num());
    for (int32 i = 0; i < UEOffsets.Num(); ++i)
    {
        Result[i].LocalTransform = FTransform(FQuat::Identity, UEOffsets[i]);
        Result[i].ParentIndex    = ParentIndices[i];
    }
    return Result;
}

// Builds a layout where joint[i].id = i+1 and parentId = parent_index+1 (or 0 for root).
static RenderStreamLink::FSkeletalLayout BuildD3Layout(
    const TArray<FString>&                     Names,
    const TArray<RenderStreamLink::Transform>& D3Transforms,
    const TArray<int32>&                       ParentIndices)
{
    RenderStreamLink::FSkeletalLayout Layout;
    Layout.version = 1;
    const int32 N = Names.Num();
    Layout.jointNames.SetNum(N);
    Layout.joints.SetNum(N);
    for (int32 i = 0; i < N; ++i)
    {
        Layout.jointNames[i]       = Names[i];
        Layout.joints[i].id        = static_cast<uint64_t>(i + 1);
        Layout.joints[i].transform = D3Transforms[i];
        Layout.joints[i].parentId  = (ParentIndices[i] == INDEX_NONE)
            ? 0
            : static_cast<uint64_t>(ParentIndices[i] + 1);
    }
    return Layout;
}

static RenderStreamLink::FSkeletalPose BuildD3IdentityPose(
    const RenderStreamLink::FSkeletalLayout& Layout)
{
    RenderStreamLink::FSkeletalPose Pose;
    Pose.layoutId        = 0;
    Pose.layoutVersion   = Layout.version;
    Pose.rootPosition    = FVector3f::ZeroVector;
    Pose.rootOrientation = FQuat4f::Identity;
    const int32 N = Layout.joints.Num();
    Pose.joints.SetNum(N);
    for (int32 i = 0; i < N; ++i)
    {
        Pose.joints[i].id        = Layout.joints[i].id;
        Pose.joints[i].transform = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f};
    }
    return Pose;
}

// 1:1 mapping: source bone at index i (named Names[i]) -> mesh bone index i
static TMap<FName, int32> BuildIdentityNameMap(const TArray<FString>& Names)
{
    TMap<FName, int32> Result;
    for (int32 i = 0; i < Names.Num(); ++i)
        Result.Add(FName(*Names[i]), i);
    return Result;
}

// Find which indices are multi-child parents
static TSet<int32> FindMultiChildParents(const TArray<int32>& ParentIndices)
{
    TMap<int32, int32> ChildCount;
    for (int32 i = 0; i < ParentIndices.Num(); ++i)
    {
        if (ParentIndices[i] != INDEX_NONE)
            ChildCount.FindOrAdd(ParentIndices[i], 0)++;
    }
    TSet<int32> Result;
    for (const auto& Pair : ChildCount)
        if (Pair.Value > 1)
            Result.Add(Pair.Key);
    return Result;
}

static void CheckPositions(
    FAutomationTestBase*   Test,
    const TArray<FVector>& Actual,
    const TArray<FVector>& Expected,
    float                  ToleranceCm,
    const FString&         Context)
{
    Test->TestEqual(Context + TEXT(" bone count"), Actual.Num(), Expected.Num());
    const int32 N = FMath::Min(Actual.Num(), Expected.Num());
    for (int32 i = 0; i < N; ++i)
    {
        const float Dist = FVector::Dist(Actual[i], Expected[i]);
        Test->TestTrue(
            FString::Printf(TEXT("%s bone[%d] within %.2f cm (dist=%.4f cm)"),
                *Context, i, ToleranceCm, Dist),
            Dist <= ToleranceCm);
    }
}

// Check bone directions (parent-to-child normalised vectors).
// Skips root bones and children of multi-child parents.
static void CheckBoneDirections(
    FAutomationTestBase*   Test,
    const TArray<FVector>& ActualPositions,
    const TArray<FVector>& ExpectedPositions,
    const TArray<int32>&   ParentIndices,
    const TSet<int32>&     MultiChildParents,
    float                  AngleToleranceDeg,
    const FString&         Context)
{
    Test->TestEqual(Context + TEXT(" bone count"), ActualPositions.Num(), ExpectedPositions.Num());
    const int32 N = FMath::Min(ActualPositions.Num(), ExpectedPositions.Num());
    for (int32 i = 0; i < N; ++i)
    {
        const int32 ParentIdx = ParentIndices[i];
        if (ParentIdx == INDEX_NONE)
            continue; // skip root
        if (MultiChildParents.Contains(ParentIdx))
            continue; // skip children of multi-child parents

        const FVector ActualDir   = (ActualPositions[i]   - ActualPositions[ParentIdx]).GetSafeNormal();
        const FVector ExpectedDir = (ExpectedPositions[i] - ExpectedPositions[ParentIdx]).GetSafeNormal();

        if (ActualDir.IsNearlyZero() || ExpectedDir.IsNearlyZero())
            continue; // skip zero-length bones

        const float Dot      = FMath::Clamp(FVector::DotProduct(ActualDir, ExpectedDir), -1.f, 1.f);
        const float AngleDeg = FMath::RadiansToDegrees(FMath::Acos(Dot));
        Test->TestTrue(
            FString::Printf(TEXT("%s bone[%d] direction within %.1f deg (angle=%.2f deg)"),
                *Context, i, AngleToleranceDeg, AngleDeg),
            AngleDeg <= AngleToleranceDeg);
    }
}

// Run full retargeting pipeline; returns actual world positions of mesh bones.
static TArray<FVector> RunRetargeting(
    const TArray<FRetargetMeshBone>&         MeshBones,
    const RenderStreamLink::FSkeletalLayout& Layout,
    const TMap<FName, int32>&                NameMap,
    const RenderStreamLink::FSkeletalPose&   Pose,
    const TSet<FName>&                       SkipCorrectionNames = TSet<FName>(),
    bool                                     bAlignBoneLengths = false)
{
    using namespace RenderStreamRetargeting;

    FRetargetInitData InitData;
    InitialiseRetargeting(MeshBones, Layout, NameMap, SkipCorrectionNames, bAlignBoneLengths, InitData);

    TArray<FTransform> BoneTransforms;
    BoneTransforms.SetNum(MeshBones.Num());
    for (int32 i = 0; i < MeshBones.Num(); ++i)
        BoneTransforms[i] = MeshBones[i].LocalTransform;

    BuildRetargetedPose(Pose, InitData, BoneTransforms);

    TArray<int32> MeshParentIndices;
    MeshParentIndices.SetNum(MeshBones.Num());
    for (int32 i = 0; i < MeshBones.Num(); ++i)
        MeshParentIndices[i] = MeshBones[i].ParentIndex;

    return ComputeWorldPositions(BoneTransforms, MeshParentIndices);
}

// Run full retargeting pipeline; returns actual world TRANSFORMS of mesh bones.
static TArray<FTransform> RunRetargetingTransforms(
    const TArray<FRetargetMeshBone>&         MeshBones,
    const RenderStreamLink::FSkeletalLayout& Layout,
    const TMap<FName, int32>&                NameMap,
    const RenderStreamLink::FSkeletalPose&   Pose,
    const TSet<FName>&                       SkipCorrectionNames = TSet<FName>(),
    bool                                     bAlignBoneLengths = false)
{
    using namespace RenderStreamRetargeting;

    FRetargetInitData InitData;
    InitialiseRetargeting(MeshBones, Layout, NameMap, SkipCorrectionNames, bAlignBoneLengths, InitData);

    TArray<FTransform> BoneTransforms;
    BoneTransforms.SetNum(MeshBones.Num());
    for (int32 i = 0; i < MeshBones.Num(); ++i)
        BoneTransforms[i] = MeshBones[i].LocalTransform;

    BuildRetargetedPose(Pose, InitData, BoneTransforms);

    TArray<int32> MeshParentIndices;
    MeshParentIndices.SetNum(MeshBones.Num());
    for (int32 i = 0; i < MeshBones.Num(); ++i)
        MeshParentIndices[i] = MeshBones[i].ParentIndex;

    return ComputeWorldTransforms(BoneTransforms, MeshParentIndices);
}

// Helper: build mesh UE offsets from d3 transforms
static TArray<FVector> D3ToUEOffsets(const TArray<RenderStreamLink::Transform>& D3T)
{
    TArray<FVector> Offsets;
    Offsets.SetNum(D3T.Num());
    for (int32 i = 0; i < D3T.Num(); ++i)
        Offsets[i] = RenderStreamRetargeting::ConvertD3TransformToUE(D3T[i]).GetTranslation();
    return Offsets;
}

// ---------------------------------------------------------------------------
// Test 1 — IdentityPose
// 4-bone collinear chain. Source layout == mesh layout. Identity pose.
// Source == mesh: positions should match exactly.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_SkeletonRetargeting_IdentityPose,
    "RenderStream.SkeletonRetargeting.IdentityPose",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTest_SkeletonRetargeting_IdentityPose::RunTest(const FString& Parameters)
{
    using namespace RenderStreamRetargeting;

    const TArray<FString> Names   = {"Pelvis", "Spine", "Chest", "Neck"};
    const TArray<int32>   Parents = {INDEX_NONE, 0, 1, 2};

    const RenderStreamLink::Transform D3Root   = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f};
    const RenderStreamLink::Transform D3Offset = {0.1f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f};
    const TArray<RenderStreamLink::Transform> D3T = {D3Root, D3Offset, D3Offset, D3Offset};

    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(Names, D3T, Parents);
    const RenderStreamLink::FSkeletalPose   Pose   = BuildD3IdentityPose(Layout);

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(D3ToUEOffsets(D3T), Parents);
    const TMap<FName, int32>        NameMap   = BuildIdentityNameMap(Names);

    const TArray<FVector> Actual   = RunRetargeting(MeshBones, Layout, NameMap, Pose);
    const TArray<FVector> Expected = ComputeExpectedPositionsFromSource(Layout, Pose);

    CheckPositions(this, Actual, Expected, 0.1f, TEXT("IdentityPose"));
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 — SimpleRotation
// 4-bone collinear chain. Source == mesh layout. Spine rotated 90 deg around d3 Z.
// Source == mesh: positions should match.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_SkeletonRetargeting_SimpleRotation,
    "RenderStream.SkeletonRetargeting.SimpleRotation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTest_SkeletonRetargeting_SimpleRotation::RunTest(const FString& Parameters)
{
    using namespace RenderStreamRetargeting;

    const TArray<FString> Names   = {"Pelvis", "Spine", "Chest", "Neck"};
    const TArray<int32>   Parents = {INDEX_NONE, 0, 1, 2};

    const RenderStreamLink::Transform D3Root   = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f};
    const RenderStreamLink::Transform D3Offset = {0.1f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f};
    const TArray<RenderStreamLink::Transform> D3T = {D3Root, D3Offset, D3Offset, D3Offset};

    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(Names, D3T, Parents);

    const float HalfAngle = FMath::DegreesToRadians(90.f) * 0.5f;
    RenderStreamLink::FSkeletalPose Pose = BuildD3IdentityPose(Layout);
    Pose.joints[1].transform = {0.f, 0.f, 0.f, 0.f, 0.f, FMath::Sin(HalfAngle), FMath::Cos(HalfAngle)};

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(D3ToUEOffsets(D3T), Parents);
    const TMap<FName, int32>        NameMap   = BuildIdentityNameMap(Names);

    const TArray<FVector> Actual   = RunRetargeting(MeshBones, Layout, NameMap, Pose);
    const TArray<FVector> Expected = ComputeExpectedPositionsFromSource(Layout, Pose);

    CheckPositions(this, Actual, Expected, 0.5f, TEXT("SimpleRotation"));
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 — OutOfPlaneSingleChild
// 3-bone chain: Pelvis -> Spine -> RightShoulder.
// Spine has one child, so orientation correction IS applied.
// Source != mesh: check bone directions.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_SkeletonRetargeting_OutOfPlaneSingleChild,
    "RenderStream.SkeletonRetargeting.OutOfPlaneSingleChild",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTest_SkeletonRetargeting_OutOfPlaneSingleChild::RunTest(const FString& Parameters)
{
    using namespace RenderStreamRetargeting;

    const TArray<FString> Names   = {"Pelvis", "Spine", "RightShoulder"};
    const TArray<int32>   Parents = {INDEX_NONE, 0, 1};

    const TArray<RenderStreamLink::Transform> SourceD3 = {
        {0.f,  0.f, 0.f,  0.f, 0.f, 0.f, 1.f},
        {0.f,  0.f, 0.1f, 0.f, 0.f, 0.f, 1.f},
        {0.1f, 0.f, 0.f,  0.f, 0.f, 0.f, 1.f},
    };
    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(Names, SourceD3, Parents);
    const RenderStreamLink::FSkeletalPose   Pose   = BuildD3IdentityPose(Layout);

    const TArray<RenderStreamLink::Transform> MeshD3 = {
        {0.f,  0.f,  0.f,  0.f, 0.f, 0.f, 1.f},
        {0.f,  0.f,  0.1f, 0.f, 0.f, 0.f, 1.f},
        {0.f,  0.1f, 0.f,  0.f, 0.f, 0.f, 1.f},
    };

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(D3ToUEOffsets(MeshD3), Parents);
    const TMap<FName, int32>        NameMap   = BuildIdentityNameMap(Names);

    const TArray<FVector> Actual   = RunRetargeting(MeshBones, Layout, NameMap, Pose);
    const TArray<FVector> Expected = ComputeExpectedPositionsFromSource(Layout, Pose);

    const TSet<int32> MultiChildParents = FindMultiChildParents(Parents);
    CheckBoneDirections(this, Actual, Expected, Parents, MultiChildParents, 1.0f, TEXT("OutOfPlaneSingleChild"));
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 — OutOfPlaneMultiChild
// 5-bone skeleton: Pelvis -> Spine -> {Neck, LeftShoulder, RightShoulder}.
// Spine has 3 children with out-of-plane offsets. Averaged multi-child
// orientation correction should align all children's directions.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_SkeletonRetargeting_OutOfPlaneMultiChild,
    "RenderStream.SkeletonRetargeting.OutOfPlaneMultiChild",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTest_SkeletonRetargeting_OutOfPlaneMultiChild::RunTest(const FString& Parameters)
{
    using namespace RenderStreamRetargeting;

    const TArray<FString> Names   = {"Pelvis", "Spine", "Neck", "LeftShoulder", "RightShoulder"};
    const TArray<int32>   Parents = {INDEX_NONE, 0, 1, 1, 1};

    const TArray<RenderStreamLink::Transform> SourceD3 = {
        {0.f,    0.f,    0.f,  0.f, 0.f, 0.f, 1.f},
        {0.f,    0.f,   0.1f,  0.f, 0.f, 0.f, 1.f},
        {0.f,    0.f,   0.1f,  0.f, 0.f, 0.f, 1.f},
        {0.08f,  0.1f,  0.f,   0.f, 0.f, 0.f, 1.f},
        {0.08f, -0.1f,  0.f,   0.f, 0.f, 0.f, 1.f},
    };
    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(Names, SourceD3, Parents);
    const RenderStreamLink::FSkeletalPose   Pose   = BuildD3IdentityPose(Layout);

    const TArray<RenderStreamLink::Transform> MeshD3 = {
        {0.f,   0.f,   0.f,  0.f, 0.f, 0.f, 1.f},
        {0.f,   0.f,  0.1f,  0.f, 0.f, 0.f, 1.f},
        {0.f,   0.f,  0.1f,  0.f, 0.f, 0.f, 1.f},
        {0.f,   0.1f, 0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f,  -0.1f, 0.f,   0.f, 0.f, 0.f, 1.f},
    };

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(D3ToUEOffsets(MeshD3), Parents);
    const TMap<FName, int32>        NameMap   = BuildIdentityNameMap(Names);

    const TArray<FVector> Actual   = RunRetargeting(MeshBones, Layout, NameMap, Pose);
    const TArray<FVector> Expected = ComputeExpectedPositionsFromSource(Layout, Pose);

    // With multi-child orientation correction, all children should have correct directions.
    // Use direction check with no multi-child exclusions.
    const TSet<int32> NoExclusions;
    CheckBoneDirections(this, Actual, Expected, Parents, NoExclusions, 2.0f, TEXT("OutOfPlaneMultiChild"));
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 — FullDefaultLayoutIdentityPose
// Full 18-bone Default layout. Source == mesh. Identity pose.
// Source == mesh: positions should match exactly.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_SkeletonRetargeting_FullDefaultLayoutIdentityPose,
    "RenderStream.SkeletonRetargeting.FullDefaultLayoutIdentityPose",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTest_SkeletonRetargeting_FullDefaultLayoutIdentityPose::RunTest(const FString& Parameters)
{
    using namespace RenderStreamRetargeting;

    const TArray<FString> Names = {
        "Pelvis", "Spine", "Chest", "Neck",
        "LeftClavicle", "LeftShoulder", "LeftElbow", "LeftWrist",
        "LeftHip", "LeftKnee", "LeftAnkle",
        "RightClavicle", "RightShoulder", "RightElbow", "RightWrist",
        "RightHip", "RightKnee", "RightAnkle"
    };
    const TArray<int32> Parents = {
        INDEX_NONE, 0, 1, 2,
        2, 4, 5, 6,
        0, 8, 9,
        2, 11, 12, 13,
        0, 15, 16,
    };

    const TArray<RenderStreamLink::Transform> D3T = {
        {0.f,    0.f,    0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f,   0.05f,  0.12f,  0.f, 0.f, 0.f, 1.f},
        {0.f,   0.05f,  0.12f,  0.f, 0.f, 0.f, 1.f},
        {0.f,   0.f,    0.15f,  0.f, 0.f, 0.f, 1.f},
        {0.f,   0.15f,  0.05f,  0.f, 0.f, 0.f, 1.f},
        {0.f,   0.15f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,   0.28f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,   0.25f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {-0.05f, 0.1f, -0.05f,  0.f, 0.f, 0.f, 1.f},
        {-0.42f, 0.f,   0.f,    0.f, 0.f, 0.f, 1.f},
        {-0.4f,  0.f,   0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,  -0.15f,  0.05f,  0.f, 0.f, 0.f, 1.f},
        {0.f,  -0.15f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,  -0.28f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,  -0.25f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {-0.05f,-0.1f, -0.05f,  0.f, 0.f, 0.f, 1.f},
        {-0.42f, 0.f,   0.f,    0.f, 0.f, 0.f, 1.f},
        {-0.4f,  0.f,   0.f,    0.f, 0.f, 0.f, 1.f},
    };

    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(Names, D3T, Parents);
    const RenderStreamLink::FSkeletalPose   Pose   = BuildD3IdentityPose(Layout);

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(D3ToUEOffsets(D3T), Parents);
    const TMap<FName, int32>        NameMap   = BuildIdentityNameMap(Names);

    const TArray<FVector> Actual   = RunRetargeting(MeshBones, Layout, NameMap, Pose);
    const TArray<FVector> Expected = ComputeExpectedPositionsFromSource(Layout, Pose);

    CheckPositions(this, Actual, Expected, 0.1f, TEXT("FullDefaultLayoutIdentityPose"));
    return true;
}

// ---------------------------------------------------------------------------
// Test 6 — RealisticAlternativeLayout
// Full 18-bone skeleton. Source has out-of-plane clavicle/hip offsets;
// mesh is purely lateral. Animated pose.
// Source != mesh: check bone directions, skipping multi-child parent children.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_SkeletonRetargeting_RealisticAlternativeLayout,
    "RenderStream.SkeletonRetargeting.RealisticAlternativeLayout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTest_SkeletonRetargeting_RealisticAlternativeLayout::RunTest(const FString& Parameters)
{
    using namespace RenderStreamRetargeting;

    const TArray<FString> Names = {
        "Pelvis", "Spine", "Chest", "Neck",
        "LeftClavicle", "LeftShoulder", "LeftElbow", "LeftWrist",
        "LeftHip", "LeftKnee", "LeftAnkle",
        "RightClavicle", "RightShoulder", "RightElbow", "RightWrist",
        "RightHip", "RightKnee", "RightAnkle"
    };
    const TArray<int32> Parents = {
        INDEX_NONE, 0, 1, 2,
        2, 4, 5, 6,
        0, 8, 9,
        2, 11, 12, 13,
        0, 15, 16,
    };

    const TArray<RenderStreamLink::Transform> SourceD3 = {
        {0.f,    0.f,    0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f,   0.05f,  0.12f,  0.f, 0.f, 0.f, 1.f},
        {0.f,   0.05f,  0.12f,  0.f, 0.f, 0.f, 1.f},
        {0.f,   0.f,    0.15f,  0.f, 0.f, 0.f, 1.f},
        {0.05f, 0.15f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,   0.15f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,   0.28f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,   0.25f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {-0.05f, 0.1f, -0.05f,  0.f, 0.f, 0.f, 1.f},
        {-0.42f, 0.f,   0.f,    0.f, 0.f, 0.f, 1.f},
        {-0.4f,  0.f,   0.f,    0.f, 0.f, 0.f, 1.f},
        {0.05f,-0.15f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,  -0.15f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,  -0.28f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,  -0.25f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {-0.05f,-0.1f, -0.05f,  0.f, 0.f, 0.f, 1.f},
        {-0.42f, 0.f,   0.f,    0.f, 0.f, 0.f, 1.f},
        {-0.4f,  0.f,   0.f,    0.f, 0.f, 0.f, 1.f},
    };
    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(Names, SourceD3, Parents);

    const TArray<RenderStreamLink::Transform> MeshD3 = {
        {0.f,    0.f,    0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f,   0.05f,  0.12f,  0.f, 0.f, 0.f, 1.f},
        {0.f,   0.05f,  0.12f,  0.f, 0.f, 0.f, 1.f},
        {0.f,   0.f,    0.15f,  0.f, 0.f, 0.f, 1.f},
        {0.f,   0.15f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,   0.15f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,   0.28f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,   0.25f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,   0.1f,  -0.05f,  0.f, 0.f, 0.f, 1.f},
        {-0.42f, 0.f,   0.f,    0.f, 0.f, 0.f, 1.f},
        {-0.4f,  0.f,   0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,  -0.15f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,  -0.15f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,  -0.28f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,  -0.25f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,  -0.1f,  -0.05f,  0.f, 0.f, 0.f, 1.f},
        {-0.42f, 0.f,   0.f,    0.f, 0.f, 0.f, 1.f},
        {-0.4f,  0.f,   0.f,    0.f, 0.f, 0.f, 1.f},
    };

    // Non-trivial pose
    RenderStreamLink::FSkeletalPose Pose = BuildD3IdentityPose(Layout);
    {
        const float H = FMath::DegreesToRadians(10.f) * 0.5f;
        Pose.joints[1].transform = {0.f, 0.f, 0.f, 0.f, 0.f, FMath::Sin(H), FMath::Cos(H)};
    }
    {
        const float H = FMath::DegreesToRadians(45.f) * 0.5f;
        Pose.joints[5].transform = {0.f, 0.f, 0.f, 0.f, FMath::Sin(H), 0.f, FMath::Cos(H)};
    }
    {
        const float H = FMath::DegreesToRadians(15.f) * 0.5f;
        Pose.joints[15].transform = {0.f, 0.f, 0.f, 0.f, FMath::Sin(H), 0.f, FMath::Cos(H)};
    }

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(D3ToUEOffsets(MeshD3), Parents);
    const TMap<FName, int32>        NameMap   = BuildIdentityNameMap(Names);

    const TArray<FVector> Actual   = RunRetargeting(MeshBones, Layout, NameMap, Pose);
    const TArray<FVector> Expected = ComputeExpectedPositionsFromSource(Layout, Pose);

    // With multi-child orientation correction, all bones should have correct directions.
    const TSet<int32> NoExclusions;
    CheckBoneDirections(this, Actual, Expected, Parents, NoExclusions, 2.0f, TEXT("RealisticAlternativeLayout"));
    return true;
}

// ---------------------------------------------------------------------------
// Test 7 — MultiChildWithSingleChildDescendants
// Verifies that single-child descendants of multi-child parents get correct
// orientation corrections. Spine has 3 children, each with a single-child
// chain extending from it.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_SkeletonRetargeting_MultiChildWithSingleChildDescendants,
    "RenderStream.SkeletonRetargeting.MultiChildWithSingleChildDescendants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTest_SkeletonRetargeting_MultiChildWithSingleChildDescendants::RunTest(const FString& Parameters)
{
    using namespace RenderStreamRetargeting;

    // Pelvis(0) -> Spine(1) -> {Neck(2), LeftShoulder(3), RightShoulder(4)}
    // Neck(2) -> Head(5)
    // LeftShoulder(3) -> LeftElbow(6) -> LeftWrist(7)
    // RightShoulder(4) -> RightElbow(8) -> RightWrist(9)
    const TArray<FString> Names = {
        "Pelvis", "Spine", "Neck", "LeftShoulder", "RightShoulder",
        "Head", "LeftElbow", "LeftWrist", "RightElbow", "RightWrist"
    };
    const TArray<int32> Parents = {
        INDEX_NONE, 0, 1, 1, 1,
        2, 3, 6, 4, 8
    };

    // Source layout: shoulders have out-of-plane X component
    const TArray<RenderStreamLink::Transform> SourceD3 = {
        {0.f,    0.f,   0.f,   0.f, 0.f, 0.f, 1.f},  // Pelvis
        {0.f,    0.f,   0.15f, 0.f, 0.f, 0.f, 1.f},  // Spine
        {0.f,    0.f,   0.12f, 0.f, 0.f, 0.f, 1.f},  // Neck
        {0.06f,  0.12f, 0.f,   0.f, 0.f, 0.f, 1.f},  // LeftShoulder (X out-of-plane)
        {0.06f, -0.12f, 0.f,   0.f, 0.f, 0.f, 1.f},  // RightShoulder (X out-of-plane)
        {0.f,    0.f,   0.1f,  0.f, 0.f, 0.f, 1.f},  // Head
        {0.f,    0.25f, 0.f,   0.f, 0.f, 0.f, 1.f},  // LeftElbow
        {0.f,    0.22f, 0.f,   0.f, 0.f, 0.f, 1.f},  // LeftWrist
        {0.f,   -0.25f, 0.f,   0.f, 0.f, 0.f, 1.f},  // RightElbow
        {0.f,   -0.22f, 0.f,   0.f, 0.f, 0.f, 1.f},  // RightWrist
    };
    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(Names, SourceD3, Parents);
    const RenderStreamLink::FSkeletalPose   Pose   = BuildD3IdentityPose(Layout);

    // Mesh layout: shoulders purely lateral (no out-of-plane X)
    const TArray<RenderStreamLink::Transform> MeshD3 = {
        {0.f,    0.f,   0.f,   0.f, 0.f, 0.f, 1.f},  // Pelvis
        {0.f,    0.f,   0.15f, 0.f, 0.f, 0.f, 1.f},  // Spine
        {0.f,    0.f,   0.12f, 0.f, 0.f, 0.f, 1.f},  // Neck
        {0.f,    0.12f, 0.f,   0.f, 0.f, 0.f, 1.f},  // LeftShoulder (purely lateral)
        {0.f,   -0.12f, 0.f,   0.f, 0.f, 0.f, 1.f},  // RightShoulder (purely lateral)
        {0.f,    0.f,   0.1f,  0.f, 0.f, 0.f, 1.f},  // Head
        {0.f,    0.25f, 0.f,   0.f, 0.f, 0.f, 1.f},  // LeftElbow
        {0.f,    0.22f, 0.f,   0.f, 0.f, 0.f, 1.f},  // LeftWrist
        {0.f,   -0.25f, 0.f,   0.f, 0.f, 0.f, 1.f},  // RightElbow
        {0.f,   -0.22f, 0.f,   0.f, 0.f, 0.f, 1.f},  // RightWrist
    };

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(D3ToUEOffsets(MeshD3), Parents);
    const TMap<FName, int32>        NameMap   = BuildIdentityNameMap(Names);

    const TArray<FVector> Actual   = RunRetargeting(MeshBones, Layout, NameMap, Pose);
    const TArray<FVector> Expected = ComputeExpectedPositionsFromSource(Layout, Pose);

    const TSet<int32> MultiChildParents = FindMultiChildParents(Parents);
    // Spine (index 1) is multi-child parent; its children (2,3,4) are skipped.
    // But descendants (5,6,7,8,9) are single-child and should have correct directions.
    CheckBoneDirections(this, Actual, Expected, Parents, MultiChildParents, 1.0f,
        TEXT("MultiChildWithSingleChildDescendants"));
    return true;
}

// ---------------------------------------------------------------------------
// Test 8 — RotationWithDifferentLayout
// Single-child chain with different layouts, applying a non-trivial pose rotation.
// Verifies orientation correction works correctly under pose animation.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_SkeletonRetargeting_RotationWithDifferentLayout,
    "RenderStream.SkeletonRetargeting.RotationWithDifferentLayout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTest_SkeletonRetargeting_RotationWithDifferentLayout::RunTest(const FString& Parameters)
{
    using namespace RenderStreamRetargeting;

    // 4-bone chain: Root -> A -> B -> C
    // Source: B is along d3 X from A; mesh: B is along d3 Y from A
    const TArray<FString> Names   = {"Root", "A", "B", "C"};
    const TArray<int32>   Parents = {INDEX_NONE, 0, 1, 2};

    const TArray<RenderStreamLink::Transform> SourceD3 = {
        {0.f,  0.f, 0.f,  0.f, 0.f, 0.f, 1.f},
        {0.f,  0.f, 0.15f, 0.f, 0.f, 0.f, 1.f},
        {0.1f, 0.f, 0.f,  0.f, 0.f, 0.f, 1.f},  // along d3 X
        {0.1f, 0.f, 0.f,  0.f, 0.f, 0.f, 1.f},
    };
    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(Names, SourceD3, Parents);

    // Apply 45 deg rotation on joint A around d3 Z
    const float H = FMath::DegreesToRadians(45.f) * 0.5f;
    RenderStreamLink::FSkeletalPose Pose = BuildD3IdentityPose(Layout);
    Pose.joints[1].transform = {0.f, 0.f, 0.f, 0.f, 0.f, FMath::Sin(H), FMath::Cos(H)};

    const TArray<RenderStreamLink::Transform> MeshD3 = {
        {0.f,  0.f,  0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f,  0.f,  0.15f, 0.f, 0.f, 0.f, 1.f},
        {0.f,  0.1f, 0.f,   0.f, 0.f, 0.f, 1.f},  // along d3 Y (different from source)
        {0.f,  0.1f, 0.f,   0.f, 0.f, 0.f, 1.f},
    };

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(D3ToUEOffsets(MeshD3), Parents);
    const TMap<FName, int32>        NameMap   = BuildIdentityNameMap(Names);

    const TArray<FVector> Actual   = RunRetargeting(MeshBones, Layout, NameMap, Pose);
    const TArray<FVector> Expected = ComputeExpectedPositionsFromSource(Layout, Pose);

    const TSet<int32> MultiChildParents = FindMultiChildParents(Parents);
    CheckBoneDirections(this, Actual, Expected, Parents, MultiChildParents, 2.0f,
        TEXT("RotationWithDifferentLayout"));
    return true;
}

// ---------------------------------------------------------------------------
// Test 9 — UnmappedMiddleBone
// Source has an extra bone in the spine that doesn't exist in the mesh.
// Source: Root(0) -> SpineLower(1) -> SpineMiddle(2) -> SpineUpper(3) -> Head(4)
// Mesh:   Root(0) -> SpineLower(1) -> SpineUpper(2) -> Head(3)
// SpineMiddle is unmapped. When SpineMiddle gets a pose rotation, SpineUpper
// and Head should move as if the rotation propagated through the chain.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_SkeletonRetargeting_UnmappedMiddleBone,
    "RenderStream.SkeletonRetargeting.UnmappedMiddleBone",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTest_SkeletonRetargeting_UnmappedMiddleBone::RunTest(const FString& Parameters)
{
    using namespace RenderStreamRetargeting;

    // --- Source skeleton (5 bones) ---
    const TArray<FString> SourceNames = {
        "Root", "SpineLower", "SpineMiddle", "SpineUpper", "Head"
    };
    const TArray<int32> SourceParents = {INDEX_NONE, 0, 1, 2, 3};

    const TArray<RenderStreamLink::Transform> SourceD3 = {
        {0.f, 0.f, 0.f,  0.f, 0.f, 0.f, 1.f},  // Root
        {0.f, 0.f, 0.1f, 0.f, 0.f, 0.f, 1.f},   // SpineLower
        {0.f, 0.f, 0.1f, 0.f, 0.f, 0.f, 1.f},   // SpineMiddle
        {0.f, 0.f, 0.1f, 0.f, 0.f, 0.f, 1.f},   // SpineUpper
        {0.f, 0.f, 0.1f, 0.f, 0.f, 0.f, 1.f},   // Head
    };
    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(SourceNames, SourceD3, SourceParents);

    // --- Mesh skeleton (4 bones, no SpineMiddle) ---
    const TArray<FString> MeshNames = {"Root", "SpineLower", "SpineUpper", "Head"};
    const TArray<int32>   MeshParents = {INDEX_NONE, 0, 1, 2};

    const TArray<RenderStreamLink::Transform> MeshD3 = {
        {0.f, 0.f, 0.f,  0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.1f, 0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.1f, 0.f, 0.f, 0.f, 1.f},  // SpineUpper (parent=SpineLower in mesh)
        {0.f, 0.f, 0.1f, 0.f, 0.f, 0.f, 1.f},
    };
    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(D3ToUEOffsets(MeshD3), MeshParents);

    // Name map: source -> mesh (SpineMiddle is NOT in the map)
    TMap<FName, int32> NameMap;
    NameMap.Add(FName("Root"), 0);
    NameMap.Add(FName("SpineLower"), 1);
    // SpineMiddle intentionally unmapped
    NameMap.Add(FName("SpineUpper"), 2);
    NameMap.Add(FName("Head"), 3);

    // --- Test A: Identity pose (baseline) ---
    {
        const RenderStreamLink::FSkeletalPose IdentityPose = BuildD3IdentityPose(Layout);
        const TArray<FVector> Actual   = RunRetargeting(MeshBones, Layout, NameMap, IdentityPose);
        const TArray<FVector> Expected = ComputeExpectedPositionsFromSource(Layout, IdentityPose);

        // Expected source positions (all along Z): Root(0,0,0), SL(0,0,10), SM(0,0,20), SU(0,0,30), Head(0,0,40)
        // Mesh has 4 bones. We expect SpineUpper at source SpineUpper position and Head at source Head position.
        // Check that mesh bones are at reasonable positions with identity pose.
        TestTrue(TEXT("UnmappedMiddle identity: mesh has 4 bones"), Actual.Num() == 4);
    }

    // --- Test B: Rotate SpineMiddle (unmapped) by 90 deg around d3 Z ---
    {
        const float H = FMath::DegreesToRadians(90.f) * 0.5f;
        RenderStreamLink::FSkeletalPose Pose = BuildD3IdentityPose(Layout);
        // SpineMiddle is source index 2
        Pose.joints[2].transform = {0.f, 0.f, 0.f, 0.f, 0.f, FMath::Sin(H), FMath::Cos(H)};

        const TArray<FVector> Actual = RunRetargeting(MeshBones, Layout, NameMap, Pose);
        const TArray<FVector> Expected = ComputeExpectedPositionsFromSource(Layout, Pose);

        // With 90 deg rotation on SpineMiddle, SpineUpper and Head should move sideways.
        // Expected source world positions:
        //   Root: (0,0,0), SpineLower: (0,0,10), SpineMiddle: (0,0,20)
        //   SpineUpper: rotated 90 around Z from (0,0,10) -> (10,0,0) + parent(0,0,20) = (10,0,20) in UE coords
        //   Head: (0,0,10) rotated by 90 around Z -> (10,0,0) + SpineUpper = (20,0,20) in UE coords
        // Verify that SpineUpper (mesh index 2) has actually moved sideways.
        // If the bug is present, SpineUpper stays at (0,0,30) — straight up.
        const FVector SpineUpperPos = Actual[2]; // mesh index 2 = SpineUpper
        const FVector HeadPos       = Actual[3]; // mesh index 3 = Head

        // SpineUpper should NOT be directly above SpineLower if SpineMiddle was rotated 90 deg.
        // In the buggy case, it would still be at roughly (0, 0, 30).
        // In the correct case, it should have a significant lateral offset.
        const FVector SpineLowerPos = Actual[1]; // mesh index 1
        const float LateralOffset = FVector::Dist2D(SpineUpperPos, SpineLowerPos);
        TestTrue(
            FString::Printf(TEXT("UnmappedMiddle rotated: SpineUpper lateral offset = %.2f cm (should be > 5)"), LateralOffset),
            LateralOffset > 5.f);

        // Head should be even further offset
        const float HeadLateralOffset = FVector::Dist2D(HeadPos, SpineLowerPos);
        TestTrue(
            FString::Printf(TEXT("UnmappedMiddle rotated: Head lateral offset = %.2f cm (should be > 10)"), HeadLateralOffset),
            HeadLateralOffset > 10.f);
    }

    // --- Test C: Rotate SpineMiddle by 45 deg, check directions ---
    {
        const float H = FMath::DegreesToRadians(45.f) * 0.5f;
        RenderStreamLink::FSkeletalPose Pose = BuildD3IdentityPose(Layout);
        Pose.joints[2].transform = {0.f, 0.f, 0.f, 0.f, 0.f, FMath::Sin(H), FMath::Cos(H)};

        const TArray<FVector> Actual = RunRetargeting(MeshBones, Layout, NameMap, Pose);
        const TArray<FVector> Expected = ComputeExpectedPositionsFromSource(Layout, Pose);

        // Check that SpineUpper->Head direction matches expected
        // (Both are mapped, single-child chain, so direction should be correct)
        const FVector ActualDir = (Actual[3] - Actual[2]).GetSafeNormal();
        const FVector ExpectedDir = (Expected[4] - Expected[3]).GetSafeNormal(); // source indices 4,3
        const float Dot = FMath::Clamp(FVector::DotProduct(ActualDir, ExpectedDir), -1.f, 1.f);
        const float AngleDeg = FMath::RadiansToDegrees(FMath::Acos(Dot));
        TestTrue(
            FString::Printf(TEXT("UnmappedMiddle 45deg: SpineUpper->Head direction within 2 deg (angle=%.2f)"), AngleDeg),
            AngleDeg <= 2.f);
    }

    return true;
}

// ---------------------------------------------------------------------------
// Test 10 — UnmappedMultipleMiddleBones
// Source has TWO extra bones in the spine that don't exist in the mesh.
// Source: Root(0) -> A(1) -> B(2) -> C(3) -> D(4) -> E(5)
// Mesh:   Root(0) -> A(1) -> D(2) -> E(3)
// B and C are unmapped. When both get pose rotations, D and E should
// accumulate all intermediate rotations.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_SkeletonRetargeting_UnmappedMultipleMiddleBones,
    "RenderStream.SkeletonRetargeting.UnmappedMultipleMiddleBones",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTest_SkeletonRetargeting_UnmappedMultipleMiddleBones::RunTest(const FString& Parameters)
{
    using namespace RenderStreamRetargeting;

    // --- Source skeleton (6 bones) ---
    const TArray<FString> SourceNames = {"Root", "A", "B", "C", "D", "E"};
    const TArray<int32> SourceParents = {INDEX_NONE, 0, 1, 2, 3, 4};

    const TArray<RenderStreamLink::Transform> SourceD3 = {
        {0.f, 0.f, 0.f,  0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.1f, 0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.1f, 0.f, 0.f, 0.f, 1.f},  // B (unmapped)
        {0.f, 0.f, 0.1f, 0.f, 0.f, 0.f, 1.f},  // C (unmapped)
        {0.f, 0.f, 0.1f, 0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.1f, 0.f, 0.f, 0.f, 1.f},
    };
    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(SourceNames, SourceD3, SourceParents);

    // --- Mesh skeleton (4 bones) ---
    const TArray<FString> MeshNames = {"Root", "A", "D", "E"};
    const TArray<int32>   MeshParents = {INDEX_NONE, 0, 1, 2};
    const TArray<RenderStreamLink::Transform> MeshD3 = {
        {0.f, 0.f, 0.f,  0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.1f, 0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.1f, 0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.1f, 0.f, 0.f, 0.f, 1.f},
    };
    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(D3ToUEOffsets(MeshD3), MeshParents);

    TMap<FName, int32> NameMap;
    NameMap.Add(FName("Root"), 0);
    NameMap.Add(FName("A"), 1);
    NameMap.Add(FName("D"), 2);
    NameMap.Add(FName("E"), 3);

    // Rotate B by 45 deg and C by 45 deg around d3 Z (total 90 deg)
    const float H45 = FMath::DegreesToRadians(45.f) * 0.5f;
    RenderStreamLink::FSkeletalPose Pose = BuildD3IdentityPose(Layout);
    Pose.joints[2].transform = {0.f, 0.f, 0.f, 0.f, 0.f, FMath::Sin(H45), FMath::Cos(H45)};
    Pose.joints[3].transform = {0.f, 0.f, 0.f, 0.f, 0.f, FMath::Sin(H45), FMath::Cos(H45)};

    const TArray<FVector> Actual = RunRetargeting(MeshBones, Layout, NameMap, Pose);
    const TArray<FVector> Expected = ComputeExpectedPositionsFromSource(Layout, Pose);

    // D (mesh 2) should have significant lateral offset from A (mesh 1)
    const float DLateral = FVector::Dist2D(Actual[2], Actual[1]);
    TestTrue(
        FString::Printf(TEXT("UnmappedMultiple: D lateral offset = %.2f cm (should be > 5)"), DLateral),
        DLateral > 5.f);

    // E (mesh 3) should have even more lateral offset
    const float ELateral = FVector::Dist2D(Actual[3], Actual[1]);
    TestTrue(
        FString::Printf(TEXT("UnmappedMultiple: E lateral offset = %.2f cm (should be > 10)"), ELateral),
        ELateral > 10.f);

    // Check D->E direction matches expected source D->E direction
    const FVector ActualDir = (Actual[3] - Actual[2]).GetSafeNormal();
    const FVector ExpectedDir = (Expected[5] - Expected[4]).GetSafeNormal();
    const float Dot = FMath::Clamp(FVector::DotProduct(ActualDir, ExpectedDir), -1.f, 1.f);
    const float AngleDeg = FMath::RadiansToDegrees(FMath::Acos(Dot));
    TestTrue(
        FString::Printf(TEXT("UnmappedMultiple: D->E direction within 2 deg (angle=%.2f)"), AngleDeg),
        AngleDeg <= 2.f);

    return true;
}

// ---------------------------------------------------------------------------
// Test 11 — IdenticalLayoutsWithPose
// Full 18-bone skeleton. Source == mesh. Non-trivial pose.
// Source == mesh: positions should match (regression test for pose application).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_SkeletonRetargeting_IdenticalLayoutsWithPose,
    "RenderStream.SkeletonRetargeting.IdenticalLayoutsWithPose",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTest_SkeletonRetargeting_IdenticalLayoutsWithPose::RunTest(const FString& Parameters)
{
    using namespace RenderStreamRetargeting;

    const TArray<FString> Names = {
        "Pelvis", "Spine", "Chest", "Neck",
        "LeftClavicle", "LeftShoulder", "LeftElbow", "LeftWrist",
        "LeftHip", "LeftKnee", "LeftAnkle",
        "RightClavicle", "RightShoulder", "RightElbow", "RightWrist",
        "RightHip", "RightKnee", "RightAnkle"
    };
    const TArray<int32> Parents = {
        INDEX_NONE, 0, 1, 2,
        2, 4, 5, 6,
        0, 8, 9,
        2, 11, 12, 13,
        0, 15, 16,
    };

    const TArray<RenderStreamLink::Transform> D3T = {
        {0.f,    0.f,    0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f,   0.05f,  0.12f,  0.f, 0.f, 0.f, 1.f},
        {0.f,   0.05f,  0.12f,  0.f, 0.f, 0.f, 1.f},
        {0.f,   0.f,    0.15f,  0.f, 0.f, 0.f, 1.f},
        {0.f,   0.15f,  0.05f,  0.f, 0.f, 0.f, 1.f},
        {0.f,   0.15f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,   0.28f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,   0.25f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {-0.05f, 0.1f, -0.05f,  0.f, 0.f, 0.f, 1.f},
        {-0.42f, 0.f,   0.f,    0.f, 0.f, 0.f, 1.f},
        {-0.4f,  0.f,   0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,  -0.15f,  0.05f,  0.f, 0.f, 0.f, 1.f},
        {0.f,  -0.15f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,  -0.28f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,  -0.25f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {-0.05f,-0.1f, -0.05f,  0.f, 0.f, 0.f, 1.f},
        {-0.42f, 0.f,   0.f,    0.f, 0.f, 0.f, 1.f},
        {-0.4f,  0.f,   0.f,    0.f, 0.f, 0.f, 1.f},
    };

    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(Names, D3T, Parents);

    // Non-trivial pose: multiple joints rotated
    RenderStreamLink::FSkeletalPose Pose = BuildD3IdentityPose(Layout);
    {
        const float H = FMath::DegreesToRadians(10.f) * 0.5f;
        Pose.joints[1].transform = {0.f, 0.f, 0.f, 0.f, 0.f, FMath::Sin(H), FMath::Cos(H)};
    }
    {
        const float H = FMath::DegreesToRadians(45.f) * 0.5f;
        Pose.joints[5].transform = {0.f, 0.f, 0.f, 0.f, FMath::Sin(H), 0.f, FMath::Cos(H)};
    }
    {
        const float H = FMath::DegreesToRadians(15.f) * 0.5f;
        Pose.joints[15].transform = {0.f, 0.f, 0.f, 0.f, FMath::Sin(H), 0.f, FMath::Cos(H)};
    }

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(D3ToUEOffsets(D3T), Parents);
    const TMap<FName, int32>        NameMap   = BuildIdentityNameMap(Names);

    const TArray<FVector> Actual   = RunRetargeting(MeshBones, Layout, NameMap, Pose);
    const TArray<FVector> Expected = ComputeExpectedPositionsFromSource(Layout, Pose);

    CheckPositions(this, Actual, Expected, 0.5f, TEXT("IdenticalLayoutsWithPose"));
    return true;
}

// ---------------------------------------------------------------------------
// Test 12 — SkipCorrectionBasic
// 4-bone chain: Hip -> Knee -> Ankle -> Foot.
// Source foot is horizontal (d3 X), mesh foot tilts downward (d3 X+Z negative).
// Without skip: Ankle gets orientation-corrected to point foot in source direction.
// With "Foot" skipped: Ankle is NOT corrected, foot keeps mesh rest-pose direction.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_SkeletonRetargeting_SkipCorrectionBasic,
    "RenderStream.SkeletonRetargeting.SkipCorrectionBasic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTest_SkeletonRetargeting_SkipCorrectionBasic::RunTest(const FString& Parameters)
{
    using namespace RenderStreamRetargeting;

    const TArray<FString> Names   = {"Hip", "Knee", "Ankle", "Foot"};
    const TArray<int32>   Parents = {INDEX_NONE, 0, 1, 2};

    // Source: vertical chain then horizontal foot
    const TArray<RenderStreamLink::Transform> SourceD3 = {
        {0.f, 0.f, 0.f,  0.f, 0.f, 0.f, 1.f},   // Hip
        {0.f, 0.f, -0.4f, 0.f, 0.f, 0.f, 1.f},   // Knee (down)
        {0.f, 0.f, -0.4f, 0.f, 0.f, 0.f, 1.f},   // Ankle (down)
        {0.1f, 0.f, 0.f,  0.f, 0.f, 0.f, 1.f},   // Foot (horizontal, along d3 X)
    };
    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(Names, SourceD3, Parents);
    const RenderStreamLink::FSkeletalPose   Pose   = BuildD3IdentityPose(Layout);

    // Mesh: same except foot tilts downward
    const TArray<RenderStreamLink::Transform> MeshD3 = {
        {0.f, 0.f, 0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, -0.4f, 0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, -0.4f, 0.f, 0.f, 0.f, 1.f},
        {0.08f, 0.f, -0.06f, 0.f, 0.f, 0.f, 1.f}, // Foot tilts down
    };

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(D3ToUEOffsets(MeshD3), Parents);
    const TMap<FName, int32>        NameMap   = BuildIdentityNameMap(Names);

    // WITHOUT skip: ankle gets corrected, foot direction matches source (horizontal)
    {
        const TArray<FVector> Actual = RunRetargeting(MeshBones, Layout, NameMap, Pose);
        const TArray<FVector> Expected = ComputeExpectedPositionsFromSource(Layout, Pose);

        // With correction, Ankle->Foot direction should match source direction (horizontal)
        const FVector ActualDir = (Actual[3] - Actual[2]).GetSafeNormal();
        const FVector ExpectedDir = (Expected[3] - Expected[2]).GetSafeNormal();
        const float Dot = FMath::Clamp(FVector::DotProduct(ActualDir, ExpectedDir), -1.f, 1.f);
        const float AngleDeg = FMath::RadiansToDegrees(FMath::Acos(Dot));
        TestTrue(
            FString::Printf(TEXT("SkipCorrectionBasic no-skip: Ankle->Foot within 2 deg of source (angle=%.2f)"), AngleDeg),
            AngleDeg <= 2.f);
    }

    // WITH skip on "Ankle": Ankle->Foot direction keeps mesh rest-pose
    {
        TSet<FName> SkipSet;
        SkipSet.Add(FName("Ankle"));
        const TArray<FVector> Actual = RunRetargeting(MeshBones, Layout, NameMap, Pose, SkipSet);

        // With skip, Ankle->Foot direction should match MESH rest-pose direction (tilted down)
        const TArray<FVector> MeshWorldPos = ComputeWorldPositions(
            [&]() {
                TArray<FTransform> T;
                T.SetNum(MeshBones.Num());
                for (int32 i = 0; i < MeshBones.Num(); ++i) T[i] = MeshBones[i].LocalTransform;
                return T;
            }(),
            Parents);

        const FVector MeshDir = (MeshWorldPos[3] - MeshWorldPos[2]).GetSafeNormal();
        const FVector ActualDir = (Actual[3] - Actual[2]).GetSafeNormal();
        const float Dot = FMath::Clamp(FVector::DotProduct(ActualDir, MeshDir), -1.f, 1.f);
        const float AngleDeg = FMath::RadiansToDegrees(FMath::Acos(Dot));
        TestTrue(
            FString::Printf(TEXT("SkipCorrectionBasic with-skip: Ankle->Foot within 2 deg of mesh (angle=%.2f)"), AngleDeg),
            AngleDeg <= 2.f);

        // Also verify it does NOT match source direction (there should be a meaningful angle)
        const TArray<FVector> Expected = ComputeExpectedPositionsFromSource(Layout, Pose);
        const FVector SourceDir = (Expected[3] - Expected[2]).GetSafeNormal();
        const float DotSrc = FMath::Clamp(FVector::DotProduct(ActualDir, SourceDir), -1.f, 1.f);
        const float AngleSrcDeg = FMath::RadiansToDegrees(FMath::Acos(DotSrc));
        TestTrue(
            FString::Printf(TEXT("SkipCorrectionBasic with-skip: Ankle->Foot differs from source (angle=%.2f)"), AngleSrcDeg),
            AngleSrcDeg > 5.f);
    }

    return true;
}

// ---------------------------------------------------------------------------
// Test 13 — SkipCorrectionWithPose
// Same skeleton as Test 12, but with a knee bend.
// Upstream bones retarget normally; skipped bone gets direct pose rotation.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_SkeletonRetargeting_SkipCorrectionWithPose,
    "RenderStream.SkeletonRetargeting.SkipCorrectionWithPose",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTest_SkeletonRetargeting_SkipCorrectionWithPose::RunTest(const FString& Parameters)
{
    using namespace RenderStreamRetargeting;

    const TArray<FString> Names   = {"Hip", "Knee", "Ankle", "Foot"};
    const TArray<int32>   Parents = {INDEX_NONE, 0, 1, 2};

    const TArray<RenderStreamLink::Transform> SourceD3 = {
        {0.f, 0.f, 0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, -0.4f, 0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, -0.4f, 0.f, 0.f, 0.f, 1.f},
        {0.1f, 0.f, 0.f,  0.f, 0.f, 0.f, 1.f},
    };
    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(Names, SourceD3, Parents);

    // 30 deg knee bend around d3 Y
    const float H = FMath::DegreesToRadians(30.f) * 0.5f;
    RenderStreamLink::FSkeletalPose Pose = BuildD3IdentityPose(Layout);
    Pose.joints[1].transform = {0.f, 0.f, 0.f, 0.f, FMath::Sin(H), 0.f, FMath::Cos(H)};

    const TArray<RenderStreamLink::Transform> MeshD3 = {
        {0.f, 0.f, 0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, -0.4f, 0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, -0.4f, 0.f, 0.f, 0.f, 1.f},
        {0.08f, 0.f, -0.06f, 0.f, 0.f, 0.f, 1.f},
    };

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(D3ToUEOffsets(MeshD3), Parents);
    const TMap<FName, int32>        NameMap   = BuildIdentityNameMap(Names);

    TSet<FName> SkipSet;
    SkipSet.Add(FName("Ankle"));
    const TArray<FVector> Actual = RunRetargeting(MeshBones, Layout, NameMap, Pose, SkipSet);

    // Verify ankle has moved (knee bend should move it)
    const TArray<FVector> MeshWorldPos = ComputeWorldPositions(
        [&]() {
            TArray<FTransform> T;
            T.SetNum(MeshBones.Num());
            for (int32 i = 0; i < MeshBones.Num(); ++i) T[i] = MeshBones[i].LocalTransform;
            return T;
        }(),
        Parents);

    const float AnkleDist = FVector::Dist(Actual[2], MeshWorldPos[2]);
    TestTrue(
        FString::Printf(TEXT("SkipCorrectionWithPose: Ankle moved from rest (dist=%.2f cm)"), AnkleDist),
        AnkleDist > 1.f);

    // Foot should also have moved (it's a descendant of the bent knee)
    const float FootDist = FVector::Dist(Actual[3], MeshWorldPos[3]);
    TestTrue(
        FString::Printf(TEXT("SkipCorrectionWithPose: Foot moved from rest (dist=%.2f cm)"), FootDist),
        FootDist > 1.f);

    // Bone count should be correct
    TestEqual(TEXT("SkipCorrectionWithPose: bone count"), Actual.Num(), 4);

    return true;
}

// ---------------------------------------------------------------------------
// Test 14 — SkipCorrectionChainEffect
// 5-bone chain A -> B -> C -> D -> E. Skip correction on C.
// B->C correction is skipped, but C->D and D->E should still work.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_SkeletonRetargeting_SkipCorrectionChainEffect,
    "RenderStream.SkeletonRetargeting.SkipCorrectionChainEffect",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTest_SkeletonRetargeting_SkipCorrectionChainEffect::RunTest(const FString& Parameters)
{
    using namespace RenderStreamRetargeting;

    const TArray<FString> Names   = {"A", "B", "C", "D", "E"};
    const TArray<int32>   Parents = {INDEX_NONE, 0, 1, 2, 3};

    // Source: C goes along d3 X from B
    const TArray<RenderStreamLink::Transform> SourceD3 = {
        {0.f,  0.f, 0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f,  0.f, 0.15f, 0.f, 0.f, 0.f, 1.f},
        {0.1f, 0.f, 0.f,   0.f, 0.f, 0.f, 1.f},  // C along d3 X
        {0.f,  0.f, 0.1f,  0.f, 0.f, 0.f, 1.f},
        {0.f,  0.f, 0.1f,  0.f, 0.f, 0.f, 1.f},
    };
    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(Names, SourceD3, Parents);
    const RenderStreamLink::FSkeletalPose   Pose   = BuildD3IdentityPose(Layout);

    // Mesh: C goes along d3 Y from B (different direction)
    const TArray<RenderStreamLink::Transform> MeshD3 = {
        {0.f,  0.f,  0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f,  0.f,  0.15f, 0.f, 0.f, 0.f, 1.f},
        {0.f,  0.1f, 0.f,   0.f, 0.f, 0.f, 1.f},  // C along d3 Y
        {0.f,  0.f,  0.1f,  0.f, 0.f, 0.f, 1.f},
        {0.f,  0.f,  0.1f,  0.f, 0.f, 0.f, 1.f},
    };

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(D3ToUEOffsets(MeshD3), Parents);
    const TMap<FName, int32>        NameMap   = BuildIdentityNameMap(Names);
    const TArray<FVector> Expected = ComputeExpectedPositionsFromSource(Layout, Pose);

    // WITH skip on B: B->C direction should match mesh, not source
    TSet<FName> SkipSet;
    SkipSet.Add(FName("B"));
    const TArray<FVector> ActualSkip = RunRetargeting(MeshBones, Layout, NameMap, Pose, SkipSet);

    // B->C direction should match MESH rest direction (not source)
    const TArray<FVector> MeshWorldPos = ComputeWorldPositions(
        [&]() {
            TArray<FTransform> T;
            T.SetNum(MeshBones.Num());
            for (int32 i = 0; i < MeshBones.Num(); ++i) T[i] = MeshBones[i].LocalTransform;
            return T;
        }(),
        Parents);

    const FVector MeshDirBC = (MeshWorldPos[2] - MeshWorldPos[1]).GetSafeNormal();
    const FVector ActualDirBC = (ActualSkip[2] - ActualSkip[1]).GetSafeNormal();
    const float DotBC = FMath::Clamp(FVector::DotProduct(ActualDirBC, MeshDirBC), -1.f, 1.f);
    const float AngleBC = FMath::RadiansToDegrees(FMath::Acos(DotBC));
    TestTrue(
        FString::Printf(TEXT("SkipChain: B->C matches mesh direction (angle=%.2f)"), AngleBC),
        AngleBC <= 2.f);

    // C->D direction should still match source (not skipped, single-child parent C)
    const FVector SourceDirCD = (Expected[3] - Expected[2]).GetSafeNormal();
    const FVector ActualDirCD = (ActualSkip[3] - ActualSkip[2]).GetSafeNormal();
    const float DotCD = FMath::Clamp(FVector::DotProduct(ActualDirCD, SourceDirCD), -1.f, 1.f);
    const float AngleCD = FMath::RadiansToDegrees(FMath::Acos(DotCD));
    TestTrue(
        FString::Printf(TEXT("SkipChain: C->D matches source direction (angle=%.2f)"), AngleCD),
        AngleCD <= 2.f);

    // D->E direction should also match source
    const FVector SourceDirDE = (Expected[4] - Expected[3]).GetSafeNormal();
    const FVector ActualDirDE = (ActualSkip[4] - ActualSkip[3]).GetSafeNormal();
    const float DotDE = FMath::Clamp(FVector::DotProduct(ActualDirDE, SourceDirDE), -1.f, 1.f);
    const float AngleDE = FMath::RadiansToDegrees(FMath::Acos(DotDE));
    TestTrue(
        FString::Printf(TEXT("SkipChain: D->E matches source direction (angle=%.2f)"), AngleDE),
        AngleDE <= 2.f);

    return true;
}

// ---------------------------------------------------------------------------
// Test 15 — SkipCorrectionPoseAxisAlignment
// Same Hip->Knee->Ankle->Foot skeleton (source foot horizontal, mesh foot tilted).
// Skip correction on Ankle (preserves Ankle->Foot mesh direction).
// Apply a rotation to Ankle in pose. The Ankle's world rotation delta
// (rest -> posed) should be the same on both source and retargeted sides,
// ensuring the joint rotates around the same global axis.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_SkeletonRetargeting_SkipCorrectionPoseAxisAlignment,
    "RenderStream.SkeletonRetargeting.SkipCorrectionPoseAxisAlignment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTest_SkeletonRetargeting_SkipCorrectionPoseAxisAlignment::RunTest(const FString& Parameters)
{
    using namespace RenderStreamRetargeting;

    const TArray<FString> Names   = {"Hip", "Knee", "Ankle", "Foot"};
    const TArray<int32>   Parents = {INDEX_NONE, 0, 1, 2};

    // Source: vertical chain then horizontal foot
    const TArray<RenderStreamLink::Transform> SourceD3 = {
        {0.f, 0.f, 0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, -0.4f, 0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, -0.4f, 0.f, 0.f, 0.f, 1.f},
        {0.1f, 0.f, 0.f,  0.f, 0.f, 0.f, 1.f},
    };
    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(Names, SourceD3, Parents);

    // Mesh: same except foot tilts downward
    const TArray<RenderStreamLink::Transform> MeshD3 = {
        {0.f, 0.f, 0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, -0.4f, 0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, -0.4f, 0.f, 0.f, 0.f, 1.f},
        {0.08f, 0.f, -0.06f, 0.f, 0.f, 0.f, 1.f},
    };

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(D3ToUEOffsets(MeshD3), Parents);
    const TMap<FName, int32>        NameMap   = BuildIdentityNameMap(Names);

    TSet<FName> SkipSet;
    SkipSet.Add(FName("Ankle"));

    // 1) Compute REST-POSE world transforms
    const RenderStreamLink::FSkeletalPose IdentityPose = BuildD3IdentityPose(Layout);
    const TArray<FTransform> SourceRestTransforms = ComputeExpectedTransformsFromSource(Layout, IdentityPose);
    const TArray<FTransform> ActualRestTransforms = RunRetargetingTransforms(
        MeshBones, Layout, NameMap, IdentityPose, SkipSet);

    // Verify skip worked: Ankle->Foot rest directions should differ
    const FVector SourceRestDir = (SourceRestTransforms[3].GetTranslation() -
        SourceRestTransforms[2].GetTranslation()).GetSafeNormal();
    const FVector ActualRestDir = (ActualRestTransforms[3].GetTranslation() -
        ActualRestTransforms[2].GetTranslation()).GetSafeNormal();
    {
        const float RestAngle = FMath::RadiansToDegrees(FMath::Acos(
            FMath::Clamp(FVector::DotProduct(SourceRestDir, ActualRestDir), -1.f, 1.f)));
        TestTrue(
            FString::Printf(TEXT("SkipPoseAxis: Rest-pose foot directions differ (angle=%.1f)"), RestAngle),
            RestAngle > 5.f);
    }

    // 2) Apply 45 deg rotation on Ankle around d3 Z (up axis)
    const float H = FMath::DegreesToRadians(45.f) * 0.5f;
    RenderStreamLink::FSkeletalPose Pose = BuildD3IdentityPose(Layout);
    Pose.joints[2].transform = {0.f, 0.f, 0.f, 0.f, 0.f, FMath::Sin(H), FMath::Cos(H)};

    const TArray<FTransform> SourcePosedTransforms = ComputeExpectedTransformsFromSource(Layout, Pose);
    const TArray<FTransform> ActualPosedTransforms = RunRetargetingTransforms(
        MeshBones, Layout, NameMap, Pose, SkipSet);

    // 3) Compare the Ankle world rotation DELTA (rest -> posed).
    //    Both source and retargeted should apply the same global rotation.
    const FQuat SourceRestRot = SourceRestTransforms[2].GetRotation();
    const FQuat SourcePosedRot = SourcePosedTransforms[2].GetRotation();
    const FQuat SourceDelta = SourcePosedRot * SourceRestRot.Inverse();

    const FQuat ActualRestRot = ActualRestTransforms[2].GetRotation();
    const FQuat ActualPosedRot = ActualPosedTransforms[2].GetRotation();
    const FQuat ActualDelta = ActualPosedRot * ActualRestRot.Inverse();

    // Compare rotation deltas: compute the angular distance between them
    const FQuat DeltaDiff = SourceDelta * ActualDelta.Inverse();
    const float DeltaAngleDeg = FMath::RadiansToDegrees(DeltaDiff.GetAngle());
    TestTrue(
        FString::Printf(TEXT("SkipPoseAxis: Ankle rotation deltas match (diff=%.1f deg)"), DeltaAngleDeg),
        DeltaAngleDeg <= 5.f);

    // 4) Verify the rotation is non-trivial (close to 45 deg)
    const float SourceDeltaAngle = FMath::RadiansToDegrees(SourceDelta.GetAngle());
    TestTrue(
        FString::Printf(TEXT("SkipPoseAxis: Source rotation is ~45 deg (actual=%.1f)"), SourceDeltaAngle),
        SourceDeltaAngle > 30.f && SourceDeltaAngle < 60.f);

    return true;
}

// ---------------------------------------------------------------------------
// Test 16 — BoneLengthAlignment_IdenticalSkeletons
// Source == mesh, alignment enabled. All ratios should be 1.0, so output
// should be identical to alignment disabled.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_SkeletonRetargeting_BoneLengthAlignment_IdenticalSkeletons,
    "RenderStream.SkeletonRetargeting.BoneLengthAlignment_IdenticalSkeletons",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTest_SkeletonRetargeting_BoneLengthAlignment_IdenticalSkeletons::RunTest(const FString& Parameters)
{
    using namespace RenderStreamRetargeting;

    const TArray<FString> Names   = {"Pelvis", "Spine", "Chest", "Neck"};
    const TArray<int32>   Parents = {INDEX_NONE, 0, 1, 2};

    const RenderStreamLink::Transform D3Root   = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f};
    const RenderStreamLink::Transform D3Offset = {0.1f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f};
    const TArray<RenderStreamLink::Transform> D3T = {D3Root, D3Offset, D3Offset, D3Offset};

    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(Names, D3T, Parents);
    const RenderStreamLink::FSkeletalPose   Pose   = BuildD3IdentityPose(Layout);

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(D3ToUEOffsets(D3T), Parents);
    const TMap<FName, int32>        NameMap   = BuildIdentityNameMap(Names);

    const TArray<FVector> WithoutAlign = RunRetargeting(MeshBones, Layout, NameMap, Pose,
        TSet<FName>(), /*bAlignBoneLengths=*/ false);
    const TArray<FVector> WithAlign    = RunRetargeting(MeshBones, Layout, NameMap, Pose,
        TSet<FName>(), /*bAlignBoneLengths=*/ true);

    CheckPositions(this, WithAlign, WithoutAlign, 0.1f, TEXT("BoneLengthAlign_Identical"));
    return true;
}

// ---------------------------------------------------------------------------
// Test 17 — BoneLengthAlignment_DifferentLengths
// 4-bone collinear chain. Source bones: 15cm each. Mesh bones: 10cm each.
// Identity pose. With alignment: positions match source oracle.
// Without alignment: positions differ from source oracle.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_SkeletonRetargeting_BoneLengthAlignment_DifferentLengths,
    "RenderStream.SkeletonRetargeting.BoneLengthAlignment_DifferentLengths",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTest_SkeletonRetargeting_BoneLengthAlignment_DifferentLengths::RunTest(const FString& Parameters)
{
    using namespace RenderStreamRetargeting;

    const TArray<FString> Names   = {"Pelvis", "Spine", "Chest", "Neck"};
    const TArray<int32>   Parents = {INDEX_NONE, 0, 1, 2};

    // Source: 15cm offsets along d3 Z
    const TArray<RenderStreamLink::Transform> SourceD3 = {
        {0.f, 0.f, 0.f,  0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.15f, 0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.15f, 0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.15f, 0.f, 0.f, 0.f, 1.f},
    };
    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(Names, SourceD3, Parents);
    const RenderStreamLink::FSkeletalPose   Pose   = BuildD3IdentityPose(Layout);

    // Mesh: 10cm offsets along d3 Z (same direction, shorter)
    const TArray<RenderStreamLink::Transform> MeshD3 = {
        {0.f, 0.f, 0.f,  0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.10f, 0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.10f, 0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.10f, 0.f, 0.f, 0.f, 1.f},
    };

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(D3ToUEOffsets(MeshD3), Parents);
    const TMap<FName, int32>        NameMap   = BuildIdentityNameMap(Names);
    const TArray<FVector>           Expected  = ComputeExpectedPositionsFromSource(Layout, Pose);

    // Without alignment: positions differ from source (mesh bones are shorter)
    const TArray<FVector> WithoutAlign = RunRetargeting(MeshBones, Layout, NameMap, Pose,
        TSet<FName>(), /*bAlignBoneLengths=*/ false);

    // Neck (index 3) should be at 30cm (mesh) vs 45cm (source) — significant difference
    const float NeckDiffWithout = FVector::Dist(WithoutAlign[3], Expected[3]);
    TestTrue(
        FString::Printf(TEXT("BoneLengthAlign_Diff without: Neck differs from source (dist=%.1f cm)"), NeckDiffWithout),
        NeckDiffWithout > 10.f);

    // With alignment: positions match source oracle
    const TArray<FVector> WithAlign = RunRetargeting(MeshBones, Layout, NameMap, Pose,
        TSet<FName>(), /*bAlignBoneLengths=*/ true);

    CheckPositions(this, WithAlign, Expected, 0.5f, TEXT("BoneLengthAlign_Diff with align"));
    return true;
}

// ---------------------------------------------------------------------------
// Test 18 — BoneLengthAlignment_WithOrientationCorrection
// 3-bone chain. Source and mesh have different bone directions AND different
// bone lengths. With alignment: positions match source oracle.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_SkeletonRetargeting_BoneLengthAlignment_WithOrientationCorrection,
    "RenderStream.SkeletonRetargeting.BoneLengthAlignment_WithOrientationCorrection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTest_SkeletonRetargeting_BoneLengthAlignment_WithOrientationCorrection::RunTest(const FString& Parameters)
{
    using namespace RenderStreamRetargeting;

    const TArray<FString> Names   = {"Root", "A", "B"};
    const TArray<int32>   Parents = {INDEX_NONE, 0, 1};

    // Source: A is 15cm up (d3 Z), B is 10cm to the right (d3 X)
    const TArray<RenderStreamLink::Transform> SourceD3 = {
        {0.f,  0.f, 0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f,  0.f, 0.15f, 0.f, 0.f, 0.f, 1.f},
        {0.10f, 0.f, 0.f,  0.f, 0.f, 0.f, 1.f},
    };
    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(Names, SourceD3, Parents);
    const RenderStreamLink::FSkeletalPose   Pose   = BuildD3IdentityPose(Layout);

    // Mesh: A is 15cm up (same), B is 8cm along d3 Y (different direction AND length)
    const TArray<RenderStreamLink::Transform> MeshD3 = {
        {0.f,  0.f,  0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f,  0.f,  0.15f, 0.f, 0.f, 0.f, 1.f},
        {0.f,  0.08f, 0.f,  0.f, 0.f, 0.f, 1.f},
    };

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(D3ToUEOffsets(MeshD3), Parents);
    const TMap<FName, int32>        NameMap   = BuildIdentityNameMap(Names);
    const TArray<FVector>           Expected  = ComputeExpectedPositionsFromSource(Layout, Pose);

    // With alignment: orientation correction aligns direction, length alignment adjusts magnitude
    const TArray<FVector> WithAlign = RunRetargeting(MeshBones, Layout, NameMap, Pose,
        TSet<FName>(), /*bAlignBoneLengths=*/ true);

    CheckPositions(this, WithAlign, Expected, 0.5f, TEXT("BoneLengthAlign_WithOrientation"));

    // Without alignment: direction is correct but distance is wrong
    const TArray<FVector> WithoutAlign = RunRetargeting(MeshBones, Layout, NameMap, Pose,
        TSet<FName>(), /*bAlignBoneLengths=*/ false);
    const float BDiffWithout = FVector::Dist(WithoutAlign[2], Expected[2]);
    TestTrue(
        FString::Printf(TEXT("BoneLengthAlign_WithOrientation without: B differs (dist=%.1f cm)"), BDiffWithout),
        BDiffWithout > 1.f);

    return true;
}

// ---------------------------------------------------------------------------
// Test 19 — BoneLengthAlignment_ZeroLengthBone
// Chain includes a zero-length mesh bone. Should not crash or produce NaN.
// Other bones should still be correct.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_SkeletonRetargeting_BoneLengthAlignment_ZeroLengthBone,
    "RenderStream.SkeletonRetargeting.BoneLengthAlignment_ZeroLengthBone",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTest_SkeletonRetargeting_BoneLengthAlignment_ZeroLengthBone::RunTest(const FString& Parameters)
{
    using namespace RenderStreamRetargeting;

    // Root -> A -> B (zero-length in mesh) -> C
    const TArray<FString> Names   = {"Root", "A", "B", "C"};
    const TArray<int32>   Parents = {INDEX_NONE, 0, 1, 2};

    // Source: all bones have non-zero offsets
    const TArray<RenderStreamLink::Transform> SourceD3 = {
        {0.f, 0.f, 0.f,  0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.1f, 0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.1f, 0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.1f, 0.f, 0.f, 0.f, 1.f},
    };
    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(Names, SourceD3, Parents);
    const RenderStreamLink::FSkeletalPose   Pose   = BuildD3IdentityPose(Layout);

    // Mesh: B has zero offset (co-located with A)
    const TArray<RenderStreamLink::Transform> MeshD3 = {
        {0.f, 0.f, 0.f,  0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.1f, 0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.f,  0.f, 0.f, 0.f, 1.f},  // zero-length!
        {0.f, 0.f, 0.1f, 0.f, 0.f, 0.f, 1.f},
    };

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(D3ToUEOffsets(MeshD3), Parents);
    const TMap<FName, int32>        NameMap   = BuildIdentityNameMap(Names);

    // Should not crash
    const TArray<FVector> Result = RunRetargeting(MeshBones, Layout, NameMap, Pose,
        TSet<FName>(), /*bAlignBoneLengths=*/ true);

    // Verify no NaN
    for (int32 i = 0; i < Result.Num(); ++i)
    {
        TestFalse(
            FString::Printf(TEXT("BoneLengthAlign_ZeroLen bone[%d] no NaN"), i),
            Result[i].ContainsNaN());
    }

    // Verify bone count
    TestEqual(TEXT("BoneLengthAlign_ZeroLen bone count"), Result.Num(), 4);

    return true;
}

// ---------------------------------------------------------------------------
// Test 20 — BoneLengthAlignment_MultiChildParent
// Multi-child parent with children having different length ratios.
// Each child should be adjusted independently.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_SkeletonRetargeting_BoneLengthAlignment_MultiChildParent,
    "RenderStream.SkeletonRetargeting.BoneLengthAlignment_MultiChildParent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTest_SkeletonRetargeting_BoneLengthAlignment_MultiChildParent::RunTest(const FString& Parameters)
{
    using namespace RenderStreamRetargeting;

    // Root -> A -> {B, C}
    const TArray<FString> Names   = {"Root", "A", "B", "C"};
    const TArray<int32>   Parents = {INDEX_NONE, 0, 1, 1};

    // Source: B is 15cm right (d3 Y), C is 10cm left (d3 -Y)
    const TArray<RenderStreamLink::Transform> SourceD3 = {
        {0.f, 0.f, 0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.15f, 0.f, 0.f, 0.f, 1.f},
        {0.f, 0.15f, 0.f, 0.f, 0.f, 0.f, 1.f},
        {0.f, -0.10f, 0.f, 0.f, 0.f, 0.f, 1.f},
    };
    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(Names, SourceD3, Parents);
    const RenderStreamLink::FSkeletalPose   Pose   = BuildD3IdentityPose(Layout);

    // Mesh: B is 10cm right, C is 20cm left (different ratios)
    const TArray<RenderStreamLink::Transform> MeshD3 = {
        {0.f, 0.f, 0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.15f, 0.f, 0.f, 0.f, 1.f},
        {0.f, 0.10f, 0.f, 0.f, 0.f, 0.f, 1.f},   // ratio = 15/10 = 1.5
        {0.f, -0.20f, 0.f, 0.f, 0.f, 0.f, 1.f},   // ratio = 10/20 = 0.5
    };

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(D3ToUEOffsets(MeshD3), Parents);
    const TMap<FName, int32>        NameMap   = BuildIdentityNameMap(Names);
    const TArray<FVector>           Expected  = ComputeExpectedPositionsFromSource(Layout, Pose);

    // With alignment: each child adjusted to match source distances
    const TArray<FVector> WithAlign = RunRetargeting(MeshBones, Layout, NameMap, Pose,
        TSet<FName>(), /*bAlignBoneLengths=*/ true);

    // B distance from A should match source (15cm)
    const float BDistActual   = FVector::Dist(WithAlign[2], WithAlign[1]);
    const float BDistExpected = FVector::Dist(Expected[2], Expected[1]);
    TestTrue(
        FString::Printf(TEXT("BoneLengthAlign_MultiChild: B dist actual=%.1f expected=%.1f"), BDistActual, BDistExpected),
        FMath::Abs(BDistActual - BDistExpected) < 0.5f);

    // C distance from A should match source (10cm)
    const float CDistActual   = FVector::Dist(WithAlign[3], WithAlign[1]);
    const float CDistExpected = FVector::Dist(Expected[3], Expected[1]);
    TestTrue(
        FString::Printf(TEXT("BoneLengthAlign_MultiChild: C dist actual=%.1f expected=%.1f"), CDistActual, CDistExpected),
        FMath::Abs(CDistActual - CDistExpected) < 0.5f);

    // Without alignment: multi-child children still get positional corrections
    // (the offset compensates for the Kabsch rotation's directional residuals,
    // which inherently adjusts distances to match source for those children).
    const TArray<FVector> WithoutAlign = RunRetargeting(MeshBones, Layout, NameMap, Pose,
        TSet<FName>(), /*bAlignBoneLengths=*/ false);
    const float BDistNoAlign = FVector::Dist(WithoutAlign[2], WithoutAlign[1]);
    const float CDistNoAlign = FVector::Dist(WithoutAlign[3], WithoutAlign[1]);

    // Multi-child children match source positions even without bone length alignment
    TestTrue(
        FString::Printf(TEXT("BoneLengthAlign_MultiChild: B without align also matches (dist=%.1f vs expected=%.1f)"),
            BDistNoAlign, BDistExpected),
        FMath::Abs(BDistNoAlign - BDistExpected) < 0.5f);

    TestTrue(
        FString::Printf(TEXT("BoneLengthAlign_MultiChild: C without align also matches (dist=%.1f vs expected=%.1f)"),
            CDistNoAlign, CDistExpected),
        FMath::Abs(CDistNoAlign - CDistExpected) < 0.5f);

    return true;
}

// ---------------------------------------------------------------------------
// Test 21 — BoneLengthAlignment_UnmappedMeshIntermediate
// Mesh has an extra bone between two mapped bones.
// Source (3 bones): Root -> A -> B
// Mesh (4 bones):   Root -> A -> X(unmapped) -> B
// The ratio must compensate for X's contribution to the chain so that B's
// world position matches source B exactly.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_SkeletonRetargeting_BoneLengthAlignment_UnmappedMeshIntermediate,
    "RenderStream.SkeletonRetargeting.BoneLengthAlignment_UnmappedMeshIntermediate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTest_SkeletonRetargeting_BoneLengthAlignment_UnmappedMeshIntermediate::RunTest(const FString& Parameters)
{
    using namespace RenderStreamRetargeting;

    // --- Source skeleton (3 bones) ---
    const TArray<FString> SourceNames   = {"Root", "A", "B"};
    const TArray<int32>   SourceParents = {INDEX_NONE, 0, 1};

    // Source: A is 10cm up from Root, B is 10cm up from A
    const TArray<RenderStreamLink::Transform> SourceD3 = {
        {0.f, 0.f, 0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.10f, 0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.10f, 0.f, 0.f, 0.f, 1.f},
    };
    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(SourceNames, SourceD3, SourceParents);
    const RenderStreamLink::FSkeletalPose   Pose   = BuildD3IdentityPose(Layout);

    // --- Mesh skeleton (4 bones, X is unmapped intermediate) ---
    const TArray<int32> MeshParents = {INDEX_NONE, 0, 1, 2};

    // Mesh: A is 5cm up, X is 3cm up from A, B is 5cm up from X
    // Total mesh dist(A,B) = 8cm, but B's local offset is only 5cm
    const TArray<RenderStreamLink::Transform> MeshD3 = {
        {0.f, 0.f, 0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.05f, 0.f, 0.f, 0.f, 1.f},  // A: 5cm
        {0.f, 0.f, 0.03f, 0.f, 0.f, 0.f, 1.f},  // X: 3cm (unmapped intermediate)
        {0.f, 0.f, 0.05f, 0.f, 0.f, 0.f, 1.f},  // B: 5cm from X
    };

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(D3ToUEOffsets(MeshD3), MeshParents);

    // Map only Root, A, B (X at mesh index 2 is NOT mapped)
    TMap<FName, int32> NameMap;
    NameMap.Add(FName("Root"), 0);
    NameMap.Add(FName("A"), 1);
    NameMap.Add(FName("B"), 3);  // B is mesh index 3

    const TArray<FVector> Expected = ComputeExpectedPositionsFromSource(Layout, Pose);

    // With alignment: mapped bones should match source positions
    const TArray<FVector> Actual = RunRetargeting(MeshBones, Layout, NameMap, Pose,
        TSet<FName>(), /*bAlignBoneLengths=*/ true);

    // Source A world = (0, 10, 0) in UE coords (d3 Z -> UE Y)
    // Source B world = (0, 20, 0)
    // A (mesh index 1) should match source A (source index 1)
    const float ADist = FVector::Dist(Actual[1], Expected[1]);
    TestTrue(
        FString::Printf(TEXT("MeshIntermediate: A position (dist=%.2f cm)"), ADist),
        ADist < 0.5f);

    // B (mesh index 3) should match source B (source index 2)
    const float BDist = FVector::Dist(Actual[3], Expected[2]);
    TestTrue(
        FString::Printf(TEXT("MeshIntermediate: B position (dist=%.2f cm)"), BDist),
        BDist < 0.5f);

    // Without alignment: B should NOT match source (mesh chain is shorter)
    const TArray<FVector> NoAlign = RunRetargeting(MeshBones, Layout, NameMap, Pose,
        TSet<FName>(), /*bAlignBoneLengths=*/ false);
    const float BDistNoAlign = FVector::Dist(NoAlign[3], Expected[2]);
    TestTrue(
        FString::Printf(TEXT("MeshIntermediate: B without align differs (dist=%.2f cm)"), BDistNoAlign),
        BDistNoAlign > 2.f);

    return true;
}

// ---------------------------------------------------------------------------
// Test 22 — BoneLengthAlignment_UnmappedSourceIntermediate
// Source has an extra unmapped bone between two mapped bones.
// Source (4 bones): Root -> A -> B(unmapped) -> C
// Mesh (3 bones):   Root -> A -> C
// The ratio should account for the accumulated source distance through B.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_SkeletonRetargeting_BoneLengthAlignment_UnmappedSourceIntermediate,
    "RenderStream.SkeletonRetargeting.BoneLengthAlignment_UnmappedSourceIntermediate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTest_SkeletonRetargeting_BoneLengthAlignment_UnmappedSourceIntermediate::RunTest(const FString& Parameters)
{
    using namespace RenderStreamRetargeting;

    // --- Source skeleton (4 bones, B is unmapped) ---
    const TArray<FString> SourceNames   = {"Root", "A", "B", "C"};
    const TArray<int32>   SourceParents = {INDEX_NONE, 0, 1, 2};

    // Source: each bone 10cm up. Total Root-to-C = 30cm, A-to-C = 20cm through B
    const TArray<RenderStreamLink::Transform> SourceD3 = {
        {0.f, 0.f, 0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.10f, 0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.10f, 0.f, 0.f, 0.f, 1.f},  // B (unmapped)
        {0.f, 0.f, 0.10f, 0.f, 0.f, 0.f, 1.f},
    };
    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(SourceNames, SourceD3, SourceParents);
    const RenderStreamLink::FSkeletalPose   Pose   = BuildD3IdentityPose(Layout);

    // --- Mesh skeleton (3 bones, no B) ---
    const TArray<int32> MeshParents = {INDEX_NONE, 0, 1};

    // Mesh: A is 10cm up, C is 8cm up from A (shorter than source A-to-C = 20cm)
    const TArray<RenderStreamLink::Transform> MeshD3 = {
        {0.f, 0.f, 0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.10f, 0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.08f, 0.f, 0.f, 0.f, 1.f},
    };

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(D3ToUEOffsets(MeshD3), MeshParents);

    // Map Root, A, C (B not mapped)
    TMap<FName, int32> NameMap;
    NameMap.Add(FName("Root"), 0);
    NameMap.Add(FName("A"), 1);
    NameMap.Add(FName("C"), 2);

    const TArray<FVector> Expected = ComputeExpectedPositionsFromSource(Layout, Pose);

    // With alignment: C should match source C
    const TArray<FVector> Actual = RunRetargeting(MeshBones, Layout, NameMap, Pose,
        TSet<FName>(), /*bAlignBoneLengths=*/ true);

    // Source C world = (0, 30, 0), source index 3
    const float CDist = FVector::Dist(Actual[2], Expected[3]);
    TestTrue(
        FString::Printf(TEXT("SourceIntermediate: C position (dist=%.2f cm)"), CDist),
        CDist < 0.5f);

    // A should also match (same length in both)
    const float ADist = FVector::Dist(Actual[1], Expected[1]);
    TestTrue(
        FString::Printf(TEXT("SourceIntermediate: A position (dist=%.2f cm)"), ADist),
        ADist < 0.5f);

    return true;
}

// ---------------------------------------------------------------------------
// Test 23 — BoneLengthAlignment_WithPoseRotation
// Different bone lengths with a non-trivial pose. Verifies that length
// alignment and pose rotations compose correctly.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_SkeletonRetargeting_BoneLengthAlignment_WithPoseRotation,
    "RenderStream.SkeletonRetargeting.BoneLengthAlignment_WithPoseRotation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTest_SkeletonRetargeting_BoneLengthAlignment_WithPoseRotation::RunTest(const FString& Parameters)
{
    using namespace RenderStreamRetargeting;

    // 4-bone collinear chain with different lengths
    const TArray<FString> Names   = {"Root", "A", "B", "C"};
    const TArray<int32>   Parents = {INDEX_NONE, 0, 1, 2};

    // Source: 15cm offsets
    const TArray<RenderStreamLink::Transform> SourceD3 = {
        {0.f, 0.f, 0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.15f, 0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.15f, 0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.15f, 0.f, 0.f, 0.f, 1.f},
    };
    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(Names, SourceD3, Parents);

    // Apply 45 deg rotation on A around d3 Z
    const float H = FMath::DegreesToRadians(45.f) * 0.5f;
    RenderStreamLink::FSkeletalPose Pose = BuildD3IdentityPose(Layout);
    Pose.joints[1].transform = {0.f, 0.f, 0.f, 0.f, 0.f, FMath::Sin(H), FMath::Cos(H)};

    // Mesh: 10cm offsets (same direction, shorter)
    const TArray<RenderStreamLink::Transform> MeshD3 = {
        {0.f, 0.f, 0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.10f, 0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.10f, 0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.10f, 0.f, 0.f, 0.f, 1.f},
    };

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(D3ToUEOffsets(MeshD3), Parents);
    const TMap<FName, int32>        NameMap   = BuildIdentityNameMap(Names);
    const TArray<FVector>           Expected  = ComputeExpectedPositionsFromSource(Layout, Pose);

    // With alignment: positions should match source oracle despite different bone lengths
    const TArray<FVector> WithAlign = RunRetargeting(MeshBones, Layout, NameMap, Pose,
        TSet<FName>(), /*bAlignBoneLengths=*/ true);

    CheckPositions(this, WithAlign, Expected, 0.5f, TEXT("BoneLengthAlign_WithPose"));

    // Without alignment: positions differ because bone lengths don't match
    const TArray<FVector> WithoutAlign = RunRetargeting(MeshBones, Layout, NameMap, Pose,
        TSet<FName>(), /*bAlignBoneLengths=*/ false);
    const float CDist = FVector::Dist(WithoutAlign[3], Expected[3]);
    TestTrue(
        FString::Printf(TEXT("BoneLengthAlign_WithPose without: C differs (dist=%.1f cm)"), CDist),
        CDist > 5.f);

    return true;
}

// ---------------------------------------------------------------------------
// Test 24 — MultiChildExactPositions
// Verifies that multi-child parent children end up at exact source POSITIONS
// (not just correct directions) both with and without bone length alignment.
// Uses the realistic 18-bone skeleton with out-of-plane clavicle/hip offsets.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_SkeletonRetargeting_MultiChildExactPositions,
    "RenderStream.SkeletonRetargeting.MultiChildExactPositions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTest_SkeletonRetargeting_MultiChildExactPositions::RunTest(const FString& Parameters)
{
    using namespace RenderStreamRetargeting;

    const TArray<FString> Names = {
        "Pelvis", "Spine", "Chest", "Neck",
        "LeftClavicle", "LeftShoulder", "LeftElbow", "LeftWrist",
        "LeftHip", "LeftKnee", "LeftAnkle",
        "RightClavicle", "RightShoulder", "RightElbow", "RightWrist",
        "RightHip", "RightKnee", "RightAnkle"
    };
    const TArray<int32> Parents = {
        INDEX_NONE, 0, 1, 2,
        2, 4, 5, 6,
        0, 8, 9,
        2, 11, 12, 13,
        0, 15, 16,
    };

    // Source has forward offsets on clavicles/hips
    const TArray<RenderStreamLink::Transform> SourceD3 = {
        {0.f,    0.f,    0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f,   0.05f,  0.12f,  0.f, 0.f, 0.f, 1.f},
        {0.f,   0.05f,  0.12f,  0.f, 0.f, 0.f, 1.f},
        {0.f,   0.f,    0.15f,  0.f, 0.f, 0.f, 1.f},
        {0.05f, 0.15f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,   0.15f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,   0.28f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,   0.25f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {-0.05f, 0.1f, -0.05f,  0.f, 0.f, 0.f, 1.f},
        {-0.42f, 0.f,   0.f,    0.f, 0.f, 0.f, 1.f},
        {-0.4f,  0.f,   0.f,    0.f, 0.f, 0.f, 1.f},
        {0.05f,-0.15f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,  -0.15f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,  -0.28f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,  -0.25f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {-0.05f,-0.1f, -0.05f,  0.f, 0.f, 0.f, 1.f},
        {-0.42f, 0.f,   0.f,    0.f, 0.f, 0.f, 1.f},
        {-0.4f,  0.f,   0.f,    0.f, 0.f, 0.f, 1.f},
    };
    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(Names, SourceD3, Parents);
    const RenderStreamLink::FSkeletalPose   Pose   = BuildD3IdentityPose(Layout);

    // Mesh: purely lateral (no forward offsets on clavicles/hips)
    const TArray<RenderStreamLink::Transform> MeshD3 = {
        {0.f,    0.f,    0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f,   0.05f,  0.12f,  0.f, 0.f, 0.f, 1.f},
        {0.f,   0.05f,  0.12f,  0.f, 0.f, 0.f, 1.f},
        {0.f,   0.f,    0.15f,  0.f, 0.f, 0.f, 1.f},
        {0.f,   0.15f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,   0.15f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,   0.28f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,   0.25f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,   0.1f,  -0.05f,  0.f, 0.f, 0.f, 1.f},
        {-0.42f, 0.f,   0.f,    0.f, 0.f, 0.f, 1.f},
        {-0.4f,  0.f,   0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,  -0.15f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,  -0.15f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,  -0.28f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,  -0.25f,  0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,  -0.1f,  -0.05f,  0.f, 0.f, 0.f, 1.f},
        {-0.42f, 0.f,   0.f,    0.f, 0.f, 0.f, 1.f},
        {-0.4f,  0.f,   0.f,    0.f, 0.f, 0.f, 1.f},
    };

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(D3ToUEOffsets(MeshD3), Parents);
    const TMap<FName, int32>        NameMap   = BuildIdentityNameMap(Names);
    const TArray<FVector>           Expected  = ComputeExpectedPositionsFromSource(Layout, Pose);

    // With bone length alignment: all positions should match source oracle
    const TArray<FVector> WithAlign = RunRetargeting(MeshBones, Layout, NameMap, Pose,
        TSet<FName>(), /*bAlignBoneLengths=*/ true);
    CheckPositions(this, WithAlign, Expected, 0.5f, TEXT("MultiChildPositions_WithAlign"));

    // Without bone length alignment: directions should still be correct
    const TArray<FVector> WithoutAlign = RunRetargeting(MeshBones, Layout, NameMap, Pose);
    const TSet<int32> NoExclusions;
    CheckBoneDirections(this, WithoutAlign, Expected, Parents, NoExclusions, 2.0f,
        TEXT("MultiChildPositions_WithoutAlign"));

    return true;
}

// ---------------------------------------------------------------------------
// Test 25 — MultiChildWithPose
// Apply non-trivial pose rotations to a skeleton with multi-child parents
// and different source/mesh layouts. Verify positions match oracle.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_SkeletonRetargeting_MultiChildWithPose,
    "RenderStream.SkeletonRetargeting.MultiChildWithPose",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTest_SkeletonRetargeting_MultiChildWithPose::RunTest(const FString& Parameters)
{
    using namespace RenderStreamRetargeting;

    // 7-bone skeleton: Root → Spine → {Neck, LeftArm, RightArm}
    //                  LeftArm → LeftHand, RightArm → RightHand
    const TArray<FString> Names = {
        "Root", "Spine", "Neck", "LeftArm", "RightArm", "LeftHand", "RightHand"
    };
    const TArray<int32> Parents = {INDEX_NONE, 0, 1, 1, 1, 3, 4};

    // Source: shoulders have forward offset (z component in d3)
    const TArray<RenderStreamLink::Transform> SourceD3 = {
        {0.f,   0.f,   0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f,   0.f,  0.15f,  0.f, 0.f, 0.f, 1.f},
        {0.f,   0.f,  0.12f,  0.f, 0.f, 0.f, 1.f},
        {0.06f, 0.2f, 0.f,    0.f, 0.f, 0.f, 1.f},
        {0.06f,-0.2f, 0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,   0.25f, 0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f,  -0.25f, 0.f,   0.f, 0.f, 0.f, 1.f},
    };
    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(Names, SourceD3, Parents);

    // Mesh: shoulders are purely lateral (no forward offset)
    const TArray<RenderStreamLink::Transform> MeshD3 = {
        {0.f,   0.f,   0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f,   0.f,  0.15f,  0.f, 0.f, 0.f, 1.f},
        {0.f,   0.f,  0.12f,  0.f, 0.f, 0.f, 1.f},
        {0.f,   0.2f, 0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,  -0.2f, 0.f,    0.f, 0.f, 0.f, 1.f},
        {0.f,   0.25f, 0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f,  -0.25f, 0.f,   0.f, 0.f, 0.f, 1.f},
    };

    // Pose: 30° rotation on Spine (d3 Z axis), 45° on LeftArm (d3 X axis)
    RenderStreamLink::FSkeletalPose Pose = BuildD3IdentityPose(Layout);
    {
        const float H = FMath::DegreesToRadians(30.f) * 0.5f;
        Pose.joints[1].transform = {0.f, 0.f, 0.f, 0.f, 0.f, FMath::Sin(H), FMath::Cos(H)};
    }
    {
        const float H = FMath::DegreesToRadians(45.f) * 0.5f;
        Pose.joints[3].transform = {0.f, 0.f, 0.f, FMath::Sin(H), 0.f, 0.f, FMath::Cos(H)};
    }

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(D3ToUEOffsets(MeshD3), Parents);
    const TMap<FName, int32>        NameMap   = BuildIdentityNameMap(Names);
    const TArray<FVector>           Expected  = ComputeExpectedPositionsFromSource(Layout, Pose);

    // With bone length alignment: positions should match source oracle
    const TArray<FVector> WithAlign = RunRetargeting(MeshBones, Layout, NameMap, Pose,
        TSet<FName>(), /*bAlignBoneLengths=*/ true);
    CheckPositions(this, WithAlign, Expected, 0.5f, TEXT("MultiChildWithPose_Aligned"));

    // Without alignment: directions should still be correct
    const TArray<FVector> WithoutAlign = RunRetargeting(MeshBones, Layout, NameMap, Pose);
    const TSet<int32> NoExclusions;
    CheckBoneDirections(this, WithoutAlign, Expected, Parents, NoExclusions, 2.0f,
        TEXT("MultiChildWithPose_Unaligned"));

    return true;
}

// ---------------------------------------------------------------------------
// Test 26 — MultiChildParent_NoRootTilt
// Regression test: when Pelvis is a multi-child root (children: Spine,
// LeftHip, RightHip) and source/mesh differ in hip offsets, the root bone
// must NOT be tilted. We verify by checking that the rest-pose world
// rotation of Pelvis is unchanged from the mesh rest-pose rotation.
// A bug where Kabsch WOD leaked into LIOD caused the root to tilt forward.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_SkeletonRetargeting_MultiChildParent_NoRootTilt,
    "RenderStream.SkeletonRetargeting.MultiChildParent_NoRootTilt",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTest_SkeletonRetargeting_MultiChildParent_NoRootTilt::RunTest(const FString& Parameters)
{
    using namespace RenderStreamRetargeting;

    // Pelvis(0) -> {Spine(1), LeftHip(2), RightHip(3)}
    // Spine -> Chest(4) -> Neck(5)
    // LeftHip -> LeftKnee(6), RightHip -> RightKnee(7)
    const TArray<FString> Names = {
        "Pelvis", "Spine", "LeftHip", "RightHip",
        "Chest", "Neck", "LeftKnee", "RightKnee"
    };
    const TArray<int32> Parents = {
        INDEX_NONE, 0, 0, 0,
        1, 4, 2, 3
    };

    // Source: hips have d3 X forward offset
    const TArray<RenderStreamLink::Transform> SourceD3 = {
        {0.f,    0.f,   0.f,   0.f, 0.f, 0.f, 1.f},  // Pelvis
        {0.f,    0.f,   0.15f, 0.f, 0.f, 0.f, 1.f},  // Spine
        {-0.05f, 0.1f, -0.05f, 0.f, 0.f, 0.f, 1.f},  // LeftHip (forward offset)
        {-0.05f,-0.1f, -0.05f, 0.f, 0.f, 0.f, 1.f},  // RightHip (forward offset)
        {0.f,    0.f,   0.15f, 0.f, 0.f, 0.f, 1.f},  // Chest
        {0.f,    0.f,   0.12f, 0.f, 0.f, 0.f, 1.f},  // Neck
        {-0.42f, 0.f,   0.f,   0.f, 0.f, 0.f, 1.f},  // LeftKnee
        {-0.42f, 0.f,   0.f,   0.f, 0.f, 0.f, 1.f},  // RightKnee
    };
    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(Names, SourceD3, Parents);
    const RenderStreamLink::FSkeletalPose   Pose   = BuildD3IdentityPose(Layout);

    // Mesh: hips are purely lateral (no forward offset)
    const TArray<RenderStreamLink::Transform> MeshD3 = {
        {0.f,    0.f,   0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f,    0.f,   0.15f, 0.f, 0.f, 0.f, 1.f},
        {0.f,    0.1f, -0.05f, 0.f, 0.f, 0.f, 1.f},  // LeftHip (no forward)
        {0.f,   -0.1f, -0.05f, 0.f, 0.f, 0.f, 1.f},  // RightHip (no forward)
        {0.f,    0.f,   0.15f, 0.f, 0.f, 0.f, 1.f},
        {0.f,    0.f,   0.12f, 0.f, 0.f, 0.f, 1.f},
        {-0.42f, 0.f,   0.f,   0.f, 0.f, 0.f, 1.f},
        {-0.42f, 0.f,   0.f,   0.f, 0.f, 0.f, 1.f},
    };

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(D3ToUEOffsets(MeshD3), Parents);
    const TMap<FName, int32>        NameMap   = BuildIdentityNameMap(Names);

    // Get retargeted rest-pose transforms
    const TArray<FTransform> ActualTransforms = RunRetargetingTransforms(
        MeshBones, Layout, NameMap, Pose);

    // Get mesh rest-pose transforms
    TArray<FTransform> MeshLocalTransforms;
    MeshLocalTransforms.SetNum(MeshBones.Num());
    for (int32 i = 0; i < MeshBones.Num(); ++i)
        MeshLocalTransforms[i] = MeshBones[i].LocalTransform;
    TArray<int32> MeshParents;
    MeshParents.SetNum(Parents.Num());
    for (int32 i = 0; i < Parents.Num(); ++i)
        MeshParents[i] = Parents[i];
    const TArray<FTransform> MeshWorldTransforms = ComputeWorldTransforms(MeshLocalTransforms, MeshParents);

    // Pelvis (root, multi-child parent) rotation should be unchanged
    {
        const FQuat ActualRot = ActualTransforms[0].GetRotation();
        const FQuat MeshRot   = MeshWorldTransforms[0].GetRotation();
        const FQuat Diff = ActualRot * MeshRot.Inverse();
        const float DiffAngle = FMath::RadiansToDegrees(Diff.GetAngle());
        TestTrue(
            FString::Printf(TEXT("NoRootTilt: Pelvis rotation unchanged (diff=%.2f deg)"), DiffAngle),
            DiffAngle <= 1.0f);
    }

    // Spine rotation should also be unchanged (single-child of Pelvis,
    // should not get compensating LIOD for a non-existent Pelvis WOD)
    {
        const FQuat ActualRot = ActualTransforms[1].GetRotation();
        const FQuat MeshRot   = MeshWorldTransforms[1].GetRotation();
        const FQuat Diff = ActualRot * MeshRot.Inverse();
        const float DiffAngle = FMath::RadiansToDegrees(Diff.GetAngle());
        TestTrue(
            FString::Printf(TEXT("NoRootTilt: Spine rotation unchanged (diff=%.2f deg)"), DiffAngle),
            DiffAngle <= 1.0f);
    }

    // Positions should still be correct
    const TArray<FVector> ActualPositions = RunRetargeting(MeshBones, Layout, NameMap, Pose);
    const TArray<FVector> Expected = ComputeExpectedPositionsFromSource(Layout, Pose);
    const TSet<int32> NoExclusions;
    CheckBoneDirections(this, ActualPositions, Expected, Parents, NoExclusions, 2.0f,
        TEXT("NoRootTilt"));

    return true;
}

// ---------------------------------------------------------------------------
// Test 27 — MultiChildParent_NoHeadTilt
// Regression test: when Chest is a non-root multi-child parent (children:
// Neck, LeftClavicle, RightClavicle) and source has forward clavicle
// offsets, the Chest/Neck/Head chain must NOT be tilted backward.
// A bug where Kabsch WOD leaked into LIOD caused backward head tilt.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_SkeletonRetargeting_MultiChildParent_NoHeadTilt,
    "RenderStream.SkeletonRetargeting.MultiChildParent_NoHeadTilt",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTest_SkeletonRetargeting_MultiChildParent_NoHeadTilt::RunTest(const FString& Parameters)
{
    using namespace RenderStreamRetargeting;

    // Pelvis(0) -> Spine(1) -> Chest(2) -> {Neck(3), LeftClav(4), RightClav(5)}
    // Neck -> Head(6)
    // LeftClav -> LeftShoulder(7), RightClav -> RightShoulder(8)
    const TArray<FString> Names = {
        "Pelvis", "Spine", "Chest", "Neck", "LeftClavicle", "RightClavicle",
        "Head", "LeftShoulder", "RightShoulder"
    };
    const TArray<int32> Parents = {
        INDEX_NONE, 0, 1, 2, 2, 2,
        3, 4, 5
    };

    // Source: clavicles have forward d3 X offset
    const TArray<RenderStreamLink::Transform> SourceD3 = {
        {0.f,    0.f,   0.f,   0.f, 0.f, 0.f, 1.f},  // Pelvis
        {0.f,    0.f,   0.15f, 0.f, 0.f, 0.f, 1.f},  // Spine
        {0.f,    0.f,   0.15f, 0.f, 0.f, 0.f, 1.f},  // Chest
        {0.f,    0.f,   0.12f, 0.f, 0.f, 0.f, 1.f},  // Neck
        {0.05f,  0.15f, 0.f,   0.f, 0.f, 0.f, 1.f},  // LeftClav (forward)
        {0.05f, -0.15f, 0.f,   0.f, 0.f, 0.f, 1.f},  // RightClav (forward)
        {0.f,    0.f,   0.1f,  0.f, 0.f, 0.f, 1.f},  // Head
        {0.f,    0.2f,  0.f,   0.f, 0.f, 0.f, 1.f},  // LeftShoulder
        {0.f,   -0.2f,  0.f,   0.f, 0.f, 0.f, 1.f},  // RightShoulder
    };
    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(Names, SourceD3, Parents);
    const RenderStreamLink::FSkeletalPose   Pose   = BuildD3IdentityPose(Layout);

    // Mesh: clavicles are purely lateral
    const TArray<RenderStreamLink::Transform> MeshD3 = {
        {0.f,    0.f,   0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f,    0.f,   0.15f, 0.f, 0.f, 0.f, 1.f},
        {0.f,    0.f,   0.15f, 0.f, 0.f, 0.f, 1.f},
        {0.f,    0.f,   0.12f, 0.f, 0.f, 0.f, 1.f},
        {0.f,    0.15f, 0.f,   0.f, 0.f, 0.f, 1.f},  // LeftClav (no forward)
        {0.f,   -0.15f, 0.f,   0.f, 0.f, 0.f, 1.f},  // RightClav (no forward)
        {0.f,    0.f,   0.1f,  0.f, 0.f, 0.f, 1.f},
        {0.f,    0.2f,  0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f,   -0.2f,  0.f,   0.f, 0.f, 0.f, 1.f},
    };

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(D3ToUEOffsets(MeshD3), Parents);
    const TMap<FName, int32>        NameMap   = BuildIdentityNameMap(Names);

    const TArray<FTransform> ActualTransforms = RunRetargetingTransforms(
        MeshBones, Layout, NameMap, Pose);

    TArray<FTransform> MeshLocalTransforms;
    MeshLocalTransforms.SetNum(MeshBones.Num());
    for (int32 i = 0; i < MeshBones.Num(); ++i)
        MeshLocalTransforms[i] = MeshBones[i].LocalTransform;
    TArray<int32> MeshParents;
    MeshParents.SetNum(Parents.Num());
    for (int32 i = 0; i < Parents.Num(); ++i)
        MeshParents[i] = Parents[i];
    const TArray<FTransform> MeshWorldTransforms = ComputeWorldTransforms(MeshLocalTransforms, MeshParents);

    // Chest (multi-child parent) rotation should be unchanged
    {
        const FQuat ActualRot = ActualTransforms[2].GetRotation();
        const FQuat MeshRot   = MeshWorldTransforms[2].GetRotation();
        const FQuat Diff = ActualRot * MeshRot.Inverse();
        const float DiffAngle = FMath::RadiansToDegrees(Diff.GetAngle());
        TestTrue(
            FString::Printf(TEXT("NoHeadTilt: Chest rotation unchanged (diff=%.2f deg)"), DiffAngle),
            DiffAngle <= 1.0f);
    }

    // Neck rotation should be unchanged (Neck→Head is same direction in source and mesh)
    {
        const FQuat ActualRot = ActualTransforms[3].GetRotation();
        const FQuat MeshRot   = MeshWorldTransforms[3].GetRotation();
        const FQuat Diff = ActualRot * MeshRot.Inverse();
        const float DiffAngle = FMath::RadiansToDegrees(Diff.GetAngle());
        TestTrue(
            FString::Printf(TEXT("NoHeadTilt: Neck rotation unchanged (diff=%.2f deg)"), DiffAngle),
            DiffAngle <= 1.0f);
    }

    // Head rotation should be unchanged
    {
        const FQuat ActualRot = ActualTransforms[6].GetRotation();
        const FQuat MeshRot   = MeshWorldTransforms[6].GetRotation();
        const FQuat Diff = ActualRot * MeshRot.Inverse();
        const float DiffAngle = FMath::RadiansToDegrees(Diff.GetAngle());
        TestTrue(
            FString::Printf(TEXT("NoHeadTilt: Head rotation unchanged (diff=%.2f deg)"), DiffAngle),
            DiffAngle <= 1.0f);
    }

    // Positions should still be correct
    const TArray<FVector> ActualPositions = RunRetargeting(MeshBones, Layout, NameMap, Pose);
    const TArray<FVector> Expected = ComputeExpectedPositionsFromSource(Layout, Pose);
    const TSet<int32> NoExclusions;
    CheckBoneDirections(this, ActualPositions, Expected, Parents, NoExclusions, 2.0f,
        TEXT("NoHeadTilt"));

    return true;
}
