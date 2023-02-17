#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <memory>

#include "GeneralProjectSettings.h"
#include "Runtime/Launch/Resources/Version.h"

struct ID3D11Device;
struct ID3D12Device;
struct ID3D12CommandQueue;
typedef struct VkDevice_T* VkDevice;
// Forward declare Windows compatible handles.
#define D3_DECLARE_HANDLE(name) \
  struct name##__;                  \
  typedef struct name##__* name
D3_DECLARE_HANDLE(HGLRC);
D3_DECLARE_HANDLE(HDC);
struct ID3D11Resource;
struct ID3D12Resource;
struct ID3D12Fence;
typedef unsigned int GLuint;
typedef struct VkDeviceMemory_T* VkDeviceMemory;
typedef uint64_t VkDeviceSize;
typedef struct VkSemaphore_T* VkSemaphore;

#define RS_PLUGIN_NAME "RenderStream-UE"

class RenderStreamLink
{
public:
    enum RSPixelFormat : uint32_t
    {
        RS_FMT_INVALID,

        RS_FMT_BGRA8,
        RS_FMT_BGRX8,

        RS_FMT_RGBA32F,

        RS_FMT_RGBA16,

        RS_FMT_RGBA8,
        RS_FMT_RGBX8,
    };

    enum RS_ERROR
    {
        RS_ERROR_SUCCESS = 0,

        // Core is not initialised
        RS_NOT_INITIALISED,

        // Core is already initialised
        RS_ERROR_ALREADYINITIALISED,

        // Given handle is invalid
        RS_ERROR_INVALIDHANDLE,

        // Maximum number of frame senders have been created
        RS_MAXSENDERSREACHED,

        RS_ERROR_BADSTREAMTYPE,

        RS_ERROR_NOTFOUND,

        RS_ERROR_INCORRECTSCHEMA,

        RS_ERROR_INVALID_PARAMETERS,

        RS_ERROR_BUFFER_OVERFLOW,

        RS_ERROR_TIMEOUT,

        RS_ERROR_STREAMS_CHANGED,

        RS_ERROR_INCOMPATIBLE_VERSION,

        RS_ERROR_FAILED_TO_GET_DXDEVICE_FROM_RESOURCE,

        RS_ERROR_FAILED_TO_INITIALISE_GPGPU,

        RS_ERROR_QUIT,

        RS_ERROR_UNSPECIFIED
    };

    // Bitmask flags
    enum FRAMEDATA_FLAGS
    {
        FRAMEDATA_NO_FLAGS = 0,
        FRAMEDATA_RESET = 1
    };

    enum REMOTEPARAMETER_FLAGS
    {
        REMOTEPARAMETER_NO_FLAGS = 0,
        REMOTEPARAMETER_NO_SEQUENCE = 1,
        REMOTEPARAMETER_READ_ONLY = 2
    };

    enum SenderFrameType
    {
        RS_FRAMETYPE_HOST_MEMORY,
        RS_FRAMETYPE_DX11_TEXTURE,
        RS_FRAMETYPE_DX12_TEXTURE,
        RS_FRAMETYPE_OPENGL_TEXTURE,
        RS_FRAMETYPE_VULKAN_TEXTURE,
        RS_FRAMETYPE_UNKNOWN
    };

    typedef uint64_t StreamHandle;
    typedef uint64_t CameraHandle;
    typedef void (*logger_t)(const char*);

#pragma pack(push, 4)
    typedef struct
    {
        uint8_t virtualReprojectionRequired;
    } D3TrackingData;  // Tracking data required by d3 but not used to render content

    typedef struct
    {
        StreamHandle id;
        CameraHandle cameraHandle;
        float x, y, z;
        float rx, ry, rz;
        float focalLength;
        float sensorX, sensorY;
        float cx, cy;
        float nearZ, farZ;
        float orthoWidth;  // If > 0, an orthographic camera should be used
        float aperture; // Apply if > 0
        float focusDistance;  // Apply if > 0
        D3TrackingData d3Tracking;
    } CameraData;

    typedef struct
    {
        double tTracked;
        double localTime;
        double localTimeDelta;
        unsigned int frameRateNumerator;
        unsigned int frameRateDenominator;
        uint32_t flags; // FRAMEDATA_FLAGS
        uint32_t scene;
    } FrameData;

    typedef struct
    {
        double tTracked;
        CameraData camera;
    } CameraResponseData;


    typedef struct
    {
        uint8_t* data;
        uint32_t stride;
        RSPixelFormat format;
    } HostMemoryData;

    typedef struct
    {
        ID3D11Resource* resource;
    } Dx11Data;

    typedef struct
    {
        ID3D12Resource* resource;
    } Dx12Data;

    typedef struct
    {
        GLuint texture;
    } OpenGlData;

    typedef struct
    {
        VkDeviceMemory memory;
        VkDeviceSize size;
        RSPixelFormat format;
        uint32_t width;
        uint32_t height;
        VkSemaphore waitSemaphore;
        uint64_t waitSemaphoreValue;
        VkSemaphore signalSemaphore;
        uint64_t signalSemaphoreValue;
    } VulkanData;

    typedef struct
    {
        SenderFrameType type;
        union
        {
            HostMemoryData cpu;
            Dx11Data dx11;
            Dx12Data dx12;
            OpenGlData gl;
            VulkanData vk;
        };
    } SenderFrame;

    typedef struct
    {
        uint32_t xOffset;
        uint32_t yOffset;
        uint32_t width;
        uint32_t height;
    } FrameRegion;

    // Normalised (0-1) clipping planes for the edges of the camera frustum, to be used to perform off-axis perspective projection, or
    // to offset and scale 2D orthographic matrices.
    typedef struct
    {
        float left;
        float right;
        float top;
        float bottom;
    } ProjectionClipping;

    typedef struct
    {
        StreamHandle handle;
        const char* channel;
        uint64_t mappingId;
        int32_t iViewpoint;
        const char* name;
        uint32_t width;
        uint32_t height;
        RSPixelFormat format;
        ProjectionClipping clipping;
    } StreamDescription;

    typedef struct
    {
        uint32_t nStreams;
        StreamDescription* streams;
    } StreamDescriptions;

    enum RemoteParameterType
    {
        RS_PARAMETER_NUMBER=0,
        RS_PARAMETER_IMAGE,
        RS_PARAMETER_POSE,      // 4x4 TR matrix
        RS_PARAMETER_TRANSFORM, // 4x4 TRS matrix
        RS_PARAMETER_TEXT,
        RS_PARAMETER_EVENT,
        RS_PARAMETER_LAST= RS_PARAMETER_EVENT
    };

    enum RemoteParameterDmxType
    {
        RS_DMX_DEFAULT,
        RS_DMX_8,
        RS_DMX_16_BE,
    };

    static const char* ParamTypeToName(RemoteParameterType type);

    typedef struct
    {
        float min;
        float max;
        float step;
        float defaultValue;
    } NumericalDefaults;

    typedef struct
    {
        const char* defaultValue;
    } TextDefaults;

    typedef union
    {
        NumericalDefaults number;
        TextDefaults text;
    } RemoteParameterTypeDefaults;

    typedef struct
    {
        uint32_t width, height;
        RSPixelFormat format;
        int64_t imageId;
    } ImageFrameData;

    typedef struct
    {
        const char* group;
        const char* displayName;
        const char* key;
        RemoteParameterType type;
        RemoteParameterTypeDefaults defaults;
        uint32_t nOptions;
        const char** options;

        int32_t dmxOffset; // DMX channel offset or auto (-1)
        RemoteParameterDmxType dmxType;
        uint32_t flags; // REMOTEPARAMETER_FLAGS
    } RemoteParameter;

    typedef struct
    {
        const char* name;
        uint32_t nParameters;
        RemoteParameter* parameters;
        uint64_t hash;
    } RemoteParameters;

    typedef struct
    {
        uint32_t nScenes;
        RemoteParameters* scenes;
    } Scenes;

    typedef struct
    {
        uint32_t nChannels;
        const char** channels;
    } Channels;

    typedef struct
    {
        const char* engineName;
        const char* engineVersion;
        const char* info;
        Channels channels;
        Scenes scenes;
    } Schema;

    typedef struct
    {
        const char* name;
        float value;
    } ProfilingEntry;

#pragma pack(pop)

#define RENDER_STREAM_VERSION_MAJOR 2
#define RENDER_STREAM_VERSION_MINOR 0

    enum UseDX12SharedHeapFlag
    {
        RS_DX12_USE_SHARED_HEAP_FLAG,
        RS_DX12_DO_NOT_USE_SHARED_HEAP_FLAG
    };

    typedef struct
    {
        const CameraResponseData* cameraData;
        uint64_t schemaHash;
        uint32_t parameterDataSize;
        void* parameterData;
        uint32_t textDataCount;
        const char** textData;
    } FrameResponseData;

    RENDERSTREAM_API static RenderStreamLink& instance();

private:
    RenderStreamLink();
    ~RenderStreamLink();

private:
    typedef void (*logger_t)(const char*);

    typedef void rs_registerLoggingFuncFn(logger_t);
    typedef void rs_registerErrorLoggingFuncFn(logger_t);
    typedef void rs_registerVerboseLoggingFuncFn(logger_t);

    typedef void rs_unregisterLoggingFuncFn();
    typedef void rs_unregisterErrorLoggingFuncFn();
    typedef void rs_unregisterVerboseLoggingFuncFn();

    typedef RS_ERROR rs_initialiseFn(int expectedVersionMajor, int expectedVersionMinor);
    typedef RS_ERROR rs_initialiseGpGpuWithDX11DeviceFn(ID3D11Device* device);
    typedef RS_ERROR rs_initialiseGpGpuWithDX12DeviceAndQueueFn(ID3D12Device* device, ID3D12CommandQueue* queue);
    typedef RS_ERROR rs_initialiseGpGpuWithOpenGlContextsFn(HGLRC glContext, HDC deviceContext);
    typedef RS_ERROR rs_initialiseGpGpuWithVulkanDeviceFn(VkDevice device);

    typedef RS_ERROR rs_shutdownFn();
    // non-isolated functions, these require init prior to use
    typedef RS_ERROR rs_useDX12SharedHeapFlagFn(UseDX12SharedHeapFlag* flag);
    typedef RS_ERROR rs_saveSchemaFn(const char* assetPath, Schema* schema); // Save schema for project file/custom executable at (assetPath)
    typedef RS_ERROR rs_loadSchemaFn(const char* assetPath, /*Out*/Schema* schema, /*InOut*/uint32_t* nBytes); // Load schema for project file/custom executable at (assetPath) into a buffer of size (nBytes) starting at (schema)
    // workload functions, these require the process to be running inside d3's asset launcher environment
    typedef RS_ERROR rs_setSchemaFn(/*InOut*/Schema* schema); // Set schema and fill in per-scene hash for use with rs_getFrameParameters
    typedef RS_ERROR rs_getStreamsFn(/*Out*/StreamDescriptions* streams, /*InOut*/uint32_t* nBytes); // Populate streams into a buffer of size (nBytes) starting at (streams)

    typedef RS_ERROR rs_awaitFrameDataFn(int timeoutMs, /*Out*/FrameData* data); // waits for any asset, any stream to request a frame, provides the parameters for that frame.
    typedef RS_ERROR rs_setFollowerFn(int isFollower); // Used to mark this node as relying on alternative mechanisms to distribute FrameData. Users must provide correct CameraResponseData to sendFrame, and call rs_beginFollowerFrame at the start of the frame, where awaitFrame would normally be called.
    typedef RS_ERROR rs_beginFollowerFrameFn(double tTracked); // Pass the engine-distributed tTracked value in, if you have called rs_setFollower(1) otherwise do not call this function.

    typedef RS_ERROR rs_getFrameParametersFn(uint64_t schemaHash, /*Out*/void* outParameterData, uint64_t outParameterDataSize);  // returns the remote parameters for this frame.
    typedef RS_ERROR rs_getFrameImageDataFn(uint64_t schemaHash, /*Out*/ImageFrameData* outParameterData, uint64_t outParameterDataCount);  // returns the remote image data for this frame.
    typedef RS_ERROR rs_getFrameImageFn(int64_t imageId, /*InOut*/const SenderFrame* data); // fills in (data) with the remote image
    typedef RS_ERROR rs_getFrameTextFn(uint64_t schemaHash, uint32_t textParamIndex, /*Out*/const char** outTextPtr); // // returns the remote text data (pointer only valid until next rs_awaitFrameData)

    typedef RS_ERROR rs_getFrameCameraFn(StreamHandle streamHandle, /*Out*/CameraData* outCameraData);  // returns the CameraData for this stream, or RS_ERROR_NOTFOUND if no camera data is available for this stream on this frame
    typedef RS_ERROR rs_sendFrameFn(StreamHandle streamHandle, const SenderFrame* data, const void* frameData); // publish a frame buffer which was generated from the associated tracking and timing information.

    typedef RS_ERROR rs_releaseImageFn(const SenderFrame* image); // release any references to image (e.g. before deletion)

    typedef RS_ERROR rs_logToD3Fn(const char * str);
    typedef RS_ERROR rs_sendProfilingDataFn(ProfilingEntry* entries, int count);
    typedef RS_ERROR rs_setNewStatusMessageFn(const char* msg);

public:
    RENDERSTREAM_API bool isAvailable();

    bool loadExplicit();
    bool unloadExplicit();

    struct ScopedSchema
    {
        ScopedSchema()
        {
            reset();
        }
        ~ScopedSchema()
        {
            free(const_cast<char*>(schema.engineName));
            free(const_cast<char*>(schema.engineVersion));
            free(const_cast<char*>(schema.info));
            for (size_t i = 0; i < schema.channels.nChannels; ++i)
                free(const_cast<char*>(schema.channels.channels[i]));
            free(schema.channels.channels);
            for (size_t i = 0; i < schema.scenes.nScenes; ++i)
            {
                RemoteParameters& scene = schema.scenes.scenes[i];
                free(const_cast<char*>(scene.name));
                for (size_t j = 0; j < scene.nParameters; ++j)
                {
                    RemoteParameter& parameter = scene.parameters[j];
                    free(const_cast<char*>(parameter.group));
                    free(const_cast<char*>(parameter.displayName));
                    free(const_cast<char*>(parameter.key));
                    if (parameter.type == RS_PARAMETER_TEXT)
                        free(const_cast<char*>(parameter.defaults.text.defaultValue));
                    for (size_t k = 0; k < parameter.nOptions; ++k)
                    {
                        free(const_cast<char*>(parameter.options[k]));
                    }
                    free(parameter.options);
                }
                free(scene.parameters);
            }
            free(schema.scenes.scenes);
        }
        void reset()
        {
            schema.engineName = _strdup(EPIC_PRODUCT_NAME);
            schema.engineVersion = _strdup(TCHAR_TO_UTF8(ENGINE_VERSION_STRING));
            schema.info = _strdup(TCHAR_TO_UTF8(*GetDefault<UGeneralProjectSettings>()->Description));
            schema.channels.nChannels = 0;
            schema.channels.channels = nullptr;
            schema.scenes.nScenes = 0;
            schema.scenes.scenes = nullptr;
        }
        ScopedSchema(const ScopedSchema&) = delete;
        ScopedSchema(ScopedSchema&& other)
        {
            schema = std::move(other.schema);
            other.reset();
        }
        ScopedSchema& operator=(const ScopedSchema&) = delete;
        ScopedSchema& operator=(ScopedSchema&& other)
        {
            schema = std::move(other.schema);
            other.reset();
            return *this;
        }

        Schema schema;
    };

public: // d3renderstream.h API, but loaded dynamically.
    rs_registerLoggingFuncFn* rs_registerLoggingFunc = nullptr;
    rs_registerErrorLoggingFuncFn* rs_registerErrorLoggingFunc = nullptr;
    rs_registerVerboseLoggingFuncFn* rs_registerVerboseLoggingFunc = nullptr;

    rs_unregisterLoggingFuncFn* rs_unregisterLoggingFunc = nullptr;
    rs_unregisterErrorLoggingFuncFn* rs_unregisterErrorLoggingFunc = nullptr;
    rs_unregisterVerboseLoggingFuncFn* rs_unregisterVerboseLoggingFunc = nullptr;

    rs_initialiseFn* rs_initialise = nullptr;
    rs_initialiseGpGpuWithDX11DeviceFn* rs_initialiseGpGpuWithDX11Device = nullptr;
    rs_initialiseGpGpuWithDX12DeviceAndQueueFn* rs_initialiseGpGpuWithDX12DeviceAndQueue = nullptr;
    rs_initialiseGpGpuWithOpenGlContextsFn* rs_initialiseGpGpuWithOpenGlContexts = nullptr;
    rs_initialiseGpGpuWithVulkanDeviceFn* rs_initialiseGpGpuWithVulkanDevice = nullptr;
    rs_useDX12SharedHeapFlagFn* rs_useDX12SharedHeapFlag = nullptr;
    rs_setSchemaFn* rs_setSchema = nullptr;
    rs_saveSchemaFn* rs_saveSchema = nullptr;
    rs_loadSchemaFn* rs_loadSchema = nullptr;
    rs_shutdownFn* rs_shutdown = nullptr;
    rs_getStreamsFn* rs_getStreams = nullptr;
    rs_awaitFrameDataFn* rs_awaitFrameData = nullptr;
    rs_setFollowerFn* rs_setFollower = nullptr;
    rs_beginFollowerFrameFn* rs_beginFollowerFrame = nullptr;
    rs_getFrameParametersFn* rs_getFrameParameters = nullptr;
    rs_getFrameImageDataFn* rs_getFrameImageData = nullptr;
    rs_getFrameImageFn* rs_getFrameImage2 = nullptr;
    rs_getFrameTextFn* rs_getFrameText = nullptr;
    rs_getFrameCameraFn* rs_getFrameCamera = nullptr;
    rs_sendFrameFn* rs_sendFrame2 = nullptr;
    rs_releaseImageFn* rs_releaseImage2 = nullptr;
    rs_logToD3Fn* rs_logToD3 = nullptr;
    rs_sendProfilingDataFn* rs_sendProfilingData = nullptr;
    rs_setNewStatusMessageFn* rs_setNewStatusMessage = nullptr;

private:
    bool m_loaded = false;
    void* m_dll = nullptr;
};

//template<typename Fn>
//bool RenderStreamLink::LoadFunc(Fn*& func, const FString& name)
