#pragma once

#include "Engine/Console.h" 
#include "RenderStreamLink.h"

class FRenderStreamLogOutputDevice : public FOutputDevice
{
public:
    FRenderStreamLogOutputDevice()
    {
        check(GLog);
        GLog->AddOutputDevice(this);
    }

    ~FRenderStreamLogOutputDevice()
    {
        if (GLog != nullptr)
        {
            GLog->RemoveOutputDevice(this);
        }
    }

protected:
    void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const class FName& Category) override
    {
        if (RenderStreamLink::instance().isAvailable())
        {
            RenderStreamLink::instance().rs_logToD3(TCHAR_TO_ANSI(*Category.ToString()));
            RenderStreamLink::instance().rs_logToD3(": ");
            RenderStreamLink::instance().rs_logToD3(TCHAR_TO_ANSI(Message));
            RenderStreamLink::instance().rs_logToD3("\n");
        }
    }

private:

};