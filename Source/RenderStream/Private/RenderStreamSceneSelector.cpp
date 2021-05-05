#include "RenderStreamSceneSelector.h"
#include "Core.h"
#include "GameFramework/Actor.h"
#include <string.h>
#include <malloc.h>
#include "RenderStream.h"

RenderStreamSceneSelector::~RenderStreamSceneSelector() = default;

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

    const static int MAX_TRIES = 3;
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
            UE_LOG(LogRenderStream, Error, TEXT("Incomptible schema"));
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


static bool validateField(FString key_, FString undecoratedSuffix, const RenderStreamLink::RemoteParameter& parameter)
{
    FString key = key_ + (undecoratedSuffix.IsEmpty() ? "" : "_" + undecoratedSuffix);

    if (key != parameter.key)
    {
        UE_LOG(LogRenderStream, Error, TEXT("Parameter mismatch - expected %s, got %s"), UTF8_TO_TCHAR(parameter.key), *key);
        return false;
    }
    return true;
}

bool RenderStreamSceneSelector::ValidateParameters(const RenderStreamLink::RemoteParameters& sceneParameters, std::initializer_list<const AActor*> Actors) const
{
    size_t offset = 0;

    for (const AActor* actor : Actors)
    {
        if (!actor)
            continue; // it's convenient at the higher level to pass nulls if there's a pattern which can miss pieces

        size_t increment = ValidateParameters(actor, sceneParameters.parameters + offset, sceneParameters.nParameters);
        if (increment == SIZE_MAX)
        {
            UE_LOG(LogRenderStream, Error, TEXT("Schema validation failed for actor '%s'"), *actor->GetName());
            return false;
        }
        offset += increment;
    }

    if (offset < sceneParameters.nParameters)
    {
        UE_LOG(LogRenderStream, Error, TEXT("Unexpected extra parameters in schema"));
        return false;
    }

    return true;
}

size_t RenderStreamSceneSelector::ValidateParameters(const AActor* Root, RenderStreamLink::RemoteParameter* const parameters, size_t numParameters) const
{
    size_t nParameters = 0;

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
            if (!validateField(Name, "", parameters[nParameters]))
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
            if (!validateField(Name, "", parameters[nParameters]))
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
            if (!validateField(Name, "", parameters[nParameters]))
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
            if (!validateField(Name, "", parameters[nParameters]))
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
                if (!validateField(Name, "x", parameters[nParameters + 0]) ||
                    !validateField(Name, "y", parameters[nParameters + 1]) ||
                    !validateField(Name, "z", parameters[nParameters + 2]))
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
                if (!validateField(Name, "r", parameters[nParameters + 0]) ||
                    !validateField(Name, "g", parameters[nParameters + 1]) ||
                    !validateField(Name, "b", parameters[nParameters + 2]) ||
                    !validateField(Name, "a", parameters[nParameters + 3]))
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
                if (!validateField(Name, "r", parameters[nParameters + 0]) ||
                    !validateField(Name, "g", parameters[nParameters + 1]) ||
                    !validateField(Name, "b", parameters[nParameters + 2]) ||
                    !validateField(Name, "a", parameters[nParameters + 3]))
                {
                    return SIZE_MAX;
                }
                nParameters += 4;
            }
            else
            {
                UE_LOG(LogRenderStream, Warning, TEXT("Unknown struct property: %s"), *Name);
            }
        }
        else
        {
            UE_LOG(LogRenderStream, Warning, TEXT("Unsupported exposed property: %s"), *Name);
        }
    }

    return nParameters;
}

void RenderStreamSceneSelector::ApplyParameters(size_t sceneId, std::initializer_list<AActor*> Actors) const
{
    check(sceneId < Schema().scenes.nScenes);
    const RenderStreamLink::RemoteParameters& params = Schema().scenes.scenes[sceneId];

    size_t nFloatParams = params.nParameters;

    std::vector<float> floatValues(nFloatParams);

    RenderStreamLink::RS_ERROR res = RenderStreamLink::instance().rs_getFrameParameters(params.hash, floatValues.data(), floatValues.size() * sizeof(float));
    if (res != RenderStreamLink::RS_ERROR_SUCCESS)
    {
        UE_LOG(LogRenderStream, Error, TEXT("Unable to get float frame parameters - %d"), res);
        return;
    }

    size_t offset = 0;

    for (AActor* actor : Actors)
    {
        if (!actor)
            continue; // it's convenient at the higher level to pass nulls if there's a pattern which can miss pieces
        offset += ApplyParameters(actor, floatValues, offset);
    }
}

size_t RenderStreamSceneSelector::ApplyParameters(AActor* Root, const std::vector<float>& parameters, const size_t offset) const
{
    size_t i = offset;
    for (TFieldIterator<FProperty> PropIt(Root->GetClass(), EFieldIteratorFlags::ExcludeSuper); PropIt; ++PropIt)
    {
        FProperty* Property = *PropIt;
        if (!Property->HasAllPropertyFlags(CPF_Edit | CPF_BlueprintVisible) || Property->HasAllPropertyFlags(CPF_DisableEditOnInstance))
        {
            continue;
        }
        else if (const FBoolProperty* BoolProperty = CastField<const FBoolProperty>(Property))
        {
            const bool v = bool(parameters.at(i));
            BoolProperty->SetPropertyValue_InContainer(Root, v);
            ++i;
        }
        else if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
        {
            const uint8 v = uint8(parameters.at(i));
            ByteProperty->SetPropertyValue_InContainer(Root, v);
            ++i;
        }
        else if (FIntProperty* IntProperty = CastField<FIntProperty>(Property))
        {
            const int32 v = int(parameters.at(i));
            IntProperty->SetPropertyValue_InContainer(Root, v);
            ++i;
        }
        else if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
        {
            const float v = parameters.at(i);
            FloatProperty->SetPropertyValue_InContainer(Root, v);
            ++i;
        }
        else if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
        {
            void* StructAddress = StructProperty->ContainerPtrToValuePtr<void>(Root);
            if (StructProperty->Struct == TBaseStructure<FVector>::Get())
            {
                FVector v(parameters.at(i), parameters.at(i + 1), parameters.at(i + 2));
                StructProperty->CopyCompleteValue(StructAddress, &v);
                i += 3;
            }
            else if (StructProperty->Struct == TBaseStructure<FColor>::Get())
            {
                FColor v(parameters.at(i) * 255, parameters.at(i + 1) * 255, parameters.at(i + 2) * 255, parameters.at(i + 3) * 255);
                StructProperty->CopyCompleteValue(StructAddress, &v);
                i += 4;
            }
            else if (StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
            {
                FLinearColor v(parameters.at(i), parameters.at(i + 1), parameters.at(i + 2), parameters.at(i + 3));
                StructProperty->CopyCompleteValue(StructAddress, &v);
                i += 4;
            }
        }
    }
    return i;
}
