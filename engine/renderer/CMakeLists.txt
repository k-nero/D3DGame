# Scene renderer: proxies, views, passes. Talks to the GPU only through
# engine_rhi handles and eng_rendergraph — portable by construction.
file(GLOB_RECURSE ENGINE_RENDERER_SRC CONFIGURE_DEPENDS
        src/*.cpp
        src/*.h
        include/*.h)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${ENGINE_RENDERER_SRC})
add_library(engine_renderer STATIC ${ENGINE_RENDERER_SRC})

target_include_directories(engine_renderer
        PUBLIC include
        PRIVATE src)

target_link_libraries(engine_renderer
        PUBLIC engine_rendergraph)
