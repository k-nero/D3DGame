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

# ===========================================================================
# engine_install_runtime_dependencies(<exe-target> <dest> <component>)
#
# The install-time twin of engine_stage_runtime_dependencies(). That one does
# POST_BUILD copies into the build tree so hitting F5 works; this one puts the
# same files into the package. Both are needed and neither implies the other:
# CPack never looks at the build directory, it re-runs the install rules into
# a staging prefix of its own.
#
# Same contract as its twin — the consumer says one line and still does not
# learn that D3D12Core.dll lives in a D3D12/ subfolder. No-op off Windows.
# ===========================================================================
function(engine_install_runtime_dependencies TARGET_NAME DEST COMPONENT)
    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR
                "engine_install_runtime_dependencies: '${TARGET_NAME}' is not a target")
    endif()

    # ------------------------------------------------------------------
    # Non-Windows: the only non-system shared library the engine drags in is
    # the Vulkan loader, which IS a link-time dependency, so the executable
    # carries @rpath/libvulkan.<ver>.dylib (a DT_NEEDED soname on Linux).
    # Boost is static in this vcpkg triplet and contributes nothing here.
    #
    # This must be handled or the package does not launch AT ALL. CMake strips
    # the build-tree RPATH on install, by design - the build tree's absolute
    # paths are meaningless in a package. What that leaves is an executable
    # with an @rpath dependency and NO LC_RPATH, and dyld's answer to that is
    # "Library not loaded ... Reason: no LC_RPATH's found", before a single
    # line of our code runs. Verified against a real package, not theorised.
    #
    # So: ship the loader next to the executable, and give the INSTALLED binary
    # an RPATH that says "look beside me". BUILD_RPATH is untouched, so the
    # normal dev loop keeps resolving out of vcpkg_installed exactly as before.
    # ------------------------------------------------------------------
    if(NOT WIN32)
        if(APPLE AND Vulkan_LIBRARY)
            # REAL_PATH, not Vulkan_LIBRARY itself. That variable names
            # libvulkan.dylib, the head of a two-link symlink chain, and
            # install(FILES) copies a symlink's CONTENT under the name it was
            # given - so the loader would land as "libvulkan.dylib" while the
            # executable asks dyld for "@rpath/libvulkan.1.4.350.dylib" and
            # still fails. Resolving first installs it under its real,
            # versioned name, which is exactly the name in the load command.
            #
            # install(IMPORTED_RUNTIME_ARTIFACTS Vulkan::Vulkan) would be the
            # tidier spelling and does NOT work here: FindVulkan declares the
            # target UNKNOWN IMPORTED, and that command takes only SHARED,
            # MODULE and executable targets.
            file(REAL_PATH "${Vulkan_LIBRARY}" ENGINE_VULKAN_LOADER)
            install(FILES "${ENGINE_VULKAN_LOADER}"
                    DESTINATION ${DEST}
                    COMPONENT ${COMPONENT})
            set_property(TARGET ${TARGET_NAME}
                    PROPERTY INSTALL_RPATH "@executable_path")
        elseif(NOT APPLE)
            # Linux would additionally need the SONAME symlink recreated
            # (DT_NEEDED says libvulkan.so.1, the real file is
            # libvulkan.so.1.x.y), and there is no window layer there yet to
            # package anything for - SDL3 at m8. Deliberately not guessed at.
            message(STATUS
                    "engine_install_runtime_dependencies: Vulkan loader staging is "
                    "macOS-only so far - a packaged '${TARGET_NAME}' will not run on Linux")
        endif()

        # NOT staged, and this is the known gap: on macOS the Vulkan DRIVER is
        # the vendored MoltenVK, reached through an ICD manifest whose absolute
        # source-tree path is compiled into engine_rhi (ENGINE_VK_ICD_FILE - see
        # engine/rhi/CMakeLists.txt). A packaged build on someone else's machine
        # therefore finds no Vulkan driver, and Backend::Vulkan fails there;
        # Metal is unaffected. Closing this needs the dylib and manifest staged
        # beside the executable AND an executable-directory primitive the engine
        # does not have yet - a runtime change, not a packaging one. Called out
        # here rather than papered over.
        return()
    endif()

    # $<TARGET_RUNTIME_DLLS:> is valid in install(FILES) since CMake 3.21; our
    # floor is 3.25. The list is guaranteed non-empty because rhi/ does
    # find_package(directx-dxc REQUIRED) on Windows — an empty genex here would
    # make the install rule malformed rather than simply do nothing.
    install(FILES $<TARGET_RUNTIME_DLLS:${TARGET_NAME}>
            DESTINATION ${DEST}
            COMPONENT ${COMPONENT})

    # ENGINE_DXIL_DLL is the find_file() cache variable set by
    # engine_stage_runtime_dependencies(). Reading it here means the two
    # functions must not be reordered relative to each other... except that it
    # is a CACHE entry, so it survives into this scope regardless of call order
    # within a configure. Still: the staging call is the one that defines it.
    if(ENGINE_DXIL_DLL)
        install(FILES ${ENGINE_DXIL_DLL}
                DESTINATION ${DEST}
                COMPONENT ${COMPONENT})
    endif()

    # RELEASE redistributable only, deliberately. The debug pair
    # (D3D12SDKLayers.dll) is a development artifact: shipping it invites the
    # debug layer to load on a user's machine, which is a large slowdown and a
    # dependency on a DLL they have no reason to have.
    if(TARGET Microsoft::DirectX12-Core)
        install(FILES $<TARGET_PROPERTY:Microsoft::DirectX12-Core,IMPORTED_LOCATION_RELEASE>
                DESTINATION ${DEST}/D3D12
                COMPONENT ${COMPONENT})
    else()
        message(WARNING
                "Microsoft::DirectX12-Core not found - packaged builds will be missing D3D12Core.dll")
    endif()
endfunction()
