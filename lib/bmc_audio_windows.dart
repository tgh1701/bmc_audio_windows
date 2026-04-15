import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';
import 'package:ffi/ffi.dart';

import 'bmc_audio_windows_bindings_generated.dart';

/// A very short-lived native function.
///
/// For very short-lived functions, it is fine to call them on the main isolate.
/// They will block the Dart execution while running the native function, so
/// only do this for native functions which are guaranteed to be short-lived.
void startBackgroundPlayer(int samplerate, int channels, int numberPrepareHeader) =>
    _bindings.startBackgroundPlayer(samplerate, channels, numberPrepareHeader);

void startBackgroundPlayerWithDevice(int samplerate, int channels, int numberPrepareHeader, int deviceIndex) =>
    _bindings.startBackgroundPlayerWithDevice(samplerate, channels, numberPrepareHeader, deviceIndex);

void pushpcmdata(Pointer<Uint8> data, int length) => _bindings.push_pcm_data(data, length);
void endpcmpush() => _bindings.end_pcm_push();

// ============================================================================
// Device Enumeration
// ============================================================================

/// Get number of available audio output devices.
int getOutputDeviceCount() => _bindings.getOutputDeviceCount();

/// Get name of output device at [index].
String getOutputDeviceName(int index) {
  final ptr = _bindings.getOutputDeviceName(index);
  return ptr.cast<Utf8>().toDartString();
}

/// Get ID of output device at [index].
String getOutputDeviceId(int index) {
  final ptr = _bindings.getOutputDeviceId(index);
  return ptr.cast<Utf8>().toDartString();
}

/// Get number of available audio input devices.
int getInputDeviceCount() => _bindings.getInputDeviceCount();

/// Get name of input device at [index].
String getInputDeviceName(int index) {
  final ptr = _bindings.getInputDeviceName(index);
  return ptr.cast<Utf8>().toDartString();
}

/// Get ID of input device at [index].
String getInputDeviceId(int index) {
  final ptr = _bindings.getInputDeviceId(index);
  return ptr.cast<Utf8>().toDartString();
}

/// Audio device info returned by enumeration.
class BmcAudioWindowsDevice {
  final int index;
  final String id;
  final String name;
  final bool isOutput;

  BmcAudioWindowsDevice({
    required this.index,
    required this.id,
    required this.name,
    required this.isOutput,
  });

  @override
  String toString() => 'BmcAudioWindowsDevice(index: $index, name: "$name", isOutput: $isOutput)';
}

/// List all output (speaker) devices.
List<BmcAudioWindowsDevice> listOutputDevices() {
  final count = getOutputDeviceCount();
  final devices = <BmcAudioWindowsDevice>[];
  for (int i = 0; i < count; i++) {
    devices.add(BmcAudioWindowsDevice(
      index: i,
      id: getOutputDeviceId(i),
      name: getOutputDeviceName(i),
      isOutput: true,
    ));
  }
  return devices;
}

/// List all input (microphone) devices.
List<BmcAudioWindowsDevice> listInputDevices() {
  final count = getInputDeviceCount();
  final devices = <BmcAudioWindowsDevice>[];
  for (int i = 0; i < count; i++) {
    devices.add(BmcAudioWindowsDevice(
      index: i,
      id: getInputDeviceId(i),
      name: getInputDeviceName(i),
      isOutput: false,
    ));
  }
  return devices;
}

// ============================================================================
// Native Library Loading
// ============================================================================

const String _libName = 'bmc_audio_windows';

/// The dynamic library in which the symbols for [BmcAudioWindowsBindings] can be found.
final DynamicLibrary _dylib = () {
  if (Platform.isMacOS || Platform.isIOS) {
    return DynamicLibrary.open('$_libName.framework/$_libName');
  }
  if (Platform.isAndroid || Platform.isLinux) {
    return DynamicLibrary.open('lib$_libName.so');
  }
  if (Platform.isWindows) {
    return DynamicLibrary.open('$_libName.dll');
  }
  throw UnsupportedError('Unknown platform: ${Platform.operatingSystem}');
}();

/// The bindings to the native functions in [_dylib].
final BmcAudioWindowsBindings _bindings = BmcAudioWindowsBindings(_dylib);
