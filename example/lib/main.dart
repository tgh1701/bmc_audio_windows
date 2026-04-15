import 'dart:io';
import 'dart:ffi';
import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'dart:async';
import 'package:ffi/ffi.dart';

import 'package:bmc_audio_windows/bmc_audio_windows.dart' as bmc_audio;

void main() {
  runApp(const MyApp());
}

class MyApp extends StatefulWidget {
  const MyApp({super.key});

  @override
  State<MyApp> createState() => _MyAppState();
}

class _MyAppState extends State<MyApp> {
  bool isPlaying = false;
  List<BmcAudioWindowsDevice> outputDevices = [];
  List<BmcAudioWindowsDevice> inputDevices = [];
  int selectedDeviceIndex = -1; // -1 = default

  @override
  void initState() {
    super.initState();
    _loadDevices();
  }

  void _loadDevices() {
    setState(() {
      outputDevices = bmc_audio.listOutputDevices();
      inputDevices = bmc_audio.listInputDevices();
    });
  }

  Future<void> playPcmFile() async {
    final result = await FilePicker.platform.pickFiles(
      type: FileType.custom,
      allowedExtensions: ['pcm'],
    );

    if (result != null && result.files.single.path != null) {
      final path = result.files.single.path!;
      final file = File(path);
      final data = await file.readAsBytes();
      const int frameSize = 640; // 20ms at 16bit mono 16kHz

      // Start player with selected device
      if (selectedDeviceIndex >= 0) {
        bmc_audio.startBackgroundPlayerWithDevice(16000, 1, 4, selectedDeviceIndex);
      } else {
        bmc_audio.startBackgroundPlayer(16000, 1, 4);
      }

      setState(() => isPlaying = true);

      for (int i = 0; i < data.length; i += frameSize) {
        if (!isPlaying) break;
        final end = (i + frameSize < data.length) ? i + frameSize : data.length;
        final slice = data.sublist(i, end);

        final ptr = calloc<Uint8>(slice.length);
        ptr.asTypedList(slice.length).setAll(0, slice);

        bmc_audio.pushpcmdata(ptr, slice.length);
        calloc.free(ptr);

        await Future.delayed(const Duration(milliseconds: 1));
      }
      bmc_audio.endpcmpush();
      setState(() => isPlaying = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      home: Scaffold(
        appBar: AppBar(title: const Text("BMC Audio Windows - Example")),
        body: Padding(
          padding: const EdgeInsets.all(16.0),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              // Output devices
              const Text("Output Devices:", style: TextStyle(fontWeight: FontWeight.bold, fontSize: 16)),
              const SizedBox(height: 8),
              if (outputDevices.isEmpty)
                const Text("No output devices found")
              else
                ...outputDevices.map((d) => RadioListTile<int>(
                  title: Text(d.name),
                  subtitle: Text("Index: ${d.index}"),
                  value: d.index,
                  groupValue: selectedDeviceIndex,
                  onChanged: (v) => setState(() => selectedDeviceIndex = v ?? -1),
                )),
              RadioListTile<int>(
                title: const Text("Default (System)"),
                value: -1,
                groupValue: selectedDeviceIndex,
                onChanged: (v) => setState(() => selectedDeviceIndex = v ?? -1),
              ),

              const Divider(),

              // Input devices
              const Text("Input Devices:", style: TextStyle(fontWeight: FontWeight.bold, fontSize: 16)),
              const SizedBox(height: 8),
              if (inputDevices.isEmpty)
                const Text("No input devices found")
              else
                ...inputDevices.map((d) => ListTile(
                  title: Text(d.name),
                  subtitle: Text("ID: ${d.id}"),
                )),

              const Divider(),
              const SizedBox(height: 16),

              // Play button
              Center(
                child: ElevatedButton.icon(
                  onPressed: isPlaying ? null : playPcmFile,
                  icon: Icon(isPlaying ? Icons.stop : Icons.play_arrow),
                  label: Text(isPlaying ? "Playing..." : "Select & Play .pcm file"),
                ),
              ),

              const SizedBox(height: 8),
              Center(
                child: TextButton(
                  onPressed: _loadDevices,
                  child: const Text("Refresh Devices"),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
