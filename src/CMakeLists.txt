# The Flutter tooling requires that developers have CMake 3.10 or later
# installed. You should not increase this version, as doing so will cause
# the plugin to fail to compile for some customers of the plugin.
cmake_minimum_required(VERSION 3.10)

project(bmc_audio_windows_library VERSION 0.0.1 LANGUAGES C)

add_library(bmc_audio_windows SHARED
  "bmc_audio_windows.c"
)

set_target_properties(bmc_audio_windows PROPERTIES
  PUBLIC_HEADER bmc_audio_windows.h
  OUTPUT_NAME "bmc_audio_windows"
)

target_compile_definitions(bmc_audio_windows PUBLIC DART_SHARED_LIB)

if (WIN32)
  # Force the .c file to genuinely compile as C on MSVC
  # (parent CMakeLists uses LANGUAGES CXX, which makes MSVC treat .c as .cpp)
  set_source_files_properties("bmc_audio_windows.c" PROPERTIES LANGUAGE C)
  set_target_properties(bmc_audio_windows PROPERTIES LINKER_LANGUAGE C)

  # WASAPI requires ole32 (COM) and uuid (IID definitions)
  target_link_libraries(bmc_audio_windows PRIVATE ole32 uuid)
  # Allow compilation warnings (don't treat as errors for 3rd party headers)
  target_compile_options(bmc_audio_windows PRIVATE /WX- /TC)
endif()

if (ANDROID)
  # Support Android 15 16k page size
  target_link_options(bmc_audio_windows PRIVATE "-Wl,-z,max-page-size=16384")
endif()

# Copy the built DLL into the Flutter app's output directory.
# This is required for ffiPlugin: true so the DLL is bundled with the app.
install(TARGETS bmc_audio_windows
  RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_PREFIX}"
)
