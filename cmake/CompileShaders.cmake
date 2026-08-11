# engine_add_shader_target(<target> <shader_dir>)
#
# Compiles every *.hlsl under <shader_dir> with DXC into
# ${CMAKE_BINARY_DIR}/shaders/. Profile is chosen by filename suffix:
#   foo.vs.hlsl -> vs_6_6      foo.ps.hlsl -> ps_6_6      foo.cs.hlsl -> cs_6_6
#
# On Windows dxc.exe comes from the directx-dxc vcpkg package; on macOS
# install DXC via the Vulkan SDK or `brew install directx-shader-compiler`.
# Mac compiles are a syntax/semantic check only - PSO-level validation
# happens on the Windows machine.
function(engine_add_shader_target TARGET_NAME SHADER_DIR)
    find_program(ENGINE_DXC dxc
        PATHS "$ENV{VCPKG_ROOT}/installed/x64-windows/tools/directx-dxc"
              "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/tools/directx-dxc")

    if(NOT ENGINE_DXC)
        message(STATUS "dxc not found - shader target '${TARGET_NAME}' skipped")
        return()
    endif()

    file(GLOB_RECURSE ENGINE_SHADERS CONFIGURE_DEPENDS "${SHADER_DIR}/*.hlsl")
    if(NOT ENGINE_SHADERS)
        return()
    endif()

    set(ENGINE_SHADER_OUT_DIR "${CMAKE_BINARY_DIR}/shaders")
    file(MAKE_DIRECTORY "${ENGINE_SHADER_OUT_DIR}")

    set(ENGINE_SHADER_OUTPUTS "")
    foreach(SHADER ${ENGINE_SHADERS})
        get_filename_component(NAME_WLE ${SHADER} NAME_WLE)   # strips .hlsl
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

        set(OUT "${ENGINE_SHADER_OUT_DIR}/${NAME_WLE}.cso")
        add_custom_command(
            OUTPUT  ${OUT}
            COMMAND ${ENGINE_DXC} -T ${PROFILE} -E main
                    $<$<CONFIG:Debug>:-Zi> $<$<CONFIG:Debug>:-Qembed_debug>
                    -Fo ${OUT} ${SHADER}
            DEPENDS ${SHADER}
            COMMENT "DXC ${PROFILE}: ${NAME_WLE}")
        list(APPEND ENGINE_SHADER_OUTPUTS ${OUT})
    endforeach()

    if(ENGINE_SHADER_OUTPUTS)
        add_custom_target(${TARGET_NAME} DEPENDS ${ENGINE_SHADER_OUTPUTS})
    endif()
endfunction()
