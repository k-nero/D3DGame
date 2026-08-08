find_package(doctest CONFIG REQUIRED)

file(GLOB ENG_TEST_SRC CONFIGURE_DEPENDS *.cpp)

add_executable(eng_tests ${ENG_TEST_SRC})

# Tests link everything portable. On macOS this pulls the Null RHI,
# which is exactly the point: render-graph logic is testable without a GPU.
target_link_libraries(eng_tests PRIVATE
    doctest::doctest
    eng_scene          # pulls renderer -> rendergraph -> rhi -> core
    eng_assets
    eng_common)

include(doctest)
doctest_discover_tests(eng_tests)
