// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "RenderStreamEditorModule.h"
#include "Modules/ModuleManager.h"

#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "ObjectTools.h"
#include "PropertyEditorDelegates.h"
#include "PropertyEditorModule.h"
#include "UObject/UObjectBase.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/SavePackage.h"
#include "Engine/AssetManager.h"
#include "Engine/LevelStreaming.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"

#include "ISettingsModule.h"
#include "RenderStreamChannelCacheAsset.h"
#include "RenderStreamChannelDefinition.h"
#include "RenderStreamCustomization.h"
#include "RenderStreamSettings.h"
#include "RenderStreamValidation.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/ObjectLibrary.h"
#include "SourceControlHelpers.h"

#include "RenderStream/Public/RenderStreamLink.h"
#include <set>
#include <string>
#include <vector>

#include "GameMapsSettings.h"

#include "MessageLog/Public/MessageLogInitializationOptions.h"
#include "MessageLog/Public/MessageLogModule.h"
#include "MessageLog/Public/IMessageLogListing.h"

DEFINE_LOG_CATEGORY(LogRenderStreamEditor);

#define LOCTEXT_NAMESPACE "RenderStreamEditor"

const FString CacheFolder = TEXT("/Game/" RS_PLUGIN_NAME "/Cache");
const FString ContentFolder = TEXT("/Game");

void FRenderStreamEditorModule::StartupModule()
{
    {
        auto& PropertyModule = FModuleManager::LoadModuleChecked< FPropertyEditorModule >("PropertyEditor");

        PropertyModule.RegisterCustomClassLayout(
            "RenderStreamChannelDefinition",
            FOnGetDetailCustomizationInstance::CreateStatic(&MakeDefinitionCustomizationInstance)
        );
        PropertyModule.RegisterCustomClassLayout(
            "RenderStreamSettings",
            FOnGetDetailCustomizationInstance::CreateStatic(&MakeSettingsCustomizationInstance)
        );


        PropertyModule.NotifyCustomizationModuleChanged();
    }

    FEditorDelegates::PostSaveExternalActors.AddRaw(this, &FRenderStreamEditorModule::OnPostSaveWorld);
    FEditorDelegates::PostSaveWorldWithContext.AddRaw(this, &FRenderStreamEditorModule::OnPostSaveWorldContext);
    FEditorDelegates::OnAssetsDeleted.AddRaw(this, &FRenderStreamEditorModule::OnAssetsDeleted);
    FCoreDelegates::OnBeginFrame.AddRaw(this, &FRenderStreamEditorModule::OnBeginFrame);
    FCoreDelegates::OnPostEngineInit.AddRaw(this, &FRenderStreamEditorModule::OnPostEngineInit);
    FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FRenderStreamEditorModule::OnObjectPostEditChange);
    FEditorDelegates::OnShutdownPostPackagesSaved.AddRaw(this, &FRenderStreamEditorModule::OnShutdownPostPackagesSaved);

    // Create a validation message log
    FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
    FMessageLogInitializationOptions InitOptions;
    InitOptions.bShowPages = false;
    InitOptions.bAllowClear = true;
    InitOptions.bShowFilters = true;
    MessageLogModule.RegisterLogListing("RenderStreamValidation", NSLOCTEXT("RenderStreamValidation", "RenderStreamValidationLogLabel", "Renderstream Validation"), InitOptions);
}

void FRenderStreamEditorModule::ShutdownModule()
{
    if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
    {
        auto& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
        PropertyModule.UnregisterCustomClassLayout("RenderStreamChannelDefinition");
        PropertyModule.UnregisterCustomClassLayout("RenderStreamSettings");
    }

    FEditorDelegates::PostSaveExternalActors.RemoveAll(this);
    FEditorDelegates::PostSaveWorldWithContext.RemoveAll(this);
    FEditorDelegates::OnAssetsDeleted.RemoveAll(this);
    FCoreDelegates::OnBeginFrame.RemoveAll(this);
    FCoreDelegates::OnPostEngineInit.RemoveAll(this);
    FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
    FEditorDelegates::OnShutdownPostPackagesSaved.RemoveAll(this);
    if (GEditor)
        GEditor->OnBlueprintCompiled().RemoveAll(this);

    UnregisterSettings();

    if (FModuleManager::Get().IsModuleLoaded("MessageLog"))
    {
        // unregister message log
        FMessageLogModule& MessageLogModule = FModuleManager::GetModuleChecked<FMessageLogModule>("MessageLog");
        MessageLogModule.UnregisterLogListing("RenderStreamValidation");
    }
}

FString FRenderStreamEditorModule::StreamName()
{
    return FString(FApp::GetProjectName()) + "_Editor"; // TODO: to support editor this will have to not be like this
}

void FRenderStreamEditorModule::DeleteCaches(const TArray<FAssetData>& InCachesToDelete)
{
    TArray<UObject*> Objects;
    for (const FAssetData& Cache : InCachesToDelete)
    {
        UPackage* Package = Cast<UPackage>(Cache.GetAsset());
        if (Package)
        {
            UObject* Asset = Package->FindAssetInPackage();
            if (Asset)
            {
                Objects.Push(Package->FindAssetInPackage());
            }
        }
    }

    if (Objects.Num() > 0) // Actually stalls for ages even if empty.
        ObjectTools::ForceDeleteObjects(Objects, false);
}

void CreateFieldInternal(FRenderStreamExposedParameterEntry& parameter, FString group, FString displayName_, FString suffix, FString key_, FString undecoratedSuffix, RenderStreamParameterType type, float min = 0.f, float max = 255.f, float step = 1.f, FString defaultValue = "0", TArray<FString> options = {})
{
    FString key = key_ + (undecoratedSuffix.IsEmpty() ? "" : "_" + undecoratedSuffix);
    FString displayName = displayName_ + (suffix.IsEmpty() ? "" : " " + suffix);

    if (options.Num() > 0)
    {
        min = 0;
        max = options.Num() - 1;
        step = 1;
    }

    parameter.Group = group;
    parameter.DisplayName = displayName;
    parameter.Key = key;
    parameter.Type = type;
    parameter.Min = min;
    parameter.Max = max;
    parameter.Step = step;
    parameter.DefaultValue = defaultValue;
    parameter.Options = options;
    parameter.DmxOffset = -1; // Auto
    parameter.DmxType = 2; // Dmx16BigEndian
}

void CreateField(FRenderStreamExposedParameterEntry& parameter, FString group, FString displayName_, FString suffix, FString key_, FString undecoratedSuffix, RenderStreamParameterType type, float min, float max, float step, float defaultValue, TArray<FString> options = {})
{
    check(type == RenderStreamParameterType::Float);
    CreateFieldInternal(parameter, group, displayName_, suffix, key_, undecoratedSuffix, type, min, max, step, FString::SanitizeFloat(defaultValue), options);
}

void CreateField(FRenderStreamExposedParameterEntry& parameter, FString group, FString displayName_, FString suffix, FString key_, FString undecoratedSuffix, RenderStreamParameterType type, FString defaultValue)
{
    check(type == RenderStreamParameterType::Text);
    CreateFieldInternal(parameter, group, displayName_, suffix, key_, undecoratedSuffix, type, 0, 0, 0, defaultValue);
}

void CreateField(FRenderStreamExposedParameterEntry& parameter, FString group, FString displayName_, FString suffix, FString key_, FString undecoratedSuffix, RenderStreamParameterType type)
{
    check(type != RenderStreamParameterType::Float);
    check(type != RenderStreamParameterType::Text);
    CreateFieldInternal(parameter, group, displayName_, suffix, key_, undecoratedSuffix, type, 0, 0, 0, "");
}


static void ConvertFields(RenderStreamLink::RemoteParameter* outputIterator, const TArray<FRenderStreamExposedParameterEntry>& input)
{
    for (const FRenderStreamExposedParameterEntry& entry : input)
    {
        RenderStreamLink::RemoteParameter& parameter = *outputIterator++;

        parameter.group = _strdup(TCHAR_TO_UTF8(*entry.Group));
        parameter.displayName = _strdup(TCHAR_TO_UTF8(*entry.DisplayName));
        parameter.key = _strdup(TCHAR_TO_UTF8(*entry.Key));
        parameter.type = RenderStreamParameterTypeToLink(entry.Type);
        if (parameter.type == RenderStreamLink::RS_PARAMETER_NUMBER)
        {
            parameter.defaults.number.min = entry.Min;
            parameter.defaults.number.max = entry.Max;
            parameter.defaults.number.step = entry.Step;
            parameter.defaults.number.defaultValue = FCString::Atof(*entry.DefaultValue);
        }
        else if (parameter.type == RenderStreamLink::RS_PARAMETER_TEXT)
        {
            parameter.defaults.text.defaultValue = _strdup(TCHAR_TO_UTF8(*entry.DefaultValue));
        }
        parameter.nOptions = uint32_t(entry.Options.Num());
        parameter.options = static_cast<const char**>(malloc(parameter.nOptions * sizeof(const char*)));
        for (size_t j = 0; j < parameter.nOptions; ++j)
        {
            parameter.options[j] = _strdup(TCHAR_TO_UTF8(*entry.Options[j]));
        }
        parameter.dmxOffset = -1; // Auto
        parameter.dmxType = RenderStreamLink::RS_DMX_16_BE;
        parameter.flags = RenderStreamLink::REMOTEPARAMETER_NO_FLAGS;
    }
}

TArray<FString> EnumOptions(const FNumericProperty* NumericProperty)
{
    TArray<FString> Options;
    if (!NumericProperty->IsEnum())
        return Options;

    const UEnum* Enum = NumericProperty->GetIntPropertyEnum();
    if (!Enum)
        return Options;

    const int64 Max = Enum->GetMaxEnumValue();
    for (int64 i = 0; i < Max; ++i)
        Options.Push(Enum->GetDisplayNameTextByIndex(i).ToString());
    return Options;
}

void GenerateParameters(TArray<FRenderStreamExposedParameterEntry>& Parameters, const AActor* Root)
{
    if (!Root)
        return;
    for (TFieldIterator<FProperty> PropIt(Root->GetClass(), EFieldIteratorFlags::ExcludeSuper); PropIt; ++PropIt)
    {
        const FProperty* Property = *PropIt;
        const FString Name = Property->GetName();
        const FString Category = Property->GetMetaData("Category");
        if (!Property->HasAllPropertyFlags(CPF_Edit | CPF_BlueprintVisible) || Property->HasAllPropertyFlags(CPF_DisableEditOnInstance))
        {
            UE_LOG(LogRenderStreamEditor, Verbose, TEXT("Unexposed property: %s"), *Name);
        }
        else if (const FBoolProperty* BoolProperty = CastField<const FBoolProperty>(Property))
        {
            const bool v = BoolProperty->GetPropertyValue_InContainer(Root);
            UE_LOG(LogRenderStreamEditor, Log, TEXT("Exposed bool property: %s is %d"), *Name, v);
            CreateField(Parameters.Emplace_GetRef(), Category, Name, "", Name, "", RenderStreamParameterType::Float, 0.f, 1.f, 1.f, v ? 1.f : 0.f, { "Off", "On" });
        }
        else if (const FByteProperty* ByteProperty = CastField<const FByteProperty>(Property))
        {
            const uint8 v = ByteProperty->GetPropertyValue_InContainer(Root);
            TArray<FString> Options = EnumOptions(ByteProperty);
            UE_LOG(LogRenderStreamEditor, Log, TEXT("Exposed int property: %s is %d [%s]"), *Name, v, *FString::Join(Options, TEXT(",")));
            const bool HasLimits = Property->HasMetaData("ClampMin") && Property->HasMetaData("ClampMax");
            const float Min = HasLimits ? FCString::Atof(*Property->GetMetaData("ClampMin")) : 0;
            const float Max = HasLimits ? FCString::Atof(*Property->GetMetaData("ClampMax")) : 255;
            CreateField(Parameters.Emplace_GetRef(), Category, Name, "", Name, "", RenderStreamParameterType::Float, Min, Max, 1.f, float(v), Options);
        }
        else if (const FIntProperty* IntProperty = CastField<const FIntProperty>(Property))
        {
            const int32 v = IntProperty->GetPropertyValue_InContainer(Root);
            TArray<FString> Options = EnumOptions(IntProperty);
            UE_LOG(LogRenderStreamEditor, Log, TEXT("Exposed int property: %s is %d [%s]"), *Name, v, *FString::Join(Options, TEXT(",")));
            const bool HasLimits = Property->HasMetaData("ClampMin") && Property->HasMetaData("ClampMax");
            const float Min = HasLimits ? FCString::Atof(*Property->GetMetaData("ClampMin")) : -1000;
            const float Max = HasLimits ? FCString::Atof(*Property->GetMetaData("ClampMax")) : +1000;
            CreateField(Parameters.Emplace_GetRef(), Category, Name, "", Name, "", RenderStreamParameterType::Float, Min, Max, 1.f, float(v), Options);
        }
        else if (const FDoubleProperty* DoubleProperty = CastField<const FDoubleProperty>(Property)) //Property defined as a float in the blueprint
        {
            const float v = DoubleProperty->GetPropertyValue_InContainer(Root);
            UE_LOG(LogRenderStreamEditor, Log, TEXT("Exposed float property: %s is %f"), *Name, v);
            const bool HasLimits = Property->HasMetaData("ClampMin") && Property->HasMetaData("ClampMax");
            const float Min = HasLimits ? FCString::Atof(*Property->GetMetaData("ClampMin")) : -1;
            const float Max = HasLimits ? FCString::Atof(*Property->GetMetaData("ClampMax")) : +1;
            CreateField(Parameters.Emplace_GetRef(), Category, Name, "", Name, "", RenderStreamParameterType::Float, Min, Max, 0.001f, v);
        }
        else if (const FFloatProperty* FloatProperty = CastField<const FFloatProperty>(Property)) 
        {
            const float v = FloatProperty->GetPropertyValue_InContainer(Root);
            UE_LOG(LogRenderStreamEditor, Log, TEXT("Exposed float property: %s is %f"), *Name, v);
            const bool HasLimits = Property->HasMetaData("ClampMin") && Property->HasMetaData("ClampMax");
            const float Min = HasLimits ? FCString::Atof(*Property->GetMetaData("ClampMin")) : -1;
            const float Max = HasLimits ? FCString::Atof(*Property->GetMetaData("ClampMax")) : +1;
            CreateField(Parameters.Emplace_GetRef(), Category, Name, "", Name, "", RenderStreamParameterType::Float, Min, Max, 0.001f, v);
        }
        else if (const FStructProperty* StructProperty = CastField<const FStructProperty>(Property)) 
        {
            const void* StructAddress = StructProperty->ContainerPtrToValuePtr<void>(Root);
            if (StructProperty->Struct == TBaseStructure<FVector>::Get())
            {
                FVector v;
                const bool HasLimits = Property->HasMetaData("ClampMin") && Property->HasMetaData("ClampMax");
                const float Min = HasLimits ? FCString::Atof(*Property->GetMetaData("ClampMin")) : -1;
                const float Max = HasLimits ? FCString::Atof(*Property->GetMetaData("ClampMax")) : +1;
                StructProperty->CopyCompleteValue(&v, StructAddress);
                UE_LOG(LogRenderStreamEditor, Log, TEXT("Exposed vector property: %s is <%f, %f, %f>"), *Name, v.X, v.Y, v.Z);
                CreateField(Parameters.Emplace_GetRef(), Category, Name, "x", Name, "x", RenderStreamParameterType::Float, Min, Max, 0.001f, v.X);
                CreateField(Parameters.Emplace_GetRef(), Category, Name, "y", Name, "y", RenderStreamParameterType::Float, Min, Max, 0.001f, v.Y);
                CreateField(Parameters.Emplace_GetRef(), Category, Name, "z", Name, "z", RenderStreamParameterType::Float, Min, Max, 0.001f, v.Z);
            }
            else if (StructProperty->Struct == TBaseStructure<FColor>::Get())
            {
                FColor v;
                StructProperty->CopyCompleteValue(&v, StructAddress);
                UE_LOG(LogRenderStreamEditor, Log, TEXT("Exposed colour property: %s is <%d, %d, %d, %d>"), *Name, v.R, v.G, v.B, v.A);
                CreateField(Parameters.Emplace_GetRef(), Category, Name, "r", Name, "r", RenderStreamParameterType::Float, 0.f, 1.f, 0.0001f, v.R / 255.f);
                CreateField(Parameters.Emplace_GetRef(), Category, Name, "g", Name, "g", RenderStreamParameterType::Float, 0.f, 1.f, 0.0001f, v.G / 255.f);
                CreateField(Parameters.Emplace_GetRef(), Category, Name, "b", Name, "b", RenderStreamParameterType::Float, 0.f, 1.f, 0.0001f, v.B / 255.f);
                CreateField(Parameters.Emplace_GetRef(), Category, Name, "a", Name, "a", RenderStreamParameterType::Float, 0.f, 1.f, 0.0001f, v.A / 255.f);
            }
            else if (StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
            {
                FLinearColor v;
                StructProperty->CopyCompleteValue(&v, StructAddress);
                UE_LOG(LogRenderStreamEditor, Log, TEXT("Exposed linear colour property: %s is <%f, %f, %f, %f>"), *Name, v.R, v.G, v.B, v.A);
                CreateField(Parameters.Emplace_GetRef(), Category, Name, "r", Name, "r", RenderStreamParameterType::Float, 0.f, 1.f, 0.0001f, v.R);
                CreateField(Parameters.Emplace_GetRef(), Category, Name, "g", Name, "g", RenderStreamParameterType::Float, 0.f, 1.f, 0.0001f, v.G);
                CreateField(Parameters.Emplace_GetRef(), Category, Name, "b", Name, "b", RenderStreamParameterType::Float, 0.f, 1.f, 0.0001f, v.B);
                CreateField(Parameters.Emplace_GetRef(), Category, Name, "a", Name, "a", RenderStreamParameterType::Float, 0.f, 1.f, 0.0001f, v.A);
            }
            else if (StructProperty->Struct == TBaseStructure<FTransform>::Get())
            {
                UE_LOG(LogRenderStreamEditor, Log, TEXT("Exposed transform property: %s"), *Name);
                CreateField(Parameters.Emplace_GetRef(), Category, Name, "", Name, "", RenderStreamParameterType::Transform);
            }
            else if (StructProperty->Struct == TBaseStructure<FRotator>::Get())
            {
                FRotator r;
                const bool HasLimits = Property->HasMetaData("ClampMin") && Property->HasMetaData("ClampMax");
                const float Min = HasLimits ? FCString::Atof(*Property->GetMetaData("ClampMin")) : -1;
                const float Max = HasLimits ? FCString::Atof(*Property->GetMetaData("ClampMax")) : +1;
                StructProperty->CopyCompleteValue(&r, StructAddress);
                UE_LOG(LogRenderStreamEditor, Log, TEXT("Exposed rotator property: %s is <%f, %f, %f>"), *Name, r.Yaw, r.Pitch, r.Roll);
                CreateField(Parameters.Emplace_GetRef(), Category, Name, "yaw", Name, "yaw", RenderStreamParameterType::Float, Min, Max, 0.001f, r.Yaw);
                CreateField(Parameters.Emplace_GetRef(), Category, Name, "pitch", Name, "pitch", RenderStreamParameterType::Float, Min, Max, 0.001f, r.Pitch);
                CreateField(Parameters.Emplace_GetRef(), Category, Name, "roll", Name, "roll", RenderStreamParameterType::Float, Min, Max, 0.001f, r.Roll);
            }
            else
            {
                UE_LOG(LogRenderStreamEditor, Log, TEXT("Exposed struct property: %s"), *Name);
            }
        }
        else if (const FObjectProperty* ObjectProperty = CastField<const FObjectProperty>(Property))
        {
            const void* ObjectAddress = ObjectProperty->ContainerPtrToValuePtr<void>(Root);
            UObject* o = ObjectProperty->GetObjectPropertyValue(ObjectAddress);
            if (const UTextureRenderTarget2D* Texture = Cast<const UTextureRenderTarget2D>(o))
            {
                UE_LOG(LogRenderStreamEditor, Log, TEXT("Exposed render texture property: %s"), *Name);
                CreateField(Parameters.Emplace_GetRef(), Category, Name, "", Name, "", RenderStreamParameterType::Image);
            }
            else
            {
                UE_LOG(LogRenderStreamEditor, Log, TEXT("Exposed object property: %s"), *Name);
            }
        }
        else if (const FTextProperty* TextProperty = CastField<const FTextProperty>(Property))
        {
            const FText v = TextProperty->GetPropertyValue_InContainer(Root);
            const FString s = v.ToString();
            UE_LOG(LogRenderStreamEditor, Log, TEXT("Exposed text property: %s is %s"), *Name, *s);
            CreateField(Parameters.Emplace_GetRef(), Category, Name, "", Name, "", RenderStreamParameterType::Text, s);
        }
        else
        {
            UE_LOG(LogRenderStreamEditor, Log, TEXT("Unsupported exposed property: %s"), *Name);
        }
    }
}

void FetchLevelCaches(
    TMap<FSoftObjectPath, URenderStreamChannelCacheAsset*> const& LevelParams,
    TArray<const URenderStreamChannelCacheAsset*>& Levels,
    const URenderStreamChannelCacheAsset* Parent)
{
    Levels.Push(Parent);
    for (FSoftObjectPath Path : Parent->SubLevels)
    {
        URenderStreamChannelCacheAsset* const* Cache = LevelParams.Find(Path);
        if (Cache != nullptr && !Levels.Contains(*Cache))
            FetchLevelCaches(LevelParams, Levels, *Cache);
    }
}

void GenerateScene(
    TMap<FSoftObjectPath, URenderStreamChannelCacheAsset*> const& LevelParams,
    RenderStreamLink::RemoteParameters& SceneParameters,
    const URenderStreamChannelCacheAsset* Cache,
    const URenderStreamChannelCacheAsset* Persistent)
{
    FString sceneName = FPackageName::GetShortName(Cache->Level.GetAssetPathName());
    SceneParameters.name = _strdup(TCHAR_TO_UTF8(*sceneName));

    TArray<const URenderStreamChannelCacheAsset*> Levels;
    if (Persistent != nullptr)
        Levels.Push(Persistent);

    FetchLevelCaches(LevelParams, Levels, Cache);

    uint32_t nParams = 0;
    for (auto Level : Levels)
        nParams += Level->ExposedParams.Num();

    SceneParameters.nParameters = nParams;
    SceneParameters.parameters = static_cast<RenderStreamLink::RemoteParameter*>(
        malloc(nParams * sizeof(RenderStreamLink::RemoteParameter)));

    size_t offset = 0;
    for (auto Level : Levels)
    {
        ConvertFields(SceneParameters.parameters + offset, Level->ExposedParams);
        offset += Level->ExposedParams.Num();
    }

    UE_LOG(LogRenderStreamEditor, Log, TEXT("Generated schema for scene: %s"), UTF8_TO_TCHAR(SceneParameters.name));
}

bool TryGetCache(const FString LevelPath, URenderStreamChannelCacheAsset*& Cache)
{
    const FString PathName = CacheFolder + LevelPath;
    const FSoftObjectPath Path(PathName);
    Cache = Cast<URenderStreamChannelCacheAsset>(Path.TryLoad());
    return Cache != nullptr;
}

URenderStreamChannelCacheAsset* GetOrCreateCache(ULevel* Level)
{
    URenderStreamChannelCacheAsset* Cache = nullptr;
    const FString LevelPath = Level->GetPackage()->GetPathName();
    if (!TryGetCache(LevelPath, Cache)) // Asset doesn't exists.
    {
        const FString PathName = CacheFolder + LevelPath;
        FString AssetName;
        if (!PathName.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
        {
            AssetName = PathName;
        }

        UPackage* Package = FindPackage(nullptr, *PathName);
        if (!Package)
        {
            Package = CreatePackage(*PathName);
        }

        Package->FullyLoad();
        Cache = NewObject<URenderStreamChannelCacheAsset>(Package, FName(AssetName), RF_Public | RF_Standalone);
    }

    return Cache;
}

URenderStreamChannelCacheAsset* UpdateLevelChannelCache(ULevel* Level)
{
    URenderStreamChannelCacheAsset* Cache = GetOrCreateCache(Level);

    // Update the Cache.
    const FString LevelPath = Level->GetPackage()->GetPathName();
    Cache->Level = LevelPath;
    Cache->Channels.Empty();
    Cache->ChannelInfoMap.Empty();
    for (auto Actor : Level->Actors)
    {
        if (Actor)
        {
            TWeakObjectPtr<URenderStreamChannelDefinition> Definition = Actor->FindComponentByClass<URenderStreamChannelDefinition>();
            if (Definition.IsValid())
            {
                FString ChannelName = TCHAR_TO_UTF8(*(Definition->GetChannelName()));
                Cache->Channels.Emplace(ChannelName);
                Cache->ChannelInfoMap.Emplace(ChannelName, FRenderStreamValidation::GetChannelInfo(Definition, Level));
            }
        }
    }

    Cache->ExposedParams.Empty();
    GenerateParameters(Cache->ExposedParams, Level->GetLevelScriptActor());

    // We can only know the sublevels of the persistent level.
    if (Level->IsPersistentLevel())
    {
        Cache->SubLevels.Empty();
        for (ULevelStreaming* SubLevel : Level->GetWorld()->GetStreamingLevels())
            Cache->SubLevels.Add(SubLevel->GetWorldAsset()->GetPackage()->GetPathName());
    }

    // Save the Cache.
    UPackage* Package = Cache->GetPackage();
    Package->MarkPackageDirty();
    FAssetRegistryModule::AssetCreated(Cache);
    const FString PackageFileName = FPackageName::LongPackageNameToFilename(CacheFolder + LevelPath, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs args;
    args.SaveFlags = EObjectFlags::RF_Public | EObjectFlags::RF_Standalone;
    args.bForceByteSwapping = true;
    bool bSaved = UPackage::SavePackage(
        Package,
        Cache,
        *PackageFileName,
        args
    );

    return Cache;
}

bool RemoveInvalidCacheEntries()
{
    TArray<URenderStreamChannelCacheAsset*> ChannelCaches;
    const auto ObjectLibrary = UObjectLibrary::CreateLibrary(URenderStreamChannelCacheAsset::StaticClass(), false, false);
    ObjectLibrary->LoadAssetsFromPath(CacheFolder);
    ObjectLibrary->GetObjects(ChannelCaches);

    TArray<FAssetData> Assets;
    const auto LevelLibrary = UObjectLibrary::CreateLibrary(ULevel::StaticClass(), false, true);
    LevelLibrary->LoadAssetDataFromPath(ContentFolder);
    LevelLibrary->GetAssetDataList(Assets);

    TArray<FAssetData> MapAssets;
    const auto MapLibrary = UObjectLibrary::CreateLibrary(UWorld::StaticClass(), false, true);
    MapLibrary->LoadAssetDataFromPath(ContentFolder);
    MapLibrary->GetAssetDataList(MapAssets);

    Assets.Append(MapAssets);

    TArray<UObject*> ObjectsToDelete;

    auto IsInvalidCacheAsset = [&Assets, &ObjectsToDelete](URenderStreamChannelCacheAsset* CacheAsset) {
        bool Invalid = false;
        
        FString CachedPath = CacheAsset->Level.ToString();
        auto MatchesCached = [&CachedPath](const FAssetData& Asset) {
            const FString PackageName = Asset.PackageName.ToString();
            return PackageName == CachedPath;
        };

        if (!Assets.FindByPredicate(MatchesCached))
            Invalid = true;

        if (Invalid)
            ObjectsToDelete.Add(CacheAsset);

        return Invalid;
    };

    const auto RemoveCount = ChannelCaches.RemoveAll(IsInvalidCacheAsset);
    if (RemoveCount > 0)
        ObjectTools::ForceDeleteObjects(ObjectsToDelete, false);

    return RemoveCount > 0;
}

void UpdateChannelCache()
{
    RemoveInvalidCacheEntries();

    UWorld* World = GEditor->GetEditorWorldContext().World();
    for (ULevel* Level : World->GetLevels())
    {
        if (Level)
            UpdateLevelChannelCache(Level);
    }

    for (const ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
    {
        if (StreamingLevel->IsLevelLoaded())
            UpdateLevelChannelCache(StreamingLevel->GetLoadedLevel());
    }

    // Loop over all levels and make sure caches exist for them.
    TArray<FAssetData> LevelAssets;
    const auto LevelLibrary = UObjectLibrary::CreateLibrary(ULevel::StaticClass(), false, true);
    LevelLibrary->LoadAssetDataFromPath(ContentFolder);
    LevelLibrary->GetAssetDataList(LevelAssets);
    for (FAssetData const& Asset : LevelAssets)
    {
        // Create the required caches if they don't exist.
        URenderStreamChannelCacheAsset* Cache;
        if (!TryGetCache(CacheFolder + Asset.GetFullName(), Cache))
            Cache = UpdateLevelChannelCache(Cast<ULevel>(Asset.FastGetAsset(true)));
    }
}

URenderStreamChannelCacheAsset* GetDefaultMapCache()
{
    const FString DefaultMap = UGameMapsSettings::GetGameDefaultMap();
    URenderStreamChannelCacheAsset* Cache = nullptr;
    TryGetCache(DefaultMap, Cache);
    // This should never be the case because we will have already generated all the caches for the levels previously.
    if (!Cache)
    {
        const FSoftObjectPath Path(DefaultMap);
        ULevel* Level = Cast<ULevel>(Path.TryLoad());
        if (Level)
            Cache = UpdateLevelChannelCache(Level);
    }

    return Cache;
}

void FRenderStreamEditorModule::RunValidation(const TArray<URenderStreamChannelCacheAsset*> Caches)
{
    FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
    TSharedPtr<IMessageLogListing> RSVLog = MessageLogModule.GetLogListing("RenderStreamValidation");
    if (RSVLog)
        RSVLog->ClearMessages();
    FRenderStreamValidation::RunValidation(Caches);
}

void FRenderStreamEditorModule::GenerateAssetMetadata()
{
    if (!RenderStreamLink::instance().isAvailable())
    {
        UE_LOG(LogRenderStreamEditor, Warning, TEXT("RenderStreamLink unavailable, skipped GenerateAssetMetadata"));
        return;
    }

    const URenderStreamSettings* settings = GetDefault<URenderStreamSettings>();

    // Update currently loaded levels
    UpdateChannelCache();

    TArray<URenderStreamChannelCacheAsset*> ChannelCaches;
    auto ObjectLibrary = UObjectLibrary::CreateLibrary(URenderStreamChannelCacheAsset::StaticClass(), false, false);
    ObjectLibrary->LoadAssetsFromPath(CacheFolder);
    ObjectLibrary->GetObjects(ChannelCaches);

    TArray<FAssetData> CachesForDelete;
    std::set<std::string> Channels;
    TMap<FSoftObjectPath, URenderStreamChannelCacheAsset*> LevelParams;
    for (size_t i = 0; i < ChannelCaches.Num(); ++i)
    {
        URenderStreamChannelCacheAsset* Cache = ChannelCaches[i];
        const FName PathName = Cache->Level.GetAssetPathName();
        if (FPackageName::DoesPackageExist(PathName.ToString()))
        {
            for (const FString& Channel : Cache->Channels)
            {
                Channels.emplace(TCHAR_TO_ANSI(*Channel));
            }

            LevelParams.Add(Cache->Level) = Cache;
        }
        else
        {
            // Remove them so we don't process deleted caches.
            CachesForDelete.Add(Cache->GetPackage());
            ChannelCaches.RemoveAt(i);
            --i;
        }
    }

    RenderStreamLink::ScopedSchema Schema;
    Schema.schema.channels.nChannels = uint32_t(Channels.size());
    Schema.schema.channels.channels = static_cast<const char**>(malloc(Schema.schema.channels.nChannels * sizeof(const char*)));
    auto It = Channels.begin();
    for (size_t i = 0; i < Schema.schema.channels.nChannels && It != Channels.end(); ++i, ++It)
        Schema.schema.channels.channels[i] = _strdup(It->c_str());

    UWorld* World = GEditor->GetEditorWorldContext().World();

    static const FString defaultMapErrMsg = "!!!ERROR: Unable to set default map. Ensure that Game Default Map is valid and \nhas been opened at least once in the Unreal Editor, or use the Maps scene selector. This is not a bug.";

    switch (settings->SceneSelector)
    {
    case ERenderStreamSceneSelector::None:
    {
        URenderStreamChannelCacheAsset* MainMap = GetDefaultMapCache();
        if (MainMap)
        {
            TArray<URenderStreamChannelCacheAsset*> SubLevels;
            for (FSoftObjectPath Path : MainMap->SubLevels)
            {
                URenderStreamChannelCacheAsset** Cache = LevelParams.Find(Path);
                if (Cache != nullptr)
                    SubLevels.Add(*Cache);
            }

            Schema.schema.scenes.nScenes = 1;
            Schema.schema.scenes.scenes = static_cast<RenderStreamLink::RemoteParameters*>(
                malloc(Schema.schema.scenes.nScenes * sizeof(RenderStreamLink::RemoteParameters)));
            RenderStreamLink::RemoteParameters* SceneParameters = Schema.schema.scenes.scenes;
            GenerateScene(LevelParams, *SceneParameters++, MainMap, nullptr);
        }
        else
        {
            UE_LOG(LogRenderStreamEditor, Error, TEXT("%s"), *defaultMapErrMsg);
            GEngine->AddOnScreenDebugMessage(-1, 20.f, FColor::Red, defaultMapErrMsg);
        }

        break;
    }

    case ERenderStreamSceneSelector::StreamingLevels:
    {
        URenderStreamChannelCacheAsset* MainMap = GetDefaultMapCache();
        if (MainMap)
        {
            Schema.schema.scenes.nScenes = 1 + MainMap->SubLevels.Num();
            Schema.schema.scenes.scenes = static_cast<RenderStreamLink::RemoteParameters*>(
                malloc(Schema.schema.scenes.nScenes * sizeof(RenderStreamLink::RemoteParameters)));
            RenderStreamLink::RemoteParameters* SceneParameters = Schema.schema.scenes.scenes;

            GenerateScene(LevelParams, *SceneParameters++, MainMap, nullptr);
            for (FSoftObjectPath Path : MainMap->SubLevels)
            {
                URenderStreamChannelCacheAsset** Cache = LevelParams.Find(Path);
                if (Cache != nullptr)
                    GenerateScene(LevelParams, *SceneParameters++, *Cache, MainMap);
            }
        }
        else
        {
            UE_LOG(LogRenderStreamEditor, Error, TEXT("%s"), *defaultMapErrMsg);
            GEngine->AddOnScreenDebugMessage(-1, 20.f, FColor::Red, defaultMapErrMsg);
        }

        break;
    }

    case ERenderStreamSceneSelector::Maps:
    {
        TMap<const URenderStreamChannelCacheAsset*, const URenderStreamChannelCacheAsset*> LevelParents;
        for (const URenderStreamChannelCacheAsset* Cache : ChannelCaches)
        {
            for (FSoftObjectPath Path : Cache->SubLevels)
            {
                URenderStreamChannelCacheAsset** Parent = LevelParams.Find(Path);
                if (Parent != nullptr)
                    LevelParents.Add(*Parent, Cache);
            }
        }

        Schema.schema.scenes.nScenes = ChannelCaches.Num();
        Schema.schema.scenes.scenes = static_cast<RenderStreamLink::RemoteParameters*>(malloc(Schema.schema.scenes.nScenes * sizeof(RenderStreamLink::RemoteParameters)));
        RenderStreamLink::RemoteParameters* SceneParameters = Schema.schema.scenes.scenes;

        for (const URenderStreamChannelCacheAsset* Cache : ChannelCaches)
        {
            const URenderStreamChannelCacheAsset** Entry = LevelParents.Find(Cache);
            GenerateScene(LevelParams, *SceneParameters++, Cache, Entry != nullptr ? *Entry : nullptr);
        }

        break;
    }
    }

    const FString projectName = FPaths::GetBaseFilename(FPaths::GetProjectFilePath()).ToLower();
    const FString fullSchemaJsonFileDir = FPaths::ProjectDir() + "rs_" + projectName + ".json";
    bool fileIsCheckedOut = false;

    const FSourceControlState shemeSCState = SourceControlHelpers::QueryFileState(fullSchemaJsonFileDir);

    if (SourceControlHelpers::IsEnabled() && FPaths::FileExists(fullSchemaJsonFileDir) && shemeSCState.bIsAdded)
        fileIsCheckedOut = SourceControlHelpers::CheckOutFile(fullSchemaJsonFileDir);

    if(SourceControlHelpers::IsEnabled() && !fileIsCheckedOut)
        UE_LOG(LogRenderStreamEditor, Error, TEXT("Schema file failed to check out."));

    if (RenderStreamLink::instance().rs_saveSchema(TCHAR_TO_UTF8(*FPaths::GetProjectFilePath()), &Schema.schema) != RenderStreamLink::RS_ERROR_SUCCESS)
    {
        UE_LOG(LogRenderStreamEditor, Error, TEXT("Failed to save schema"));
    }

    RunValidation(ChannelCaches);

    ObjectLibrary->ClearLoaded();
    DeleteCaches(CachesForDelete);
}

void FRenderStreamEditorModule::OnPostSaveWorldContext(UWorld* World, FObjectPostSaveContext context)
{
    if (context.SaveSucceeded())
        DirtyAssetMetadata = true;
}

void FRenderStreamEditorModule::OnPostSaveWorld(UWorld* World)
{
    DirtyAssetMetadata = true;
}

void FRenderStreamEditorModule::OnAssetsDeleted(const TArray<UClass*>& DeletedAssetClasses)
{
    if (DeletedAssetClasses.Contains(UWorld::StaticClass()))
        DirtyAssetMetadata = true;
}

void FRenderStreamEditorModule::OnBeginFrame()
{
    // We have to generate the metadata here because renaming a level does not trigger assets deleted
    // and the old level is still around when OnPostSaveWorld is triggered, remove this once fixed by Epic.
    if (DirtyAssetMetadata)
    {
        GenerateAssetMetadata();
        DirtyAssetMetadata = false;
    }
}

void FRenderStreamEditorModule::OnPostEngineInit()
{
    RegisterSettings();
}

void FRenderStreamEditorModule::OnObjectPostEditChange(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent)
{
    if (Object && Object->HasAnyFlags(RF_ClassDefaultObject))
    {
        // we only care if default objects have been changed eg. project settings objects like UGameMapSettings
        // add include/exclude filters here if required
        DirtyAssetMetadata = true;
    }
}

void FRenderStreamEditorModule::OnShutdownPostPackagesSaved()
{
    if (DirtyAssetMetadata)
    {
        // due to our metadata generation happening in OnBeginFrame we rely on the engine ticking in order detect that metadata should be generated
        // if however the metadata is dirty in this callback it means the editor is closing and won't tick again
        // therefore this is our last chance to generate metadata during this runtime, if we don't our metadata may be made stale
        GenerateAssetMetadata();
    }
}

void FRenderStreamEditorModule::RegisterSettings()
{
    if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
    {
        SettingsModule->RegisterSettings("Project", "Plugins", "DisguiseRenderStream",
            LOCTEXT("RuntimeSettingsName", "Disguise RenderStream"),
            LOCTEXT("RuntimeSettingsDescription", "Project settings for Disguise RenderStream plugin"),
            GetMutableDefault<URenderStreamSettings>()
        );
    }
}

void FRenderStreamEditorModule::UnregisterSettings()
{
    if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
    {
        SettingsModule->UnregisterSettings("Project", "Plugins", "DisguiseRenderStream");
    }
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRenderStreamEditorModule, RenderStreamEditor);
