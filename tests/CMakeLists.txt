include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)

set(TEST_SOURCES
    test_pam.cpp
    test_pam_dynamic.cpp
    test_persist.cpp
    test_pstring.cpp
    test_pvector.cpp
    test_pmap.cpp
    test_pallocator.cpp
    test_pjson.cpp
    test_pjson_large.cpp
    test_pjson_serial.cpp
)

add_executable(tests ${TEST_SOURCES})
target_link_libraries(tests PRIVATE bindiff Catch2::Catch2WithMain)

# Path to the large test JSON file (absolute path from sources)
target_compile_definitions(tests PRIVATE
    TEST_JSON_PATH="${CMAKE_CURRENT_SOURCE_DIR}/test.json"
)

include(CTest)
include(Catch)
catch_discover_tests(tests)
