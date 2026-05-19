# GoPxL SDK - macOS (arm64)

SDK for controlling Gocator sensors on macOS.

## Directory Structure

```
GoPxL-SDK/
├── bin/            # Built executables
├── lib/            # Shared libraries (.dylib)
├── include/        # Header files
│   ├── kApi/       # Platform abstraction layer
│   ├── GoApi/      # Gocator API
│   └── GoPxLSdk/  # GoPxL SDK (connect, configure, receive data)
└── samples/        # Sample source code
```

## Quick Start

```bash
cd samples/SimpleConnect
make
../../bin/SimpleConnect 192.168.1.10
```

## Linking in Your Project

### Compiler flags

```
-I<SDK_PATH>/include/kApi
-I<SDK_PATH>/include/GoApi
-I<SDK_PATH>/include/GoPxLSdk
```

### Linker flags

```
-L<SDK_PATH>/lib -lkApi -lGoApi -lGoPxLSdk
-Wl,-rpath,<path_to_lib_at_runtime>
```

### CMake example

```cmake
set(GOPXL_SDK_DIR "/path/to/GoPxL-SDK")

add_executable(myapp main.cpp)

target_include_directories(myapp PRIVATE
    ${GOPXL_SDK_DIR}/include/kApi
    ${GOPXL_SDK_DIR}/include/GoApi
    ${GOPXL_SDK_DIR}/include/GoPxLSdk
)

target_link_directories(myapp PRIVATE ${GOPXL_SDK_DIR}/lib)
target_link_libraries(myapp kApi GoApi GoPxLSdk)

set_target_properties(myapp PROPERTIES
    BUILD_RPATH "${GOPXL_SDK_DIR}/lib"
    INSTALL_RPATH "@executable_path/../lib"
)
```

## Key Constants

| Constant | Value | Description |
|----------|-------|-------------|
| Control Port | 3600 | TCP port for REST commands |
| GDP Port | 3601 | TCP port for data streaming |
| Web Port | 8100 | HTTP web UI (not used by SDK) |

## Minimal Code Example

```cpp
#include <kApi/kApiDef.h>
#include <GoApi/GoApiLib.h>
#include <GoPxLSdk/GoSystem.h>
#include <GoPxLSdk/GoGdpClient.h>
#include <GoPxLSdk/GoGdpMsg/GoGdpProfileUniform.h>

using namespace GoPxLSdk;

int main() {
    kAssembly goApiLib = kNULL;
    GoApiLib_Construct(&goApiLib);

    kIpAddress addr;
    kIpAddress_Parse(&addr, "192.168.1.10");

    auto system = GoSystem(addr, GO_PXL_SDK_DEFAULT_CONTROL_PORT);
    system.Connect();
    system.Start();

    auto gdp = std::make_unique<GoGdpClient>();
    gdp->Connect(system.Address(), system.GdpPort());
    gdp->ReceiveDataSync(20000);

    // Process gdp->DataSet() ...

    gdp->Close();
    system.Stop();
    system.Disconnect();
    kDestroyRef(&goApiLib);
}
```

## Platform

- macOS arm64 (Apple Silicon)
- Requires: C++17
- Built with: Apple clang
