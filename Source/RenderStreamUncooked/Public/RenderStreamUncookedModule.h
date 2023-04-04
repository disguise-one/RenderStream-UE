#pragma once

#include "Core.h"
#include "Modules/ModuleInterface.h"

DECLARE_LOG_CATEGORY_EXTERN(LogRenderStreamUncooked, Log, All);

class FRenderStreamUncookedModule : public IModuleInterface
{
public:
    //~ IModuleInterface interface
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    
};
