
file(GLOB_RECURSE GAME_SRC CONFIGURE_DEPENDS
        src/*.cpp
        src/*.h
        include/*.h)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${GAME_SRC})
add_executable(game ${GAME_SRC})

target_link_libraries(game PRIVATE engine_app engine_common)

# Keep the console subsystem in dev builds so logs are visible.
# Switch to WIN32 (GUI subsystem) for release polish later.

if(WIN32)
    # ------------------------------------------------------------------
    # Runtime DLLs next to the exe: dxcompiler.dll (linked, so covered
    # by TARGET_RUNTIME_DLLS) — dxil.dll and D3D12Core.dll are NOT
    # linked and need explicit copies.
    # ------------------------------------------------------------------
    add_custom_command(TARGET game POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_RUNTIME_DLLS:game>" "$<TARGET_FILE_DIR:game>"
        COMMAND_EXPAND_LISTS)

    # dxil.dll ships beside the dxc tool in the vcpkg package.
    find_file(ENGINE_DXIL_DLL dxil.dll
        PATHS "$ENV{VCPKG_ROOT}/installed/x64-windows/tools/directx-dxc"
              "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/tools/directx-dxc")
    if(ENGINE_DXIL_DLL)
        add_custom_command(TARGET game POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ENGINE_DXIL_DLL}" "$<TARGET_FILE_DIR:game>")
    else()
        message(WARNING "dxil.dll not found - shaders will compile but fail signing at PSO creation")
    endif()

    # Agility SDK core runtime must live in a D3D12/ subfolder matching
    # the D3D12SDKPath export in your code.
    # NOTE: verify the imported target name against your port version.
    if(TARGET Microsoft::DirectX12-Core)
        add_custom_command(TARGET game POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:game>/D3D12"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Microsoft::DirectX12-Core>"
                "$<TARGET_FILE_DIR:game>/D3D12/")
        if(TARGET Microsoft::DirectX12-Layers)
            add_custom_command(TARGET game POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:Microsoft::DirectX12-Layers>"
                    "$<TARGET_FILE_DIR:game>/D3D12/")
        endif()
    endif()
endif()

# Offline shader compilation (see cmake/CompileShaders.cmake).
# Catches HLSL syntax errors on the Mac; real PSO validation is Windows work.
engine_add_shader_target(game_shaders "${CMAKE_SOURCE_DIR}/shaders")
if(TARGET game_shaders)
    add_dependencies(game game_shaders)
endif()
