# Application base class + platform window/input/timer layer.
# The single umbrella target the game links.
find_package(imgui CONFIG REQUIRED)

file(GLOB ENG_APP_SRC CONFIGURE_DEPENDS src/*.cpp)

add_library(eng_app STATIC ${ENG_APP_SRC})

if(WIN32)
    file(GLOB ENG_APP_WIN32 CONFIGURE_DEPENDS src/win32/*.cpp)
    target_sources(eng_app PRIVATE ${ENG_APP_WIN32})
elseif(APPLE)
    file(GLOB ENG_APP_MACOS CONFIGURE_DEPENDS src/macos/*.mm src/macos/*.cpp)
    target_sources(eng_app PRIVATE ${ENG_APP_MACOS})
endif()

target_include_directories(eng_app
    PUBLIC  include
    PRIVATE src)

target_link_libraries(eng_app
    PUBLIC
        eng_scene
        eng_assets
    PRIVATE
        imgui::imgui)

if(APPLE)
    target_link_libraries(eng_app PRIVATE "-framework Cocoa" "-framework QuartzCore")
endif()
