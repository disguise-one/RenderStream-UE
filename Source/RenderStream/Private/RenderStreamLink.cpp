#include "RenderStreamLink.h"
#include "RenderStream.h"

#include "RenderStreamSettings.h"

#if defined WIN32 || defined WIN64
#define WINDOWS
#endif

#include <system_error>

#include "Interfaces/IPluginManager.h"
#include "Windows/MinWindows.h"

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

const char* RenderStreamLink::ParamTypeToName(RemoteParameterType type)
{
    static const char* ParamTypeName[] = {
        "Number",
        "Image",
        "Pose",
        "Transform",
        "Text",
    };

    static_assert(RS_PARAMETER_LAST + 1 == UE_ARRAY_COUNT(ParamTypeName), "Added a new parameter type without adding it's name!");
    return ParamTypeName[type];
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

    bool DevEnv = false;

    /* hardcoded denenv.json file that contains the path that the dll can be found in
     * place this devenv.json file in the plugin root directory
     * eg.
     * {
     *     "dllpath": "<d3sourcedir>/fbuild/<config>/d3/build/msvc"
     * }
     */
    auto GetDevD3Path = [&DevEnv]() -> FString
    {
        FString DevEnvJson = IPluginManager::Get().FindPlugin(RS_PLUGIN_NAME)->GetBaseDir() + "/devenv.json";
        if (FPaths::FileExists(DevEnvJson))
        {
            DevEnv = true;
            FString JsonString;
            FFileHelper::LoadFileToString(JsonString, *DevEnvJson);
            TSharedPtr<FJsonObject> JsonObj;
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
            if (FJsonSerializer::Deserialize(Reader, JsonObj))
            {
                if (JsonObj->HasField(TEXT("dllpath")))
                {
                    FString path = JsonObj->GetStringField(TEXT("dllpath"));
                    if (!path.EndsWith("/") || !path.EndsWith("\\"))
                        path += "/";

                    FPaths::MakePlatformFilename(path);
                    return path;
                }
            }
        }
        return "";
    };

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
        return FString(buffer);
    };

    auto SanitizePath = [](const FString& exePath)
    {
        int32 index;
        exePath.FindLastChar('\\', index);
        if (index != -1 && index != exePath.Len() - 1)
            return exePath.Left(index + 1);
        return exePath;
    };

    FString dllName("d3renderstream.dll");
    FString exePath = SanitizePath(GetDevD3Path());

    if (!FPaths::FileExists(exePath + dllName))
    {
        if (DevEnv) // attempted dev environment, log the failure
            UE_LOG(LogRenderStream, Error, TEXT("devenv.json existed but %s not found in %s."), *dllName, *exePath);

        // revert to registry
        exePath = SanitizePath(GetD3PathFromReg());
        if (!FPaths::FileExists(exePath + dllName))
        {
            UE_LOG(LogRenderStream, Error, TEXT("%s not found in %s."), *dllName, *exePath);
            return false;
        }
    }

    m_dll = LoadLibraryEx(*(exePath + dllName), NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS);
    if (m_dll == nullptr)
    {
        std::error_code e = std::error_code(GetLastError(), std::system_category());
        FString osMsg = e.message().c_str();
        UE_LOG(LogRenderStream, Error, TEXT("Failed to load %s. %s (%i)"), *(exePath + dllName), *osMsg, e.value());
        return false;
    }

#define LOAD_FN(FUNC_NAME) \
    FUNC_NAME = (FUNC_NAME ## Fn*)FPlatformProcess::GetDllExport(m_dll, TEXT(#FUNC_NAME)); \
    if (!FUNC_NAME) { \
        UE_LOG(LogRenderStream, Error, TEXT("Failed to get function " #FUNC_NAME " from DLL.")); \
        m_loaded = false; \
        return false; \
    }

    LOAD_FN(rs_initialise);
    LOAD_FN(rs_initialiseGpGpuWithDX11Device);
    LOAD_FN(rs_initialiseGpGpuWithDX12DeviceAndQueue);
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
    LOAD_FN(rs_getFrameImage);
    LOAD_FN(rs_getFrameText);

    LOAD_FN(rs_getFrameCamera);
    LOAD_FN(rs_sendFrame);

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
    if (rs_shutdown)
        rs_shutdown();
#ifdef WINDOWS
    if (m_dll)
        FreeLibrary((HMODULE)m_dll);
    m_dll = nullptr;
#endif
    return m_dll == nullptr;
}
