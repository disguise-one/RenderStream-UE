#include "RenderStreamLink.h"
#include "RenderStream.h"

#include "RenderStreamSettings.h"
#include "Engine/RendererSettings.h"

#if defined WIN32 || defined WIN64
#define WINDOWS
#endif

#include <system_error>

#include "IDisplayCluster.h"
#include "Interfaces/IPluginManager.h"
#include "Windows/MinWindows.h"

#ifdef WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#include <winver.h>
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"
#pragma comment(lib, "version.lib")
#endif

namespace {
    void log_default(const char* text) {
        UE_LOG(LogRenderStream, Log, TEXT("%s"), ANSI_TO_TCHAR(text));
    }

    void log_verbose(const char* text) {
        UE_LOG(LogRenderStream, Verbose, TEXT("%s"), ANSI_TO_TCHAR(text));
    }

    void log_error(const char* text) {
        UE_LOG(LogRenderStream, Error, TEXT("%s"), ANSI_TO_TCHAR(text));
    }
}

/*static*/ const char* RenderStreamLink::ParamTypeToName(RemoteParameterType type)
{
    static const char* ParamTypeName[] = {
        "Number",
        "Image",
        "Pose",
        "Transform",
        "Text",
        "Event",
        "Skeleton",
        "Array",
    };

    static_assert(RS_PARAMETER_LAST + 1 == UE_ARRAY_COUNT(ParamTypeName), "Added a new parameter type without adding it's name!");
    return ParamTypeName[type];
}

/*static*/ RenderStreamLink::RSColourSpace RenderStreamLink::GetWorkingColourSpace()
{
    // Note that UE will always expect linear colour space
    switch (GetDefault<URendererSettings>()->WorkingColorSpaceChoice.GetValue())
    {
    case EWorkingColorSpace::Type::sRGB:
        return RenderStreamLink::RSColourSpace::RS_COLOUR_SPACE_sRGB;
    case EWorkingColorSpace::Type::Rec2020:
        return RenderStreamLink::RSColourSpace::RS_COLOUR_SPACE_Rec2020;
    case EWorkingColorSpace::Type::ACESAP0:
        return RenderStreamLink::RSColourSpace::RS_COLOUR_SPACE_ACES2065;
    case EWorkingColorSpace::Type::ACESAP1:
        return RenderStreamLink::RSColourSpace::RS_COLOUR_SPACE_ACEScg;
    case EWorkingColorSpace::Type::P3D65:
        return RenderStreamLink::RSColourSpace::RS_COLOUR_SPACE_P3D65;
    case EWorkingColorSpace::Type::P3DCI:
        return RenderStreamLink::RSColourSpace::RS_COLOUR_SPACE_P3DCI;
    default:
        return RenderStreamLink::RSColourSpace::RS_COLOUR_SPACE_UNKNOWN;
    }
}

/*static*/ RenderStreamLink& RenderStreamLink::instance()
{
    static RenderStreamLink r;
    return r;
}

RenderStreamLink::RenderStreamLink()
{
    loadExplicit();
}

RenderStreamLink::~RenderStreamLink()
{
    unloadExplicit();
}

bool RenderStreamLink::isAvailable()
{
    return m_dll && m_loaded;
}

bool RenderStreamLink::loadExplicit()
{
    if (isAvailable())
        return true;

#ifdef WINDOWS
    
    auto GetD3PathFromReg = []() -> FString
    {
        HKEY hKey;
        HRESULT hResult = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\d3 Technologies\\d3 Production Suite", 0, KEY_READ, &hKey);
        if (FAILED(hResult))
        {
            UE_LOG(LogRenderStream, Error, TEXT("Failed to open d3 production suite registry key."));
            return "";
        }

        FString valueName("exe path");
        TCHAR buffer[512];
        DWORD bufferSize = sizeof(buffer);
        hResult = RegQueryValueExW(hKey, *valueName, 0, nullptr, reinterpret_cast<LPBYTE>(buffer), &bufferSize);
        if (FAILED(hResult))
        {
            UE_LOG(LogRenderStream, Error, TEXT("Failed to query exe path registry value."));
            return "";
        }

        FString out(buffer);
        int32 index;
        out.FindLastChar('\\', index);
        if (index != -1 && index != out.Len() - 1)
            return out.Left(index + 1);
        return out;
    };

    const FString dllName("d3renderstream.dll");
    const FString exePath = GetD3PathFromReg();
    m_dllPath = exePath + dllName;
    if (!FPaths::FileExists(m_dllPath))
    {
        UE_LOG(LogRenderStream, Error, TEXT("%s not found in %s."), *dllName, *exePath);
        return false;
    }

    auto LogFatalIfNotInEditor = [](const FString& msg)
    {
        if (!GIsEditor)
            UE_LOG(LogRenderStream, Fatal, TEXT("RenderStream instance cannot launch, the app will exit to avoid other RenderStram errors during runtime. Reason: %s"), *msg);
    };

    UE_LOG(LogRenderStream, Log, TEXT("Loading RenderStream dll at %s."), *m_dllPath);
    m_dll = LoadLibraryEx(*m_dllPath, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS);
    if (m_dll == nullptr)
    {
        std::error_code e = std::error_code(GetLastError(), std::system_category());
        FString osMsg = e.message().c_str();
        UE_LOG(LogRenderStream, Error, TEXT("Failed to load %s. %s (%i)"), *m_dllPath, *osMsg, e.value());
        LogFatalIfNotInEditor("RenderStream DLL could not be loaded.");
        return false;
    }

    auto loadFn = [&](auto& fn, const auto& fnName)
    {
        fn = (std::decay_t<decltype(fn)>)FPlatformProcess::GetDllExport(m_dll, fnName);
        if (!fn)
        {
            std::error_code e = std::error_code(GetLastError(), std::system_category());
            FString osMsg = e.message().c_str();
            UE_LOG(LogRenderStream, Error, TEXT("Failed to get function %s from DLL. %s (%i)"), fnName, *osMsg, e.value());
            m_loaded = false;
            LogFatalIfNotInEditor("A function failed to load from the RenderStream DLL, this suggests an incompatible version is installed.");
            return false;
        }
        return true;
    };

#define LOAD_FN(FUNC) \
    if (!loadFn(FUNC, TEXT(#FUNC))) \
        return false;
    
    LOAD_FN(rs_initialise);
    LOAD_FN(rs_initialiseGpGpuWithDX11Device);
    LOAD_FN(rs_initialiseGpGpuWithDX12DeviceAndQueue);
    LOAD_FN(rs_initialiseGpGpuWithOpenGlContexts);
    LOAD_FN(rs_initialiseGpGpuWithVulkanDevice);
    LOAD_FN(rs_shutdown);

    LOAD_FN(rs_registerLoggingFunc);
    LOAD_FN(rs_registerErrorLoggingFunc);
    LOAD_FN(rs_registerVerboseLoggingFunc);

    LOAD_FN(rs_unregisterLoggingFunc);
    LOAD_FN(rs_unregisterErrorLoggingFunc);
    LOAD_FN(rs_unregisterVerboseLoggingFunc);

    LOAD_FN(rs_useDX12SharedHeapFlag);

    LOAD_FN(rs_setSchema);
    LOAD_FN(rs_saveSchema);
    LOAD_FN(rs_loadSchema);

    LOAD_FN(rs_getStreams);

    LOAD_FN(rs_awaitFrameData);
    LOAD_FN(rs_setFollower);
    LOAD_FN(rs_beginFollowerFrame);

    LOAD_FN(rs_getFrameParameters);
    LOAD_FN(rs_getFrameImageData);
    LOAD_FN(rs_getFrameImage2);
    LOAD_FN(rs_getFrameText);

    LOAD_FN(rs_getSkeletonLayout);
    LOAD_FN(rs_getSkeletonJointNames);
    LOAD_FN(rs_getSkeletonJointPoses);

    LOAD_FN(rs_getFrameCamera);
    LOAD_FN(rs_sendFrame2);

    LOAD_FN(rs_releaseImage2);

    LOAD_FN(rs_logToD3);
    LOAD_FN(rs_sendProfilingData);
    LOAD_FN(rs_setNewStatusMessage);

    m_loaded = true;

    rs_registerLoggingFunc(&log_default);
    rs_registerErrorLoggingFunc(&log_error);
    rs_registerVerboseLoggingFunc(&log_verbose);

#endif

    return isAvailable();
}

bool RenderStreamLink::unloadExplicit()
{
    if (m_dll == nullptr || !m_loaded)
        return true;

    if (rs_shutdown)
        rs_shutdown();
#ifdef WINDOWS
    if (m_dll)
        FreeLibrary((HMODULE)m_dll);
    m_dll = nullptr;
#endif
    m_loaded = false;
    return m_dll == nullptr;
}

bool RenderStreamLink::GetD3Version(int32& OutMajor, int32& OutMinor, int32& OutPatch) const
{
#ifdef WINDOWS
    if (m_dllPath.IsEmpty())
        return false;

    DWORD ignored = 0;
    const DWORD infoSize = GetFileVersionInfoSizeW(*m_dllPath, &ignored);
    if (infoSize == 0)
        return false;

    TArray<uint8> buffer;
    buffer.SetNumUninitialized(static_cast<int32>(infoSize));
    if (!GetFileVersionInfoW(*m_dllPath, 0, infoSize, buffer.GetData()))
        return false;

    VS_FIXEDFILEINFO* fixedInfo = nullptr;
    UINT fixedInfoLen = 0;
    if (!VerQueryValueW(buffer.GetData(), L"\\", reinterpret_cast<LPVOID*>(&fixedInfo), &fixedInfoLen) || fixedInfo == nullptr)
        return false;

    OutMajor = static_cast<int32>(HIWORD(fixedInfo->dwFileVersionMS));
    OutMinor = static_cast<int32>(LOWORD(fixedInfo->dwFileVersionMS));
    OutPatch = static_cast<int32>(HIWORD(fixedInfo->dwFileVersionLS));
    return true;
#else
    return false;
#endif
}

bool RenderStreamLink::IsSchemaGenerationSupported() const
{
    int32 major = 0, minor = 0, patch = 0;
    if (!GetD3Version(major, minor, patch))
    {
        UE_LOG(LogRenderStream, Error, TEXT("Could not determine installed d3 version; treating schema generation as unsupported."));
        return false;
    }

    return (major > MIN_D3_VERSION_MAJOR) ||
        (major == MIN_D3_VERSION_MAJOR && minor > MIN_D3_VERSION_MINOR) ||
        (major == MIN_D3_VERSION_MAJOR && minor == MIN_D3_VERSION_MINOR && patch >= MIN_D3_VERSION_PATCH);
}
