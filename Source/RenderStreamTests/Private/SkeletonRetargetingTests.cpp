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

// ---------------------------------------------------------------------------
// Test 1 — IdentityPose
// 4-bone collinear chain. Source layout == mesh layout. Identity pose.
// Expected: positions match source layout world positions.
// Passes with current code (baseline correctness).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_SkeletonRetargeting_IdentityPose,
    "RenderStream.SkeletonRetargeting.IdentityPose",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTest_SkeletonRetargeting_IdentityPose::RunTest(const FString& Parameters)
{
    using namespace RenderStreamRetargeting;

    const TArray<FString> Names   = {"Pelvis", "Spine", "Chest", "Neck"};
    const TArray<int32>   Parents = {INDEX_NONE, 0, 1, 2};

    // All bones at d3 X offsets of 0.1 m (root at origin)
    const RenderStreamLink::Transform D3Root   = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f};
    const RenderStreamLink::Transform D3Offset = {0.1f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f};
    const TArray<RenderStreamLink::Transform> D3T = {D3Root, D3Offset, D3Offset, D3Offset};

    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(Names, D3T, Parents);
    const RenderStreamLink::FSkeletalPose   Pose   = BuildD3IdentityPose(Layout);

    // Mesh offsets derived from same d3 data (source == mesh)
    TArray<FVector> UEOffsets;
    UEOffsets.SetNum(Names.Num());
    for (int32 i = 0; i < Names.Num(); ++i)
        UEOffsets[i] = ConvertD3TransformToUE(D3T[i]).GetTranslation();

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(UEOffsets, Parents);
    const TMap<FName, int32>        NameMap   = BuildIdentityNameMap(Names);

    const TArray<FVector> Actual   = RunRetargeting(MeshBones, Layout, NameMap, Pose);
    const TArray<FVector> Expected = ComputeExpectedPositionsFromSource(Layout, Pose);

    CheckPositions(this, Actual, Expected, 0.1f, TEXT("IdentityPose"));
    return true;
}

// ---------------------------------------------------------------------------
// Test 2 — SimpleRotation
// 4-bone collinear chain. Source == mesh layout. Spine rotated 90° around d3 Z.
// Expected: child chain pivots 90° around Spine's world position.
// Passes with current code (validates single-child rotation path).
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

    // Pose: Spine gets 90° rotation around d3 Z (vertical axis)
    const float HalfAngle = FMath::DegreesToRadians(90.f) * 0.5f;
    RenderStreamLink::FSkeletalPose Pose = BuildD3IdentityPose(Layout);
    // d3 quat for Z rotation: (rx=0, ry=0, rz=sin, rw=cos)
    Pose.joints[1].transform = {0.f, 0.f, 0.f, 0.f, 0.f, FMath::Sin(HalfAngle), FMath::Cos(HalfAngle)};

    TArray<FVector> UEOffsets;
    UEOffsets.SetNum(Names.Num());
    for (int32 i = 0; i < Names.Num(); ++i)
        UEOffsets[i] = ConvertD3TransformToUE(D3T[i]).GetTranslation();

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(UEOffsets, Parents);
    const TMap<FName, int32>        NameMap   = BuildIdentityNameMap(Names);

    const TArray<FVector> Actual   = RunRetargeting(MeshBones, Layout, NameMap, Pose);
    const TArray<FVector> Expected = ComputeExpectedPositionsFromSource(Layout, Pose);

    CheckPositions(this, Actual, Expected, 0.5f, TEXT("SimpleRotation"));
    return true;
}

// ---------------------------------------------------------------------------
// Test 3 — OutOfPlaneSingleChild
// 3-bone chain: Pelvis → Spine → RightShoulder.
// Mesh shoulder offset: purely d3 Y. Source shoulder offset: purely d3 X.
// Spine has exactly ONE child (RightShoulder), so orientation correction IS applied.
// Source pose: identity.
// Expected: retargeted shoulder matches source world position.
// Passes with current code (single-child code path works).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_SkeletonRetargeting_OutOfPlaneSingleChild,
    "RenderStream.SkeletonRetargeting.OutOfPlaneSingleChild",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTest_SkeletonRetargeting_OutOfPlaneSingleChild::RunTest(const FString& Parameters)
{
    using namespace RenderStreamRetargeting;

    // Pelvis(0) -> Spine(1) -> RightShoulder(2)
    const TArray<FString> Names   = {"Pelvis", "Spine", "RightShoulder"};
    const TArray<int32>   Parents = {INDEX_NONE, 0, 1};

    // Source layout: Spine along d3 Z, RightShoulder purely along d3 X from Spine
    const TArray<RenderStreamLink::Transform> SourceD3 = {
        {0.f,  0.f, 0.f,  0.f, 0.f, 0.f, 1.f},   // Pelvis (root at origin)
        {0.f,  0.f, 0.1f, 0.f, 0.f, 0.f, 1.f},   // Spine  (up 0.1 m)
        {0.1f, 0.f, 0.f,  0.f, 0.f, 0.f, 1.f},   // RightShoulder (0.1 m along d3 X)
    };
    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(Names, SourceD3, Parents);
    const RenderStreamLink::FSkeletalPose   Pose   = BuildD3IdentityPose(Layout);

    // Mesh layout: same Spine, but RightShoulder purely along d3 Y (different direction)
    const TArray<RenderStreamLink::Transform> MeshD3 = {
        {0.f,  0.f,  0.f,  0.f, 0.f, 0.f, 1.f},  // Pelvis
        {0.f,  0.f,  0.1f, 0.f, 0.f, 0.f, 1.f},  // Spine
        {0.f,  0.1f, 0.f,  0.f, 0.f, 0.f, 1.f},  // RightShoulder (0.1 m along d3 Y)
    };
    TArray<FVector> UEOffsets;
    UEOffsets.SetNum(Names.Num());
    for (int32 i = 0; i < Names.Num(); ++i)
        UEOffsets[i] = ConvertD3TransformToUE(MeshD3[i]).GetTranslation();

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(UEOffsets, Parents);
    const TMap<FName, int32>        NameMap   = BuildIdentityNameMap(Names);

    const TArray<FVector> Actual   = RunRetargeting(MeshBones, Layout, NameMap, Pose);
    const TArray<FVector> Expected = ComputeExpectedPositionsFromSource(Layout, Pose);

    CheckPositions(this, Actual, Expected, 0.5f, TEXT("OutOfPlaneSingleChild"));
    return true;
}

// ---------------------------------------------------------------------------
// Test 4 — OutOfPlaneMultiChild
// 5-bone skeleton: Pelvis → Spine → {Neck, LeftShoulder, RightShoulder}.
// Spine has 3 children, so orientation correction is SKIPPED (known bug).
// Source shoulders have an out-of-plane d3 X component; mesh shoulders are purely lateral.
// NOTE: This test documents a known bug. It is expected to FAIL until the
// multi-child orientation correction is implemented.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTest_SkeletonRetargeting_OutOfPlaneMultiChild,
    "RenderStream.SkeletonRetargeting.OutOfPlaneMultiChild",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTest_SkeletonRetargeting_OutOfPlaneMultiChild::RunTest(const FString& Parameters)
{
    using namespace RenderStreamRetargeting;

    // Pelvis(0) -> Spine(1) -> Neck(2), LeftShoulder(3), RightShoulder(4)
    const TArray<FString> Names   = {"Pelvis", "Spine", "Neck", "LeftShoulder", "RightShoulder"};
    const TArray<int32>   Parents = {INDEX_NONE, 0, 1, 1, 1};

    // Source: shoulders have an out-of-plane d3 X component of 0.08 m
    const TArray<RenderStreamLink::Transform> SourceD3 = {
        {0.f,    0.f,    0.f,  0.f, 0.f, 0.f, 1.f},  // Pelvis (root)
        {0.f,    0.f,   0.1f,  0.f, 0.f, 0.f, 1.f},  // Spine
        {0.f,    0.f,   0.1f,  0.f, 0.f, 0.f, 1.f},  // Neck
        {0.08f,  0.1f,  0.f,   0.f, 0.f, 0.f, 1.f},  // LeftShoulder  (X out-of-plane)
        {0.08f, -0.1f,  0.f,   0.f, 0.f, 0.f, 1.f},  // RightShoulder (X out-of-plane)
    };
    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(Names, SourceD3, Parents);
    const RenderStreamLink::FSkeletalPose   Pose   = BuildD3IdentityPose(Layout);

    // Mesh: shoulders purely lateral (no out-of-plane X)
    const TArray<RenderStreamLink::Transform> MeshD3 = {
        {0.f,   0.f,   0.f,  0.f, 0.f, 0.f, 1.f},
        {0.f,   0.f,  0.1f,  0.f, 0.f, 0.f, 1.f},
        {0.f,   0.f,  0.1f,  0.f, 0.f, 0.f, 1.f},
        {0.f,   0.1f, 0.f,   0.f, 0.f, 0.f, 1.f},
        {0.f,  -0.1f, 0.f,   0.f, 0.f, 0.f, 1.f},
    };
    TArray<FVector> UEOffsets;
    UEOffsets.SetNum(Names.Num());
    for (int32 i = 0; i < Names.Num(); ++i)
        UEOffsets[i] = ConvertD3TransformToUE(MeshD3[i]).GetTranslation();

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(UEOffsets, Parents);
    const TMap<FName, int32>        NameMap   = BuildIdentityNameMap(Names);

    const TArray<FVector> Actual   = RunRetargeting(MeshBones, Layout, NameMap, Pose);
    const TArray<FVector> Expected = ComputeExpectedPositionsFromSource(Layout, Pose);

    CheckPositions(this, Actual, Expected, 0.5f, TEXT("OutOfPlaneMultiChild"));
    return true;
}

// ---------------------------------------------------------------------------
// Test 5 — FullDefaultLayoutIdentityPose
// Full 18-bone Default layout. Source == mesh. Identity pose.
// Root bone at d3 origin (world position managed by actor, not skeleton hierarchy).
// Smoke test: validates no regressions on full skeleton.
// Passes with current code.
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
        INDEX_NONE,  // Pelvis
        0,           // Spine
        1,           // Chest
        2,           // Neck
        2,           // LeftClavicle
        4,           // LeftShoulder
        5,           // LeftElbow
        6,           // LeftWrist
        0,           // LeftHip
        8,           // LeftKnee
        9,           // LeftAnkle
        2,           // RightClavicle
        11,          // RightShoulder
        12,          // RightElbow
        13,          // RightWrist
        0,           // RightHip
        15,          // RightKnee
        16,          // RightAnkle
    };

    // NOTE: Pelvis at (0,0,0) — actor root manages world position, skeleton hierarchy is relative.
    const TArray<RenderStreamLink::Transform> D3T = {
        {0.f,    0.f,    0.f,   0.f, 0.f, 0.f, 1.f},  // Pelvis (root at origin)
        {0.f,   0.05f,  0.12f,  0.f, 0.f, 0.f, 1.f},  // Spine
        {0.f,   0.05f,  0.12f,  0.f, 0.f, 0.f, 1.f},  // Chest
        {0.f,   0.f,    0.15f,  0.f, 0.f, 0.f, 1.f},  // Neck
        {0.f,   0.15f,  0.05f,  0.f, 0.f, 0.f, 1.f},  // LeftClavicle
        {0.f,   0.15f,  0.f,    0.f, 0.f, 0.f, 1.f},  // LeftShoulder
        {0.f,   0.28f,  0.f,    0.f, 0.f, 0.f, 1.f},  // LeftElbow
        {0.f,   0.25f,  0.f,    0.f, 0.f, 0.f, 1.f},  // LeftWrist
        {-0.05f, 0.1f, -0.05f,  0.f, 0.f, 0.f, 1.f},  // LeftHip
        {-0.42f, 0.f,   0.f,    0.f, 0.f, 0.f, 1.f},  // LeftKnee
        {-0.4f,  0.f,   0.f,    0.f, 0.f, 0.f, 1.f},  // LeftAnkle
        {0.f,  -0.15f,  0.05f,  0.f, 0.f, 0.f, 1.f},  // RightClavicle
        {0.f,  -0.15f,  0.f,    0.f, 0.f, 0.f, 1.f},  // RightShoulder
        {0.f,  -0.28f,  0.f,    0.f, 0.f, 0.f, 1.f},  // RightElbow
        {0.f,  -0.25f,  0.f,    0.f, 0.f, 0.f, 1.f},  // RightWrist
        {-0.05f,-0.1f, -0.05f,  0.f, 0.f, 0.f, 1.f},  // RightHip
        {-0.42f, 0.f,   0.f,    0.f, 0.f, 0.f, 1.f},  // RightKnee
        {-0.4f,  0.f,   0.f,    0.f, 0.f, 0.f, 1.f},  // RightAnkle
    };

    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(Names, D3T, Parents);
    const RenderStreamLink::FSkeletalPose   Pose   = BuildD3IdentityPose(Layout);

    // Mesh == source: derive UE offsets from same d3 data
    TArray<FVector> UEOffsets;
    UEOffsets.SetNum(Names.Num());
    for (int32 i = 0; i < Names.Num(); ++i)
        UEOffsets[i] = ConvertD3TransformToUE(D3T[i]).GetTranslation();

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(UEOffsets, Parents);
    const TMap<FName, int32>        NameMap   = BuildIdentityNameMap(Names);

    const TArray<FVector> Actual   = RunRetargeting(MeshBones, Layout, NameMap, Pose);
    const TArray<FVector> Expected = ComputeExpectedPositionsFromSource(Layout, Pose);

    CheckPositions(this, Actual, Expected, 0.1f, TEXT("FullDefaultLayoutIdentityPose"));
    return true;
}

// ---------------------------------------------------------------------------
// Test 6 — RealisticAlternativeLayout
// Full 18-bone skeleton. Source clavicles and hips have out-of-plane offsets
// (forward + lateral); mesh has same lateral magnitude but no out-of-plane
// component. Arm/leg lengths identical so oracle (source positions) matches
// correctly-retargeted mesh positions.
// Non-trivial animated pose: left arm raised 45°, 10° torso twist, right hip 15°.
// NOTE: Expected to FAIL with current code (multi-child orientation correction
// skipped for Chest which has 3 children). PASSES after bug fix.
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
        INDEX_NONE,  // Pelvis
        0,           // Spine
        1,           // Chest
        2,           // Neck
        2,           // LeftClavicle
        4,           // LeftShoulder
        5,           // LeftElbow
        6,           // LeftWrist
        0,           // LeftHip
        8,           // LeftKnee
        9,           // LeftAnkle
        2,           // RightClavicle
        11,          // RightShoulder
        12,          // RightElbow
        13,          // RightWrist
        0,           // RightHip
        15,          // RightKnee
        16,          // RightAnkle
    };

    // Source layout: clavicles have out-of-plane forward (d3 X) component;
    // hips have out-of-plane forward (d3 X) component.
    // NOTE: Pelvis at origin; all offsets are relative to parent.
    const TArray<RenderStreamLink::Transform> SourceD3 = {
        {0.f,    0.f,    0.f,   0.f, 0.f, 0.f, 1.f},   // Pelvis (root)
        {0.f,   0.05f,  0.12f,  0.f, 0.f, 0.f, 1.f},   // Spine
        {0.f,   0.05f,  0.12f,  0.f, 0.f, 0.f, 1.f},   // Chest
        {0.f,   0.f,    0.15f,  0.f, 0.f, 0.f, 1.f},   // Neck
        {0.05f, 0.15f,  0.f,    0.f, 0.f, 0.f, 1.f},   // LeftClavicle  (X out-of-plane)
        {0.f,   0.15f,  0.f,    0.f, 0.f, 0.f, 1.f},   // LeftShoulder
        {0.f,   0.28f,  0.f,    0.f, 0.f, 0.f, 1.f},   // LeftElbow
        {0.f,   0.25f,  0.f,    0.f, 0.f, 0.f, 1.f},   // LeftWrist
        {-0.05f, 0.1f, -0.05f,  0.f, 0.f, 0.f, 1.f},   // LeftHip (out-of-plane X and Z)
        {-0.42f, 0.f,   0.f,    0.f, 0.f, 0.f, 1.f},   // LeftKnee
        {-0.4f,  0.f,   0.f,    0.f, 0.f, 0.f, 1.f},   // LeftAnkle
        {0.05f,-0.15f,  0.f,    0.f, 0.f, 0.f, 1.f},   // RightClavicle (X out-of-plane)
        {0.f,  -0.15f,  0.f,    0.f, 0.f, 0.f, 1.f},   // RightShoulder
        {0.f,  -0.28f,  0.f,    0.f, 0.f, 0.f, 1.f},   // RightElbow
        {0.f,  -0.25f,  0.f,    0.f, 0.f, 0.f, 1.f},   // RightWrist
        {-0.05f,-0.1f, -0.05f,  0.f, 0.f, 0.f, 1.f},   // RightHip (out-of-plane)
        {-0.42f, 0.f,   0.f,    0.f, 0.f, 0.f, 1.f},   // RightKnee
        {-0.4f,  0.f,   0.f,    0.f, 0.f, 0.f, 1.f},   // RightAnkle
    };
    const RenderStreamLink::FSkeletalLayout Layout = BuildD3Layout(Names, SourceD3, Parents);

    // Mesh layout: clavicles purely lateral (no out-of-plane X); same arm/leg lengths.
    const TArray<RenderStreamLink::Transform> MeshD3 = {
        {0.f,    0.f,    0.f,   0.f, 0.f, 0.f, 1.f},   // Pelvis
        {0.f,   0.05f,  0.12f,  0.f, 0.f, 0.f, 1.f},   // Spine
        {0.f,   0.05f,  0.12f,  0.f, 0.f, 0.f, 1.f},   // Chest
        {0.f,   0.f,    0.15f,  0.f, 0.f, 0.f, 1.f},   // Neck
        {0.f,   0.15f,  0.f,    0.f, 0.f, 0.f, 1.f},   // LeftClavicle  (purely lateral)
        {0.f,   0.15f,  0.f,    0.f, 0.f, 0.f, 1.f},   // LeftShoulder
        {0.f,   0.28f,  0.f,    0.f, 0.f, 0.f, 1.f},   // LeftElbow
        {0.f,   0.25f,  0.f,    0.f, 0.f, 0.f, 1.f},   // LeftWrist
        {0.f,   0.1f,  -0.05f,  0.f, 0.f, 0.f, 1.f},   // LeftHip (no forward X)
        {-0.42f, 0.f,   0.f,    0.f, 0.f, 0.f, 1.f},   // LeftKnee
        {-0.4f,  0.f,   0.f,    0.f, 0.f, 0.f, 1.f},   // LeftAnkle
        {0.f,  -0.15f,  0.f,    0.f, 0.f, 0.f, 1.f},   // RightClavicle (purely lateral)
        {0.f,  -0.15f,  0.f,    0.f, 0.f, 0.f, 1.f},   // RightShoulder
        {0.f,  -0.28f,  0.f,    0.f, 0.f, 0.f, 1.f},   // RightElbow
        {0.f,  -0.25f,  0.f,    0.f, 0.f, 0.f, 1.f},   // RightWrist
        {0.f,  -0.1f,  -0.05f,  0.f, 0.f, 0.f, 1.f},   // RightHip (no forward X)
        {-0.42f, 0.f,   0.f,    0.f, 0.f, 0.f, 1.f},   // RightKnee
        {-0.4f,  0.f,   0.f,    0.f, 0.f, 0.f, 1.f},   // RightAnkle
    };
    TArray<FVector> UEOffsets;
    UEOffsets.SetNum(Names.Num());
    for (int32 i = 0; i < Names.Num(); ++i)
        UEOffsets[i] = ConvertD3TransformToUE(MeshD3[i]).GetTranslation();

    const TArray<FRetargetMeshBone> MeshBones = BuildMeshBones(UEOffsets, Parents);
    const TMap<FName, int32>        NameMap   = BuildIdentityNameMap(Names);

    // Non-trivial pose: Spine 10° twist, LeftShoulder 45° raise, RightHip 15° flexion
    RenderStreamLink::FSkeletalPose Pose = BuildD3IdentityPose(Layout);
    {
        // Spine (index 1): 10° around d3 Z (vertical)
        const float H = FMath::DegreesToRadians(10.f) * 0.5f;
        Pose.joints[1].transform = {0.f, 0.f, 0.f, 0.f, 0.f, FMath::Sin(H), FMath::Cos(H)};
    }
    {
        // LeftShoulder (index 5): 45° around d3 Y (lateral)
        const float H = FMath::DegreesToRadians(45.f) * 0.5f;
        Pose.joints[5].transform = {0.f, 0.f, 0.f, 0.f, FMath::Sin(H), 0.f, FMath::Cos(H)};
    }
    {
        // RightHip (index 15): 15° around d3 Y (flexion)
        const float H = FMath::DegreesToRadians(15.f) * 0.5f;
        Pose.joints[15].transform = {0.f, 0.f, 0.f, 0.f, FMath::Sin(H), 0.f, FMath::Cos(H)};
    }

    const TArray<FVector> Actual   = RunRetargeting(MeshBones, Layout, NameMap, Pose);
    const TArray<FVector> Expected = ComputeExpectedPositionsFromSource(Layout, Pose);

    CheckPositions(this, Actual, Expected, 1.0f, TEXT("RealisticAlternativeLayout"));
    return true;
}
