# ===========================================================================
# engine_stage_runtime_dependencies(<exe-target>)
#
# Puts everything the engine needs at runtime next to <exe-target>, and links
# the bits that must be compiled per-executable. No-op off Windows.
#
# This exists so that a consumer does not have to know that D3D12Core.dll goes
# into a D3D12/ subfolder, that dxil.dll is not a link-time dependency, or that
# the Agility exports get dead-stripped out of a static library. Those are
# engine implementation details; leaking them into every consumer's
# CMakeLists is how a "library" stops being one.
# ===========================================================================
function(engine_stage_runtime_dependencies TARGET_NAME)
    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR
                "engine_stage_runtime_dependencies: '${TARGET_NAME}' is not a target")
    endif()
    if(NOT WIN32)
        return()
    endif()

    # D3D12SDKVersion/D3D12SDKPath must be exported by the EXECUTABLE. A static
    # library dead-strips the object because nothing references it, so the
    # source is carried on an INTERFACE target and compiled into each exe.
    if(TARGET engine_agility_exports)
        target_link_libraries(${TARGET_NAME} PRIVATE engine_agility_exports)
    endif()

    # dxcompiler.dll IS a link-time dependency, so TARGET_RUNTIME_DLLS finds it.
    # (find_package(directx-dxc REQUIRED) in rhi/ guarantees the list is
    # non-empty on Windows; an empty list would make this command malformed.)
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_RUNTIME_DLLS:${TARGET_NAME}>"
                    "$<TARGET_FILE_DIR:${TARGET_NAME}>"
            COMMAND_EXPAND_LISTS
            COMMENT "Staging runtime DLLs for ${TARGET_NAME}")

    # dxil.dll is NOT linked — it ships beside the dxc tool and is loaded at
    # runtime to sign the compiled shader. Without it, DXC compiles fine and
    # PSO creation then rejects the unsigned bytecode.
    find_file(ENGINE_DXIL_DLL dxil.dll
            PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/directx-dxc"
                  "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/tools/directx-dxc"
                  "$ENV{VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/tools/directx-dxc"
                  "$ENV{VCPKG_ROOT}/installed/x64-windows/tools/directx-dxc"
            NO_DEFAULT_PATH)
    if(ENGINE_DXIL_DLL)
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${ENGINE_DXIL_DLL}" "$<TARGET_FILE_DIR:${TARGET_NAME}>"
                COMMENT "Staging dxil.dll for ${TARGET_NAME}")
    else()
        message(WARNING
                "dxil.dll not found - shaders will compile but fail signing at PSO creation")
    endif()

    # Agility redistributables must live in a D3D12/ subfolder next to the exe;
    # D3D12SDKPath in d3d12_agility.cpp points there. make_directory runs at
    # build time, not configure time, so it lands in the right per-config dir
    # under a multi-config generator.
    if(TARGET Microsoft::DirectX12-Core)
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                        "$<TARGET_FILE_DIR:${TARGET_NAME}>/D3D12"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "$<TARGET_PROPERTY:Microsoft::DirectX12-Core,IMPORTED_LOCATION_RELEASE>"
                        "$<TARGET_FILE_DIR:${TARGET_NAME}>/D3D12"
                COMMAND_EXPAND_LISTS
                COMMENT "Staging D3D12Core.dll for ${TARGET_NAME}")
    else()
        message(WARNING "Microsoft::DirectX12-Core not found - D3D12Core.dll will be missing")
    endif()

    # D3D12SDKLayers.dll only exists in the debug redistributable.
    if(TARGET Microsoft::DirectX12-Layers)
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "$<TARGET_PROPERTY:Microsoft::DirectX12-Layers,IMPORTED_LOCATION_DEBUG>"
                        "$<TARGET_FILE_DIR:${TARGET_NAME}>/D3D12"
                COMMAND_EXPAND_LISTS
                COMMENT "Staging D3D12SDKLayers.dll for ${TARGET_NAME}")
    endif()
endfunction()
