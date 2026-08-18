# Application base class + platform window/input/timer layer. The engine owns
# the loop: a consumer derives from engine::app::Application and overrides
# on_frame(). This is the topmost engine layer.
add_library(engine_app OBJECT)

find_package(imgui CONFIG REQUIRED)   # private: no engine public header includes imgui

file(GLOB ENGINE_APP_SRC CONFIGURE_DEPENDS src/*.cpp src/*.h)
file(GLOB_RECURSE ENGINE_APP_PUBLIC_HEADERS CONFIGURE_DEPENDS include/*.h)
list(APPEND ENGINE_APP_SRC ${ENGINE_APP_PUBLIC_HEADERS})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${ENGINE_APP_SRC})
target_sources(engine_app PRIVATE ${ENGINE_APP_SRC})

# Platform window layer. Plain GLOB inside the platform block, same reason as
# in rhi/: a recursive glob at the top would compile every platform at once.
if (WIN32)
    file(GLOB ENGINE_APP_WIN32 CONFIGURE_DEPENDS src/win32/*.cpp src/win32/*.h)
    target_sources(engine_app PRIVATE ${ENGINE_APP_WIN32})
elseif (APPLE)
    file(GLOB ENGINE_APP_MACOS CONFIGURE_DEPENDS src/macos/*.mm src/macos/*.cpp src/macos/*.h)
    set_source_files_properties(src/macos/app_macos.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_sources(engine_app PRIVATE ${ENGINE_APP_MACOS})
endif ()

target_include_directories(engine_app
        PUBLIC  include
        PRIVATE src)

target_link_libraries(engine_app
        PUBLIC  engine_scene engine_assets
        PRIVATE imgui::imgui)

if (APPLE)
    target_link_libraries(engine_app PRIVATE "-framework Cocoa" "-framework QuartzCore")
endif ()
