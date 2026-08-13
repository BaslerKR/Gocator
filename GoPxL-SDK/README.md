# GoPxL SDK Layout

This directory contains the GoPxL headers and platform-specific libraries consumed by the Gocator CMake target. The SDK content remains governed by its vendor license and compatibility matrix.

## Layout

```text
GoPxL-SDK/
|-- include/
|   |-- kApi/
|   |-- GoApi/
|   `-- GoPxLSdk/
`-- lib/
    |-- apple_silicon/
    |-- linux_arm64/
    |-- linux_arm64d/
    |-- linux_x64/
    |-- linux_x64d/
    |-- win64/
    `-- win64d/
```

The bundled set supports Apple Silicon, Linux arm64/x64, and Windows x64 in the configurations represented above. It does not contain macOS x86_64 or Linux armv7 libraries. Do not infer support for an absent directory from a CMake architecture branch.

## CMake Selection

`GOPXL_SDK_DIR` identifies this root. `GOPXL_PLATFORM_DIR` identifies a directory below `lib/` and may be supplied explicitly:

```cmake
set(GOPXL_SDK_DIR "/path/to/GoPxL-SDK" CACHE PATH "")
set(GOPXL_PLATFORM_DIR "linux_x64" CACHE STRING "")
```

The selected libraries must match the active build configuration. Configure-time `CMAKE_BUILD_TYPE` is not sufficient to select Debug and Release libraries under a multi-config generator; consumers must not accept a Debug validation until the generated target resolves the matching directory per configuration.

Runtime libraries from the same SDK directory must be available to the loader. Header and link success alone do not prove runtime or sensor-firmware compatibility.
