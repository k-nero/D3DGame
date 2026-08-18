# Scene renderer: proxies, views, passes. Talks to the GPU only through
# engine_rhi handles and engine_rendergraph — portable by construction.
add_library(engine_renderer OBJECT)

file(GLOB_RECURSE ENGINE_RENDERER_SRC CONFIGURE_DEPENDS
        src/*.cpp src/*.h
        include/*.h)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${ENGINE_RENDERER_SRC})
target_sources(engine_renderer PRIVATE ${ENGINE_RENDERER_SRC})

if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_include_directories(engine_renderer PUBLIC include)
endif()
target_include_directories(engine_renderer PRIVATE src)

target_link_libraries(engine_renderer PUBLIC engine_rendergraph)
