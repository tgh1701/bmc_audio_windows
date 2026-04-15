# bmc_audio_windows

Flutter FFI plugin phát audio PCM và liệt kê thiết bị âm thanh trên Windows qua WASAPI.

## Tính năng

| Tính năng | Mô tả |
|-----------|-------|
| 🔊 **WASAPI Playback** | Phát audio PCM16LE low-latency qua Windows Audio Session API |
| 🎛️ **Chọn thiết bị output** | Phát audio ra loa/tai nghe cụ thể theo index |
| 📋 **Liệt kê output devices** | Lấy tên, ID của tất cả thiết bị phát (speakers) |
| 🎤 **Liệt kê input devices** | Lấy tên, ID của tất cả thiết bị thu (microphones) |

## Cài đặt

```yaml
dependencies:
  bmc_audio_windows:
    git:
      url: https://github.com/tgh1701/bmc_audio_windows.git
      ref: main
```

## Sử dụng

### Import

```dart
import 'package:bmc_audio_windows/bmc_audio_windows.dart';
```

### Liệt kê thiết bị

```dart
// Output devices (speakers)
final outputDevices = listOutputDevices();
for (final d in outputDevices) {
  print('[${d.index}] ${d.name} — ${d.id}');
}

// Input devices (microphones)
final inputDevices = listInputDevices();
for (final d in inputDevices) {
  print('[${d.index}] ${d.name} — ${d.id}');
}
```

### Phát audio PCM

```dart
import 'dart:ffi';
import 'package:ffi/ffi.dart';
import 'package:bmc_audio_windows/bmc_audio_windows.dart' as bmc_audio;

// 1. Khởi tạo player (default device)
bmc_audio.startBackgroundPlayer(16000, 1, 4);
// sampleRate=16000, channels=1(mono), numberPrepareHeader=4

// Hoặc chọn thiết bị cụ thể:
// bmc_audio.startBackgroundPlayerWithDevice(16000, 1, 4, deviceIndex);

// 2. Đẩy dữ liệu PCM
final pcmData = ... ; // Uint8List chứa PCM16LE
final ptr = calloc<Uint8>(pcmData.length);
ptr.asTypedList(pcmData.length).setAll(0, pcmData);
bmc_audio.pushpcmdata(ptr, pcmData.length);
calloc.free(ptr);

// 3. Kết thúc phát
bmc_audio.endpcmpush();
```

### Phát file .pcm hoàn chỉnh

```dart
import 'dart:io';

Future<void> playPcmFile(String filePath) async {
  final data = await File(filePath).readAsBytes();
  const frameSize = 640; // 20ms tại 16kHz mono 16-bit

  bmc_audio.startBackgroundPlayer(16000, 1, 4);

  for (int i = 0; i < data.length; i += frameSize) {
    final end = (i + frameSize < data.length) ? i + frameSize : data.length;
    final slice = data.sublist(i, end);

    final ptr = calloc<Uint8>(slice.length);
    ptr.asTypedList(slice.length).setAll(0, slice);
    bmc_audio.pushpcmdata(ptr, slice.length);
    calloc.free(ptr);

    await Future.delayed(const Duration(milliseconds: 1));
  }

  bmc_audio.endpcmpush();
}
```

## API Reference

### Playback

| Hàm | Mô tả |
|-----|-------|
| `startBackgroundPlayer(sampleRate, channels, buffers)` | Tạo thread phát WASAPI với default device |
| `startBackgroundPlayerWithDevice(sampleRate, channels, buffers, deviceIndex)` | Phát với device cụ thể (index từ `listOutputDevices()`) |
| `pushpcmdata(Pointer<Uint8> data, int length)` | Đẩy dữ liệu PCM16LE vào queue (max 4096 bytes) |
| `endpcmpush()` | Báo hiệu kết thúc dữ liệu, thread tự dừng khi queue hết |

### Device Enumeration

| Hàm | Return | Mô tả |
|-----|--------|-------|
| `listOutputDevices()` | `List<BmcAudioWindowsDevice>` | Liệt kê tất cả speakers |
| `listInputDevices()` | `List<BmcAudioWindowsDevice>` | Liệt kê tất cả microphones |
| `getOutputDeviceCount()` | `int` | Số lượng output devices |
| `getOutputDeviceName(index)` | `String` | Tên device output |
| `getOutputDeviceId(index)` | `String` | ID device output |
| `getInputDeviceCount()` | `int` | Số lượng input devices |
| `getInputDeviceName(index)` | `String` | Tên device input |
| `getInputDeviceId(index)` | `String` | ID device input |

### BmcAudioWindowsDevice

```dart
class BmcAudioWindowsDevice {
  final int index;     // Index (0-based) để truyền vào startBackgroundPlayerWithDevice
  final String id;     // WASAPI endpoint ID
  final String name;   // Tên hiển thị (friendly name)
  final bool isOutput; // true = speaker, false = microphone
}
```

## Cấu trúc project

```
bmc_audio_windows/
├── lib/
│   ├── bmc_audio_windows.dart                    # Dart API chính
│   └── bmc_audio_windows_bindings_generated.dart  # FFI bindings (auto-generated)
├── src/
│   ├── bmc_audio_windows.c    # Native WASAPI implementation
│   ├── bmc_audio_windows.h    # Header file
│   └── CMakeLists.txt         # Build config
├── example/                   # Example Flutter app
├── windows/                   # Windows plugin config
├── pubspec.yaml
└── ffigen.yaml                # FFI code gen config
```

## Lưu ý

- Chỉ hỗ trợ **Windows** (WASAPI API)
- Chỉ phát **1 stream tại 1 thời điểm** (gọi `startBackgroundPlayer` lần 2 sẽ dừng stream cũ)
- Format: **PCM16LE** (16-bit signed little-endian)
- Max frame size: **4096 bytes** per push

## Regenerate FFI Bindings

```bash
dart run ffigen --config ffigen.yaml
```

## License

Proprietary — BMC Technology Vietnam
