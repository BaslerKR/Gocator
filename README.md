# Gocator

Gocator is a C++17 acquisition and configuration facade for LMI Gocator sensors using the GoPxL SDK.

## Capabilities

- Discover sensors and open a connection by IP address.
- Receive `GoDataSet` profile or surface payloads through registered callbacks.
- Configure scan length, scan mode, exposure, intensity, and uniform spacing.
- Read parameter schemas and values or write individual scanner and sensor parameters through the REST resources; scanner resources include firmware-exposed motion and alignment settings.
- Preserve schema-provided numeric precision and increments in the typed parameter descriptors consumed by the host device panel, including sub-unit spacing values.
- Ship a widget-free Playground device package; the host's common device panel owns the session UI.

## Requirements

- CMake 3.20 or newer and a C++17 compiler.
- A GoPxL SDK whose headers and libraries match the target operating system, architecture, compiler, and build configuration.
- Qt 6 Core and Concurrent only when the Playground device plugin is built; no Qt Widgets, Resources widget or OpenGL dependency.

The repository-local SDK layout is documented in [GoPxL-SDK/README.md](GoPxL-SDK/README.md). Set `GOPXL_SDK_DIR` to another compatible SDK root when necessary. Automatic selection supports only the architectures represented by the bundled directories and resolves Debug separately under multi-config generators. `GOPXL_PLATFORM_DIR` overrides the automatic directory for every configuration; the caller is then responsible for supplying a configuration-compatible SDK directory.

Consumers that stage SDK runtimes can read `GOCATOR_GOPXL_SDK_DIR`, `GOCATOR_GOPXL_PLATFORM_DIR`, `GOCATOR_GOPXL_PLATFORM_DIR_DEBUG`, and `GOCATOR_GOPXL_PLATFORM_DIR_RELEASE` from the `gocator_core` target. These properties are the module-owned selection result; consumers must not duplicate the platform map.

## Integration

```cmake
add_subdirectory(path/to/Gocator/C++ Gocator-build)
target_link_libraries(consumer PRIVATE gocator_core)
```

Configuring the core never searches for or links Qt. There is no widget target: the Playground device package is widget-free and the host device panel owns discovery, connect, Single, Live and feature browsing. The scan settings the retired widget applied before a grab are ordinary typed parameters; a consumer writes them through the same `setParameterValue` route before `source.start`.

The optional Playground adapter is disabled by default. Enable `GOCATOR_BUILD_PLAYGROUND_ADAPTER` only when the neutral GraphicsEngine contract target is available. Consumers that want owned frames include `Utility/PlaygroundAdapter/GocatorGraphicsFrameStream.h` and link `Gocator::PlaygroundAdapter`; GDP conversion stays in `GocatorDataSetGraphicsFrameAdapter`.

The optional Playground device plugin is a separate MODULE. Enable `GOCATOR_BUILD_PLAYGROUND_PLUGIN` only when `Playground::DeviceRuntimeContract` and the adapter are available; the plugin links Qt Core and Concurrent only. Package identity lives in `Utility/PlaygroundAdapter/Package/Package.cmake` as quoted `set(PLAYGROUND_PLUGIN_* ...)` assignments; the MODULE must set `PLAYGROUND_PLUGIN_PACKAGE` and include `DeviceRuntime/Contract/DeviceRuntime.h`. The host emits `plugin.json` from that identity plus `DeviceRuntime.h`.

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

## SDK coverage and control semantics

The module follows the bundled GoPxL SDK public contracts. It does not infer
software-trigger REST routes from other Gocator SDK generations.

| GoPxL capability | Module integration |
| --- | --- |
| Discovery, connect/disconnect, GDP reception, `Start`/`Stop` | Existing acquisition facade and controls. Live keeps reception active; the configured sensor trigger source determines when data arrives. |
| `RunningState` | `systemState()` and the read-only System / Running state tree field. This is the sensor state, distinct from the local GDP grabbing flag. |
| `QuickEditEnabled`, `EnableQuickEdit`, `DisableQuickEdit` | `systemState()`, `setQuickEditEnabled()` and System / Quick edit. This suppresses recalculation of existing scan data during edits; it is not a trigger or acquisition mode. No automatic enable/disable is performed. |
| Resource schema/data/`SetJson` | Scanner and Sensor feature trees; boolean, signed 64-bit integer, floating-point, string and enumeration values. Object read-only access is inherited and JSON-pointer tokens are escaped. |
| `Client`, managed `Resource`/`ResourceManager`, `SensorPath`, disconnect handlers | SDK infrastructure; arbitrary resource traversal, subscriptions, multi-sensor selection and automatic disconnect recovery are not exposed as consumer commands by this facade. |
| Software trigger | No dedicated command in the supported `GoSystem` API. No inferred REST endpoint or simulated trigger is added. |

The optional adapter exposes `IDeviceParameterControl` for the **System** scope:
`runningState` is a read-only enumeration (`ready`, `running`, `conflict`), and
`quickEditEnabled` is a read/write boolean. The existing GUI and callable path
use the same core SDK functions. Targets contain a controller identity and
connection generation; observations have revisions, and writes validate a fresh
observation before SDK dispatch. Completion preserves actual readback, or an
acknowledged/uncertain effect when the SDK or follow-up observation fails.
The same adapter exposes `IDeviceConnectionControl` for discovery, connect and
disconnect. Targets come from the cached discovery catalog; `targetId` is the
sensor address and the model, serial and virtual flag are descriptive
attributes. `connectionId` is a controller-scoped connection generation, so an
id from an earlier connection is refused as stale. Discovery is refused while
connected or grabbing. Single and Live use the standard lifecycle over the same
acquisition facade: `device.single` calls `grab(1)` and completes after its
frame is delivered, while `device.live-start`/`device.live-stop` call `grab()`
and `stop()`. `source.start`/`source.stop` share that authority and never issue
a second start. Single while Live is refused with
"Stop Live before a single grab." and a second Single is refused while the first
is in flight; neither refusal has an effect. Discovery, connect and disconnect
share one module control gate between the Qt widget and callable clients, and
one acquisition reservation serializes `source.start` with live start so neither
issues a second grab. A busy gate refuses with `busy`, and the widget disables
its controls while callable work runs.

Commands and Scanner/Sensor typed API targets remain explicitly unsupported;
the existing Scanner/Sensor GUI resource editors remain available.

Initial schemas, values and device labels are fetched on a worker. Only copied
JSON updates the GUI. Requests arriving during a read are coalesced into a
follow-up read, with schema rebuilds taking priority over value-only refreshes.
The static System category remains present with unavailable values after an
initial observation failure and can recover on a value refresh. System writes are serialized with connection destruction;
this does not turn two remote SDK observations into an atomic device snapshot
or establish a settings-to-frame fence. Structured arrays and unsupported schema
types are not converted into speculative editors.

## Validation

`GOCATOR_BUILD_TESTS=ON` builds the controller, adapter and parameter-loopback tests; no widget test remains. A successful build establishes only compile/link compatibility; discovery, REST configuration, profile/surface payloads, reconnect behavior, and throughput require validation with the intended SDK package and sensor firmware.

## Installed consumer SDK plugin build

Build the existing plugin project using this repository, an installed Plugin
SDK, the matching Qt kit and its vendor SDK. No consumer source tree or
renderer is needed. The plugin switch enables its adapter and vendor SDK
implementation; no widget or theme installation is pulled into the package.

```sh
cmake -S C++ -B build/plugin -DCMAKE_PREFIX_PATH="<PluginSDK>;<Qt-prefix>" -DCMAKE_CONFIGURATION_TYPES=Release -DCMAKE_BUILD_TYPE=Release -DGOCATOR_BUILD_PLAYGROUND_PLUGIN=ON -DGOCATOR_BUILD_TESTS=ON
cmake --build build/plugin --config Release --parallel 4
ctest --test-dir build/plugin -C Release --output-on-failure
cmake --build build/plugin --config Release --target GocatorPluginPackageStage
```

Supply the existing vendor SDK options when it is not in the documented default
location. The generated package is `build/plugin/packages/Release/plugins/<id>/current`
on multi-configuration generators and retains the module-owned runtime layout.
It includes the manifest, icon and SDK identity. Staging reconciles only this
generated package. Use a fresh build directory or explicitly update
`PlaygroundPluginSDK_DIR` when selecting another SDK. This Release software
gate does not establish physical-device, Debug or cross-platform acceptance.
Set `CMAKE_BUILD_TYPE=Release` for a single-configuration generator. The staged
package links no Qt Widgets, Resources widget/dialog library or OpenGL; check
its binaries with `otool -L` on macOS, `readelf -d` on Linux or `dumpbin` on
Windows.
