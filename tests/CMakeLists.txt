include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)

set(TEST_SOURCES
    test_bugs.cpp
    test_serialization.cpp
    test_hash.cpp
    test_object_store.cpp
    test_persist_std.cpp
    test_persistent_string.cpp
    test_persistent_array.cpp
    test_persistent_map.cpp
    test_persistent_json_value.cpp
    test_persistent_json_store.cpp
    test_persistent_json_store_integration.cpp
)

add_executable(tests ${TEST_SOURCES})
target_link_libraries(tests PRIVATE bindiff Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(tests)
