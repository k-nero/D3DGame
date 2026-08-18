# Parsing + refcounted registry. Pure file -> CPU structs; GPU upload happens
# renderer-side, so this module stays portable and mac-testable.
add_library(engine_assets OBJECT)

# All private: none of these appear in an engine public header.
find_package(nlohmann_json CONFIG REQUIRED)
find_path(STB_INCLUDE_DIRS "stb_image.h" REQUIRED)
find_path(CGLTF_INCLUDE_DIRS "cgltf.h" REQUIRED)

file(GLOB_RECURSE ENGINE_ASSETS_SRC CONFIGURE_DEPENDS
        src/*.cpp src/*.h
        include/*.h)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${ENGINE_ASSETS_SRC})
target_sources(engine_assets PRIVATE ${ENGINE_ASSETS_SRC})

if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_include_directories(engine_assets PUBLIC include)
endif()
target_include_directories(engine_assets
        PRIVATE src ${STB_INCLUDE_DIRS} ${CGLTF_INCLUDE_DIRS})

target_link_libraries(engine_assets
        PUBLIC  engine_core
        PRIVATE nlohmann_json::nlohmann_json)
