# Foundation layer: assert/log/arena/pool/refcount/object/math.
# Shared properties (std level, warnings, ENGINE_BUILDING, visibility, PIC)
# are applied centrally in engine/CMakeLists.txt — do not repeat them here.
add_library(engine_core OBJECT)

file(GLOB_RECURSE ENGINE_CORE_SRC CONFIGURE_DEPENDS
        src/*.cpp src/*.h
        include/*.h)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${ENGINE_CORE_SRC})
target_sources(engine_core PRIVATE ${ENGINE_CORE_SRC})

target_include_directories(engine_core
        PUBLIC  include
        PRIVATE src)

# DirectXMath and Boost::log are PUBLIC because core/math.h and core/log.h
# include their headers; Boost::log_setup is only used by log.cpp.
target_link_libraries(engine_core
        PUBLIC  Microsoft::DirectXMath Boost::log
        PRIVATE Boost::log_setup)
