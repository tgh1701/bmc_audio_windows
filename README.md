# bmc_audio_windows

BMC Audio Windows — WASAPI audio playback and device enumeration plugin for Flutter on Windows.

## Features

- 🔊 **WASAPI Playback** — Low-latency audio output using Windows Audio Session API
- 🎛️ **Device Selection** — Choose specific output device by index
- 📋 **Device Enumeration** — List all input/output audio devices with names and IDs
- 🎤 **Input Device Listing** — Enumerate microphone devices for UI selection

## Usage

```dart
import 'package:bmc_audio_windows/bmc_audio_windows.dart';

// List output devices
final devices = listOutputDevices();
for (final d in devices) {
  print('${d.name} (${d.id})');
}

// Start playback on default device
startBackgroundPlayer(16000, 1, 4);

// Start playback on specific device
startBackgroundPlayerWithDevice(16000, 1, 4, deviceIndex);

// Push PCM data
pushpcmdata(ptr, length);

// End playback
endpcmpush();
```

## License

Proprietary — BMC Technology Vietnam
