include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)

set(TEST_SOURCES
    test_pam.cpp
    test_persist.cpp
    test_pstring.cpp
    test_pvector.cpp
    test_pmap.cpp
    test_pallocator.cpp
    test_pjson.cpp
)

add_executable(tests ${TEST_SOURCES})
target_link_libraries(tests PRIVATE bindiff Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(tests)
