#ifndef BMC_AUDIO_WINDOWS_H
#define BMC_AUDIO_WINDOWS_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#if _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

#if _WIN32
#define FFI_PLUGIN_EXPORT __declspec(dllexport)
#else
#define FFI_PLUGIN_EXPORT
#endif

// ============================================================================
// Output (Playback) — WASAPI on Windows, waveOut fallback
// ============================================================================

/// Start background playback thread with default output device.
FFI_PLUGIN_EXPORT
void startBackgroundPlayer(int sampleRate, int channels, int numberPrepareHeader);

/// Start background playback thread with a specific output device.
/// deviceIndex: 0-based index from getOutputDeviceCount(). -1 = default.
FFI_PLUGIN_EXPORT
void startBackgroundPlayerWithDevice(int sampleRate, int channels, int numberPrepareHeader, int deviceIndex);

/// Push PCM data to the playback queue.
FFI_PLUGIN_EXPORT
BOOL push_pcm_data(const uint8_t* data, int length);

/// Signal end of PCM data (flushes remaining buffers and stops).
FFI_PLUGIN_EXPORT
void end_pcm_push();

// ============================================================================
// Device Enumeration
// ============================================================================

/// Get number of available output (render) devices.
FFI_PLUGIN_EXPORT
int getOutputDeviceCount(void);

/// Get the name of an output device by index.
/// Returns pointer to internal static buffer (valid until next call).
FFI_PLUGIN_EXPORT
const char* getOutputDeviceName(int index);

/// Get the ID string of an output device by index.
/// Returns pointer to internal static buffer (valid until next call).
FFI_PLUGIN_EXPORT
const char* getOutputDeviceId(int index);

/// Get number of available input (capture) devices.
FFI_PLUGIN_EXPORT
int getInputDeviceCount(void);

/// Get the name of an input device by index.
FFI_PLUGIN_EXPORT
const char* getInputDeviceName(int index);

/// Get the ID string of an input device by index.
FFI_PLUGIN_EXPORT
const char* getInputDeviceId(int index);

#endif // BMC_AUDIO_WINDOWS_H
