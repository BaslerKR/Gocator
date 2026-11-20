# Device-owned Playground plugin identity.
# Allowed commands are quoted `set(PLAYGROUND_PLUGIN_* ...)` assignments.
# Host schema, ABI, and icon names come from DeviceRuntime.h.
# Repeat NAME=value to declare multiple environment paths for one variable.
set(PLAYGROUND_PLUGIN_ID "gocator")
set(PLAYGROUND_PLUGIN_VERSION "0.3.0")
# The standard connection lifecycle and hardware resource claims first ship in Core 0.6.6.
# Device-runtime ABI 8.0 has not shipped yet; C1 renames the interface without a version bump.
set(PLAYGROUND_PLUGIN_MINIMUM_CORE_VERSION "0.6.6")
set(PLAYGROUND_PLUGIN_DISPLAY_NAME "Gocator")
# Optional presentation metadata; consumers must export it to manifests/catalogs.
set(PLAYGROUND_PLUGIN_SUBTITLE "LMI GoPxL")
set(PLAYGROUND_PLUGIN_DESCRIPTION "Discover and configure Gocator sensors, and acquire profile and surface data.")
set(PLAYGROUND_PLUGIN_ADD_ACTION_TEXT "Gocator")
set(PLAYGROUND_PLUGIN_SESSION_TYPE "Gocator")
set(PLAYGROUND_PLUGIN_MENU_ORDER 300)
set(PLAYGROUND_PLUGIN_INSTANCING "package")
set(PLAYGROUND_PLUGIN_SESSION_SERVICES "graphicsEngine" "scriptEditor" "threeD")
set(PLAYGROUND_PLUGIN_MODULE_WINDOWS "GocatorPlugin.dll")
set(PLAYGROUND_PLUGIN_MODULE_LINUX "GocatorPlugin.so")
set(PLAYGROUND_PLUGIN_MODULE_MACOS "GocatorPlugin.so")
set(PLAYGROUND_PLUGIN_LIBRARY_DIRECTORIES
    "runtime")

set(PLAYGROUND_PLUGIN_PANEL_REDISCOVER_FIRST ON)
