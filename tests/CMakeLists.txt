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
    test_pam_metrics.cpp
    test_pvector.cpp
    test_pam_perf.cpp
    test_pjson.cpp
    test_pjson_large.cpp
    test_pjson_serial.cpp
    test_pjson_opt.cpp
    test_pjson_bench.cpp
    test_pjson_node.cpp
    test_pjson_codec.cpp
    test_pjson_db_perf.cpp
    test_pjson_db_perf_large.cpp
    test_pjson_db_errors.cpp
    test_pjson_clone.cpp
    test_pmm_basic.cpp
    test_pmem_array_pmm.cpp
    test_pmap_pmm.cpp
    test_pstring_pmm.cpp
    test_pstringview_pmm.cpp
    test_pjson_pool_pmm.cpp
    test_pam_pmm.cpp
    test_fptr_pmm.cpp
    test_pallocator_pmm.cpp
    test_pjson_db_pmm.cpp
    test_pjson_db_batch.cpp
    test_regression_pmm_types.cpp
    test_compat_save_load.cpp
    test_pjson_ref_stability.cpp
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
