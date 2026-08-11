# World/Actor/Component, tick groups, transform hierarchy.
# Links engine_renderer because PrimitiveComponent owns its render proxy.
file(GLOB_RECURSE ENGINE_SCENE_SRC CONFIGURE_DEPENDS
        src/*.cpp
        src/*.h
        include/*.h)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${ENGINE_SCENE_SRC})
add_library(engine_scene STATIC ${ENGINE_SCENE_SRC})

target_include_directories(engine_scene
    PUBLIC  include
    PRIVATE src)

target_link_libraries(engine_scene
    PUBLIC engine_renderer)
