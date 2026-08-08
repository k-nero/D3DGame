# World/Actor/Component, tick groups, transform hierarchy.
# Links eng_renderer because PrimitiveComponent owns its render proxy.
file(GLOB_RECURSE ENG_SCENE_SRC CONFIGURE_DEPENDS src/*.cpp)

add_library(eng_scene STATIC ${ENG_SCENE_SRC})

target_include_directories(eng_scene
    PUBLIC  include
    PRIVATE src)

target_link_libraries(eng_scene
    PUBLIC eng_renderer)
