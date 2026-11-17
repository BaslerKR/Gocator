## Unreleased

- Move to the widget-free session ABI: `discoverDevices` and `createSession` take only an error string and no longer exchange an opaque `QVariantMap`. Discovery retains its device catalog in a plugin-owned, mutex-guarded member that session creation consumes, and the session drops the obsolete `createDockWidgets`/dock override. The discovery serialization helpers are gone.

- Build against the widget-free device runtime SDK: the contract target is `Playground::DeviceRuntimeContract`, headers live under `DeviceRuntime/Contract`, and the interface is `IDeviceRuntime`. The plugin object derives directly from `QObject`/`IDeviceRuntime`, so the package needs neither the widget-side template nor Qt Widgets headers.

- Remove the Qt widget library and the widget tests. `QGocatorWidget`, `GocatorControlUi`, `GocatorAcquisitionSettings`, `GocatorQtWidget`, `GocatorPresentation` and `QGocatorWidgetTests` are gone; the plugin session creates no widget or dock and the host's common device panel owns the session UI. The scan settings the widget applied before a grab are exposed as typed parameters and written through the same `setParameterValue` route, including the former forced `exposureMode` value; no device behavior or parameter is lost. The package target links `Playground::DeviceRuntimeContract`, `Gocator::PlaygroundAdapter`, Qt Core and Qt Concurrent only: no Qt Widgets, Resources widget or dialog library, and no OpenGL.

- Drop favourites saved by the retired widget; the Host panel uses its package-level default `Plugins/gocator/Favorites` (owner decision https://github.com/Minu-Park/Playground/issues/440#issuecomment-6941202857).
- Expose the System, Scanner and Sensor parameter scopes as one typed tree: GoPxL schema labels, nested category paths, enum labels, numeric bounds, units, read-only access and per-parameter revisions. Scanner and sensor leaves write through JSON-pointer paths with an actual readback.
- Add module-owned parameter change subscriptions over the same tree. A bounded queue turns overflow into one explicit resync snapshot; observation runs on a module thread only while a subscription is active, so no GUI timer polls the sensor. Writability is re-observed after acquisition changes and after every write instead of being cached from a locked state.
- Delete `Gocator::configure`. The scan settings it applied are explicit parameter writes before `source.start`, including the former `exposureMode` value; output selection that acquisition needs moves into `grab()`.
- Accept an undiscovered address in `requestConnect` and report the SDK's refusal reason when GoPxL cannot reach it.
- Add the standard `device.single`, `device.live-start` and `device.live-stop` lifecycle over `grab(1)`, `grab()` and `stop()`. Single completes after its frame is delivered; Live and Single share one acquisition authority with `source.start`/`source.stop`, so neither path issues a second start. Single while Live is refused with "Stop Live before a single grab.", and a second Single is refused while the first is in flight; refusals carry their reason and have no effect.
- Cover the controller through the Playground parameter loopback codec and synthetic structure, notification, overflow, refusal and acquisition-setting tests.
- Give virtual and serial-less targets an index-free `address` identity attribute and drop exact duplicate discovery entries, so distinct sensors no longer collide in the Host's stable-target inventory. Real sensors keep their serial-based identity across DHCP address changes.

- Deliver 3D range frames through the GraphicsEngine range writer into Playground shared frame storage. A controller that provides storage gets every range channel stored once in its slot; an empty storage result drops the frame instead of copying it elsewhere.

## v0.3.0 (2026-11-13)

- Require Playground 0.6.6 or later; this package uses the device plugin interface introduced in that release.

- Implement the standard `IDeviceConnectionControl` lifecycle over the cached discovery catalog and a shared GUI/API control gate; discovery, connect and disconnect are callable while Single and Live remain unavailable in favor of `source.start`/`source.stop`. Stale connection ids and concurrent GUI work are refused before effects.

- Retain pending feature refreshes while a read is active, prioritize schema rebuilds, and recover the static System controls after an initial observation failure without reconnecting.

- Expose official GoSystem running-state and Quick Edit controls through the core facade, existing feature tree and typed System parameter capability, with connection/revision checks and actual readback.
- Fetch initial feature resources off the GUI thread; support plain strings, exact signed 64-bit integer values, escaped JSON-pointer keys and inherited read-only access without inventing trigger commands.
- Keep a failed acquisition start inactive until the producer reports an actual transition.

- Expose producer runtime diagnostics and log final counters after acquisition callbacks drain, including short runs.

- Show live producer telemetry in the device statusbar: an `fps` bubble over the delivered-frame delta and a warning `failed` bubble only when a grab callback failed. Counters reset when live acquisition starts, and both bubbles stay hidden while not grabbing.
- Treat an empty Gocator discovery as informational; a missing camera no longer appears as a System Logs warning.
- Take the binary display name from the package definition (`Gocator`).

## v0.2.1 (2026-10-20)

- Use the neutral LMI artwork for the package icon.

- Use the shared connect and disconnect artwork for connection controls.

- Use the shared refresh, single-grab, and live glyphs on the device toolbar.

- Publish the independently built device package with installed SDK validation.

- Document explicit Release-only installed SDK configuration.
- Build the standalone device plugin against an installed consumer SDK and stage its module-owned runtime package.

## v0.2.2 (2026-10-29)

- Show the next Live grab command with cameras/stop artwork in all pointer and focus states, and keep its tooltip and accessible name synchronized with acquisition state.

- Connect icons name the current state (`disconnected`/`connect`); a consumer theme can preview the other state on hover.

- Declare semantic layout, device-panel and action-density scopes through Qt properties; preserve independent plain-Qt builds without a theme dependency.

- Share the SDK-independent Qt control composition between the runtime widget and isolated presentation consumers.

## v0.2.0 (2026-10-08)

- Report acquisition lifecycle to consumers and retain opt-in stopped-device feature snapshots at the current selector state; expose parameter requests and operation outcomes through diagnostics without changing capture policy.

- Declare the Gocator display name, LMI GoPxL subtitle, and profile/surface acquisition summary in package metadata.

- Publish range output through the identified GraphicsFrame resource collection contract.

- Limit the control statusbar to Idle, Connected, and Live; route operation messages to application logs.

- Isolate status and grab callback exceptions so consumer failures cannot escape into the GDP acquisition thread.

- Publish the device controls through the generic host-managed plugin dock contract.

- Link `GocatorPlugin` only through `Gocator::QtWidget` and `Gocator::PlaygroundAdapter` so `libgocator_core.a` is not passed twice to the linker.

- Require ABI / Qt IID 5.0 and recompilation; older packages are rejected. Declare immutable GraphicsEngine + Script Editor capabilities directly on the session.

- Stage GoPxL dylibs in the Playground plugin `runtime` payload on macOS instead of the host `Contents/Frameworks` extra list.

- Link `Playground::DeviceRuntimeContract` for the `IDeviceRuntime` MODULE.
- Own package identity in `Utility/PlaygroundAdapter/Package/Package.cmake`; the host emits `plugin.json` from `DeviceRuntime.h`.
- Keep the session source controller in `Utility/PlaygroundAdapter/Source` as `GocatorSourceController`.
- Declare the Playground plugin runtime payload from this module; the host copies it into the package.
- Move the GraphicsFrame stream/adapter into `Utility/PlaygroundAdapter` as `Gocator::PlaygroundAdapter`.
- Publish GraphicsFrame through `GocatorGraphicsFrameStream`; the converter header stays in the adapter translation unit.

- Keep Gocator GDP adapters range-only; GraphicsEngine derives point-cloud and surface views from the authoritative range payload.
- Drain in-flight GraphicsFrame adapter callbacks before stream destruction and add an empty-dataset adapter test.
- Move GDP callback registration and GraphicsFrame conversion into the module adapter stream; the parent receives only owned GraphicsFrame values.
- Mark point-cloud range conversions as `ExplicitXY` when per-sample coordinates are supplied.
- Keep the GraphicsFrame adapter target independent of Qt GUI.
- Route Gocator 3D handoff through the host-owned `GraphicsFrame` payload contract.
- Make the module the single owner of GoPxL platform selection, resolve Debug libraries per configuration under multi-config generators, reject unsupported bundled architectures, and preserve explicit SDK directory overrides.
- Split the opt-in `Gocator::QtWidget` static target from `gocator_core`; Qt integration is now disabled by default and fails configuration explicitly when requested without Qt 6.

- Preserve Range and Intensity semantic field metadata, including the source bit depth for intensity payloads, in converted range frames.
- Updated the optional scene adapter to consume a neutral scene-contract target without inheriting the visualization runtime; range conversion output is unchanged.
- Replace the corrupted host-layout README and the inaccurate single-platform SDK note with standalone, code-aligned contracts.
- Expose scanner-resource root fields through the parameter API, settings snapshot, and optional Qt feature tree so firmware-provided motion and alignment settings can be edited and applied.
- Derive floating-point editor precision and increments from the resource schema so fine spacing values are not rounded or forced to a coarse default step.

## v0.1.1

- Use a manually edited IP address for connection attempts instead of stale discovered-device selection data.
- Declare millimeter output explicitly in the organized range scene adapter and use unit-tagged scale/offset fields.
