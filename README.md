# Gocator

Gocator is a C++17 acquisition and configuration facade for LMI Gocator sensors using the GoPxL SDK.

## Capabilities

- Discover sensors and open a connection by IP address.
- Receive `GoDataSet` profile or surface payloads through registered callbacks.
- Configure scan length, scan mode, exposure, intensity, and uniform spacing.
- Read parameter schemas and values or write individual scanner and sensor parameters through the REST resources; scanner resources include firmware-exposed motion and alignment settings.
- Preserve schema-provided numeric precision and increments in the optional Qt feature editor, including sub-unit spacing values.
- Build an optional Qt 6 feature widget without adding Qt to the core target.

## Requirements

- CMake 3.20 or newer and a C++17 compiler.
- A GoPxL SDK whose headers and libraries match the target operating system, architecture, compiler, and build configuration.
- Qt 6 Widgets and Concurrent only when `GOCATOR_BUILD_QT_UI=ON`.

The repository-local SDK layout is documented in [GoPxL-SDK/README.md](GoPxL-SDK/README.md). Set `GOPXL_SDK_DIR` to another compatible SDK root when necessary. Automatic selection supports only the architectures represented by the bundled directories and resolves Debug separately under multi-config generators. `GOPXL_PLATFORM_DIR` overrides the automatic directory for every configuration; the caller is then responsible for supplying a configuration-compatible SDK directory.

Consumers that stage SDK runtimes can read `GOCATOR_GOPXL_SDK_DIR`, `GOCATOR_GOPXL_PLATFORM_DIR`, `GOCATOR_GOPXL_PLATFORM_DIR_DEBUG`, and `GOCATOR_GOPXL_PLATFORM_DIR_RELEASE` from the `gocator_core` target. These properties are the module-owned selection result; consumers must not duplicate the platform map.

## Integration

```cmake
set(GOCATOR_BUILD_QT_UI OFF CACHE BOOL "" FORCE)
add_subdirectory(path/to/Gocator/C++ Gocator-build)
target_link_libraries(consumer PRIVATE gocator_core)
```

Qt integration is explicit and disabled by default. A Qt consumer enables the option and links only the UI target; `Gocator::QtWidget` brings in `gocator_core`, Qt Widgets, and Qt Concurrent transitively:

```cmake
set(GOCATOR_BUILD_QT_UI ON CACHE BOOL "" FORCE)
add_subdirectory(path/to/Gocator/C++ Gocator-build)
target_link_libraries(qt_consumer PRIVATE Gocator::QtWidget)
```

Requesting the UI without an available Qt 6 Widgets/Concurrent package is a configure error. The contents and public dependencies of `gocator_core` do not change when the UI is enabled.

The optional GraphicsFrame adapter is disabled by default and requires the neutral GraphicsEngine contract target; it does not require the visualization renderer.

## Acquisition Contract

```cpp
#include "Gocator.h"

Gocator sensor;
const auto devices = sensor.discoverDevices();
if (!devices.empty() && sensor.open(devices.front().address)) {
    sensor.setScanMode(Gocator::SurfaceMode);
    sensor.registerGrabCallback(
        [](const GoPxLSdk::GoDataSet& dataSet, std::size_t sequence) {
            // Consume dataSet during this callback.
        });
    sensor.grab();
}
```

Grab callbacks run on the module's receive path. Keep callbacks bounded and avoid synchronous logging or GUI access there. Stop acquisition before closing or destroying the facade.

Parameter schema, data, and write methods perform synchronous REST requests. A GUI or latency-sensitive consumer must move those calls off its event thread and define its own timeout and cancellation behavior.

## Validation

`GOCATOR_BUILD_TESTS=ON` requires `GOCATOR_BUILD_QT_UI=ON` because the focused test executable exercises `QGocatorWidget`. A successful build establishes only compile/link compatibility; discovery, REST configuration, profile/surface payloads, reconnect behavior, and throughput require validation with the intended SDK package and sensor firmware.
