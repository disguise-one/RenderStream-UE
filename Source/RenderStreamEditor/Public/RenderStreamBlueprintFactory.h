#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "RenderStreamBlueprintFactory.generated.h"

UCLASS()
class URenderStreamBlueprintFactory : public UFactory
{
    GENERATED_BODY()

public:
    URenderStreamBlueprintFactory();

    virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
    virtual FText GetDisplayName() const override;
    virtual uint32 GetMenuCategories() const override;
    virtual FString GetDefaultNewAssetName() const override;
};
