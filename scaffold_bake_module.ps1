[CmdletBinding()]
param(
    # Project root (folder containing the .uproject) to scaffold the editor bake module into.
    [Parameter(Mandatory = $true)]
    [string] $ProjectDir,

    # Runtime game module name. The bake module is named "<GameModule>Bake".
    # Defaults to a sanitized form of the .uproject base name.
    [string] $GameModule
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $ProjectDir)) {
    throw "Project directory not found: $ProjectDir"
}
$ProjectDir = (Resolve-Path -LiteralPath $ProjectDir).Path

$uproject = Get-ChildItem -LiteralPath $ProjectDir -Filter '*.uproject' -File | Select-Object -First 1
if (-not $uproject) {
    throw "No .uproject found in '$ProjectDir' - is that the project root?"
}

if (-not $GameModule) {
    $GameModule = ($uproject.BaseName -replace '[^A-Za-z0-9_]', '')
    if ($GameModule -match '^\d') { $GameModule = "M$GameModule" }
    if (-not $GameModule)         { $GameModule = 'Game' }
}
$BakeModule = "${GameModule}Bake"

$sourceDir = Join-Path $ProjectDir 'Source'
$moduleDir = Join-Path $sourceDir $BakeModule
$privateDir = Join-Path $moduleDir 'Private'

# Skip if the bake module's Build.cs already exists - keeps the script idempotent.
if (Test-Path -LiteralPath (Join-Path $moduleDir "$BakeModule.Build.cs")) {
    Write-Host "Bake module '$BakeModule' already exists - not scaffolding." -ForegroundColor DarkGray
    return
}

Write-Host "Scaffolding editor bake module '$BakeModule' into: $sourceDir" -ForegroundColor Cyan
New-Item -ItemType Directory -Path $privateDir -Force | Out-Null

# --- Build.cs: an Editor-type module that can drive UnrealEd + Blueprint authoring
$buildCs = @"
using UnrealBuildTool;

public class $BakeModule : ModuleRules
{
    public $BakeModule(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "UnrealEd",          // NewBlankMap / SaveMap / FBlueprintEditorUtils / UEditorLevelUtils
            "BlueprintGraph",    // UEdGraphSchema_K2 pin categories / UK2Node_CustomEvent
            "CinematicCamera",   // ACineCameraActor / UCineCameraComponent
            "RenderStream",       // URenderStreamChannelDefinition
            "RenderStreamEditor", // FRenderStreamEditorModule::GenerateAssetMetadata (schema bake)
            "AssetRegistry",      // FAssetRegistryModule::AssetCreated for baked assets
            "MaterialEditor",     // UMaterialEditingLibrary for the face material
        });
    }
}
"@

# --- Module implementation (a plain default editor module)
$moduleCpp = @"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, $BakeModule);
"@

# --- Commandlet header (fixed class name, so the bake step invokes it deterministically)
$commandletH = @"
#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RenderStreamTestBakeCommandlet.generated.h"

// Builds the fixed RenderStream test scene (cube + camera channel + exposed level-blueprint
// variables) and saves it, which triggers the RenderStream schema/channel-cache bake.
// Run headless with:  UnrealEditor-Cmd.exe <uproject> -run=RenderStreamTestBake -unattended -nullrhi
UCLASS()
class URenderStreamTestBakeCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    URenderStreamTestBakeCommandlet();
    virtual int32 Main(const FString& Params) override;
};
"@

# --- Commandlet implementation
$commandletCpp = @"
#include "RenderStreamTestBakeCommandlet.h"

#include "FileHelpers.h"   // UEditorLoadingAndSavingUtils::NewBlankMap / SaveMap
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "GameFramework/RotatingMovementComponent.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "RenderStreamChannelDefinition.h"
#include "RenderStreamSceneSelector.h"
#include "RenderStreamEditorModule.h"
#include "Modules/ModuleManager.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Engine/LevelScriptBlueprint.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_VariableGet.h"
#include "K2Node_CallFunction.h"
#include "EditorLevelUtils.h"
#include "Engine/LevelStreaming.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/TextRenderActor.h"
#include "Components/TextRenderComponent.h"
#include "Components/LightComponent.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/EngineTypes.h"
#include "Kismet/KismetMathLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogRenderStreamTestBake, Log, All);

namespace
{
    // Add an exposed (Instance-Editable + Blueprint-Visible) variable to the level blueprint.
    // RenderStream only picks up variables with CPF_Edit|CPF_BlueprintVisible and NOT
    // CPF_DisableEditOnInstance (see RenderStreamSceneSelector::GetProperties).
    void AddExposedVar(UBlueprint* BP, const FName Name, const FEdGraphPinType& PinType, const FString& DefaultValue, const FString& Category = TEXT("RenderStream"))
    {
        if (FBlueprintEditorUtils::FindNewVariableIndex(BP, Name) != INDEX_NONE)
            return;

        FBlueprintEditorUtils::AddMemberVariable(BP, Name, PinType, DefaultValue);

        const int32 Idx = FBlueprintEditorUtils::FindNewVariableIndex(BP, Name);
        if (Idx != INDEX_NONE)
        {
            FBPVariableDescription& Var = BP->NewVariables[Idx];
            Var.PropertyFlags |= (CPF_Edit | CPF_BlueprintVisible);
            Var.PropertyFlags &= ~CPF_DisableEditOnInstance;
            // The variable's category becomes the parameter group in the RenderStream schema.
            FBlueprintEditorUtils::SetBlueprintVariableCategory(BP, Name, nullptr, FText::FromString(Category));
        }
    }

    // Set the min/max range for a numeric exposed variable. RenderStream reads ClampMin/ClampMax
    // metadata for the parameter's schema range (floats otherwise default to -1..1).
    void SetVarRange(UBlueprint* BP, const FName Name, float Min, float Max)
    {
        FBlueprintEditorUtils::SetBlueprintVariableMetaData(BP, Name, nullptr, TEXT("ClampMin"), FString::SanitizeFloat(Min));
        FBlueprintEditorUtils::SetBlueprintVariableMetaData(BP, Name, nullptr, TEXT("ClampMax"), FString::SanitizeFloat(Max));
    }

    // Create and save a render-target asset under /Game/RenderTargets. A real render target
    // instance is required as the default value of a texture parameter - RenderStream only
    // registers an exposed object property as an image parameter when its value casts to
    // UTextureRenderTarget2D (RenderStreamEditorModule.cpp / RenderStreamSceneSelector.cpp).
    UTextureRenderTarget2D* CreateRenderTarget(const FString& AssetName)
    {
        const FString PackageName = TEXT("/Game/RenderTargets/") + AssetName;
        UPackage* Package = CreatePackage(*PackageName);
        Package->FullyLoad();   // load any existing on-disk asset so re-baking overwrites cleanly

        UTextureRenderTarget2D* RT = FindObject<UTextureRenderTarget2D>(Package, *AssetName);
        if (!RT)
            RT = NewObject<UTextureRenderTarget2D>(Package, *AssetName, RF_Public | RF_Standalone);
        RT->RenderTargetFormat = RTF_RGBA8;
        RT->InitAutoFormat(256, 256);

        FAssetRegistryModule::AssetCreated(RT);
        Package->MarkPackageDirty();

        const FString FileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        UPackage::SavePackage(Package, RT, *FileName, SaveArgs);
        return RT;
    }

    // Base unlit material with a "Texture" parameter wired to emissive. Face material
    // instances bind each render target to that parameter.
    UMaterial* CreateFaceMaterial()
    {
        const FString PackageName = TEXT("/Game/Materials/M_Face");
        UPackage* Package = CreatePackage(*PackageName);
        Package->FullyLoad();
        if (UMaterial* Existing = FindObject<UMaterial>(Package, TEXT("M_Face")))
            return Existing;   // already baked; reuse (avoids duplicating expressions)

        UMaterial* Material = NewObject<UMaterial>(Package, TEXT("M_Face"), RF_Public | RF_Standalone);
        Material->SetShadingModel(MSM_Unlit);

        if (UMaterialExpressionTextureSampleParameter2D* TexParam = Cast<UMaterialExpressionTextureSampleParameter2D>(
                UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureSampleParameter2D::StaticClass())))
        {
            TexParam->ParameterName = TEXT("Texture");
            TexParam->Texture = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
            UMaterialEditingLibrary::ConnectMaterialProperty(TexParam, FString(), MP_EmissiveColor);
        }
        UMaterialEditingLibrary::RecompileMaterial(Material);

        FAssetRegistryModule::AssetCreated(Material);
        Package->MarkPackageDirty();
        const FString FileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        UPackage::SavePackage(Package, Material, *FileName, SaveArgs);
        return Material;
    }

    // Material instance binding one render target to the base material's "Texture" parameter.
    UMaterialInstanceConstant* CreateFaceMaterialInstance(const FString& AssetName, UMaterialInterface* Parent, UTexture* Texture)
    {
        const FString PackageName = TEXT("/Game/Materials/") + AssetName;
        UPackage* Package = CreatePackage(*PackageName);
        Package->FullyLoad();

        UMaterialInstanceConstant* MIC = FindObject<UMaterialInstanceConstant>(Package, *AssetName);
        if (!MIC)
            MIC = NewObject<UMaterialInstanceConstant>(Package, *AssetName, RF_Public | RF_Standalone);
        MIC->SetParentEditorOnly(Parent);
        MIC->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Texture")), Texture);
        MIC->PostEditChange();

        FAssetRegistryModule::AssetCreated(MIC);
        Package->MarkPackageDirty();
        const FString FileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        UPackage::SavePackage(Package, MIC, *FileName, SaveArgs);
        return MIC;
    }

    // Spawn a plane on part of a cube face, attached so it rotates with the cube. RelScale
    // lets a caller shrink the plane (e.g. 0.5 in X for a half-face).
    void SpawnCubeFace(UWorld* World, AActor* Cube, const FString& Label, const FVector& RelLocation, const FRotator& RelRotation, const FVector& RelScale, UMaterialInterface* Material)
    {
        AStaticMeshActor* Face = World->SpawnActor<AStaticMeshActor>();
        if (!Face)
            return;

        Face->SetActorLabel(Label);
        UStaticMeshComponent* Mesh = Face->GetStaticMeshComponent();
        Mesh->SetMobility(EComponentMobility::Movable);
        if (UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")))
            Mesh->SetStaticMesh(PlaneMesh);
        if (Material)
            Mesh->SetMaterial(0, Material);

        Face->AttachToActor(Cube, FAttachmentTransformRules::KeepRelativeTransform);
        Face->SetActorRelativeLocation(RelLocation);
        Face->SetActorRelativeRotation(RelRotation);
        Face->SetActorRelativeScale3D(RelScale);
    }

    // Set a hard-object variable's value on an actor instance by name (used to assign the
    // render-target defaults on the level script actor after the blueprint is compiled).
    void SetObjectVar(AActor* Actor, const FName VarName, UObject* Value)
    {
        if (!Actor)
            return;
        if (FObjectPropertyBase* Prop = FindFProperty<FObjectPropertyBase>(Actor->GetClass(), VarName))
            Prop->SetObjectPropertyValue_InContainer(Actor, Value);
    }

    // Add a plain (non-exposed) member variable to a blueprint. Unlike AddExposedVar it keeps
    // CPF_DisableEditOnInstance set so RenderStream does not treat it as a scene parameter.
    void AddInternalVar(UBlueprint* BP, const FName Name, const FEdGraphPinType& PinType)
    {
        if (FBlueprintEditorUtils::FindNewVariableIndex(BP, Name) != INDEX_NONE)
            return;
        FBlueprintEditorUtils::AddMemberVariable(BP, Name, PinType);
        const int32 Idx = FBlueprintEditorUtils::FindNewVariableIndex(BP, Name);
        if (Idx != INDEX_NONE)
            BP->NewVariables[Idx].PropertyFlags |= CPF_DisableEditOnInstance;
    }

    UK2Node_VariableGet* AddVarGetNode(UEdGraph* Graph, const FName VarName, int32 X, int32 Y)
    {
        UK2Node_VariableGet* Node = NewObject<UK2Node_VariableGet>(Graph);
        Node->VariableReference.SetSelfMember(VarName);
        Node->NodePosX = X;
        Node->NodePosY = Y;
        Graph->AddNode(Node, false, false);
        Node->CreateNewGuid();
        Node->PostPlacedNewNode();
        Node->AllocateDefaultPins();
        return Node;
    }

    UK2Node_CallFunction* AddCallNode(UEdGraph* Graph, UFunction* Function, int32 X, int32 Y)
    {
        UK2Node_CallFunction* Node = NewObject<UK2Node_CallFunction>(Graph);
        Node->SetFromFunction(Function);
        Node->NodePosX = X;
        Node->NodePosY = Y;
        Graph->AddNode(Node, false, false);
        Node->CreateNewGuid();
        Node->PostPlacedNewNode();
        Node->AllocateDefaultPins();
        return Node;
    }

    void LinkPins(UEdGraphPin* A, UEdGraphPin* B)
    {
        if (A && B)
            A->MakeLinkTo(B);
    }

    // Author, in the event graph, an Event Tick that pushes the exposed parameters onto their
    // scene targets every frame:
    //   Tick -> SetText(Caption) -> SetVisibility(Visible) -> SetTextRenderColor(toFColor(Colour))
    //        -> DirectionTarget.SetIntensity(DirectionIntensity)
    //        -> PointTarget.SetIntensity(PointIntensity)
    // Assumes the Caption/Visible/Colour/DirectionIntensity/PointIntensity/CaptionTarget/
    // DirectionTarget/PointTarget variables already exist on the blueprint.
    void WireParametersToScene(UBlueprint* BP)
    {
        UEdGraph* Graph = BP->UbergraphPages.Num() > 0 ? BP->UbergraphPages[0] : nullptr;
        if (!Graph)
            return;

        UK2Node_Event* TickNode = NewObject<UK2Node_Event>(Graph);
        TickNode->EventReference.SetExternalMember(TEXT("ReceiveTick"), AActor::StaticClass());
        TickNode->bOverrideFunction = true;
        TickNode->NodePosX = 0;
        TickNode->NodePosY = 400;
        Graph->AddNode(TickNode, false, false);
        TickNode->CreateNewGuid();
        TickNode->PostPlacedNewNode();
        TickNode->AllocateDefaultPins();

        UK2Node_VariableGet* GetTarget    = AddVarGetNode(Graph, TEXT("CaptionTarget"),     250, 200);
        UK2Node_VariableGet* GetDirLight  = AddVarGetNode(Graph, TEXT("DirectionTarget"),   250, 300);
        UK2Node_VariableGet* GetPointLight= AddVarGetNode(Graph, TEXT("PointTarget"),       250, 400);
        UK2Node_VariableGet* GetCaption   = AddVarGetNode(Graph, TEXT("Caption"),           250, 600);
        UK2Node_VariableGet* GetVisible   = AddVarGetNode(Graph, TEXT("Visible"),           250, 700);
        UK2Node_VariableGet* GetColour    = AddVarGetNode(Graph, TEXT("Colour"),            250, 800);
        UK2Node_VariableGet* GetDirInt    = AddVarGetNode(Graph, TEXT("DirectionIntensity"),250, 900);
        UK2Node_VariableGet* GetPointInt  = AddVarGetNode(Graph, TEXT("PointIntensity"),    250, 1000);

        UClass* TextClass = UTextRenderComponent::StaticClass();
        UClass* LightClass = ULightComponent::StaticClass();
        // K2_SetText is the BlueprintCallable setter (plain SetText is UFUNCTION() only).
        UK2Node_CallFunction* SetTextNode  = AddCallNode(Graph, TextClass->FindFunctionByName(TEXT("K2_SetText")),          500, 400);
        UK2Node_CallFunction* SetVisNode   = AddCallNode(Graph, TextClass->FindFunctionByName(TEXT("SetVisibility")),       800, 400);
        UK2Node_CallFunction* SetColorNode = AddCallNode(Graph, TextClass->FindFunctionByName(TEXT("SetTextRenderColor")), 1100, 400);
        UK2Node_CallFunction* SetDirIntNode   = AddCallNode(Graph, LightClass->FindFunctionByName(TEXT("SetIntensity")),   1400, 400);
        UK2Node_CallFunction* SetPointIntNode = AddCallNode(Graph, LightClass->FindFunctionByName(TEXT("SetIntensity")),   1700, 400);
        // Colour is an FLinearColor; SetTextRenderColor takes an FColor - convert via the autocast.
        UK2Node_CallFunction* ConvNode     = AddCallNode(Graph, UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("Conv_LinearColorToColor")), 800, 800);

        // Exec chain: Tick -> SetText -> SetVisibility -> SetTextRenderColor -> SetIntensity x2.
        LinkPins(TickNode->FindPin(UEdGraphSchema_K2::PN_Then), SetTextNode->GetExecPin());
        LinkPins(SetTextNode->FindPin(UEdGraphSchema_K2::PN_Then), SetVisNode->GetExecPin());
        LinkPins(SetVisNode->FindPin(UEdGraphSchema_K2::PN_Then), SetColorNode->GetExecPin());
        LinkPins(SetColorNode->FindPin(UEdGraphSchema_K2::PN_Then), SetDirIntNode->GetExecPin());
        LinkPins(SetDirIntNode->FindPin(UEdGraphSchema_K2::PN_Then), SetPointIntNode->GetExecPin());

        // Targets (self): text component for the text calls, each light component for its intensity.
        LinkPins(SetTextNode->FindPin(UEdGraphSchema_K2::PN_Self),  GetTarget->FindPin(TEXT("CaptionTarget")));
        LinkPins(SetVisNode->FindPin(UEdGraphSchema_K2::PN_Self),   GetTarget->FindPin(TEXT("CaptionTarget")));
        LinkPins(SetColorNode->FindPin(UEdGraphSchema_K2::PN_Self), GetTarget->FindPin(TEXT("CaptionTarget")));
        LinkPins(SetDirIntNode->FindPin(UEdGraphSchema_K2::PN_Self),   GetDirLight->FindPin(TEXT("DirectionTarget")));
        LinkPins(SetPointIntNode->FindPin(UEdGraphSchema_K2::PN_Self), GetPointLight->FindPin(TEXT("PointTarget")));

        // Values.
        LinkPins(SetTextNode->FindPin(TEXT("Value")),               GetCaption->FindPin(TEXT("Caption")));
        LinkPins(SetVisNode->FindPin(TEXT("bNewVisibility")),       GetVisible->FindPin(TEXT("Visible")));
        LinkPins(ConvNode->FindPin(TEXT("InLinearColor")),          GetColour->FindPin(TEXT("Colour")));
        LinkPins(SetColorNode->FindPin(TEXT("Value")),              ConvNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue));
        LinkPins(SetDirIntNode->FindPin(TEXT("NewIntensity")),      GetDirInt->FindPin(TEXT("DirectionIntensity")));
        LinkPins(SetPointIntNode->FindPin(TEXT("NewIntensity")),    GetPointInt->FindPin(TEXT("PointIntensity")));
    }

    // Add a custom event that starts (bEnable=true) or stops (bEnable=false) the cube's spin by
    // toggling its rotating-movement component tick. Exposed as a RenderStream custom event
    // (RS_PARAMETER_EVENT); firing it from disguise runs this. Assumes the RotationTarget variable
    // exists on the blueprint.
    void AddRotationEvent(UBlueprint* BP, const FName EventName, bool bEnable, int32 NodePosY)
    {
        UEdGraph* Graph = BP->UbergraphPages.Num() > 0 ? BP->UbergraphPages[0] : nullptr;
        if (!Graph)
            return;

        for (UEdGraphNode* Node : Graph->Nodes)
            if (const UK2Node_CustomEvent* Existing = Cast<UK2Node_CustomEvent>(Node))
                if (Existing->CustomFunctionName == EventName)
                    return;

        UK2Node_CustomEvent* EventNode = NewObject<UK2Node_CustomEvent>(Graph);
        EventNode->CustomFunctionName = EventName;
        EventNode->NodePosX = 0;
        EventNode->NodePosY = NodePosY;
        Graph->AddNode(EventNode, false, false);
        EventNode->CreateNewGuid();
        EventNode->PostPlacedNewNode();
        EventNode->AllocateDefaultPins();

        UK2Node_VariableGet* GetTarget = AddVarGetNode(Graph, TEXT("RotationTarget"), 250, NodePosY + 60);
        UK2Node_CallFunction* SetTickNode = AddCallNode(Graph, UActorComponent::StaticClass()->FindFunctionByName(TEXT("SetComponentTickEnabled")), 500, NodePosY);

        LinkPins(EventNode->FindPin(UEdGraphSchema_K2::PN_Then), SetTickNode->GetExecPin());
        LinkPins(SetTickNode->FindPin(UEdGraphSchema_K2::PN_Self), GetTarget->FindPin(TEXT("RotationTarget")));
        if (UEdGraphPin* EnabledPin = SetTickNode->FindPin(TEXT("bEnabled")))
            EnabledPin->DefaultValue = bEnable ? TEXT("true") : TEXT("false");
    }

    // Spawn a CineCamera carrying a RenderStream channel definition. The channel name is the
    // actor label (URenderStreamChannelDefinition::GetChannelName). Returns the channel
    // definition so the caller can configure per-channel force-visible / force-hidden.
    URenderStreamChannelDefinition* SpawnPlateCamera(UWorld* World, const FString& Label, const FVector& Location, const FRotator& Rotation)
    {
        ACineCameraActor* Camera = World->SpawnActor<ACineCameraActor>(Location, Rotation);
        if (!Camera)
            return nullptr;

        Camera->SetActorLabel(Label);
        if (UCineCameraComponent* Cine = Camera->GetCineCameraComponent())
        {
            Cine->SetCurrentFocalLength(35.f);
            Cine->Filmback.SensorWidth = 36.f;
            Cine->Filmback.SensorHeight = 20.25f;
            Cine->FocusSettings.FocusMethod = ECameraFocusMethod::Disable;
        }

        // Serialized instance component so it persists in the saved map and is seen when the
        // RenderStream channel cache is built.
        URenderStreamChannelDefinition* ChannelDef = NewObject<URenderStreamChannelDefinition>(
            Camera, URenderStreamChannelDefinition::StaticClass(), TEXT("RenderStreamChannelDefinition"));
        Camera->AddInstanceComponent(ChannelDef);
        ChannelDef->RegisterComponent();
        return ChannelDef;
    }

    // Create a streaming sub-level with a rigid-body cube-rain spawner and its own exposed params.
    // Gives StreamingLevels mode real per-sub-level schema content, and records the sub-level under
    // the persistent map. The exposed params drive the spawner (which reads them off the level
    // script actor each tick): SubLevelParticleSize/Intensity/Speed, SubLevelEnabled, SubLevelTexture.
    void AddSubLevel(const FString& SubLevelPackagePath)
    {
        ULevelStreaming* Streaming = UEditorLevelUtils::CreateNewStreamingLevel(
            ULevelStreamingDynamic::StaticClass(), SubLevelPackagePath, /*bMoveSelectedActorsIntoNewLevel*/ false);
        if (!Streaming)
        {
            UE_LOG(LogRenderStreamTestBake, Warning, TEXT("Failed to create sub-level %s"), *SubLevelPackagePath);
            return;
        }

        ULevel* SubLevel = Streaming->GetLoadedLevel();
        if (!SubLevel)
            return;

        // Render target driven by the sub-level's texture parameter; shown on the falling cubes.
        UTextureRenderTarget2D* SubRT = CreateRenderTarget(TEXT("RT_SubLevel"));

        // Spawn the cube-rain spawner (a runtime actor in the game module) into the sub-level.
        // Referenced by class path so this editor module needs no build dependency on the game module.
        if (UWorld* SubWorld = SubLevel->GetWorld())
        {
            if (UClass* SpawnerClass = LoadClass<AActor>(nullptr, TEXT("/Script/$GameModule.CubeRainSpawner")))
            {
                FActorSpawnParameters SpawnParams;
                SpawnParams.OverrideLevel = SubLevel;
                if (AActor* Spawner = SubWorld->SpawnActor<AActor>(SpawnerClass, FVector(0, 0, 600), FRotator::ZeroRotator, SpawnParams))
                    Spawner->SetActorLabel(TEXT("CubeRain"));
            }
            else
            {
                UE_LOG(LogRenderStreamTestBake, Warning, TEXT("CubeRainSpawner class not found - sub-level will have no cube rain."));
            }
        }

        if (ULevelScriptBlueprint* SubLSB = SubLevel->GetLevelScriptBlueprint(/*bDontCreate*/ false))
        {
            FEdGraphPinType FloatPin;
            FloatPin.PinCategory = UEdGraphSchema_K2::PC_Real;
            FloatPin.PinSubCategory = UEdGraphSchema_K2::PC_Double;

            FEdGraphPinType BoolPin;
            BoolPin.PinCategory = UEdGraphSchema_K2::PC_Boolean;

            FEdGraphPinType TexPin;
            TexPin.PinCategory = UEdGraphSchema_K2::PC_Object;
            TexPin.PinSubCategoryObject = UTextureRenderTarget2D::StaticClass();

            AddExposedVar(SubLSB, TEXT("SubLevelParticleSize"),      FloatPin, TEXT("1.000000"), TEXT("SubLevel"));
            AddExposedVar(SubLSB, TEXT("SubLevelParticleIntensity"), FloatPin, TEXT("1.000000"), TEXT("SubLevel"));
            AddExposedVar(SubLSB, TEXT("SubLevelParticleSpeed"),     FloatPin, TEXT("1.000000"), TEXT("SubLevel"));
            AddExposedVar(SubLSB, TEXT("SubLevelEnabled"),           BoolPin,  TEXT("true"),     TEXT("SubLevel"));
            AddExposedVar(SubLSB, TEXT("SubLevelTexture"),           TexPin,   FString(),        TEXT("SubLevel"));

            // 0..5 slider range in disguise for the numeric particle controls.
            SetVarRange(SubLSB, TEXT("SubLevelParticleSize"),      0.f, 5.f);
            SetVarRange(SubLSB, TEXT("SubLevelParticleIntensity"), 0.f, 5.f);
            SetVarRange(SubLSB, TEXT("SubLevelParticleSpeed"),     0.f, 5.f);

            FKismetEditorUtilities::CompileBlueprint(SubLSB);

            if (ALevelScriptActor* SubLSA = SubLevel->GetLevelScriptActor())
                SetObjectVar(SubLSA, TEXT("SubLevelTexture"), SubRT);
        }

        // Re-save the sub-level now that it has a level script blueprint with exposed params.
        FEditorFileUtils::SaveLevel(SubLevel);
    }

    // Author the fixed test scene into World: lights, rotating cube with textured faces, the
    // three plate cameras, and the exposed level-blueprint params + custom events.
    void BuildScene(UWorld* World)
    {
        // Directional light so the cube is actually visible in a blank map. Retained so the
        // DirectionIntensity parameter can drive its intensity (wired in the level blueprint below).
        ADirectionalLight* Light = World->SpawnActor<ADirectionalLight>(FVector(-250, 400, 400), FRotator(0, -45, -45));
        if (Light)
        {
            Light->SetActorLabel(TEXT("DirectionalLight"));
        }

        // Dim point light for soft fill so the cube's shadowed faces aren't black. Retained so
        // the PointIntensity parameter can drive its intensity (wired in the level blueprint below).
        APointLight* Fill = World->SpawnActor<APointLight>(FVector(-130, -130, 250), FRotator::ZeroRotator);
        if (Fill)
        {
            Fill->SetActorLabel(TEXT("PointLight"));
        }

        // Cube.
        AStaticMeshActor* Cube = World->SpawnActor<AStaticMeshActor>(FVector(0, 0, 50), FRotator::ZeroRotator);
        if (Cube)
        {
            Cube->SetActorLabel(TEXT("Cube"));
            UStaticMeshComponent* CubeMeshComp = Cube->GetStaticMeshComponent();
            CubeMeshComp->SetMobility(EComponentMobility::Movable);
            if (UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
                CubeMeshComp->SetStaticMesh(CubeMesh);

            // Collision so the falling particle cubes bounce off it. It never simulates physics,
            // so gravity does not move it - RotatingMovementComponent keeps spinning it in place.
            CubeMeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
            CubeMeshComp->SetCollisionObjectType(ECC_WorldDynamic);
            CubeMeshComp->SetCollisionResponseToAllChannels(ECR_Block);

            // Rotate the cube to match the DX11 sample cube in LibGpuVideoCodec
            // (VideoRenderDX11.cpp: RotationRollPitchYaw(anim, -anim, 0)) -> pitch +, yaw -, roll 0.
            // Added as a serialized instance component so it persists in the saved map;
            // RotatingMovementComponent drives the owner's root each tick (degrees/second).
            URotatingMovementComponent* Rotator = NewObject<URotatingMovementComponent>(
                Cube, URotatingMovementComponent::StaticClass(), TEXT("RotatingMovement"));
            Rotator->RotationRate = FRotator(90.f, -90.f, 0.f);
            // Default state is rotating: the component ticks from load. StopRotation/StartRotation
            // custom events toggle this at runtime.
            Rotator->PrimaryComponentTick.bStartWithTickEnabled = true;
            Cube->AddInstanceComponent(Rotator);
            Rotator->RegisterComponent();
        }

        // Three cameras framing the cube from the same position, each a RenderStream channel:
        //   RenderStreamCamera - the plain streamed channel.
        //   backplate          - default visible, cube force-hidden (content behind the cube).
        //   frontplate         - default hidden, cube force-visible (only the cube shows).
        const FVector CameraLoc(-400, 0, 100);
        const FRotator CameraRot(-10, 0, 0);

        SpawnPlateCamera(World, TEXT("RenderStreamCamera"), CameraLoc, CameraRot);

        if (URenderStreamChannelDefinition* Back = SpawnPlateCamera(World, TEXT("backplate"), CameraLoc, CameraRot))
        {
            Back->DefaultVisibility = EChannelVisibilty::Visible;
            if (Cube)
                Back->Hidden.Add(TSoftObjectPtr<AActor>(Cube));
        }

        if (URenderStreamChannelDefinition* Front = SpawnPlateCamera(World, TEXT("frontplate"), CameraLoc, CameraRot))
        {
            Front->DefaultVisibility = EChannelVisibilty::Hidden;
            if (Cube)
                Front->Visible.Add(TSoftObjectPtr<AActor>(Cube));
        }

        // Twelve render-target texture parameters. All are exposed on the level blueprint below;
        // the first six get wired to the cube faces, the remaining six are schema-only.
        TArray<UTextureRenderTarget2D*> RenderTargets;
        for (int32 i = 0; i < 12; ++i)
            RenderTargets.Add(CreateRenderTarget(FString::Printf(TEXT("RT_%02d"), i)));

        // Wire all twelve render targets onto the cube: each of the six faces is split into two
        // half-planes (one texture per half) via an unlit material; the planes attach to the cube
        // so they rotate with it.
        if (Cube)
        {
            UMaterial* FaceMaterial = CreateFaceMaterial();
            // Plane's front (+Z normal) must point outward on each face. Side faces flip the
            // sign vs. the naive rotation so the textured side faces out, not into the cube.
            const struct { const TCHAR* Label; FVector Loc; FRotator Rot; } Faces[] = {
                { TEXT("Face_PosX"), FVector( 50.5f,  0.0f,   0.0f ), FRotator(-90.f, 0.f,   0.f) },
                { TEXT("Face_NegX"), FVector(-50.5f,  0.0f,   0.0f ), FRotator( 90.f, 0.f,   0.f) },
                { TEXT("Face_PosY"), FVector(  0.0f,  50.5f,  0.0f ), FRotator(  0.f, 0.f,  90.f) },
                { TEXT("Face_NegY"), FVector(  0.0f, -50.5f,  0.0f ), FRotator(  0.f, 0.f, -90.f) },
                { TEXT("Face_PosZ"), FVector(  0.0f,  0.0f,  50.5f ), FRotator(  0.f, 0.f,   0.f) },
                { TEXT("Face_NegZ"), FVector(  0.0f,  0.0f, -50.5f ), FRotator(180.f, 0.f,   0.f) },
            };
            for (int32 f = 0; f < 6; ++f)
            {
                // Offset each half by a quarter-face along the plane's (rotated) local X axis, and
                // scale that axis to 0.5 so the two 50-wide halves tile the 100-wide face.
                const FVector HalfOffset = Faces[f].Rot.RotateVector(FVector(25.f, 0.f, 0.f));
                for (int32 h = 0; h < 2; ++h)
                {
                    const int32 Tex = f * 2 + h;
                    const FVector HalfLoc = Faces[f].Loc + (h == 0 ? -HalfOffset : HalfOffset);
                    UMaterialInstanceConstant* FaceInstance =
                        CreateFaceMaterialInstance(FString::Printf(TEXT("MI_Face_%02d"), Tex), FaceMaterial, RenderTargets[Tex]);
                    SpawnCubeFace(World, Cube, FString::Printf(TEXT("%s_%d"), Faces[f].Label, h),
                        HalfLoc, Faces[f].Rot, FVector(0.5f, 1.f, 1.f), FaceInstance);
                }
            }
        }

        // Text actor that displays the Caption parameter (wired via the level blueprint below).
        // Placed above the cube, facing the camera (camera looks along +X, so face -X via Yaw 180).
        ATextRenderActor* CaptionActor = World->SpawnActor<ATextRenderActor>(FVector(0, 0, 150), FRotator(0, 180, 0));
        if (CaptionActor)
        {
            CaptionActor->SetActorLabel(TEXT("CaptionText"));
            CaptionActor->GetTextRender()->SetText(FText::FromString(TEXT("Caption")));
            CaptionActor->GetTextRender()->SetHorizontalAlignment(EHTA_Center);
        }

        // Exposed level-blueprint variables -> become the RenderStream parameter schema.
        if (ULevelScriptBlueprint* LSB = World->PersistentLevel->GetLevelScriptBlueprint(/*bDontCreate*/ false))
        {
            FEdGraphPinType FloatPin;
            FloatPin.PinCategory = UEdGraphSchema_K2::PC_Real;
            FloatPin.PinSubCategory = UEdGraphSchema_K2::PC_Double;

            FEdGraphPinType BoolPin;
            BoolPin.PinCategory = UEdGraphSchema_K2::PC_Boolean;

            FEdGraphPinType ColorPin;
            ColorPin.PinCategory = UEdGraphSchema_K2::PC_Struct;
            ColorPin.PinSubCategoryObject = TBaseStructure<FLinearColor>::Get();

            FEdGraphPinType TextPin;
            TextPin.PinCategory = UEdGraphSchema_K2::PC_Text;

            FEdGraphPinType TexPin;
            TexPin.PinCategory = UEdGraphSchema_K2::PC_Object;
            TexPin.PinSubCategoryObject = UTextureRenderTarget2D::StaticClass();

            AddExposedVar(LSB, TEXT("DirectionIntensity"), FloatPin, TEXT("1.000000"),                               TEXT("Lighting"));
            AddExposedVar(LSB, TEXT("PointIntensity"),     FloatPin, TEXT("1.000000"),                               TEXT("Lighting"));
            AddExposedVar(LSB, TEXT("Visible"),    BoolPin,  TEXT("true"),                                           TEXT("Label"));
            AddExposedVar(LSB, TEXT("Colour"),     ColorPin, TEXT("(R=1.000000,G=1.000000,B=1.000000,A=1.000000)"), TEXT("Label"));
            AddExposedVar(LSB, TEXT("Caption"),    TextPin,  FString(),                                             TEXT("Label"));

            for (int32 i = 0; i < RenderTargets.Num(); ++i)
                AddExposedVar(LSB, *FString::Printf(TEXT("Texture%02d"), i), TexPin, FString(), TEXT("Texture"));

            // Internal (non-exposed) targets for the parameter graph: the caption text component
            // and the directional light component, plus the graph that drives them each frame.
            FEdGraphPinType TextCompPin;
            TextCompPin.PinCategory = UEdGraphSchema_K2::PC_Object;
            TextCompPin.PinSubCategoryObject = UTextRenderComponent::StaticClass();
            AddInternalVar(LSB, TEXT("CaptionTarget"), TextCompPin);

            FEdGraphPinType LightCompPin;
            LightCompPin.PinCategory = UEdGraphSchema_K2::PC_Object;
            LightCompPin.PinSubCategoryObject = ULightComponent::StaticClass();
            AddInternalVar(LSB, TEXT("DirectionTarget"), LightCompPin);
            AddInternalVar(LSB, TEXT("PointTarget"), LightCompPin);

            FEdGraphPinType RotationCompPin;
            RotationCompPin.PinCategory = UEdGraphSchema_K2::PC_Object;
            RotationCompPin.PinSubCategoryObject = URotatingMovementComponent::StaticClass();
            AddInternalVar(LSB, TEXT("RotationTarget"), RotationCompPin);

            WireParametersToScene(LSB);

            // Custom events -> RenderStream custom events (RS_PARAMETER_EVENT). Start/stop the
            // cube's spin by enabling/disabling its rotating-movement component tick.
            AddRotationEvent(LSB, TEXT("StartRotation"), true,  0);
            AddRotationEvent(LSB, TEXT("StopRotation"),  false, 200);

            FKismetEditorUtilities::CompileBlueprint(LSB);

            // Assign object defaults on the re-instanced level script actor: render targets so
            // the exposed object properties register as images, and the Caption text target so
            // the level-blueprint tick has a component to write to.
            if (ALevelScriptActor* LSA = World->PersistentLevel->GetLevelScriptActor())
            {
                for (int32 i = 0; i < RenderTargets.Num(); ++i)
                    SetObjectVar(LSA, *FString::Printf(TEXT("Texture%02d"), i), RenderTargets[i]);
                if (CaptionActor)
                    SetObjectVar(LSA, TEXT("CaptionTarget"), CaptionActor->GetTextRender());
                if (Light)
                    SetObjectVar(LSA, TEXT("DirectionTarget"), Light->GetLightComponent());
                if (Fill)
                    SetObjectVar(LSA, TEXT("PointTarget"), Fill->GetLightComponent());
                if (Cube)
                    SetObjectVar(LSA, TEXT("RotationTarget"), Cube->FindComponentByClass<URotatingMovementComponent>());
            }
        }

        // Report what RenderStream will expose from this scene (validates the bake authoring; the
        // schema itself is generated by RenderStreamEditor when the project is loaded in-editor).
        if (ALevelScriptActor* LSA = World->PersistentLevel->GetLevelScriptActor())
        {
            const TArray<FProperty*> Props = RenderStreamSceneSelector::GetProperties(LSA);
            const TArray<UFunction*> Events = RenderStreamSceneSelector::GetEvents(LSA);
            int32 NumTextures = 0;
            for (FProperty* P : Props)
                if (const FObjectPropertyBase* Obj = CastField<FObjectPropertyBase>(P))
                    if (Cast<UTextureRenderTarget2D>(Obj->GetObjectPropertyValue_InContainer(LSA)))
                        ++NumTextures;

            int32 NumChannels = 0;
            for (AActor* Actor : World->PersistentLevel->Actors)
                if (Actor && Actor->FindComponentByClass<URenderStreamChannelDefinition>())
                    ++NumChannels;

            UE_LOG(LogRenderStreamTestBake, Display,
                TEXT("Scene exposes %d parameters (%d render-texture), %d custom events, %d channels."),
                Props.Num(), NumTextures, Events.Num(), NumChannels);
        }
    }
}

URenderStreamTestBakeCommandlet::URenderStreamTestBakeCommandlet()
{
    IsClient = false;
    IsServer = false;
    IsEditor = true;
    LogToConsole = true;
}

int32 URenderStreamTestBakeCommandlet::Main(const FString& /*Params*/)
{
    // Two persistent maps, each a selectable RenderStream scene in Maps mode, and each with a
    // streaming sub-level carrying its own params for StreamingLevels mode.
    const struct { const TCHAR* Map; const TCHAR* SubLevel; } Scenes[] = {
        { TEXT("/Game/Maps/RenderStreamTest"),  TEXT("/Game/Maps/RenderStreamTest_Sub")  },
        { TEXT("/Game/Maps/RenderStreamTest2"), TEXT("/Game/Maps/RenderStreamTest2_Sub") },
    };

    for (const auto& Scene : Scenes)
    {
        UE_LOG(LogRenderStreamTestBake, Display, TEXT("Baking RenderStream test scene: %s"), Scene.Map);

        UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(/*bSaveExistingMap*/ false);
        if (!World)
        {
            UE_LOG(LogRenderStreamTestBake, Error, TEXT("Failed to create a new map."));
            return 1;
        }

        BuildScene(World);
        AddSubLevel(Scene.SubLevel);

        if (!UEditorLoadingAndSavingUtils::SaveMap(World, Scene.Map))
        {
            UE_LOG(LogRenderStreamTestBake, Error, TEXT("Failed to save map %s"), Scene.Map);
            return 1;
        }

        // Generate the RenderStream schema/channel caches now, while this map is the loaded
        // world. In the editor this runs deferred on the next frame; a commandlet never ticks,
        // so drive it explicitly. Doing it per-map (not once at the end) means the metadata scan
        // only sees loaded/already-cached levels and never has to load an unloaded world from
        // disk (which fails headless). The final iteration writes the complete rs_<project>.json.
        if (FRenderStreamEditorModule* Editor = FModuleManager::GetModulePtr<FRenderStreamEditorModule>(TEXT("RenderStreamEditor")))
            Editor->GenerateAssetMetadata();

        UE_LOG(LogRenderStreamTestBake, Display, TEXT("Baked RenderStream test scene: %s"), Scene.Map);
    }

    return 0;
}
"@

# Write UTF-8 without BOM (UBT's C#/C++ parsers dislike a leading BOM).
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
function Write-TextFile([string] $Path, [string] $Content) {
    [System.IO.File]::WriteAllText($Path, $Content, $utf8NoBom)
    Write-Host "  wrote $Path" -ForegroundColor DarkGray
}

Write-TextFile (Join-Path $moduleDir "$BakeModule.Build.cs")                     $buildCs
Write-TextFile (Join-Path $privateDir "$BakeModule.cpp")                          $moduleCpp
Write-TextFile (Join-Path $privateDir 'RenderStreamTestBakeCommandlet.h')         $commandletH
Write-TextFile (Join-Path $privateDir 'RenderStreamTestBakeCommandlet.cpp')       $commandletCpp

# --- Register the editor module in the .uproject ------------------------------
$json = Get-Content -Raw -LiteralPath $uproject.FullName | ConvertFrom-Json
$modules = @($json.Modules)
if ($modules.Name -contains $BakeModule) {
    Write-Host "  .uproject already declares module '$BakeModule'." -ForegroundColor DarkGray
} else {
    $modules += [pscustomobject]@{
        Name         = $BakeModule
        Type         = 'Editor'
        LoadingPhase = 'Default'
    }
    $json.Modules = $modules
    Write-TextFile $uproject.FullName ($json | ConvertTo-Json -Depth 20)
    Write-Host "  registered editor module '$BakeModule' in $($uproject.Name)" -ForegroundColor DarkGray
}

Write-Host "Bake module scaffolded." -ForegroundColor Green
