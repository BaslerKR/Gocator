# Device-owned Playground plugin identity.
# Allowed commands are quoted `set(PLAYGROUND_PLUGIN_* ...)` assignments.
# Host schema, ABI, and icon names come from DevicePluginPackage.h.
# Repeat NAME=value to declare multiple environment paths for one variable.
set(PLAYGROUND_PLUGIN_ID "gocator")
set(PLAYGROUND_PLUGIN_VERSION "0.1.2")
set(PLAYGROUND_PLUGIN_DISPLAY_NAME "LMI Gocator")
set(PLAYGROUND_PLUGIN_ADD_ACTION_TEXT "Gocator")
set(PLAYGROUND_PLUGIN_SESSION_TYPE "Gocator")
set(PLAYGROUND_PLUGIN_MENU_ORDER 300)
set(PLAYGROUND_PLUGIN_LIBRARY_WINDOWS "GocatorPlugin.dll")
set(PLAYGROUND_PLUGIN_LIBRARY_LINUX "GocatorPlugin.so")
set(PLAYGROUND_PLUGIN_LIBRARY_MACOS "GocatorPlugin.so")
set(PLAYGROUND_PLUGIN_LIBRARY_DIRECTORIES
    "runtime")
