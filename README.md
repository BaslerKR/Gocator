# Gocator

Gocator is a C++17 acquisition and configuration facade for LMI Gocator sensors using the GoPxL SDK.

## Capabilities

- Discover sensors and open a connection by IP address or `address:port` target.
- Receive `GoDataSet` image, profile or surface payloads through registered callbacks.
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
if (!devices.empty()) {
    const auto& target = devices.front();
    const std::string targetId = target.address + ":" + std::to_string(target.controlPort);
    if (sensor.open(targetId)) {
        sensor.setScanMode(Gocator::SurfaceMode);
        sensor.registerGrabCallback(
            [](const GoPxLSdk::GoDataSet& dataSet, std::size_t sequence) {
                // Consume dataSet during this callback.
            });
        sensor.grab();
    }
}
```

Grab callbacks run on the module's receive path. Keep callbacks bounded and dispatch connection or acquisition commands to another thread so the receive path can finish before Stop or Disconnect drains it. Avoid synchronous logging or GUI access there. `grab()` returns whether reception started, including finite grabs that finish before the caller observes the grabbing state. Disconnect settles the local connection and GDP state before publishing status changes. If device Stop fails, `close(true)` reports that failure after disconnecting. A timed-out Stop is never resent during teardown.

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

The optional adapter exposes `IDeviceParameterControl` for the **System** scope: `runningState` is a read-only enumeration (`ready`, `running`, `conflict`), and `quickEditEnabled` is a read/write boolean. The host device panel and callable path use the same core SDK functions. Targets contain a controller identity and connection generation; observations have revisions, and writes validate a fresh observation before SDK dispatch. Completion preserves actual readback, or an acknowledged/uncertain effect when the SDK or follow-up observation fails.

The same adapter exposes `IDeviceConnectionControl` for discovery, connect and disconnect. Targets come from the cached discovery catalog; `targetId` is the sensor address for the default control port and `address:port` for a non-default port. The model, serial and virtual flag are descriptive attributes. A non-default control port is also published as an identity attribute because the Host hashes the full attribute set. A single default-port record with unchanged model, serial, virtual and address attributes keeps its previous stable ID; merging previously duplicate endpoint records can intentionally change or retire those IDs. Discovery logs every reported device, and connection targets require an address, usable control and GDP ports, and a local device without an address conflict. `connectionId` is a controller-scoped connection generation, so an id from an earlier connection is refused as stale. Discovery is refused while connected or grabbing. Single and Live use the standard lifecycle over the same acquisition facade: `device.single` calls `grab(1)` and completes after its frame is delivered, while `device.live-start`/`device.live-stop` call `grab()` and `stop()`. `source.start`/`source.stop` share that authority and never issue a second start. An already-running GoPxL system is reused without changing its existing GDP output configuration, so active Emulator replay does not produce a duplicate Start refusal or lose another client's outputs. The replay owner must provide a usable output, and local Single/Live stop leaves replay started by another client running. A local Start assumes exclusive control of GDP output configuration; clients that may start concurrently must attach to an already-running system instead. Single while Live is refused with "Stop Live before a single grab." and a second Single is refused while the first is in flight; neither refusal has an effect. Discovery, connect and disconnect share one module control gate for API-driven lifecycle operations, and one acquisition reservation serializes `source.start` with live start so neither issues a second grab. A busy gate refuses with `busy` and an explicit reason.

Commands and Scanner/Sensor typed API targets remain explicitly unsupported; the existing Scanner/Sensor resource editors in the host device panel remain available.

Initial schemas, values and device labels are fetched on a worker. Only copied JSON updates the host device panel. Requests arriving during a read are coalesced into a follow-up read, with schema rebuilds taking priority over value-only refreshes. The static System category remains present with unavailable values after an initial observation failure and can recover on a value refresh. System writes are serialized with connection destruction; this does not turn two remote SDK observations into an atomic device snapshot or establish a settings-to-frame fence. Structured arrays and unsupported schema types are not converted into speculative editors.

Acquisition output setup reports at most one operational failure per attempt. The optional package-wide diagnostic sink receives only the failed preparation, enable or registration stage. A concise warning names the stage when no sink is installed. The observation omits SDK exception text and device identities. A source selection or state check failure aborts before the first output write. An enable or registration failure is observed and acquisition can continue with the existing outputs. The deliberately tolerated scan-mode and remove-all exceptions produce no observation. The module copies the sink when each session is created, and the owner keeps it valid until sessions and producer work have stopped.

When a GoPxL PC instance or Emulator replays data without a scan engine, output setup selects one authoritative data source instead of a scanner output. It prefers profile range sources, falls back to surface range sources, and uses an image source only when no range source exists. The adapter publishes range-only when a dataset contains any range candidate; a damaged or storage-refused range does not fall back to an image in the same dataset. Only a dataset with no range candidate can publish an image-only `GraphicsFrame`. Common Mono, Bayer, RGB/BGR and RGBA/BGRA formats are supported; unsupported formats, including GEV 12-packed layouts that differ from PFNC, are refused. Word-aligned Mono and Bayer samples retain their declared bit width and alignment. Simultaneous image/range publication is tracked separately in [Playground issue #654](https://github.com/Minu-Park/Playground/issues/654). Discovered control ports are retained for address-based connections; an unknown address uses the GoPxL default control port, while an address with multiple non-default endpoints requires an explicit port.

When a range is requested, the optional range sink reports the first rejected GDP range message and whether another message yielded a usable range. It distinguishes a dataset without a supported range message from an invalid input and a refused or misaligned destination. Surface sample arrays must match the reported grid. Auxiliary intensity with a different grid is omitted. Single reports each observed failure. Live samples the first and every hundredth occurrence per reason in an acquisition. These observations are independent of output setup failures and detailed capture.

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
