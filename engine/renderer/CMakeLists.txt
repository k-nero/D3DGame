# Scene renderer: proxies, views, passes. Talks to the GPU only through
# eng_rhi handles and eng_rendergraph — portable by construction.
file(GLOB_RECURSE ENG_RENDERER_SRC CONFIGURE_DEPENDS
        src/*.cpp
        src/*.h
        include/*.h)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${ENG_RENDERER_SRC})
add_library(eng_renderer STATIC ${ENG_RENDERER_SRC})

target_include_directories(eng_renderer
    PUBLIC  include
    PRIVATE src)

target_link_libraries(eng_renderer
    PUBLIC eng_rendergraph)
