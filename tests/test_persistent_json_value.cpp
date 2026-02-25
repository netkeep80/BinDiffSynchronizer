#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cstring>
#include <type_traits>

#include "jgit/persistent_json_value.h"

// =============================================================================
// Task 2.3 - jgit::persistent_json_value unit tests
//
// Tests cover all 7 JSON types: null, boolean, number_int, number_float,
// string, array (id), object (id), plus layout and raw-byte round-trip.
// =============================================================================

using namespace jgit;

TEST_CASE("Task 2.3.1: persistent_json_value is trivially copyable and fixed-size",
          "[persistent_json_value][layout]")
{
    REQUIRE(std::is_trivially_copyable<persistent_json_value>::value);
    // Size must be deterministic across calls (it is a compile-time constant).
    REQUIRE(sizeof(persistent_json_value) == sizeof(persistent_json_value));
    // Must be at least 8 bytes type+pad + sizeof(persistent_string).
    REQUIRE(sizeof(persistent_json_value) >= sizeof(persistent_string) + 8);
}

TEST_CASE("Task 2.3.2: persistent_json_value default constructor gives null",
          "[persistent_json_value][null]")
{
    persistent_json_value v;
    REQUIRE(v.is_null());
    REQUIRE(!v.is_bool());
    REQUIRE(!v.is_int());
    REQUIRE(!v.is_float());
    REQUIRE(!v.is_string());
    REQUIRE(!v.is_array());
    REQUIRE(!v.is_object());
}

TEST_CASE("Task 2.3.3: persistent_json_value make_null",
          "[persistent_json_value][null]")
{
    auto v = persistent_json_value::make_null();
    REQUIRE(v.is_null());
    REQUIRE(v.type == json_type::null);
}

TEST_CASE("Task 2.3.4: persistent_json_value make_bool",
          "[persistent_json_value][boolean]")
{
    auto vt = persistent_json_value::make_bool(true);
    REQUIRE(vt.is_bool());
    REQUIRE(vt.get_bool() == true);
    REQUIRE(!vt.is_null());

    auto vf = persistent_json_value::make_bool(false);
    REQUIRE(vf.is_bool());
    REQUIRE(vf.get_bool() == false);
}

TEST_CASE("Task 2.3.5: persistent_json_value make_int",
          "[persistent_json_value][number_int]")
{
    auto v1 = persistent_json_value::make_int(42);
    REQUIRE(v1.is_int());
    REQUIRE(v1.get_int() == 42);

    auto v2 = persistent_json_value::make_int(-9223372036854775807LL - 1);  // INT64_MIN
    REQUIRE(v2.is_int());
    REQUIRE(v2.get_int() == INT64_MIN);

    auto v3 = persistent_json_value::make_int(0);
    REQUIRE(v3.is_int());
    REQUIRE(v3.get_int() == 0);
}

TEST_CASE("Task 2.3.6: persistent_json_value make_float",
          "[persistent_json_value][number_float]")
{
    auto v = persistent_json_value::make_float(3.14);
    REQUIRE(v.is_float());
    REQUIRE(v.get_float() == Catch::Approx(3.14));
    REQUIRE(!v.is_int());

    auto v2 = persistent_json_value::make_float(-0.0);
    REQUIRE(v2.is_float());
}

TEST_CASE("Task 2.3.7: persistent_json_value make_string from C string",
          "[persistent_json_value][string]")
{
    auto v = persistent_json_value::make_string("hello");
    REQUIRE(v.is_string());
    REQUIRE(std::string(v.get_string().c_str()) == "hello");
    REQUIRE(!v.is_null());
}

TEST_CASE("Task 2.3.8: persistent_json_value make_string from std::string",
          "[persistent_json_value][string]")
{
    std::string s = "world";
    auto v = persistent_json_value::make_string(s);
    REQUIRE(v.is_string());
    REQUIRE(v.get_string().to_std_string() == "world");
}

TEST_CASE("Task 2.3.9: persistent_json_value make_string long string (> SSO_SIZE)",
          "[persistent_json_value][string]")
{
    // persistent_string::SSO_SIZE == 23, so 30-char string goes to long_buf.
    std::string long_str(30, 'x');
    auto v = persistent_json_value::make_string(long_str);
    REQUIRE(v.is_string());
    REQUIRE(v.get_string().to_std_string() == long_str);
}

TEST_CASE("Task 2.3.10: persistent_json_value make_array stores id",
          "[persistent_json_value][array]")
{
    auto v = persistent_json_value::make_array(7);
    REQUIRE(v.is_array());
    REQUIRE(v.get_array_id() == 7);
    REQUIRE(!v.is_object());
}

TEST_CASE("Task 2.3.11: persistent_json_value make_object stores id",
          "[persistent_json_value][object]")
{
    auto v = persistent_json_value::make_object(42);
    REQUIRE(v.is_object());
    REQUIRE(v.get_object_id() == 42);
    REQUIRE(!v.is_array());
}

TEST_CASE("Task 2.3.12: persistent_json_value survives raw-byte copy (persist<T> pattern)",
          "[persistent_json_value][layout]")
{
    // Simulate what persist<T> does: save and restore via raw byte copy.
    auto original = persistent_json_value::make_int(12345678LL);

    // Raw copy.
    unsigned char buf[sizeof(persistent_json_value)];
    std::memcpy(buf, &original, sizeof(original));

    // Reconstruct.
    persistent_json_value restored;
    std::memcpy(&restored, buf, sizeof(restored));

    REQUIRE(restored.is_int());
    REQUIRE(restored.get_int() == 12345678LL);
}

TEST_CASE("Task 2.3.13: persistent_json_value string survives raw-byte copy",
          "[persistent_json_value][string][layout]")
{
    auto original = persistent_json_value::make_string("raw-copy-test");

    unsigned char buf[sizeof(persistent_json_value)];
    std::memcpy(buf, &original, sizeof(original));

    persistent_json_value restored;
    std::memcpy(&restored, buf, sizeof(restored));

    REQUIRE(restored.is_string());
    REQUIRE(restored.get_string().to_std_string() == "raw-copy-test");
}

TEST_CASE("Task 2.3.14: persistent_json_value all types have distinct type tags",
          "[persistent_json_value][layout]")
{
    REQUIRE(static_cast<uint8_t>(json_type::null)         == 0);
    REQUIRE(static_cast<uint8_t>(json_type::boolean)      == 1);
    REQUIRE(static_cast<uint8_t>(json_type::number_int)   == 2);
    REQUIRE(static_cast<uint8_t>(json_type::number_float) == 3);
    REQUIRE(static_cast<uint8_t>(json_type::string)       == 4);
    REQUIRE(static_cast<uint8_t>(json_type::array)        == 5);
    REQUIRE(static_cast<uint8_t>(json_type::object)       == 6);
}
