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
    const RenderStreamLink::FSkeletalPose&   Pose)
{
    using namespace RenderStreamRetargeting;

    FRetargetInitData InitData;
    InitialiseRetargeting(MeshBones, Layout, NameMap, InitData);

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
// Spine has 3 children: children of Spine are skipped in direction check.
// No single-child descendants to check here, so this just verifies no crash.
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

    const TSet<int32> MultiChildParents = FindMultiChildParents(Parents);
    // All non-root bones are children of Spine (multi-child), so direction checks are skipped.
    // This test validates no crash and correct bone count.
    CheckBoneDirections(this, Actual, Expected, Parents, MultiChildParents, 1.0f, TEXT("OutOfPlaneMultiChild"));
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

    const TSet<int32> MultiChildParents = FindMultiChildParents(Parents);
    CheckBoneDirections(this, Actual, Expected, Parents, MultiChildParents, 2.0f, TEXT("RealisticAlternativeLayout"));
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
