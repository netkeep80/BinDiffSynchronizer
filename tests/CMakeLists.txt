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
    test_commit.cpp
    # Task 3.1 — persistence infrastructure tests
    test_persist_core.cpp
    test_fptr.cpp
    test_address_manager.cpp
    test_cache.cpp
    test_memory_device.cpp
    test_static_page_device.cpp
    # Task 3.2 — persist<T>/fptr<T> with jgit types
    test_persist_persistent_string.cpp
    test_persist_persistent_map.cpp
    test_persist_persistent_json_value.cpp
    test_persistent_json_store_v2.cpp
    # Task 3.3 — Instantiate nlohmann::basic_json<> with persistent classes
    test_persistent_string_extended.cpp
    test_persist_json.cpp
)

add_executable(tests ${TEST_SOURCES})
target_link_libraries(tests PRIVATE bindiff Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(tests)
