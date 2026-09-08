# Gocator-owned Playground plugin runtime payload. The host copies
# PLAYGROUND_PLUGIN_RUNTIME_PAYLOAD_DIR into plugins/gocator/current/runtime.

function(gocator_prepare_playground_plugin_runtime target_name)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR
            "[Gocator] ${target_name} must exist before its plugin runtime is prepared.")
    endif()

    if(NOT TARGET gocator_core)
        message(FATAL_ERROR
            "[Gocator] gocator_core must exist before plugin runtime is prepared.")
    endif()

    get_target_property(gopxl_sdk_dir gocator_core GOCATOR_GOPXL_SDK_DIR)
    get_target_property(gopxl_debug_platform_dir gocator_core GOCATOR_GOPXL_PLATFORM_DIR_DEBUG)
    get_target_property(gopxl_release_platform_dir gocator_core GOCATOR_GOPXL_PLATFORM_DIR_RELEASE)
    if(NOT gopxl_sdk_dir OR gopxl_sdk_dir MATCHES "-NOTFOUND$")
        message(FATAL_ERROR
            "[Gocator] gocator_core did not publish GOCATOR_GOPXL_SDK_DIR.")
    endif()
    string(TOLOWER "${CMAKE_BUILD_TYPE}" gopxl_build_type)
    if(gopxl_build_type STREQUAL "debug")
        set(gopxl_payload_platform_dir "${gopxl_debug_platform_dir}")
    else()
        set(gopxl_payload_platform_dir "${gopxl_release_platform_dir}")
    endif()

    set(search_paths)
    set(host_bundle_files)
    set(payload_dir "")
    set(gopxl_sdk_lib_dir "${gopxl_sdk_dir}/lib/${gopxl_payload_platform_dir}")
    list(APPEND search_paths "${gopxl_sdk_lib_dir}")

    if(APPLE)
        set(gocator_macos_runtime_files
            "${gopxl_sdk_lib_dir}/libGoApi.dylib"
            "${gopxl_sdk_lib_dir}/libGoPxLSdk.dylib"
            "${gopxl_sdk_lib_dir}/libkApi.dylib"
        )
        foreach(gocator_runtime_file IN LISTS gocator_macos_runtime_files)
            if(NOT EXISTS "${gocator_runtime_file}")
                message(FATAL_ERROR
                    "[Gocator] Required GoPxL macOS runtime is missing: ${gocator_runtime_file}")
            endif()
            list(APPEND host_bundle_files "${gocator_runtime_file}")
        endforeach()
    endif()

    if(WIN32)
        set(payload_dir "${CMAKE_CURRENT_BINARY_DIR}/plugin-runtime")
        file(MAKE_DIRECTORY "${payload_dir}")
        foreach(gopxl_runtime_name GoApi.dll GoPxLSdk.dll kApi.dll)
            foreach(gopxl_required_platform_dir
                    IN ITEMS
                        "${gopxl_debug_platform_dir}"
                        "${gopxl_release_platform_dir}")
                set(gopxl_required_runtime_file
                    "${gopxl_sdk_dir}/lib/${gopxl_required_platform_dir}/${gopxl_runtime_name}")
                if(NOT EXISTS "${gopxl_required_runtime_file}")
                    message(FATAL_ERROR
                        "[Gocator] Required GoPxL runtime is missing: "
                        "${gopxl_required_runtime_file}")
                endif()
            endforeach()
            set(gopxl_runtime_file "${gopxl_sdk_lib_dir}/${gopxl_runtime_name}")
            file(COPY "${gopxl_runtime_file}" DESTINATION "${payload_dir}")
        endforeach()
        set_target_properties(${target_name} PROPERTIES
            PLAYGROUND_PLUGIN_RUNTIME_SEARCH_PATHS_DEBUG
                "${gopxl_sdk_dir}/lib/${gopxl_debug_platform_dir}"
            PLAYGROUND_PLUGIN_RUNTIME_SEARCH_PATHS_RELEASE
                "${gopxl_sdk_dir}/lib/${gopxl_release_platform_dir}"
        )
    endif()

    set_target_properties(${target_name} PROPERTIES
        PLAYGROUND_PLUGIN_RUNTIME_DEPENDENCY_DEST "runtime"
        PLAYGROUND_PLUGIN_RUNTIME_SEARCH_PATHS "${search_paths}"
        PLAYGROUND_PLUGIN_HOST_BUNDLE_FILES "${host_bundle_files}"
    )
    if(payload_dir)
        set_target_properties(${target_name} PROPERTIES
            PLAYGROUND_PLUGIN_RUNTIME_PAYLOAD_DIR "${payload_dir}"
        )
    endif()
endfunction()
