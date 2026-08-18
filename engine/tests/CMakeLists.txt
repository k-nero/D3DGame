# The engine's own tests. They link engine::engine, i.e. the same target a
# third-party consumer links — so a public header that is missing from the
# facade's interface breaks the tests, not just some future user's build.
find_package(doctest CONFIG REQUIRED)

file(GLOB ENGINE_TEST_SRC CONFIGURE_DEPENDS *.cpp)
add_executable(engine_tests ${ENGINE_TEST_SRC})

# Target-scoped, not add_compile_definitions(): the directory-scope form leaks
# into every target added under this directory afterwards.
target_compile_definitions(engine_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)

target_link_libraries(engine_tests PRIVATE
        doctest::doctest
        engine::engine
        engine_warnings)   # our own code, so warn on it

include(doctest)
doctest_discover_tests(engine_tests)
