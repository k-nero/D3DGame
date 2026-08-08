# Fully portable: compiles and unit-tests against the Null RHI on macOS.
file(GLOB_RECURSE ENG_RENDERGRAPH_SRC CONFIGURE_DEPENDS src/*.cpp)

add_library(eng_rendergraph STATIC ${ENG_RENDERGRAPH_SRC})

target_include_directories(eng_rendergraph
    PUBLIC  include
    PRIVATE src)

target_link_libraries(eng_rendergraph
    PUBLIC eng_rhi)
