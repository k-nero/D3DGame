find_package(doctest CONFIG REQUIRED)

file(GLOB ENGINE_TEST_SRC CONFIGURE_DEPENDS *.cpp)

add_executable(engine_tests ${ENGINE_TEST_SRC}
        test_rhi_null.cpp)
add_compile_definitions(DOCTEST_CONFIG_USE_STD_HEADERS)
# Tests link everything portable. On macOS this pulls the Null RHI,
# which is exactly the point: render-graph logic is testable without a GPU.
target_link_libraries(engine_tests PRIVATE
    doctest::doctest
    engine_scene          # pulls renderer -> rendergraph -> rhi -> core
    engine_assets
    engine_core
    engine_common)

include(doctest)
doctest_discover_tests(engine_tests)
