## Unreleased

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
