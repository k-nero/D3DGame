# ===========================================================================
# engine_add_shader_target(<target> <shader_dir>)
#
# Compiles every *.hlsl under <shader_dir> with DXC into
# ${ENGINE_SHADER_OUTPUT_DIR}. Profile comes from the filename suffix:
#   foo.vs.hlsl -> vs_6_6   foo.ps.hlsl -> ps_6_6   foo.cs.hlsl -> cs_6_6
#
# If dxc is not installed the target is simply not created (the caller guards
# with if(TARGET ...)): Mac compiles are a syntax/semantic check, and losing
# them must not fail the build. Real PSO-level validation is Windows work.
#
# On Windows dxc.exe comes from the directx-dxc vcpkg package; on macOS install
# it via the Vulkan SDK or `brew install directx-shader-compiler`.
# ===========================================================================

# Where .cso files land. A cache variable so a consumer embedding the engine can
# redirect it, and so the runtime side has one name to agree with.
set(ENGINE_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders"
        CACHE PATH "Directory compiled shader bytecode is written to")

function(engine_add_shader_target TARGET_NAME SHADER_DIR)
    # Search vcpkg's tools dir for the ACTIVE triplet first, then the historical
    # x64-windows hard-code, then the system PATH (brew / Vulkan SDK on macOS).
    # The old version only ever looked in x64-windows, so it never found dxc on
    # a Mac and silently skipped every shader.
    find_program(ENGINE_DXC
            NAMES dxc dxc.exe
            HINTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/directx-dxc"
                  "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/tools/directx-dxc"
                  "$ENV{VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/tools/directx-dxc"
                  "$ENV{VCPKG_ROOT}/installed/x64-windows/tools/directx-dxc"
                  "$ENV{VULKAN_SDK}/bin"
            DOC "DirectX Shader Compiler executable")

    if(NOT ENGINE_DXC)
        message(STATUS "dxc not found - shader target '${TARGET_NAME}' skipped")
        return()
    endif()

    file(GLOB_RECURSE ENGINE_SHADERS CONFIGURE_DEPENDS "${SHADER_DIR}/*.hlsl")
    if(NOT ENGINE_SHADERS)
        message(STATUS "no .hlsl files under ${SHADER_DIR} - shader target '${TARGET_NAME}' skipped")
        return()
    endif()

    set(ENGINE_SHADER_OUTPUTS "")
    foreach(SHADER IN LISTS ENGINE_SHADERS)
        get_filename_component(NAME_WLE ${SHADER} NAME_WLE)   # strips .hlsl only
        if(NAME_WLE MATCHES "\\.vs$")
            set(PROFILE vs_6_6)
        elseif(NAME_WLE MATCHES "\\.ps$")
            set(PROFILE ps_6_6)
        elseif(NAME_WLE MATCHES "\\.cs$")
            set(PROFILE cs_6_6)
        else()
            message(WARNING "Shader ${SHADER} has no .vs/.ps/.cs suffix - skipped")
            continue()
        endif()

        set(OUT "${ENGINE_SHADER_OUTPUT_DIR}/${NAME_WLE}.cso")
        add_custom_command(
                OUTPUT  ${OUT}
                # make_directory here, not file(MAKE_DIRECTORY) at configure
                # time: the tree survives a `rm -rf` of the build dir mid-session.
                COMMAND ${CMAKE_COMMAND} -E make_directory "${ENGINE_SHADER_OUTPUT_DIR}"
                COMMAND ${ENGINE_DXC} -T ${PROFILE} -E main
                        $<$<CONFIG:Debug>:-Zi> $<$<CONFIG:Debug>:-Qembed_debug>
                        -Fo ${OUT} ${SHADER}
                DEPENDS ${SHADER}
                COMMENT "DXC ${PROFILE}: ${NAME_WLE}"
                VERBATIM)
        list(APPEND ENGINE_SHADER_OUTPUTS ${OUT})
    endforeach()

    if(ENGINE_SHADER_OUTPUTS)
        add_custom_target(${TARGET_NAME} DEPENDS ${ENGINE_SHADER_OUTPUTS})
    endif()
endfunction()
