# Fully portable: compiles and unit-tests against the Null RHI on macOS.
file(GLOB_RECURSE ENGINE_RENDERGRAPH_SRC CONFIGURE_DEPENDS
        src/*.cpp
        src/*.h
        include/*.h)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${ENGINE_RENDERGRAPH_SRC})
add_library(engine_rendergraph STATIC ${ENGINE_RENDERGRAPH_SRC})

target_include_directories(engine_rendergraph
    PUBLIC  include
    PRIVATE src)

target_link_libraries(engine_rendergraph
    PUBLIC engine_rhi)
