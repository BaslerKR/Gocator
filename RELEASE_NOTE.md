## Unreleased

- Drop the removed `GraphicsRangeFrameWriterFailure::MisalignedStorage` case in `GocatorDataSetGraphicsFrameAdapter` and adopt `DevicePluginSessionCapability::GraphicsViewer`.

- Publish immutable GraphicsFrame handles with builder-backed image and range spans while preserving single-lease frame storage and one-copy device conversion.

- Mark dependent `GoJson::Get<T>()` calls in the data-source helpers with `template` so the plugin compiles with Apple clang 21, which rejects the unmarked form.

- Plan a Gocator frame's range and image as one frame-storage request through the Playground `IFrameStorageProvider::acquireFrame()` contract, so a frame with both components is written once into one worker slot and published in place with a single admission. The adapter requires `Playground::DeviceRuntimeContract` and the frame-storage interface version 2.0.

- Complete Single only after the adapter hands off a usable frame and acquisition stops. Preserve Stop failures in the command result and release the local connection on Disconnect even when device Stop fails. Bind automatic finite-grab stops to their own acquisition.

- Reject surface arrays that disagree with the declared grid and omit mismatched auxiliary intensity. Report the full required image extent for short rows and store only declared image rows.

- Convert Gocator GDP image-only replay messages into owned GraphicsImage payloads for common Mono, Bayer, RGB/BGR and RGBA/BGRA formats, preserve word-aligned source bit widths, and report image rejection evidence through the device image diagnostics contract. GEV 12-packed layouts that differ from PFNC are refused rather than misdecoded.

- Support GoPxL PC and Emulator replay jobs without scan engines by selecting one authoritative GDP output, preferring profile range sources, falling back to surface range sources, and using an image source only when no range source exists. A dataset with a usable range candidate publishes the range together with the first valid image in one frame whose components share a single planned frame-storage lease and are published in place. A dataset whose image is invalid publishes range-only and still reports the image rejection, a damaged range candidate suppresses image fallback, and an image-only dataset publishes the image alone. A refused storage plan drops the whole frame once, without retrying component by component.

- Encode non-default control ports in connection target IDs, keep same-address PC instances and Emulators distinct, refuse ambiguous port-less targets, and list only local discovery entries with an address, usable control and GDP ports, and no address conflict. Log every reported device.

- Retain discovered control ports for address-based Gocator connections and keep the default port only for unknown targets.

- Tolerate an already-running GoPxL system during acquisition start so Single and Live can attach to active Emulator replay without a duplicate Start refusal or output reconfiguration, while leaving replay started by another client running when local acquisition stops.

- Cover replay-source selection, malformed data-source links, profile-first fallback, image-only fallback, packed and word-aligned image layouts, ambiguous targets, cached control-port resolution, and external replay ownership with focused tests.

- Report GDP range assembly failures, destination storage failures, and retained range state through the optional range diagnostic sink.

- Report acquisition output preparation failures by stage through the optional operational diagnostic sink, with a concise warning when no sink is installed.
- Verify the three output failure diagnostic mappings when building the package and cover continuation after an output removal failure.

- Declare device panel presentation in package session metadata.

- Ship the package icon from this repository (`C++/Utility/PlaygroundAdapter/Package/icon.png`) instead of the SDK-installed Resources brand asset. Include `DeviceRuntime/Contract/DevicePluginSession.h` from the contract include root; the SDK no longer installs a `PlaygroundWidget` header path or vendor icons.

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
