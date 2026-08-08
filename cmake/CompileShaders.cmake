# eng_add_shader_target(<target> <shader_dir>)
#
# Compiles every *.hlsl under <shader_dir> with DXC into
# ${CMAKE_BINARY_DIR}/shaders/. Profile is chosen by filename suffix:
#   foo.vs.hlsl -> vs_6_6      foo.ps.hlsl -> ps_6_6      foo.cs.hlsl -> cs_6_6
#
# On Windows dxc.exe comes from the directx-dxc vcpkg package; on macOS
# install DXC via the Vulkan SDK or `brew install directx-shader-compiler`.
# Mac compiles are a syntax/semantic check only - PSO-level validation
# happens on the Windows machine.
function(eng_add_shader_target TARGET_NAME SHADER_DIR)
    find_program(ENG_DXC dxc
        PATHS "$ENV{VCPKG_ROOT}/installed/x64-windows/tools/directx-dxc"
              "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/tools/directx-dxc")

    if(NOT ENG_DXC)
        message(STATUS "dxc not found - shader target '${TARGET_NAME}' skipped")
        return()
    endif()

    file(GLOB_RECURSE ENG_SHADERS CONFIGURE_DEPENDS "${SHADER_DIR}/*.hlsl")
    if(NOT ENG_SHADERS)
        return()
    endif()

    set(ENG_SHADER_OUT_DIR "${CMAKE_BINARY_DIR}/shaders")
    file(MAKE_DIRECTORY "${ENG_SHADER_OUT_DIR}")

    set(ENG_SHADER_OUTPUTS "")
    foreach(SHADER ${ENG_SHADERS})
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

        set(OUT "${ENG_SHADER_OUT_DIR}/${NAME_WLE}.cso")
        add_custom_command(
            OUTPUT  ${OUT}
            COMMAND ${ENG_DXC} -T ${PROFILE} -E main
                    $<$<CONFIG:Debug>:-Zi> $<$<CONFIG:Debug>:-Qembed_debug>
                    -Fo ${OUT} ${SHADER}
            DEPENDS ${SHADER}
            COMMENT "DXC ${PROFILE}: ${NAME_WLE}")
        list(APPEND ENG_SHADER_OUTPUTS ${OUT})
    endforeach()

    if(ENG_SHADER_OUTPUTS)
        add_custom_target(${TARGET_NAME} DEPENDS ${ENG_SHADER_OUTPUTS})
    endif()
endfunction()
