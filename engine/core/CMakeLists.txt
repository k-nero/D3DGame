find_package(directxmath CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS log)
find_package(Boost REQUIRED COMPONENTS log log_setup)

file(GLOB_RECURSE ENGINE_CORE_SRC CONFIGURE_DEPENDS
        src/*.cpp
        src/*.h
        include/*.h)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${ENGINE_CORE_SRC})
add_library(engine_core STATIC ${ENGINE_CORE_SRC})

target_include_directories(engine_core PUBLIC include        # eng/core/*.h — the public surface
    PRIVATE src)

target_link_libraries(engine_core PUBLIC Microsoft::DirectXMath engine_common)
target_link_libraries(engine_core PRIVATE Boost::log Boost::log_setup)
# core must never see a platform graphics header. Nothing to enforce
# mechanically here — the enforcement is that no d3d12/dxgi/Metal lib
# is linked and no such include path is visible.
