#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <type_traits>

#include "persist.h"

// =============================================================================
// Task 3.1.1 — Tests for persist<_T> core class
// =============================================================================
//
// Scope: persist<T> saves/loads a trivially-copyable T to/from a named file.
// Tests verify correct round-trip persistence, operator=, operator _Tref,
// the static_assert guard, and that the file size equals sizeof(T).
// =============================================================================

namespace {
    // Helper: generate a unique temp filename for each test so tests do not
    // interfere with each other.
    std::string tmp_name(const char* tag)
    {
        return std::string("./test_persist_") + tag + ".tmp";
    }

    // Remove a file if it exists (cleanup helper).
    void rm(const std::string& path)
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
} // anonymous namespace

// ---------------------------------------------------------------------------
// 3.1.1.1 — save/load persist<int> across construction/destruction cycles
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.1.1.1: persist<int> saves on destruction, loads on construction",
          "[task3.1][persist_core]")
{
    std::string fname = tmp_name("int_roundtrip");
    rm(fname);

    // Phase 1: create, write value, destroy → file written
    {
        persist<int> p(fname);
        p = 12345;
    }

    // Phase 2: recreate with same name, read back
    {
        persist<int> p(fname);
        int val = static_cast<int>(p);
        REQUIRE(val == 12345);
    }

    rm(fname);
}

// ---------------------------------------------------------------------------
// 3.1.1.2 — save/load persist<double> across cycles
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.1.1.2: persist<double> saves and loads correctly",
          "[task3.1][persist_core]")
{
    std::string fname = tmp_name("double_roundtrip");
    rm(fname);

    {
        persist<double> p(fname);
        p = 3.14159265358979;
    }
    {
        persist<double> p(fname);
        double val = static_cast<double>(p);
        REQUIRE(val == 3.14159265358979);
    }

    rm(fname);
}

// ---------------------------------------------------------------------------
// 3.1.1.3 — save/load persist<T> for a user-defined POD struct
// ---------------------------------------------------------------------------
namespace {
    struct Point { int x; int y; };
} // anonymous namespace

TEST_CASE("Task 3.1.1.3: persist<POD struct> saves and loads all fields",
          "[task3.1][persist_core]")
{
    static_assert(std::is_trivially_copyable<Point>::value,
                  "Point must be trivially copyable for this test");

    std::string fname = tmp_name("pod_struct_roundtrip");
    rm(fname);

    {
        persist<Point> p(fname);
        Point pt = p;
        pt.x = 42;
        pt.y = -7;
        p = pt;
    }
    {
        persist<Point> p(fname);
        Point pt = static_cast<Point>(p);
        REQUIRE(pt.x == 42);
        REQUIRE(pt.y == -7);
    }

    rm(fname);
}

// ---------------------------------------------------------------------------
// 3.1.1.4 — static_assert blocks non-trivially-copyable types at compile time
// ---------------------------------------------------------------------------
// This test verifies the INTENT of the static_assert — we cannot execute a
// failed static_assert at runtime, so we verify the trait that guards it.
TEST_CASE("Task 3.1.1.4: types not trivially copyable are rejected by persist<T>",
          "[task3.1][persist_core]")
{
    // Types that SHOULD work with persist<T>
    REQUIRE(std::is_trivially_copyable<int>::value);
    REQUIRE(std::is_trivially_copyable<double>::value);
    REQUIRE(std::is_trivially_copyable<Point>::value);

    // Types that must NOT be used with persist<T>
    REQUIRE_FALSE(std::is_trivially_copyable<std::string>::value);
    REQUIRE_FALSE(std::is_trivially_copyable<std::vector<int>>::value);
}

// ---------------------------------------------------------------------------
// 3.1.1.5 — operator= and operator _Tref work correctly
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.1.1.5: persist<int> operator= and implicit conversion work",
          "[task3.1][persist_core]")
{
    std::string fname = tmp_name("ops");
    rm(fname);

    persist<int> p(fname);
    p = 100;
    int val = static_cast<int>(p);
    REQUIRE(val == 100);

    p = -999;
    int val2 = static_cast<int>(p);
    REQUIRE(val2 == -999);

    rm(fname);
}

// ---------------------------------------------------------------------------
// 3.1.1.6 — file size equals sizeof(T)
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.1.1.6: persist<T> file size equals sizeof(T)",
          "[task3.1][persist_core]")
{
    std::string fname = tmp_name("filesize");
    rm(fname);

    {
        persist<int> p(fname);
        p = 42;
    }

    auto file_size = std::filesystem::file_size(fname);
    REQUIRE(file_size == sizeof(int));

    rm(fname);
}
