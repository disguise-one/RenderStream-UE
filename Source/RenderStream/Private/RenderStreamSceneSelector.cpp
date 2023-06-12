#include "RenderStreamSceneSelector.h"
#include "GameFramework/Actor.h"
#include "Engine/TextureRenderTarget2D.h"
#include <string.h>
#include <malloc.h>
#include "RenderStream.h"
#include "RenderStreamHelper.h"
#include "RSUCHelpers.inl"
#include "RenderStreamSettings.h"
#include "Engine/LevelStreaming.h"
#include "Engine/LevelScriptActor.h"
#include "RenderStreamSettings.h"

#include "RenderCore/Public/ProfilingDebugging/RealtimeGPUProfiler.h"

RenderStreamSceneSelector::~RenderStreamSceneSelector() = default;

void RenderStreamSceneSelector::GetAllLevels(TArray<AActor*>& Actors, ULevel * Level) const
{
    if (Level)
    {
        auto Actor = static_cast<AActor*>(Level->GetLevelScriptActor());
        if (Actor && !Actors.Contains(Actor))
            Actors.Push(Actor);

        if (Level->IsPersistentLevel())
        {
            const auto World = Level->GetWorld();
            for (const ULevelStreaming* SubLevel : World->GetStreamingLevels())
                if (SubLevel->GetLoadedLevel() != Level)
                    GetAllLevels(Actors, SubLevel->GetLoadedLevel());

            for (ULevel* SubLevel : World->GetLevels())
                if (SubLevel != Level)
                    GetAllLevels(Actors, SubLevel);
        }
    }
}

enum RenderStreamSceneSelector::SchemaStatus RenderStreamSceneSelector::SchemaStatus() const
{
    if (m_schemaMem.empty())
    {
        if (m_defaultSchema.schema.scenes.scenes)
            return SchemaStatus::UsingDefault;
        return SchemaStatus::NotLoaded;
    }
    return SchemaStatus::Loaded;
}

const RenderStreamLink::Schema& RenderStreamSceneSelector::Schema() const
{
    if (!m_schemaMem.empty())
        return *reinterpret_cast<const RenderStreamLink::Schema*>(m_schemaMem.data());
    else
        return m_defaultSchema.schema;
}


void RenderStreamSceneSelector::LoadSchemas(const UWorld& World)
{
    const std::string AssetPath = TCHAR_TO_UTF8(*FPaths::GetProjectFilePath());
    uint32_t nBytes = 0;
    RenderStreamLink::instance().rs_loadSchema(AssetPath.c_str(), nullptr, &nBytes);

    constexpr static int MAX_TRIES = 3;
    int iterations = 0;

    RenderStreamLink::RS_ERROR res = RenderStreamLink::RS_ERROR_BUFFER_OVERFLOW;
    do
    {
        m_schemaMem.resize(nBytes);
        res = RenderStreamLink::instance().rs_loadSchema(AssetPath.c_str(), reinterpret_cast<RenderStreamLink::Schema*>(m_schemaMem.data()), &nBytes);

        if (res == RenderStreamLink::RS_ERROR_SUCCESS)
            break;

        ++iterations;
    } while (res == RenderStreamLink::RS_ERROR_BUFFER_OVERFLOW && iterations < MAX_TRIES);

    bool loaded = true;
    if (res == RenderStreamLink::RS_ERROR_SUCCESS)
    {
        if (!OnLoadedSchema(World, Schema()))
        {
            UE_LOG(LogRenderStream, Error, TEXT("Incompatible schema"));
            loaded = false;
        }
    }
    else
    {
        UE_LOG(LogRenderStream, Error, TEXT("Unable to load schema - error %d"), res);
        loaded = false;
    }

    // Failed the above, get something set to be in a valid state.
    if (!loaded)
    {
        m_schemaMem.clear();
        m_defaultSchema.reset();
        RenderStreamLink::Schema& Schema = m_defaultSchema.schema;
        Schema.scenes.nScenes = 1;
        Schema.scenes.scenes = static_cast<RenderStreamLink::RemoteParameters*>(malloc(Schema.scenes.nScenes * sizeof(RenderStreamLink::RemoteParameters)));
        Schema.scenes.scenes[0].name = _strdup("Default");
        Schema.scenes.scenes[0].nParameters = 0;
        Schema.scenes.scenes[0].parameters = nullptr;
        res = RenderStreamLink::instance().rs_setSchema(&Schema);
        if (res != RenderStreamLink::RS_ERROR_SUCCESS)
            UE_LOG(LogRenderStream, Error, TEXT("Unable to set default schema - error %d"), res);
    }
}


static bool validateField(FString key_, FString undecoratedSuffix, RenderStreamLink::RemoteParameterType expectedType, const RenderStreamLink::RemoteParameter& parameter)
{
    const FString key = key_ + (undecoratedSuffix.IsEmpty() ? "" : "_" + undecoratedSuffix);
    
    if (key != parameter.key || expectedType != parameter.type)
    {
        UE_LOG(LogRenderStream, Error, 
            TEXT("Parameter mismatch - Expected parameter with key %s and type %s, got parameter with key %s and type %s."), 
            UTF8_TO_TCHAR(parameter.key), UTF8_TO_TCHAR(RenderStreamLink::ParamTypeToName(parameter.type)), *key, UTF8_TO_TCHAR(RenderStreamLink::ParamTypeToName(expectedType)));
        return false;
    }
    return true;
}

bool RenderStreamSceneSelector::ValidateParameters(const RenderStreamLink::RemoteParameters& sceneParameters, const TArray<AActor*>& Actors) const
{
    size_t offset = 0;

    for (const AActor* actor : Actors)
    {
        if (!actor)
            continue; // it's convenient at the higher level to pass nulls if there's a pattern which can miss pieces

        const size_t increment = ValidateParameters(actor, sceneParameters.parameters + offset, sceneParameters.nParameters);
        if (increment == SIZE_MAX)
        {
            UE_LOG(LogRenderStream, Error, TEXT("Schema validation failed for actor '%s'"), *actor->GetName());
            return false;
        }
        offset += increment;
    }

    if (offset < sceneParameters.nParameters)
    {
        UE_LOG(LogRenderStream, Error,
            TEXT("Unexpected extra parameters in schema (nactors = %d, offset = %d, nparams = %d)"), 
            Actors.Num(), offset, sceneParameters.nParameters);
        return false;
    }

    return true;
}

size_t RenderStreamSceneSelector::ValidateParameters(const AActor* Root, RenderStreamLink::RemoteParameter* const parameters, size_t numParameters) const
{
    size_t nParameters = 0;

    const URenderStreamSettings* settings = GetDefault<URenderStreamSettings>();

    if (settings->GenerateEvents)
    {
        for (TFieldIterator<UFunction> FuncIt(Root->GetClass()); FuncIt; ++FuncIt)
        {
            if (FuncIt->HasAnyFunctionFlags(FUNC_BlueprintEvent) && FuncIt->HasAnyFunctionFlags(FUNC_BlueprintCallable))
            {
                const FString Name = FuncIt->GetName();
                UE_LOG(LogRenderStream, Log, TEXT("Exposed custom event: %s"), *Name);
                if (numParameters < nParameters + 1)
                {
                    UE_LOG(LogRenderStream, Error, TEXT("Property %s not exposed in schema"), *Name);
                    return SIZE_MAX;
                }
                if (!validateField(Name, "", RenderStreamLink::RS_PARAMETER_EVENT, parameters[nParameters]))
                    return SIZE_MAX;
                ++nParameters;
            }
        }
    }

    for (TFieldIterator<FProperty> PropIt(Root->GetClass(), EFieldIteratorFlags::ExcludeSuper); PropIt; ++PropIt)
    {
        const FProperty* Property = *PropIt;
        const FString Name = Property->GetName();
        if (!Property->HasAllPropertyFlags(CPF_Edit | CPF_BlueprintVisible) || Property->HasAllPropertyFlags(CPF_DisableEditOnInstance))
        {
            UE_LOG(LogRenderStream, Verbose, TEXT("Unexposed property: %s"), *Name);
        }
        else if (const FBoolProperty* BoolProperty = CastField<const FBoolProperty>(Property))
        {
            UE_LOG(LogRenderStream, Log, TEXT("Exposed bool property: %s"), *Name);
            if (numParameters < nParameters + 1)
            {
                UE_LOG(LogRenderStream, Error, TEXT("Property %s not exposed in schema"), *Name);
                return SIZE_MAX;
            }
            if (!validateField(Name, "", RenderStreamLink::RS_PARAMETER_NUMBER, parameters[nParameters]))
                return SIZE_MAX;
            ++nParameters;
        }
        else if (const FByteProperty* ByteProperty = CastField<const FByteProperty>(Property))
        {
            UE_LOG(LogRenderStream, Log, TEXT("Exposed int property: %s"), *Name);
            if (numParameters < nParameters + 1)
            {
                UE_LOG(LogRenderStream, Error, TEXT("Property %s not exposed in schema"), *Name);
                return SIZE_MAX;
            }
            if (!validateField(Name, "", RenderStreamLink::RS_PARAMETER_NUMBER, parameters[nParameters]))
                return SIZE_MAX;
            ++nParameters;
        }
        else if (const FIntProperty* IntProperty = CastField<const FIntProperty>(Property))
        {
            UE_LOG(LogRenderStream, Log, TEXT("Exposed int property: %s"), *Name);
            if (numParameters < nParameters + 1)
            {
                UE_LOG(LogRenderStream, Error, TEXT("Property %s not exposed in schema"), *Name);
                return SIZE_MAX;
            }
            if (!validateField(Name, "", RenderStreamLink::RS_PARAMETER_NUMBER, parameters[nParameters]))
                return SIZE_MAX;
            ++nParameters;
        }
        else if (const FDoubleProperty* DoubleProperty = CastField<const FDoubleProperty>(Property))
        {
            UE_LOG(LogRenderStream, Log, TEXT("Exposed float property: %s"), *Name);
            if (numParameters < nParameters + 1)
            {
                UE_LOG(LogRenderStream, Error, TEXT("Property %s not exposed in schema"), *Name);
                return SIZE_MAX;
            }
            if (!validateField(Name, "", RenderStreamLink::RS_PARAMETER_NUMBER, parameters[nParameters]))
                return SIZE_MAX;
            ++nParameters;
        }
        else if (const FFloatProperty* FloatProperty = CastField<const FFloatProperty>(Property))
        {
            UE_LOG(LogRenderStream, Log, TEXT("Exposed float property: %s"), *Name);
            if (numParameters < nParameters + 1)
            {
                UE_LOG(LogRenderStream, Error, TEXT("Property %s not exposed in schema"), *Name);
                return SIZE_MAX;
            }
            if (!validateField(Name, "", RenderStreamLink::RS_PARAMETER_NUMBER, parameters[nParameters]))
                return SIZE_MAX;
            ++nParameters;
        }
        else if (const FStructProperty* StructProperty = CastField<const FStructProperty>(Property))
        {
            const void* StructAddress = StructProperty->ContainerPtrToValuePtr<void>(Root);
            if (StructProperty->Struct == TBaseStructure<FVector>::Get())
            {
                UE_LOG(LogRenderStream, Log, TEXT("Exposed vector property: %s"), *Name);
                if (numParameters < nParameters + 3)
                {
                    UE_LOG(LogRenderStream, Error, TEXT("Properties for %s not exposed in schema"), *Name);
                    return SIZE_MAX;
                }
                if (!validateField(Name, "x", RenderStreamLink::RS_PARAMETER_NUMBER, parameters[nParameters + 0]) ||
                    !validateField(Name, "y", RenderStreamLink::RS_PARAMETER_NUMBER, parameters[nParameters + 1]) ||
                    !validateField(Name, "z", RenderStreamLink::RS_PARAMETER_NUMBER, parameters[nParameters + 2]))
                {
                    return SIZE_MAX;
                }
                nParameters += 3;
            }
            else if (StructProperty->Struct == TBaseStructure<FColor>::Get())
            {
                UE_LOG(LogRenderStream, Log, TEXT("Exposed colour property: %s"), *Name);
                if (numParameters < nParameters + 4)
                {
                    UE_LOG(LogRenderStream, Error, TEXT("Properties for %s not exposed in schema"), *Name);
                    return SIZE_MAX;
                }
                if (!validateField(Name, "r", RenderStreamLink::RS_PARAMETER_NUMBER, parameters[nParameters + 0]) ||
                    !validateField(Name, "g", RenderStreamLink::RS_PARAMETER_NUMBER, parameters[nParameters + 1]) ||
                    !validateField(Name, "b", RenderStreamLink::RS_PARAMETER_NUMBER, parameters[nParameters + 2]) ||
                    !validateField(Name, "a", RenderStreamLink::RS_PARAMETER_NUMBER, parameters[nParameters + 3]))
                {
                    return SIZE_MAX;
                }
                nParameters += 4;
            }
            else if (StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
            {
                UE_LOG(LogRenderStream, Log, TEXT("Exposed linear colour property: %s"), *Name);
                if (numParameters < nParameters + 4)
                {
                    UE_LOG(LogRenderStream, Error, TEXT("Properties for %s not exposed in schema"), *Name);
                    return SIZE_MAX;
                }
                if (!validateField(Name, "r", RenderStreamLink::RS_PARAMETER_NUMBER, parameters[nParameters + 0]) ||
                    !validateField(Name, "g", RenderStreamLink::RS_PARAMETER_NUMBER, parameters[nParameters + 1]) ||
                    !validateField(Name, "b", RenderStreamLink::RS_PARAMETER_NUMBER, parameters[nParameters + 2]) ||
                    !validateField(Name, "a", RenderStreamLink::RS_PARAMETER_NUMBER, parameters[nParameters + 3]))
                {
                    return SIZE_MAX;
                }
                nParameters += 4;
            }
            else if (StructProperty->Struct == TBaseStructure<FTransform>::Get())
            {
                UE_LOG(LogRenderStream, Log, TEXT("Exposed transform property: %s"), *Name);
                if (numParameters < nParameters + 1)
                {
                    UE_LOG(LogRenderStream, Error, TEXT("Properties for %s not exposed in schema"), *Name);
                    return SIZE_MAX;
                }
                validateField(Name, "", RenderStreamLink::RS_PARAMETER_TRANSFORM, parameters[nParameters]);
                ++nParameters;
            }
            else if (StructProperty->Struct == TBaseStructure<FRotator>::Get())
            {
                UE_LOG(LogRenderStream, Log, TEXT("Exposed rotator property: %s"), *Name);
                if (numParameters < nParameters + 3)
                {
                    UE_LOG(LogRenderStream, Error, TEXT("Properties for %s not exposed in schema"), *Name);
                    return SIZE_MAX;
                }
                if (!validateField(Name, "yaw", RenderStreamLink::RS_PARAMETER_NUMBER, parameters[nParameters + 0]) ||
                    !validateField(Name, "pitch", RenderStreamLink::RS_PARAMETER_NUMBER, parameters[nParameters + 1]) ||
                    !validateField(Name, "roll", RenderStreamLink::RS_PARAMETER_NUMBER, parameters[nParameters + 2]))
                {
                    return SIZE_MAX;
                }
                nParameters += 3;
            }
            else
            {
                UE_LOG(LogRenderStream, Warning, TEXT("Unknown struct property: %s"), *Name);
            }
        }
        else if (const FObjectProperty* ObjectProperty = CastField<const FObjectProperty>(Property))
        {
            const void* ObjectAddress = ObjectProperty->ContainerPtrToValuePtr<void>(Root);
            UObject* o = ObjectProperty->GetObjectPropertyValue(ObjectAddress);
            if (UTextureRenderTarget2D* Texture = Cast<UTextureRenderTarget2D>(o))
            {
                UE_LOG(LogRenderStream, Log, TEXT("Exposed render texture property: %s"), *Name);
                if (numParameters < nParameters + 1)
                {
                    UE_LOG(LogRenderStream, Error, TEXT("Properties for %s not exposed in schema"), *Name);
                    return SIZE_MAX;
                }
                validateField(Name, "", RenderStreamLink::RS_PARAMETER_IMAGE, parameters[nParameters]);
                ++nParameters;
            }
            else
            {
                UE_LOG(LogRenderStream, Warning, TEXT("Unknown object property: %s"), *Name);
            }
        }
        else if (const FSoftObjectProperty* SoftObjectProperty = CastField<const FSoftObjectProperty>(Property))
        {
            const void* SoftObjectAddress = SoftObjectProperty->ContainerPtrToValuePtr<void>(Root);
            const FSoftObjectPtr& o = SoftObjectProperty->GetPropertyValue(SoftObjectAddress);
            const FSoftObjectPath PropKey = o.ToSoftObjectPath();
            if (TSoftObjectPtr<USkeleton> Skeleton(PropKey); Skeleton.IsValid() || Skeleton.IsPending())
            {
                UE_LOG(LogRenderStream, Log, TEXT("Exposed skeleton property: %s"), *Name);
                if (numParameters < nParameters + 1)
                {
                    UE_LOG(LogRenderStream, Error, TEXT("Properties for %s not exposed in schema"), *Name);
                    return SIZE_MAX;
                }
                validateField(Name, "", RenderStreamLink::RS_PARAMETER_SKELETON, parameters[nParameters]);
                ++nParameters;
            }
        }
        else if (const FTextProperty* TextProperty = CastField<const FTextProperty>(Property))
        {
            UE_LOG(LogRenderStream, Log, TEXT("Exposed text property: %s"), *Name);
            if (numParameters < nParameters + 1)
            {
                UE_LOG(LogRenderStream, Error, TEXT("Properties for %s not exposed in schema"), *Name);
                return SIZE_MAX;
            }
            validateField(Name, "", RenderStreamLink::RS_PARAMETER_TEXT, parameters[nParameters]);
            ++nParameters;
        }
        else
        {
            UE_LOG(LogRenderStream, Warning, TEXT("Unsupported exposed property: %s"), *Name);
        }
    }

    UE_LOG(LogRenderStream, Log, TEXT("Exposed level '%s' with %d parameters"), *Root->GetActorNameOrLabel(), nParameters);
    return nParameters;
}

void RenderStreamSceneSelector::ApplyParameters(uint32_t sceneId, const TArray<AActor*>& Actors)
{
    if (sceneId >= Schema().scenes.nScenes)
    {
        UE_LOG(LogRenderStream, Fatal, TEXT("Error attempting to select scene %d out of %d scenes. Ensure that all relevant scenes have been loaded in the Unreal Editor at least once."), sceneId, Schema().scenes.nScenes);
    }
    const RenderStreamLink::RemoteParameters& params = Schema().scenes.scenes[sceneId];

    size_t nFloatParams = 0;
    size_t nImageParams = 0;
    size_t nTextParams = 0;
    size_t nPoseParams = 0;
    for (size_t i = 0; i < params.nParameters ; ++i)
    {
        const RenderStreamLink::RemoteParameter& param = params.parameters[i];
        switch (param.type)
        {
        case RenderStreamLink::RS_PARAMETER_NUMBER:
        case RenderStreamLink::RS_PARAMETER_EVENT:
            nFloatParams++;
            break;
        case RenderStreamLink::RS_PARAMETER_IMAGE:
            nImageParams++;
            break;
        case RenderStreamLink::RS_PARAMETER_POSE:
        case RenderStreamLink::RS_PARAMETER_TRANSFORM:
            nFloatParams += 16;
            break;
        case RenderStreamLink::RS_PARAMETER_TEXT:
            nTextParams++;
            break;
        case RenderStreamLink::RS_PARAMETER_SKELETON:
            nPoseParams++;
            break;
        default:
            UE_LOG(LogRenderStream, Error, TEXT("Unhandled parameter type"));
            return;
        }
    }
    
    std::vector<float> floatValues(nFloatParams);
    std::vector<RenderStreamLink::ImageFrameData> imageValues(nImageParams);

    RenderStreamLink::RS_ERROR res = RenderStreamLink::instance().rs_getFrameParameters(params.hash, floatValues.data(), floatValues.size() * sizeof(float));
    if (res != RenderStreamLink::RS_ERROR_SUCCESS)
    {
        UE_LOG(LogRenderStream, Error, TEXT("Unable to get float frame parameters - %d"), res);
        return;
    }
    res = RenderStreamLink::instance().rs_getFrameImageData(params.hash, imageValues.data(), imageValues.size());
    if (res != RenderStreamLink::RS_ERROR_SUCCESS)
    {
        UE_LOG(LogRenderStream, Error, TEXT("Unable to get image frame parameters - %d"), res);
        return;
    }

    // These are updated by ApplyParameters to allow each actor to operate on the next set of data.
    const RenderStreamLink::RemoteParameter* paramsPtr = params.parameters;
    const float* floatValuesPtr = floatValues.data();
    const RenderStreamLink::ImageFrameData* imageValuesPtr = imageValues.data();
    for (AActor* actor : Actors)
    {
        if (!actor)
            continue; // it's convenient at the higher level to pass nulls if there's a pattern which can miss pieces
        ApplyParameters(actor, params.hash, &paramsPtr, params.nParameters, &floatValuesPtr, floatValues.size(), &imageValuesPtr, imageValues.size());
    }

    m_floatValuesLast = floatValues; // event parameters need to lookup previous values
}

void RenderStreamSceneSelector::ApplyParameters(AActor* Root, uint64_t specHash, const RenderStreamLink::RemoteParameter** ppParams, const size_t nParams, const float** ppFloatValues, const size_t nFloatVals, const RenderStreamLink::ImageFrameData** ppImageValues, const size_t nImageVals)
{
    auto toggle = FHardwareInfo::GetHardwareInfo(NAME_RHI);
    struct
    {
        RenderStreamLink::RSPixelFormat fmt;
        EPixelFormat ue;
    } formatMap[] = {
        // NB. FTextureRenderTargetResource::IsSupportedFormat
        { RenderStreamLink::RS_FMT_INVALID, EPixelFormat::PF_Unknown },
        { RenderStreamLink::RS_FMT_BGRA8, EPixelFormat::PF_R8G8B8A8 }, // dx11-CUDA interop only supports RGBA sRGB
        { RenderStreamLink::RS_FMT_BGRX8, EPixelFormat::PF_R8G8B8A8 }, // dx11-CUDA interop only supports RGBA sRGB
        { RenderStreamLink::RS_FMT_RGBA32F, EPixelFormat::PF_FloatRGBA},
        { RenderStreamLink::RS_FMT_RGBA16, EPixelFormat::PF_A16B16G16R16 },
        { RenderStreamLink::RS_FMT_RGBA8, EPixelFormat::PF_R8G8B8A8},
        { RenderStreamLink::RS_FMT_RGBX8, EPixelFormat::PF_R8G8B8A8 },
    };

    size_t iParam = 0;
    size_t iFloat = 0;
    size_t iImage = 0;
    size_t iText = 0;
    size_t iPose = 0;

    const float* floatValues = *ppFloatValues;
    const RenderStreamLink::ImageFrameData* imageValues = *ppImageValues;

    const URenderStreamSettings* settings = GetDefault<URenderStreamSettings>();

    if (settings->GenerateEvents)
    {
        for (TFieldIterator<UFunction> FuncIt(Root->GetClass()); FuncIt; ++FuncIt)
        {
            if (FuncIt->HasAnyFunctionFlags(FUNC_BlueprintEvent) && FuncIt->HasAnyFunctionFlags(FUNC_BlueprintCallable))
            {
                if (!m_floatValuesLast.data()) // first frame
                    break;
                if (floatValues[iFloat] > m_floatValuesLast.data()[iFloat]) // value increment signals an invoke
                {
                    uint8* Buffer = static_cast<uint8*>(FMemory_Alloca(FuncIt->ParmsSize));
                    FFrame Frame = FFrame(Root, *FuncIt, Buffer);
                    FuncIt->Invoke(Root, Frame, Buffer);
                    UE_LOG(LogRenderStream, Verbose, TEXT("Event Invoked"));
                }
                ++iFloat;
            }
        }
    }

    for (TFieldIterator<FProperty> PropIt(Root->GetClass(), EFieldIteratorFlags::ExcludeSuper); PropIt && iParam < nParams; ++PropIt)
    {
        FProperty* Property = *PropIt;
        if (!Property->HasAllPropertyFlags(CPF_Edit | CPF_BlueprintVisible) || Property->HasAllPropertyFlags(CPF_DisableEditOnInstance))
            continue;

        if (Property->IsA(FBoolProperty::StaticClass()) ||
            Property->IsA(FByteProperty::StaticClass()) ||
            Property->IsA(FIntProperty::StaticClass()) ||
            Property->IsA(FFloatProperty::StaticClass()) ||
            Property->IsA(FDoubleProperty::StaticClass()))
        {
            if (iFloat >= nFloatVals)
            {
                UE_LOG(LogRenderStream, Verbose, TEXT("Attempt to read float value from disguise that is out of range. Does the metadata need to be regenerated?"));
                continue;
            }

            if (const FBoolProperty* BoolProperty = CastField<const FBoolProperty>(Property))
            {
                const bool v = bool(floatValues[iFloat]);
                BoolProperty->SetPropertyValue_InContainer(Root, v);
            }
            else if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
            {
                const uint8 v = uint8(floatValues[iFloat]);
                ByteProperty->SetPropertyValue_InContainer(Root, v);
            }
            else if (FIntProperty* IntProperty = CastField<FIntProperty>(Property))
            {
                const int32 v = int(floatValues[iFloat]);
                IntProperty->SetPropertyValue_InContainer(Root, v);
            }
            else if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
            {
                const float v = floatValues[iFloat];
                FloatProperty->SetPropertyValue_InContainer(Root, v);
            }
            else if (FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
            {
                const float v = floatValues[iFloat];
                DoubleProperty->SetPropertyValue_InContainer(Root, v);
            }
            ++iFloat;
        }
        else if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
        {
            void* StructAddress = StructProperty->ContainerPtrToValuePtr<void>(Root);
            const UScriptStruct* vec = TBaseStructure<FVector>::Get();
            const UScriptStruct* col = TBaseStructure<FColor>::Get();
            const UScriptStruct* linCol = TBaseStructure<FLinearColor>::Get();
            const UScriptStruct* trans = TBaseStructure<FTransform>::Get();
            const UScriptStruct* rot = TBaseStructure<FRotator>::Get();
            const size_t inc = StructProperty->Struct == vec ? 3
                                : StructProperty->Struct == col || StructProperty->Struct == linCol ? 4
                                : StructProperty->Struct == trans ? 16
                                : StructProperty->Struct == rot ? 3
                                : 0;
            if (iFloat + (inc - 1) >= nFloatVals)
            {
                UE_LOG(LogRenderStream, Verbose, TEXT("Attempt to read a vector/color/transform value from disguise that is out of range. Does the metadata need to be regenerated?"));
                continue;
            }

            if (StructProperty->Struct == vec)
            {
                FVector v(floatValues[iFloat], floatValues[iFloat + 1], floatValues[iFloat + 2]);
                StructProperty->CopyCompleteValue(StructAddress, &v);
            }
            else if (StructProperty->Struct == col)
            {
                FColor v(floatValues[iFloat] * 255, floatValues[iFloat + 1] * 255, floatValues[iFloat + 2] * 255, floatValues[iFloat + 3] * 255);
                StructProperty->CopyCompleteValue(StructAddress, &v);
            }
            else if (StructProperty->Struct == linCol)
            {
                FLinearColor v(floatValues[iFloat], floatValues[iFloat + 1], floatValues[iFloat + 2], floatValues[iFloat + 3]);
                StructProperty->CopyCompleteValue(StructAddress, &v);
            }
            else if (StructProperty->Struct == trans)
            {
                static const FMatrix YUpMatrix(FVector(0.0f, 0.0f, 1.0f), FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f), FVector(0.0f, 0.0f, 0.0f));

                FMatrix m(
                    FPlane(floatValues[iFloat + 0], floatValues[iFloat + 1], floatValues[iFloat + 2], floatValues[iFloat + 3]),
                    FPlane(floatValues[iFloat + 4], floatValues[iFloat + 5], floatValues[iFloat + 6], floatValues[iFloat + 7]),
                    FPlane(floatValues[iFloat + 8], floatValues[iFloat + 9], floatValues[iFloat + 10], floatValues[iFloat + 11]),
                    FPlane(floatValues[iFloat + 12], floatValues[iFloat + 13], floatValues[iFloat + 14], floatValues[iFloat + 15])
                );

                FTransform v = d3ToUEHelpers::Convertd3TransformToUE(m, YUpMatrix);

                StructProperty->CopyCompleteValue(StructAddress, &v);
            }
            else if (StructProperty->Struct == rot)
            {
                FRotator r(floatValues[iFloat], floatValues[iFloat + 1], floatValues[iFloat + 2]);
                StructProperty->CopyCompleteValue(StructAddress, &r);
            }
            iFloat += inc;
        }
        else if (const FObjectProperty* ObjectProperty = CastField<const FObjectProperty>(Property))
        {
            const void* ObjectAddress = ObjectProperty->ContainerPtrToValuePtr<void>(Root);
            UObject* o = ObjectProperty->GetObjectPropertyValue(ObjectAddress);
            if (UTextureRenderTarget2D* Texture = Cast<UTextureRenderTarget2D>(o))
            {
                if (iImage >= nImageVals)
                {
                    UE_LOG(LogRenderStream, Verbose, TEXT("Attempt to read a image value from disguise that is out of range. Does the metadata need to be regenerated?"));
                    continue;
                }

                const RenderStreamLink::ImageFrameData& frameData = imageValues[iImage];
                if (!Texture->bGPUSharedFlag || Texture->GetFormat() != formatMap[frameData.format].ue)
                {
                    Texture->bGPUSharedFlag = true;
                    Texture->InitCustomFormat(frameData.width, frameData.height, formatMap[frameData.format].ue, false);
                }
                else
                {
                    Texture->ResizeTarget(frameData.width, frameData.height);
                }

                ENQUEUE_RENDER_COMMAND(GetTex)(
                [this, toggle, Texture, frameData, iImage](FRHICommandListImmediate& RHICmdList)
                {
                    SCOPED_DRAW_EVENTF(RHICmdList, MediaCapture, TEXT("RS Tex Param Block %d"), iImage);
                    const auto rtResource = Texture->GetRenderTargetResource();
                    if (!rtResource)
                    {
                        return;
                    }
                    void* resource = rtResource->TextureRHI->GetNativeResource();

                    RenderStreamLink::SenderFrame data = {};
                    if (toggle == "D3D11")
                    {
                        data.type = RenderStreamLink::SenderFrameType::RS_FRAMETYPE_DX11_TEXTURE;
                        data.dx11.resource = static_cast<ID3D11Resource*>(resource);
                        auto err = RenderStreamLink::instance().rs_getFrameImage2(frameData.imageId, &data);
                    }
                    else if (toggle == "D3D12")
                    {
                        {
                            SCOPED_DRAW_EVENTF(RHICmdList, MediaCapture, TEXT("RS Tex Param Flush"));
                            RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);
                        }

                        data.type = RenderStreamLink::SenderFrameType::RS_FRAMETYPE_DX12_TEXTURE;
                        data.dx12.resource = static_cast<ID3D12Resource*>(resource);
                        
                        SCOPED_DRAW_EVENTF(RHICmdList, MediaCapture, TEXT("RS getFrameImage2 %d"), iImage);
                        if (RenderStreamLink::instance().rs_getFrameImage2(frameData.imageId, &data) != RenderStreamLink::RS_ERROR_SUCCESS)
                        {

                        }
                    }
                    else if (toggle == "Vulkan")
                    {
                        {
                            SCOPED_DRAW_EVENTF(RHICmdList, MediaCapture, TEXT("RS Tex Param Flush"));
                            RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);
                        }

                        FVulkanTexture* VulkanTexture = FVulkanTexture::Cast(rtResource->TextureRHI->GetTexture2D());
                        auto point2 = VulkanTexture->GetSizeXY();

                        data.type = RenderStreamLink::SenderFrameType::RS_FRAMETYPE_VULKAN_TEXTURE;
                        data.vk.memory = VulkanTexture->GetAllocationHandle();
                        data.vk.size = VulkanTexture->GetAllocationOffset() + VulkanTexture->GetMemorySize();
                        data.vk.format = frameData.format;
                        data.vk.width = uint32_t(point2.X);
                        data.vk.height = uint32_t(point2.Y);
                        // TODO: semaphores

                        SCOPED_DRAW_EVENTF(RHICmdList, MediaCapture, TEXT("RS getFrameImage2 %d"), iImage);
                        if (RenderStreamLink::instance().rs_getFrameImage2(frameData.imageId, &data) != RenderStreamLink::RS_ERROR_SUCCESS)
                        {

                        }
                    }
                    else
                    {
                        UE_LOG(LogRenderStream, Error, TEXT("RenderStream tried to send frame with unsupported RHI backend."));
                        return;
                    }
                });
                ++iImage;
            }
        }
        else if (const FSoftObjectProperty* SoftObjectProperty = CastField<const FSoftObjectProperty>(Property))
        {
            const void* SoftObjectAddress = SoftObjectProperty->ContainerPtrToValuePtr<void>(Root);
            const FSoftObjectPtr& o = SoftObjectProperty->GetPropertyValue(SoftObjectAddress);
            FSoftObjectPath PropKey = o.ToSoftObjectPath();
            if (TSoftObjectPtr<USkeleton> Skeleton(PropKey); Skeleton.IsValid() || Skeleton.IsPending())
            {
                ApplySkeletalPose(specHash, iPose++, Property->GetName(), PropKey);
            }
        }
        else if (const FTextProperty* TextProperty = CastField<const FTextProperty>(Property))
        {
            const char* cString = nullptr;
            if (RenderStreamLink::instance().rs_getFrameText(specHash, iText, &cString) == RenderStreamLink::RS_ERROR_SUCCESS)
            {
                TextProperty->SetPropertyValue_InContainer(Root, FText::FromString(UTF8_TO_TCHAR(cString)));
            }
            ++iText;
        }
        ++iParam;
    }

    *ppFloatValues += iFloat;
    *ppImageValues += iImage;
    *ppParams += iParam;
}

void RenderStreamSceneSelector::ApplySkeletalPose(uint64_t specHash, size_t iPose, const FString& ParamKey, RenderStreamLink::FAnimDataKey& PropKey)
{
    // first get the pose for this param index
    RenderStreamLink::FSkeletalPose Pose;
    {
        RenderStreamLink::SkeletonPose rsPose{};

        const auto isNotAssignedSkeleton = [](RenderStreamLink::RS_ERROR error, size_t nJoints)->bool {
            return  error == RenderStreamLink::RS_ERROR_SUCCESS && nJoints == 0;
        };

        int nJoints;
        if (const RenderStreamLink::RS_ERROR Err = RenderStreamLink::instance().rs_getSkeletonJointPoses(specHash, iPose, nullptr, &nJoints);
            isNotAssignedSkeleton(Err, nJoints))
        {
            // not assigned skeleton is valid workflow; however we want to early out to avoid handling empty data
            return;
        }
        else if (Err != RenderStreamLink::RS_ERROR_SUCCESS)
        {
            UE_LOG(LogRenderStream, Error, TEXT("RenderStream failed to get skeletal pose size for index %llu. Error: %d"), iPose, Err);
            return;
        }

        Pose.joints.SetNum(nJoints);
        rsPose.joints = Pose.joints.GetData();
        if (const RenderStreamLink::RS_ERROR Err = RenderStreamLink::instance().rs_getSkeletonJointPoses(specHash, iPose, &rsPose, &nJoints);
            Err  != RenderStreamLink::RS_ERROR_SUCCESS)
        {
            UE_LOG(LogRenderStream, Error, TEXT("RenderStream failed to get skeletal pose %llu with %d joints. Error: %d"), iPose, Err, nJoints);
            return;
        }

        Pose.layoutId = rsPose.layoutId;
        Pose.layoutVersion = rsPose.layoutVersion;
        Pose.rootPosition = FVector3f(rsPose.rootTransform.x, rsPose.rootTransform.y, rsPose.rootTransform.z);
        Pose.rootOrientation = FQuat4f(rsPose.rootTransform.rx, rsPose.rootTransform.ry, rsPose.rootTransform.rz, rsPose.rootTransform.rw);
    }

    // check the layout cache for the layout associated with this pose
    const RenderStreamLink::FSkeletalLayout* Layout = m_skeletalLayoutCache.Find(Pose.layoutId);
    if (!Layout || Layout->version != Pose.layoutVersion)
    {
        // we either haven't seen this layout before or the version expected by the pose is different to our cached version so refresh
        RenderStreamLink::FSkeletalLayout newLayout{};
        RenderStreamLink::SkeletonLayout rsLayout{};
        int nJoints;
        if (RenderStreamLink::instance().rs_getSkeletonLayout(specHash, Pose.layoutId, nullptr, &nJoints) != RenderStreamLink::RS_ERROR_SUCCESS)
        {
            UE_LOG(LogRenderStream, Error, TEXT("RenderStream failed to get layout %llu size for skeletal pose %llu."), Pose.layoutId, iPose);
            return;
        }

        newLayout.joints.SetNum(nJoints);
        rsLayout.joints = newLayout.joints.GetData();
        if (RenderStreamLink::instance().rs_getSkeletonLayout(specHash, Pose.layoutId, &rsLayout, &nJoints) != RenderStreamLink::RS_ERROR_SUCCESS)
        {
            UE_LOG(LogRenderStream, Error, TEXT("RenderStream failed to get layout %llu for skeletal pose %llu."), Pose.layoutId, iPose);
            return;
        }
        
        TArray<int> nameLengths;
        nameLengths.SetNum(nJoints);
        TArray<int*> nameLengthPtrs;
        nameLengthPtrs.SetNum(nJoints);
        for (int32 i = 0; i < nJoints; ++i)
            nameLengthPtrs[i] = &nameLengths[i];

        if (RenderStreamLink::instance().rs_getSkeletonJointNames(specHash, Pose.layoutId, nullptr, nameLengthPtrs.GetData(), &nJoints) != RenderStreamLink::RS_ERROR_SUCCESS)
        {
            UE_LOG(LogRenderStream, Error, TEXT("RenderStream failed to get joint name lengths for layout %llu and skeletal pose %llu."), Pose.layoutId, iPose);
            return;
        }
        
        TArray<std::string> jointNames;
        jointNames.SetNum(nJoints);
        for (int32 i = 0; i < nJoints; ++i)
            jointNames[i].resize(nameLengths[i]);
        TArray<const char*> jointNamesCStrings;
        jointNamesCStrings.SetNum(nJoints);
        for (int32 i = 0; i < nJoints; ++i)
            jointNamesCStrings[i] = jointNames[i].c_str();

        if (RenderStreamLink::instance().rs_getSkeletonJointNames(specHash, Pose.layoutId, jointNamesCStrings.GetData(), nullptr, &nJoints) != RenderStreamLink::RS_ERROR_SUCCESS)
        {
            UE_LOG(LogRenderStream, Error, TEXT("RenderStream failed to get joint names for layout %llu and skeletal pose %llu."), Pose.layoutId, iPose);
            return;
        }

        newLayout.jointNames.SetNum(nJoints);
        for (int32 i = 0; i < newLayout.jointNames.Num(); ++i)
            newLayout.jointNames[i] = FString(jointNames[i].c_str());
        newLayout.version = rsLayout.version;
        Layout = &m_skeletalLayoutCache.Emplace(Pose.layoutId, newLayout);
    }

    FRenderStreamModule::Get()->PushAnimDataToSource(PropKey, ParamKey, *Layout, Pose);
}
