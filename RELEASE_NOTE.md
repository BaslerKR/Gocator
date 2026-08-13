## Unreleased

- Split the opt-in `Gocator::QtWidget` static target from `gocator_core`; Qt integration is now disabled by default and fails configuration explicitly when requested without Qt 6.
- Updated the optional scene adapter to consume a neutral scene-contract target without inheriting the visualization runtime; range conversion output is unchanged.
- Replace the corrupted host-layout README and the inaccurate single-platform SDK note with standalone, code-aligned contracts.

## v0.1.1

- Use a manually edited IP address for connection attempts instead of stale discovered-device selection data.
- Declare millimeter output explicitly in the organized range scene adapter and use unit-tagged scale/offset fields.
