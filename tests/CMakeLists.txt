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
    # Task 3.3.4 — Final persistent_json with persistent_map_adapter / persistent_array_adapter
    test_persist_json_final.cpp
    # Task 3.5 — Integration tests
    test_phase3_integration.cpp
)

add_executable(tests ${TEST_SOURCES})
target_link_libraries(tests PRIVATE bindiff Catch2::Catch2WithMain)

# Provide the path to tests/test.json as a compile-time definition so that
# test_persist_json_final.cpp can find it regardless of the working directory.
target_compile_definitions(tests PRIVATE
    TEST_JSON_PATH="${CMAKE_CURRENT_SOURCE_DIR}/test.json"
)

include(CTest)
include(Catch)
catch_discover_tests(tests)
