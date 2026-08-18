# Pass declaration, resource lifetime, barrier generation, transient aliasing.
# Fully portable: no platform types, so it is unit-testable without a GPU.
add_library(engine_rendergraph OBJECT)

file(GLOB_RECURSE ENGINE_RENDERGRAPH_SRC CONFIGURE_DEPENDS
        src/*.cpp src/*.h
        include/*.h)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${ENGINE_RENDERGRAPH_SRC})
target_sources(engine_rendergraph PRIVATE ${ENGINE_RENDERGRAPH_SRC})

# include/ does not exist until this module grows public headers; naming a
# missing directory here would end up in the facade's interface.
if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_include_directories(engine_rendergraph PUBLIC include)
endif()
target_include_directories(engine_rendergraph PRIVATE src)

target_link_libraries(engine_rendergraph PUBLIC engine_rhi)
