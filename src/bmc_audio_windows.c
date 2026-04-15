#include "bmc_audio_windows.h"

// WASAPI headers
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <process.h>

#pragma comment(lib, "ole32.lib")

// Manually define GUIDs that the linker needs.
// initguid.h doesn't work reliably when Flutter's build system
// compiles .c files via CXX toolchain.
#ifdef __cplusplus
#define GUID_SECT
#else
#define GUID_SECT
#endif

// {BCDE0395-E52F-467C-8E3D-C4579291692E}
static const GUID MY_CLSID_MMDeviceEnumerator = {
    0xBCDE0395, 0xE52F, 0x467C,
    {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}
};

// {A95664D2-9614-4F35-A746-DE8DB63617E6}
static const GUID MY_IID_IMMDeviceEnumerator = {
    0xA95664D2, 0x9614, 0x4F35,
    {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}
};

// {1CB9AD4C-DBFA-4c32-B178-C2F568A703B2}
static const GUID MY_IID_IAudioClient = {
    0x1CB9AD4C, 0xDBFA, 0x4c32,
    {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}
};

// {F294ACFC-3146-4483-A7BF-ADDCA7C260E2}
static const GUID MY_IID_IAudioRenderClient = {
    0xF294ACFC, 0x3146, 0x4483,
    {0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2}
};

#define MAX_FRAME 4096
#define REFTIMES_PER_SEC 10000000
#define REFTIMES_PER_MILLISEC 10000

// ============================================================================
// PCM Queue (shared between old waveOut path and new WASAPI path)
// ============================================================================

typedef struct PCMNode {
    uint8_t* data;
    int length;
    struct PCMNode* next;
} PCMNode;

typedef struct PlaybackParams {
    int sampleRate;
    int channels;
    int numberPrepareHeader;
    int deviceIndex;
} PlaybackParams;

static PCMNode* queueHead = NULL;
static PCMNode* queueTail = NULL;

static HANDLE queueMutex = NULL;
static HANDLE queueEvent = NULL;
static int running = 0;
static volatile int pcmFinished = 0;
static HANDLE playbackThreadHandle = NULL;

#define LOG(fmt, ...) { \
    char buf[512]; \
    sprintf_s(buf, sizeof(buf), fmt, __VA_ARGS__); \
    OutputDebugStringA(buf); \
}

// ============================================================================
// Queue operations
// ============================================================================

static void enqueue_pcm(const uint8_t* data, int length) {
    if (!data || length <= 0) return;

    PCMNode* node = (PCMNode*)malloc(sizeof(PCMNode));
    if (!node) return;
    node->data = (uint8_t*)malloc(length);
    if (!node->data) { free(node); return; }
    memcpy(node->data, data, length);
    node->length = length;
    node->next = NULL;

    WaitForSingleObject(queueMutex, INFINITE);
    if (queueTail) {
        queueTail->next = node;
        queueTail = node;
    } else {
        queueHead = queueTail = node;
    }
    SetEvent(queueEvent);
    ReleaseMutex(queueMutex);
}

static PCMNode* dequeue_pcm() {
    WaitForSingleObject(queueMutex, INFINITE);
    PCMNode* node = queueHead;
    if (node) {
        queueHead = node->next;
        if (!queueHead) queueTail = NULL;
    }
    ReleaseMutex(queueMutex);
    return node;
}

static void cleanup_queue() {
    WaitForSingleObject(queueMutex, INFINITE);
    while (queueHead) {
        PCMNode* temp = queueHead;
        queueHead = queueHead->next;
        free(temp->data);
        free(temp);
    }
    queueHead = NULL;
    queueTail = NULL;
    ReleaseMutex(queueMutex);
}

static void free_pcm_node(PCMNode* node) {
    if (node) {
        free(node->data);
        free(node);
    }
}

// ============================================================================
// WASAPI Device Enumeration (COM-based)
// ============================================================================

// Internal: Get IMMDeviceEnumerator. Caller must Release() and CoUninitialize().
static HRESULT get_enumerator(IMMDeviceEnumerator** ppEnum) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return hr;

    hr = CoCreateInstance(
        &MY_CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
        &MY_IID_IMMDeviceEnumerator, (void**)ppEnum);
    return hr;
}

// Internal: count devices of given data flow
static int count_devices(EDataFlow flow) {
    IMMDeviceEnumerator* pEnum = NULL;
    IMMDeviceCollection* pColl = NULL;
    UINT count = 0;

    if (FAILED(get_enumerator(&pEnum))) return 0;

    HRESULT hr = pEnum->lpVtbl->EnumAudioEndpoints(pEnum, flow, DEVICE_STATE_ACTIVE, &pColl);
    if (SUCCEEDED(hr)) {
        pColl->lpVtbl->GetCount(pColl, &count);
        pColl->lpVtbl->Release(pColl);
    }
    pEnum->lpVtbl->Release(pEnum);
    CoUninitialize();
    return (int)count;
}

// Internal: get device property string
static BOOL get_device_prop(EDataFlow flow, int index, const PROPERTYKEY* pkey, char* buf, int bufSize) {
    IMMDeviceEnumerator* pEnum = NULL;
    IMMDeviceCollection* pColl = NULL;
    IMMDevice* pDev = NULL;
    IPropertyStore* pProps = NULL;
    BOOL ok = FALSE;

    buf[0] = '\0';
    if (FAILED(get_enumerator(&pEnum))) return FALSE;

    HRESULT hr = pEnum->lpVtbl->EnumAudioEndpoints(pEnum, flow, DEVICE_STATE_ACTIVE, &pColl);
    if (FAILED(hr)) goto cleanup;

    hr = pColl->lpVtbl->Item(pColl, (UINT)index, &pDev);
    if (FAILED(hr)) goto cleanup;

    hr = pDev->lpVtbl->OpenPropertyStore(pDev, STGM_READ, &pProps);
    if (FAILED(hr)) goto cleanup;

    PROPVARIANT varName;
    PropVariantInit(&varName);
    hr = pProps->lpVtbl->GetValue(pProps, pkey, &varName);
    if (SUCCEEDED(hr) && varName.vt == VT_LPWSTR && varName.pwszVal) {
        WideCharToMultiByte(CP_UTF8, 0, varName.pwszVal, -1, buf, bufSize, NULL, NULL);
        ok = TRUE;
    }
    PropVariantClear(&varName);

cleanup:
    if (pProps) pProps->lpVtbl->Release(pProps);
    if (pDev) pDev->lpVtbl->Release(pDev);
    if (pColl) pColl->lpVtbl->Release(pColl);
    if (pEnum) pEnum->lpVtbl->Release(pEnum);
    CoUninitialize();
    return ok;
}

// Internal: get device ID string
static BOOL get_device_id_string(EDataFlow flow, int index, char* buf, int bufSize) {
    IMMDeviceEnumerator* pEnum = NULL;
    IMMDeviceCollection* pColl = NULL;
    IMMDevice* pDev = NULL;
    BOOL ok = FALSE;
    LPWSTR pwszId = NULL;

    buf[0] = '\0';
    if (FAILED(get_enumerator(&pEnum))) return FALSE;

    HRESULT hr = pEnum->lpVtbl->EnumAudioEndpoints(pEnum, flow, DEVICE_STATE_ACTIVE, &pColl);
    if (FAILED(hr)) goto cleanup;

    hr = pColl->lpVtbl->Item(pColl, (UINT)index, &pDev);
    if (FAILED(hr)) goto cleanup;

    hr = pDev->lpVtbl->GetId(pDev, &pwszId);
    if (SUCCEEDED(hr) && pwszId) {
        WideCharToMultiByte(CP_UTF8, 0, pwszId, -1, buf, bufSize, NULL, NULL);
        CoTaskMemFree(pwszId);
        ok = TRUE;
    }

cleanup:
    if (pDev) pDev->lpVtbl->Release(pDev);
    if (pColl) pColl->lpVtbl->Release(pColl);
    if (pEnum) pEnum->lpVtbl->Release(pEnum);
    CoUninitialize();
    return ok;
}

// ============================================================================
// FFI Exports: Device Enumeration
// ============================================================================

FFI_PLUGIN_EXPORT
int getOutputDeviceCount(void) {
    return count_devices(eRender);
}

static char s_nameBuf[512];

FFI_PLUGIN_EXPORT
const char* getOutputDeviceName(int index) {
    get_device_prop(eRender, index, &PKEY_Device_FriendlyName, s_nameBuf, sizeof(s_nameBuf));
    return s_nameBuf;
}

static char s_idBuf[512];

FFI_PLUGIN_EXPORT
const char* getOutputDeviceId(int index) {
    get_device_id_string(eRender, index, s_idBuf, sizeof(s_idBuf));
    return s_idBuf;
}

FFI_PLUGIN_EXPORT
int getInputDeviceCount(void) {
    return count_devices(eCapture);
}

FFI_PLUGIN_EXPORT
const char* getInputDeviceName(int index) {
    get_device_prop(eCapture, index, &PKEY_Device_FriendlyName, s_nameBuf, sizeof(s_nameBuf));
    return s_nameBuf;
}

FFI_PLUGIN_EXPORT
const char* getInputDeviceId(int index) {
    get_device_id_string(eCapture, index, s_idBuf, sizeof(s_idBuf));
    return s_idBuf;
}

// ============================================================================
// WASAPI Playback Thread
// ============================================================================

// Internal: get IMMDevice for a specific index, or default if index == -1
static HRESULT get_render_device(int deviceIndex, IMMDevice** ppDevice) {
    IMMDeviceEnumerator* pEnum = NULL;
    HRESULT hr = get_enumerator(&pEnum);
    if (FAILED(hr)) return hr;

    if (deviceIndex < 0) {
        hr = pEnum->lpVtbl->GetDefaultAudioEndpoint(pEnum, eRender, eConsole, ppDevice);
    } else {
        IMMDeviceCollection* pColl = NULL;
        hr = pEnum->lpVtbl->EnumAudioEndpoints(pEnum, eRender, DEVICE_STATE_ACTIVE, &pColl);
        if (SUCCEEDED(hr)) {
            hr = pColl->lpVtbl->Item(pColl, (UINT)deviceIndex, ppDevice);
            pColl->lpVtbl->Release(pColl);
        }
    }
    pEnum->lpVtbl->Release(pEnum);
    // Note: don't CoUninitialize here, caller's thread owns COM
    return hr;
}

static unsigned __stdcall wasapi_playback_thread(void* arg) {
    PlaybackParams* params = (PlaybackParams*)arg;
    int sampleRate = params->sampleRate;
    int channels = params->channels;
    int deviceIndex = params->deviceIndex;
    free(params);

    HRESULT hr;
    IMMDevice* pDevice = NULL;
    IAudioClient* pAudioClient = NULL;
    IAudioRenderClient* pRenderClient = NULL;
    WAVEFORMATEX* pwfx = NULL;
    UINT32 bufferFrameCount = 0;
    HANDLE hEvent = NULL;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        LOG("[bmc_audio_windows] CoInitializeEx failed: 0x%08X\n", hr);
        running = 0;
        return 1;
    }

    // Get device
    hr = get_render_device(deviceIndex, &pDevice);
    if (FAILED(hr)) {
        LOG("[bmc_audio_windows] get_render_device failed: 0x%08X\n", hr);
        goto exit;
    }

    // Activate audio client
    hr = pDevice->lpVtbl->Activate(pDevice, &MY_IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&pAudioClient);
    if (FAILED(hr)) {
        LOG("[bmc_audio_windows] Activate IAudioClient failed: 0x%08X\n", hr);
        goto exit;
    }

    // Set up WAVEFORMATEX for our PCM format
    WAVEFORMATEX wfx = {0};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = (WORD)channels;
    wfx.nSamplesPerSec = (DWORD)sampleRate;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize = 0;

    // Initialize audio client in shared mode with event-driven buffering
    REFERENCE_TIME hnsRequestedDuration = 30 * REFTIMES_PER_MILLISEC; // 30ms buffer
    hr = pAudioClient->lpVtbl->Initialize(pAudioClient,
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
        hnsRequestedDuration, 0, &wfx, NULL);

    if (FAILED(hr)) {
        LOG("[bmc_audio_windows] IAudioClient::Initialize failed: 0x%08X\n", hr);
        goto exit;
    }

    // Create event for buffer-ready notification
    hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    hr = pAudioClient->lpVtbl->SetEventHandle(pAudioClient, hEvent);
    if (FAILED(hr)) {
        LOG("[bmc_audio_windows] SetEventHandle failed: 0x%08X\n", hr);
        goto exit;
    }

    // Get buffer size
    hr = pAudioClient->lpVtbl->GetBufferSize(pAudioClient, &bufferFrameCount);
    if (FAILED(hr)) {
        LOG("[bmc_audio_windows] GetBufferSize failed: 0x%08X\n", hr);
        goto exit;
    }

    // Get render client
    hr = pAudioClient->lpVtbl->GetService(pAudioClient, &MY_IID_IAudioRenderClient, (void**)&pRenderClient);
    if (FAILED(hr)) {
        LOG("[bmc_audio_windows] GetService IAudioRenderClient failed: 0x%08X\n", hr);
        goto exit;
    }

    LOG("[bmc_audio_windows] WASAPI initialized: %dHz %dch, buffer=%u frames, device=%d\n",
        sampleRate, channels, bufferFrameCount, deviceIndex);

    // Start playback
    hr = pAudioClient->lpVtbl->Start(pAudioClient);
    if (FAILED(hr)) {
        LOG("[bmc_audio_windows] Start failed: 0x%08X\n", hr);
        goto exit;
    }

    // Playback loop
    int bytesPerFrame = channels * 2; // 16-bit PCM

    // Leftover buffer for partial frames from the queue
    uint8_t* leftover = NULL;
    int leftoverLen = 0;
    int leftoverCap = 0;

    while (running) {
        // Wait for buffer event or timeout
        DWORD waitResult = WaitForSingleObject(hEvent, 100);

        if (!running) break;

        // How many frames can we write?
        UINT32 numFramesPadding = 0;
        hr = pAudioClient->lpVtbl->GetCurrentPadding(pAudioClient, &numFramesPadding);
        if (FAILED(hr)) continue;

        UINT32 numFramesAvailable = bufferFrameCount - numFramesPadding;
        if (numFramesAvailable == 0) continue;

        BYTE* pData = NULL;
        hr = pRenderClient->lpVtbl->GetBuffer(pRenderClient, numFramesAvailable, &pData);
        if (FAILED(hr)) continue;

        int bytesNeeded = numFramesAvailable * bytesPerFrame;
        int bytesFilled = 0;

        // First, drain leftover
        if (leftoverLen > 0) {
            int toCopy = (leftoverLen < bytesNeeded) ? leftoverLen : bytesNeeded;
            memcpy(pData, leftover, toCopy);
            bytesFilled += toCopy;
            leftoverLen -= toCopy;
            if (leftoverLen > 0) {
                memmove(leftover, leftover + toCopy, leftoverLen);
            }
        }

        // Fill from queue
        while (bytesFilled < bytesNeeded) {
            PCMNode* node = dequeue_pcm();
            if (!node) {
                if (pcmFinished) break;
                // No data available — fill rest with silence
                break;
            }

            int available = node->length;
            int remaining = bytesNeeded - bytesFilled;

            if (available <= remaining) {
                memcpy(pData + bytesFilled, node->data, available);
                bytesFilled += available;
                free_pcm_node(node);
            } else {
                // Partial: copy what fits, save rest to leftover
                memcpy(pData + bytesFilled, node->data, remaining);
                bytesFilled += remaining;

                int leftoverNew = available - remaining;
                if (leftoverNew > leftoverCap) {
                    leftover = (uint8_t*)realloc(leftover, leftoverNew);
                    leftoverCap = leftoverNew;
                }
                memcpy(leftover, node->data + remaining, leftoverNew);
                leftoverLen = leftoverNew;
                free_pcm_node(node);
            }
        }

        // Zero-fill any remainder (silence)
        if (bytesFilled < bytesNeeded) {
            memset(pData + bytesFilled, 0, bytesNeeded - bytesFilled);
        }

        DWORD flags = 0;
        pRenderClient->lpVtbl->ReleaseBuffer(pRenderClient, numFramesAvailable, flags);

        if (pcmFinished && leftoverLen == 0) {
            // Check if queue is empty
            WaitForSingleObject(queueMutex, INFINITE);
            BOOL empty = (queueHead == NULL);
            ReleaseMutex(queueMutex);
            if (empty) break;
        }
    }

    // Wait a bit for final buffers to play
    Sleep(100);

    pAudioClient->lpVtbl->Stop(pAudioClient);
    LOG("[bmc_audio_windows] WASAPI playback stopped\n");

    free(leftover);

exit:
    if (pRenderClient) pRenderClient->lpVtbl->Release(pRenderClient);
    if (pAudioClient) pAudioClient->lpVtbl->Release(pAudioClient);
    if (pDevice) pDevice->lpVtbl->Release(pDevice);
    if (hEvent) CloseHandle(hEvent);
    cleanup_queue();
    CoUninitialize();
    running = 0;
    return 0;
}

// ============================================================================
// FFI Exports: Playback Control
// ============================================================================

static void start_player_internal(int sampleRate, int channels, int numberPrepareHeader, int deviceIndex) {
    // Stop any existing playback
    if (running) {
        running = 0;
        pcmFinished = 1;
        if (queueEvent) SetEvent(queueEvent);
        if (playbackThreadHandle) {
            WaitForSingleObject(playbackThreadHandle, 2000);
            CloseHandle(playbackThreadHandle);
            playbackThreadHandle = NULL;
        }
    }

    if (!queueMutex) queueMutex = CreateMutex(NULL, FALSE, NULL);
    queueEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    pcmFinished = 0;
    running = 1;

    PlaybackParams* params = (PlaybackParams*)malloc(sizeof(PlaybackParams));
    params->sampleRate = sampleRate;
    params->channels = channels;
    params->numberPrepareHeader = numberPrepareHeader;
    params->deviceIndex = deviceIndex;

    playbackThreadHandle = (HANDLE)_beginthreadex(NULL, 0, wasapi_playback_thread, params, 0, NULL);
}

FFI_PLUGIN_EXPORT
void startBackgroundPlayer(int sampleRate, int channels, int numberPrepareHeader) {
    start_player_internal(sampleRate, channels, numberPrepareHeader, -1);  // -1 = default device
}

FFI_PLUGIN_EXPORT
void startBackgroundPlayerWithDevice(int sampleRate, int channels, int numberPrepareHeader, int deviceIndex) {
    start_player_internal(sampleRate, channels, numberPrepareHeader, deviceIndex);
}

FFI_PLUGIN_EXPORT
BOOL push_pcm_data(const uint8_t* data, int length) {
    if (data && length > 0 && length <= MAX_FRAME) {
        enqueue_pcm(data, length);
        return TRUE;
    }
    return FALSE;
}

FFI_PLUGIN_EXPORT
void end_pcm_push() {
    pcmFinished = 1;
    if (queueEvent) SetEvent(queueEvent);
}
