cmake_minimum_required(VERSION 3.20)

project(Gocator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MACOSX_RPATH ON)

set(GOPXL_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../GoPxL-SDK" CACHE PATH "GoPxL SDK root")
set(GOCATOR_DEFAULT_QT_PREFIX "/opt/Qt/6.7.2/gcc_64" CACHE PATH "Default Qt prefix")
option(GOCATOR_BUILD_QT_UI "Build the optional Qt Widgets integration target" OFF)
option(GOCATOR_BUILD_TESTS "Build focused Gocator module tests" OFF)
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

if(DEFINED GOPXL_PLATFORM_DIR AND NOT GOPXL_PLATFORM_DIR STREQUAL "")
    set(gopxl_platform_debug_dir "${GOPXL_PLATFORM_DIR}")
    set(gopxl_platform_release_dir "${GOPXL_PLATFORM_DIR}")
else()
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" gopxl_system_processor)
    if(APPLE)
        set(gopxl_apple_architectures "${CMAKE_OSX_ARCHITECTURES}")
        if(NOT gopxl_apple_architectures)
            set(gopxl_apple_architectures "${gopxl_system_processor}")
        endif()
        foreach(gopxl_apple_architecture IN LISTS gopxl_apple_architectures)
            string(TOLOWER "${gopxl_apple_architecture}" gopxl_apple_architecture)
            if(NOT gopxl_apple_architecture MATCHES "^(arm64|aarch64)$")
                message(FATAL_ERROR
                    "[Gocator] The bundled GoPxL macOS libraries support Apple Silicon only; "
                    "set GOPXL_SDK_DIR and GOPXL_PLATFORM_DIR explicitly for another architecture."
                )
            endif()
        endforeach()
        set(gopxl_platform_debug_dir "apple_silicon")
        set(gopxl_platform_release_dir "apple_silicon")
    elseif(WIN32)
        set(gopxl_windows_architecture "${CMAKE_GENERATOR_PLATFORM}")
        if(NOT gopxl_windows_architecture)
            set(gopxl_windows_architecture "${gopxl_system_processor}")
        endif()
        string(TOLOWER "${gopxl_windows_architecture}" gopxl_windows_architecture)
        if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8
           OR NOT gopxl_windows_architecture MATCHES "^(amd64|x64|x86_64)$")
            message(FATAL_ERROR
                "[Gocator] The bundled GoPxL Windows libraries support x64 only; "
                "set GOPXL_SDK_DIR and GOPXL_PLATFORM_DIR explicitly for another architecture."
            )
        endif()
        set(gopxl_platform_debug_dir "win64d")
        set(gopxl_platform_release_dir "win64")
    elseif(UNIX)
        if(gopxl_system_processor MATCHES "^(aarch64|arm64)$")
            set(gopxl_platform_debug_dir "linux_arm64d")
            set(gopxl_platform_release_dir "linux_arm64")
        elseif(gopxl_system_processor MATCHES "^(amd64|x64|x86_64)$"
               AND CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(gopxl_platform_debug_dir "linux_x64d")
            set(gopxl_platform_release_dir "linux_x64")
        else()
            message(FATAL_ERROR
                "[Gocator] The bundled GoPxL Linux libraries support arm64 and x64 only; "
                "set GOPXL_SDK_DIR and GOPXL_PLATFORM_DIR explicitly for another architecture."
            )
        endif()
    else()
        message(FATAL_ERROR
            "[Gocator] Automatic GoPxL platform selection does not support this operating system; "
            "set GOPXL_SDK_DIR and GOPXL_PLATFORM_DIR explicitly."
        )
    endif()
endif()

if(CMAKE_CONFIGURATION_TYPES
   AND NOT gopxl_platform_debug_dir STREQUAL gopxl_platform_release_dir)
    set(gopxl_platform_dir
        "$<IF:$<CONFIG:Debug>,${gopxl_platform_debug_dir},${gopxl_platform_release_dir}>"
    )
else()
    string(TOLOWER "${CMAKE_BUILD_TYPE}" gopxl_build_type)
    if(gopxl_build_type STREQUAL "debug")
        set(gopxl_platform_dir "${gopxl_platform_debug_dir}")
    else()
        set(gopxl_platform_dir "${gopxl_platform_release_dir}")
    endif()
endif()

foreach(gopxl_required_platform_dir
        IN ITEMS "${gopxl_platform_debug_dir}" "${gopxl_platform_release_dir}")
    if(NOT IS_DIRECTORY "${GOPXL_SDK_DIR}/lib/${gopxl_required_platform_dir}")
        message(FATAL_ERROR
            "[Gocator] GoPxL library directory does not exist: "
            "${GOPXL_SDK_DIR}/lib/${gopxl_required_platform_dir}"
        )
    endif()
endforeach()

set_property(TARGET gocator_core PROPERTY
    GOCATOR_GOPXL_PLATFORM_DIR "${gopxl_platform_dir}")
set_property(TARGET gocator_core PROPERTY
    GOCATOR_GOPXL_PLATFORM_DIR_DEBUG "${gopxl_platform_debug_dir}")
set_property(TARGET gocator_core PROPERTY
    GOCATOR_GOPXL_PLATFORM_DIR_RELEASE "${gopxl_platform_release_dir}")
set_property(TARGET gocator_core PROPERTY
    GOCATOR_GOPXL_SDK_DIR "${GOPXL_SDK_DIR}")

target_link_directories(gocator_core
    PUBLIC
        "${GOPXL_SDK_DIR}/lib/${gopxl_platform_dir}"
)

target_link_libraries(gocator_core
    PUBLIC
        kApi
        GoApi
        GoPxLSdk
)

if(GOCATOR_BUILD_QT_UI)
    find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)

    add_library(GocatorQtWidget STATIC
        Utility/Qt/QGocatorWidget.cpp
        Utility/Qt/QGocatorWidget.h
    )
    add_library(Gocator::QtWidget ALIAS GocatorQtWidget)
    set_target_properties(GocatorQtWidget PROPERTIES AUTOMOC ON)
    target_compile_features(GocatorQtWidget PUBLIC cxx_std_17)
    target_compile_definitions(GocatorQtWidget PUBLIC GOCATOR_HAS_UI)
    target_link_libraries(GocatorQtWidget
        PUBLIC
            gocator_core
            Qt6::Widgets
            Qt6::Concurrent
    )
    message(STATUS "[Gocator] Gocator::QtWidget enabled.")
endif()

if(GOCATOR_BUILD_TESTS)
    if(NOT GOCATOR_BUILD_QT_UI)
        message(FATAL_ERROR "GOCATOR_BUILD_TESTS requires GOCATOR_BUILD_QT_UI=ON.")
    endif()

    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()

    add_executable(QGocatorWidgetTests
        tests/QGocatorWidgetTests.cpp
    )
    set_target_properties(QGocatorWidgetTests PROPERTIES
        AUTOMOC ON
        WIN32_EXECUTABLE FALSE
    )
    target_link_libraries(QGocatorWidgetTests
        PRIVATE
            Gocator::QtWidget
            Qt6::Test
    )

    if(WIN32)
        add_test(NAME QGocatorWidgetTests
            COMMAND ${CMAKE_COMMAND} -E env
                "PATH=${GOPXL_SDK_DIR}/lib/${gopxl_platform_dir};$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
                "QT_QPA_PLATFORM=offscreen"
                $<TARGET_FILE:QGocatorWidgetTests>
        )
    else()
        add_test(NAME QGocatorWidgetTests COMMAND QGocatorWidgetTests)
        set_tests_properties(QGocatorWidgetTests PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        )
    endif()
endif()

set(gocator_runtime_targets
    gocator_core
)

foreach(target ${gocator_runtime_targets})
    set_target_properties(${target} PROPERTIES
        BUILD_RPATH "${GOPXL_SDK_DIR}/lib/${gopxl_platform_dir}"
        INSTALL_RPATH "@executable_path/../GoPxL-SDK/lib/${gopxl_platform_dir}"
    )
endforeach()

if(GOCATOR_BUILD_GRAPHICSENGINE_ADAPTER)
    if(NOT TARGET GraphicsEngine::Contracts)
        message(FATAL_ERROR
            "[Gocator] GOCATOR_BUILD_GRAPHICSENGINE_ADAPTER=ON requires "
            "GraphicsEngine::Contracts. Add GraphicsEngine first or disable the adapter."
        )
    endif()

    find_package(OpenMP REQUIRED COMPONENTS CXX)
    add_library(GocatorGraphicsEngineAdapter STATIC
        Utility/GraphicsEngine/GocatorDataSetGraphicsFrameAdapter.h
        Utility/GraphicsEngine/GocatorDataSetGraphicsFrameAdapter.cpp
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
            GraphicsEngine::Contracts
        PRIVATE
            OpenMP::OpenMP_CXX
    )

    if(GOCATOR_BUILD_TESTS)
        add_executable(GocatorGraphicsFrameAdapterTests
            tests/GraphicsFrameAdapterTest.cpp
        )
        target_compile_features(GocatorGraphicsFrameAdapterTests PRIVATE cxx_std_17)
        target_link_libraries(GocatorGraphicsFrameAdapterTests
            PRIVATE Gocator::GraphicsEngineAdapter
        )
        if(WIN32)
            add_test(NAME GocatorGraphicsFrameAdapterTests
                COMMAND ${CMAKE_COMMAND} -E env
                    "PATH=${GOPXL_SDK_DIR}/lib/${gopxl_platform_dir};$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
                    $<TARGET_FILE:GocatorGraphicsFrameAdapterTests>)
        else()
            add_test(NAME GocatorGraphicsFrameAdapterTests
                COMMAND GocatorGraphicsFrameAdapterTests)
        endif()
    endif()

    message(STATUS "[Gocator] Gocator::GraphicsEngineAdapter enabled.")
endif()
