#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <type_traits>
#include <string>
#include <cmath>

#include "persist.h"
#include "jgit/persistent_json_value.h"

// =============================================================================
// Task 3.2.3 — Tests for persist<jgit::persistent_json_value>
// =============================================================================
//
// Scope: verify that persist<persistent_json_value> compiles and correctly
// saves/loads all JSON value types (null, boolean, integer, float, string,
// array, object) across construction/destruction cycles.
//
// persistent_json_value is already trivially copyable (verified by static_assert
// in persistent_json_value.h), so no changes to the header are needed.
// =============================================================================

namespace {
    std::string tmp_name_pjv(const char* tag)
    {
        return std::string("./test_persist_pjv_") + tag + ".tmp";
    }

    void rm_pjv(const std::string& path)
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
} // anonymous namespace

// ---------------------------------------------------------------------------
// 3.2.3.1 — persist<persistent_json_value> for null
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.2.3.1: persist<persistent_json_value> null value round-trip",
          "[task3.2][persist_persistent_json_value]")
{
    using jgit::persistent_json_value;

    std::string fname = tmp_name_pjv("null");
    rm_pjv(fname);

    {
        persist<persistent_json_value> p(fname);
        persistent_json_value v = persistent_json_value::make_null();
        p = v;
    }

    {
        persist<persistent_json_value> p(fname);
        persistent_json_value loaded = static_cast<persistent_json_value>(p);
        REQUIRE(loaded.is_null());
    }

    rm_pjv(fname);
}

// ---------------------------------------------------------------------------
// 3.2.3.2 — persist<persistent_json_value> for boolean
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.2.3.2: persist<persistent_json_value> boolean value round-trip",
          "[task3.2][persist_persistent_json_value]")
{
    using jgit::persistent_json_value;

    std::string fname_true  = tmp_name_pjv("bool_true");
    std::string fname_false = tmp_name_pjv("bool_false");
    rm_pjv(fname_true);
    rm_pjv(fname_false);

    {
        persist<persistent_json_value> p(fname_true);
        p = persistent_json_value::make_bool(true);
    }
    {
        persist<persistent_json_value> p(fname_true);
        persistent_json_value loaded = static_cast<persistent_json_value>(p);
        REQUIRE(loaded.is_bool());
        REQUIRE(loaded.get_bool() == true);
    }

    {
        persist<persistent_json_value> p(fname_false);
        p = persistent_json_value::make_bool(false);
    }
    {
        persist<persistent_json_value> p(fname_false);
        persistent_json_value loaded = static_cast<persistent_json_value>(p);
        REQUIRE(loaded.is_bool());
        REQUIRE(loaded.get_bool() == false);
    }

    rm_pjv(fname_true);
    rm_pjv(fname_false);
}

// ---------------------------------------------------------------------------
// 3.2.3.3 — persist<persistent_json_value> for integer
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.2.3.3: persist<persistent_json_value> integer value round-trip",
          "[task3.2][persist_persistent_json_value]")
{
    using jgit::persistent_json_value;

    std::string fname = tmp_name_pjv("int");
    rm_pjv(fname);

    const int64_t expected_val = -9876543210LL;

    {
        persist<persistent_json_value> p(fname);
        p = persistent_json_value::make_int(expected_val);
    }

    {
        persist<persistent_json_value> p(fname);
        persistent_json_value loaded = static_cast<persistent_json_value>(p);
        REQUIRE(loaded.is_int());
        REQUIRE(loaded.get_int() == expected_val);
    }

    rm_pjv(fname);
}

// ---------------------------------------------------------------------------
// 3.2.3.4 — persist<persistent_json_value> for float
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.2.3.4: persist<persistent_json_value> float value round-trip",
          "[task3.2][persist_persistent_json_value]")
{
    using jgit::persistent_json_value;

    std::string fname = tmp_name_pjv("float");
    rm_pjv(fname);

    const double expected_val = 2.718281828459045;

    {
        persist<persistent_json_value> p(fname);
        p = persistent_json_value::make_float(expected_val);
    }

    {
        persist<persistent_json_value> p(fname);
        persistent_json_value loaded = static_cast<persistent_json_value>(p);
        REQUIRE(loaded.is_float());
        REQUIRE(loaded.get_float() == expected_val);
    }

    rm_pjv(fname);
}

// ---------------------------------------------------------------------------
// 3.2.3.5 — persist<persistent_json_value> for short string (SSO path)
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.2.3.5: persist<persistent_json_value> SSO string round-trip",
          "[task3.2][persist_persistent_json_value]")
{
    using jgit::persistent_json_value;

    std::string fname = tmp_name_pjv("str_sso");
    rm_pjv(fname);

    const char* short_str = "hello";  // <= 23 chars: SSO path

    {
        persist<persistent_json_value> p(fname);
        p = persistent_json_value::make_string(short_str);
    }

    {
        persist<persistent_json_value> p(fname);
        persistent_json_value loaded = static_cast<persistent_json_value>(p);
        REQUIRE(loaded.is_string());
        REQUIRE(loaded.get_string() == short_str);
        REQUIRE_FALSE(loaded.get_string().is_long);  // SSO path
    }

    rm_pjv(fname);
}

// ---------------------------------------------------------------------------
// 3.2.3.6 — persist<persistent_json_value> for long string (LONG_BUF path)
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.2.3.6: persist<persistent_json_value> long string round-trip",
          "[task3.2][persist_persistent_json_value]")
{
    using jgit::persistent_json_value;

    std::string fname = tmp_name_pjv("str_long");
    rm_pjv(fname);

    const std::string long_str(100, 'z');  // 100 chars: LONG_BUF path

    {
        persist<persistent_json_value> p(fname);
        p = persistent_json_value::make_string(long_str);
    }

    {
        persist<persistent_json_value> p(fname);
        persistent_json_value loaded = static_cast<persistent_json_value>(p);
        REQUIRE(loaded.is_string());
        REQUIRE(loaded.get_string() == long_str);
        REQUIRE(loaded.get_string().is_long);  // LONG_BUF path
    }

    rm_pjv(fname);
}

// ---------------------------------------------------------------------------
// 3.2.3.7 — persist<persistent_json_value> for array (saves only array_id)
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.2.3.7: persist<persistent_json_value> array ID round-trip",
          "[task3.2][persist_persistent_json_value]")
{
    using jgit::persistent_json_value;

    std::string fname = tmp_name_pjv("array");
    rm_pjv(fname);

    const uint32_t expected_array_id = 42u;

    {
        persist<persistent_json_value> p(fname);
        p = persistent_json_value::make_array(expected_array_id);
    }

    {
        persist<persistent_json_value> p(fname);
        persistent_json_value loaded = static_cast<persistent_json_value>(p);
        REQUIRE(loaded.is_array());
        REQUIRE(loaded.get_array_id() == expected_array_id);
    }

    rm_pjv(fname);
}

// ---------------------------------------------------------------------------
// 3.2.3.8 — persist<persistent_json_value> for object (saves only object_id)
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.2.3.8: persist<persistent_json_value> object ID round-trip",
          "[task3.2][persist_persistent_json_value]")
{
    using jgit::persistent_json_value;

    std::string fname = tmp_name_pjv("object");
    rm_pjv(fname);

    const uint32_t expected_object_id = 99u;

    {
        persist<persistent_json_value> p(fname);
        p = persistent_json_value::make_object(expected_object_id);
    }

    {
        persist<persistent_json_value> p(fname);
        persistent_json_value loaded = static_cast<persistent_json_value>(p);
        REQUIRE(loaded.is_object());
        REQUIRE(loaded.get_object_id() == expected_object_id);
    }

    rm_pjv(fname);
}
