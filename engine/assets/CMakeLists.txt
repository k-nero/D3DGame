# Parsing + refcounted registry. Pure file -> CPU structs; GPU upload
# happens renderer-side, so this module stays portable and mac-testable.
find_package(nlohmann_json CONFIG REQUIRED)
find_path(STB_INCLUDE_DIRS   "stb_image.h" REQUIRED)
find_path(CGLTF_INCLUDE_DIRS "cgltf.h"     REQUIRED)

file(GLOB_RECURSE ENG_ASSETS_SRC CONFIGURE_DEPENDS src/*.cpp)

add_library(eng_assets STATIC ${ENG_ASSETS_SRC})

target_include_directories(eng_assets
    PUBLIC  include
    PRIVATE src ${STB_INCLUDE_DIRS} ${CGLTF_INCLUDE_DIRS})

target_link_libraries(eng_assets
    PUBLIC  eng_core
    PRIVATE nlohmann_json::nlohmann_json)
