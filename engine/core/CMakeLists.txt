find_package(directxmath CONFIG REQUIRED)

file(GLOB_RECURSE ENG_CORE_SRC CONFIGURE_DEPENDS src/*.cpp)

add_library(eng_core STATIC ${ENG_CORE_SRC})

target_include_directories(eng_core
    PUBLIC  include        # eng/core/*.h — the public surface
    PRIVATE src)

target_link_libraries(eng_core
    PUBLIC
        Microsoft::DirectXMath
        eng_common)

# core must never see a platform graphics header. Nothing to enforce
# mechanically here — the enforcement is that no d3d12/dxgi/Metal lib
# is linked and no such include path is visible.
