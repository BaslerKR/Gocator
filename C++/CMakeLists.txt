cmake_minimum_required(VERSION 3.20)

project(Gocator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MACOSX_RPATH ON)

set(GOPXL_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../GoPxL-SDK" CACHE PATH "GoPxL SDK root")
set(GOCATOR_DEFAULT_QT_PREFIX "/opt/Qt/6.7.2/gcc_64" CACHE PATH "Default Qt prefix")
option(GOCATOR_BUILD_QT_UI "Build Qt Widgets debug UI when Qt is available" ON)
option(GOCATOR_BUILD_GRAPHICSENGINE_ADAPTER
    "Build the optional Gocator-to-GraphicsEngine adapter target."
    OFF
)

if(GOCATOR_BUILD_QT_UI AND NOT CMAKE_PREFIX_PATH AND EXISTS "${GOCATOR_DEFAULT_QT_PREFIX}")
    list(APPEND CMAKE_PREFIX_PATH "${GOCATOR_DEFAULT_QT_PREFIX}")
endif()

add_library(gocator_core
    Gocator.cpp
    Internal/GocatorAcquisition.cpp
    Internal/GocatorConnection.cpp
    Internal/GocatorDiscovery.cpp
    Internal/GocatorParameterSet.cpp
    Internal/GocatorResourceClient.cpp
    Internal/GocatorSdkRuntime.cpp
    Internal/GocatorSettingsManager.cpp
)

target_include_directories(gocator_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_include_directories(gocator_core
    SYSTEM PUBLIC
        "${GOPXL_SDK_DIR}/include/kApi"
        "${GOPXL_SDK_DIR}/include/GoApi"
        "${GOPXL_SDK_DIR}/include/GoPxLSdk"
)

if(NOT DEFINED GOPXL_PLATFORM_DIR)
    if(APPLE)
        set(GOPXL_PLATFORM_DIR "apple_silicon")
    elseif(WIN32)
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(GOPXL_PLATFORM_DIR "win64d")
        else()
            set(GOPXL_PLATFORM_DIR "win64")
        endif()
    elseif(UNIX AND NOT APPLE)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
            if(CMAKE_BUILD_TYPE STREQUAL "Debug")
                set(GOPXL_PLATFORM_DIR "linux_arm64d")
            else()
                set(GOPXL_PLATFORM_DIR "linux_arm64")
            endif()
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
            if(CMAKE_BUILD_TYPE STREQUAL "Debug")
                set(GOPXL_PLATFORM_DIR "linux_arm7d")
            else()
                set(GOPXL_PLATFORM_DIR "linux_arm7")
            endif()
        else()
            if(CMAKE_BUILD_TYPE STREQUAL "Debug")
                set(GOPXL_PLATFORM_DIR "linux_x64d")
            else()
                set(GOPXL_PLATFORM_DIR "linux_x64")
            endif()
        endif()
    endif()
endif()

target_link_directories(gocator_core
    PUBLIC
        "${GOPXL_SDK_DIR}/lib/${GOPXL_PLATFORM_DIR}"
)

target_link_libraries(gocator_core
    PUBLIC
        kApi
        GoApi
        GoPxLSdk
)

if(GOCATOR_BUILD_QT_UI)
    find_package(Qt6 QUIET COMPONENTS Widgets Concurrent)
    if(Qt6_FOUND)
        set_target_properties(gocator_core PROPERTIES AUTOMOC ON)
        target_sources(gocator_core PRIVATE
            Utility/Qt/QGocatorWidget.cpp
            Utility/Qt/QGocatorWidget.h
        )
        target_link_libraries(gocator_core PUBLIC Qt6::Widgets Qt6::Concurrent)
        target_compile_definitions(gocator_core PUBLIC GOCATOR_HAS_UI)
    endif()
endif()

set(gocator_runtime_targets
    gocator_core
)

foreach(target ${gocator_runtime_targets})
    set_target_properties(${target} PROPERTIES
        BUILD_RPATH "${GOPXL_SDK_DIR}/lib/${GOPXL_PLATFORM_DIR}"
        INSTALL_RPATH "@executable_path/../GoPxL-SDK/lib/${GOPXL_PLATFORM_DIR}"
    )
endforeach()

if(GOCATOR_BUILD_GRAPHICSENGINE_ADAPTER)
    if(NOT TARGET GraphicsEngine::GraphicsEngine)
        message(FATAL_ERROR
            "[Gocator] GOCATOR_BUILD_GRAPHICSENGINE_ADAPTER=ON requires "
            "GraphicsEngine::GraphicsEngine. Add GraphicsEngine first or disable the adapter."
        )
    endif()

    add_library(GocatorGraphicsEngineAdapter STATIC
        Utility/GraphicsEngine/GocatorDataSetScene3DAdapter.h
        Utility/GraphicsEngine/GocatorDataSetScene3DAdapter.cpp
    )
    add_library(Gocator::GraphicsEngineAdapter ALIAS GocatorGraphicsEngineAdapter)
    target_compile_features(GocatorGraphicsEngineAdapter PUBLIC cxx_std_17)
    target_include_directories(GocatorGraphicsEngineAdapter
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/Utility/GraphicsEngine"
    )
    target_link_libraries(GocatorGraphicsEngineAdapter
        PUBLIC
            gocator_core
            GraphicsEngine::GraphicsEngine
    )
    message(STATUS "[Gocator] Gocator::GraphicsEngineAdapter enabled.")
endif()
