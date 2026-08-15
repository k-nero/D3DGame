# Application base class + platform window/input/timer layer.
# The single umbrella target the game links.
find_package(imgui CONFIG REQUIRED)

file(GLOB_RECURSE ENGINE_APP_SRC CONFIGURE_DEPENDS
        src/*.cpp
        src/*.h
        include/*.h)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${ENGINE_APP_SRC})
add_library(engine_app STATIC ${ENGINE_APP_SRC})

if (WIN32)
    file(GLOB ENGINE_APP_WIN32 CONFIGURE_DEPENDS src/win32/*.cpp)
    target_sources(engine_app PRIVATE ${ENGINE_APP_WIN32})
elseif (APPLE)
    file(GLOB ENGINE_APP_MACOS CONFIGURE_DEPENDS src/macos/*.mm src/macos/*.cpp)
    target_sources(engine_app PRIVATE ${ENGINE_APP_MACOS})
endif ()

target_include_directories(engine_app
        PUBLIC include
        PRIVATE src)

target_link_libraries(engine_app
        PUBLIC
        engine_scene
        engine_assets
        PRIVATE
        imgui::imgui)

if (APPLE)
    target_link_libraries(engine_app PRIVATE "-framework Cocoa" "-framework QuartzCore")
endif ()
