#include <catch2/catch_test_macros.hpp>

#include <type_traits>
#include <cstdint>

#include "pjson.h"

// =============================================================================
// Tests for pjson (persistent discriminated-union JSON value)
// =============================================================================

// ---------------------------------------------------------------------------
// Layout: pjson_data must be trivially copyable
// ---------------------------------------------------------------------------
TEST_CASE("pjson_data: is trivially copyable", "[pjson][layout]")
{
    REQUIRE(std::is_trivially_copyable<pjson_data>::value);
}

TEST_CASE("pjson_kv_pair: is trivially copyable", "[pjson][layout]")
{
    REQUIRE(std::is_trivially_copyable<pjson_kv_pair>::value);
}

// ---------------------------------------------------------------------------
// Phase 3: поля payload.string_val, array_val, object_val используют uintptr_t
// ---------------------------------------------------------------------------
TEST_CASE("pjson_data: payload slot fields are uintptr_t (Phase 3)",
          "[pjson][layout][phase3]")
{
    // Все поля slot/size в payload должны иметь размер sizeof(void*)
    // для согласованности с Phase 2 PAM API.
    REQUIRE(sizeof(pjson_data::payload_t::string_val.chars_slot) == sizeof(void*));
    REQUIRE(sizeof(pjson_data::payload_t::string_val.length) == sizeof(void*));
    REQUIRE(sizeof(pjson_data::payload_t::array_val.data_slot) == sizeof(void*));
    REQUIRE(sizeof(pjson_data::payload_t::array_val.size) == sizeof(void*));
    REQUIRE(sizeof(pjson_data::payload_t::object_val.pairs_slot) == sizeof(void*));
    REQUIRE(sizeof(pjson_data::payload_t::object_val.size) == sizeof(void*));
}

// ---------------------------------------------------------------------------
// null value
// ---------------------------------------------------------------------------
TEST_CASE("pjson: zero-initialised pjson_data is null", "[pjson][null]")
{
    pjson_data d{};
    pjson v(d);
    REQUIRE(v.is_null());
    REQUIRE(v.type() == pjson_type::null);
}

TEST_CASE("pjson: set_null resets any value to null", "[pjson][null]")
{
    pjson_data d{};
    pjson v(d);
    v.set_bool(true);
    REQUIRE(v.is_boolean());
    v.set_null();
    REQUIRE(v.is_null());
}

// ---------------------------------------------------------------------------
// boolean value
// ---------------------------------------------------------------------------
TEST_CASE("pjson: set_bool(true) and get_bool()", "[pjson][boolean]")
{
    pjson_data d{};
    pjson v(d);
    v.set_bool(true);
    REQUIRE(v.is_boolean());
    REQUIRE(v.get_bool() == true);
}

TEST_CASE("pjson: set_bool(false) and get_bool()", "[pjson][boolean]")
{
    pjson_data d{};
    pjson v(d);
    v.set_bool(false);
    REQUIRE(v.is_boolean());
    REQUIRE(v.get_bool() == false);
}

// ---------------------------------------------------------------------------
// integer value
// ---------------------------------------------------------------------------
TEST_CASE("pjson: set_int and get_int", "[pjson][integer]")
{
    pjson_data d{};
    pjson v(d);
    v.set_int(-42);
    REQUIRE(v.is_integer());
    REQUIRE(v.get_int() == -42);
}

TEST_CASE("pjson: set_int with INT64_MAX", "[pjson][integer]")
{
    pjson_data d{};
    pjson v(d);
    v.set_int(INT64_MAX);
    REQUIRE(v.is_integer());
    REQUIRE(v.get_int() == INT64_MAX);
}

// ---------------------------------------------------------------------------
// unsigned integer value
// ---------------------------------------------------------------------------
TEST_CASE("pjson: set_uint and get_uint", "[pjson][uinteger]")
{
    pjson_data d{};
    pjson v(d);
    v.set_uint(99u);
    REQUIRE(v.is_uinteger());
    REQUIRE(v.get_uint() == 99u);
}

TEST_CASE("pjson: set_uint with UINT64_MAX", "[pjson][uinteger]")
{
    pjson_data d{};
    pjson v(d);
    v.set_uint(UINT64_MAX);
    REQUIRE(v.is_uinteger());
    REQUIRE(v.get_uint() == UINT64_MAX);
}

// ---------------------------------------------------------------------------
// real (double) value
// ---------------------------------------------------------------------------
TEST_CASE("pjson: set_real and get_real", "[pjson][real]")
{
    pjson_data d{};
    pjson v(d);
    v.set_real(3.14);
    REQUIRE(v.is_real());
    REQUIRE(v.get_real() == 3.14);
}

TEST_CASE("pjson: is_number() true for integer, uinteger, real", "[pjson][real]")
{
    pjson_data d1{}, d2{}, d3{};
    pjson vi(d1), vu(d2), vr(d3);
    vi.set_int(1);
    vu.set_uint(2u);
    vr.set_real(3.0);
    REQUIRE(vi.is_number());
    REQUIRE(vu.is_number());
    REQUIRE(vr.is_number());
}

// ---------------------------------------------------------------------------
// string value
// ---------------------------------------------------------------------------
TEST_CASE("pjson: set_string and get_string", "[pjson][string]")
{
    pjson_data d{};
    pjson v(d);
    v.set_string("hello");
    REQUIRE(v.is_string());
    REQUIRE(std::strcmp(v.get_string(), "hello") == 0);

    v.free();
}

TEST_CASE("pjson: set_string empty string", "[pjson][string]")
{
    pjson_data d{};
    pjson v(d);
    v.set_string("");
    REQUIRE(v.is_string());
    REQUIRE(std::strcmp(v.get_string(), "") == 0);
}

TEST_CASE("pjson: set_string nullptr behaves like empty", "[pjson][string]")
{
    pjson_data d{};
    pjson v(d);
    v.set_string(nullptr);
    REQUIRE(v.is_string());
    REQUIRE(std::strcmp(v.get_string(), "") == 0);
}

TEST_CASE("pjson: reassign string frees previous allocation", "[pjson][string]")
{
    pjson_data d{};
    pjson v(d);
    v.set_string("first");
    v.set_string("second");
    REQUIRE(std::strcmp(v.get_string(), "second") == 0);
    v.free();
}

TEST_CASE("pjson: set_string size() returns string length", "[pjson][string]")
{
    pjson_data d{};
    pjson v(d);
    v.set_string("abc");
    REQUIRE(v.size() == 3u);
    v.free();
}

// ---------------------------------------------------------------------------
// array value
// ---------------------------------------------------------------------------
TEST_CASE("pjson: set_array creates empty array", "[pjson][array]")
{
    pjson_data d{};
    pjson v(d);
    v.set_array();
    REQUIRE(v.is_array());
    REQUIRE(v.empty());
    REQUIRE(v.size() == 0u);
}

TEST_CASE("pjson: push_back appends null elements", "[pjson][array]")
{
    pjson_data d{};
    pjson v(d);
    v.set_array();

    pjson_data& e0 = v.push_back();
    pjson elem0(e0);
    REQUIRE(elem0.is_null());

    REQUIRE(v.size() == 1u);

    v.free();
}

TEST_CASE("pjson: push_back multiple elements and read back", "[pjson][array]")
{
    pjson_data d{};
    pjson v(d);
    v.set_array();

    pjson(v.push_back()).set_int(10);
    pjson(v.push_back()).set_int(20);
    pjson(v.push_back()).set_int(30);

    REQUIRE(v.size() == 3u);
    REQUIRE(pjson(v[0u]).get_int() == 10);
    REQUIRE(pjson(v[1u]).get_int() == 20);
    REQUIRE(pjson(v[2u]).get_int() == 30);

    v.free();
}

TEST_CASE("pjson: push_back more than initial capacity triggers realloc", "[pjson][array]")
{
    pjson_data d{};
    pjson v(d);
    v.set_array();

    // Push 10 elements — initial capacity is 4, so multiple reallocations happen.
    for( int i = 0; i < 10; i++ )
        pjson(v.push_back()).set_int(i);

    REQUIRE(v.size() == 10u);
    for( int i = 0; i < 10; i++ )
        REQUIRE(pjson(v[static_cast<unsigned>(i)]).get_int() == i);

    v.free();
}

TEST_CASE("pjson: nested arrays", "[pjson][array]")
{
    pjson_data d{};
    pjson outer(d);
    outer.set_array();

    // Inner array at index 0.
    pjson_data& inner_d = outer.push_back();
    pjson inner(inner_d);
    inner.set_array();
    pjson(inner.push_back()).set_int(99);

    REQUIRE(outer.size() == 1u);
    pjson inner2(outer[0u]);
    REQUIRE(inner2.is_array());
    REQUIRE(inner2.size() == 1u);
    REQUIRE(pjson(inner2[0u]).get_int() == 99);

    outer.free();
}

// ---------------------------------------------------------------------------
// object value
// ---------------------------------------------------------------------------
TEST_CASE("pjson: set_object creates empty object", "[pjson][object]")
{
    pjson_data d{};
    pjson v(d);
    v.set_object();
    REQUIRE(v.is_object());
    REQUIRE(v.empty());
    REQUIRE(v.size() == 0u);
}

TEST_CASE("pjson: obj_insert and obj_find single key", "[pjson][object]")
{
    pjson_data d{};
    pjson v(d);
    v.set_object();

    pjson(v.obj_insert("x")).set_int(42);
    REQUIRE(v.size() == 1u);

    pjson_data* found = v.obj_find("x");
    REQUIRE(found != nullptr);
    REQUIRE(pjson(*found).get_int() == 42);

    v.free();
}

TEST_CASE("pjson: obj_find missing key returns nullptr", "[pjson][object]")
{
    pjson_data d{};
    pjson v(d);
    v.set_object();
    v.obj_insert("a");

    REQUIRE(v.obj_find("b") == nullptr);

    v.free();
}

TEST_CASE("pjson: obj_insert maintains sorted key order", "[pjson][object]")
{
    pjson_data d{};
    pjson v(d);
    v.set_object();

    pjson(v.obj_insert("c")).set_int(3);
    pjson(v.obj_insert("a")).set_int(1);
    pjson(v.obj_insert("b")).set_int(2);

    REQUIRE(v.size() == 3u);

    // Keys should be sorted: a, b, c.
    pjson_data* fa = v.obj_find("a");
    pjson_data* fb = v.obj_find("b");
    pjson_data* fc = v.obj_find("c");
    REQUIRE(fa != nullptr);
    REQUIRE(fb != nullptr);
    REQUIRE(fc != nullptr);
    REQUIRE(pjson(*fa).get_int() == 1);
    REQUIRE(pjson(*fb).get_int() == 2);
    REQUIRE(pjson(*fc).get_int() == 3);

    v.free();
}

TEST_CASE("pjson: obj_insert replaces existing key value", "[pjson][object]")
{
    pjson_data d{};
    pjson v(d);
    v.set_object();

    pjson(v.obj_insert("k")).set_int(1);
    REQUIRE(pjson(*v.obj_find("k")).get_int() == 1);

    pjson(v.obj_insert("k")).set_int(2);
    REQUIRE(v.size() == 1u);
    REQUIRE(pjson(*v.obj_find("k")).get_int() == 2);

    v.free();
}

TEST_CASE("pjson: obj_erase removes key", "[pjson][object]")
{
    pjson_data d{};
    pjson v(d);
    v.set_object();

    pjson(v.obj_insert("a")).set_int(1);
    pjson(v.obj_insert("b")).set_int(2);
    pjson(v.obj_insert("c")).set_int(3);
    REQUIRE(v.size() == 3u);

    bool ok = v.obj_erase("b");
    REQUIRE(ok);
    REQUIRE(v.size() == 2u);
    REQUIRE(v.obj_find("b") == nullptr);
    REQUIRE(v.obj_find("a") != nullptr);
    REQUIRE(v.obj_find("c") != nullptr);

    v.free();
}

TEST_CASE("pjson: obj_erase missing key returns false", "[pjson][object]")
{
    pjson_data d{};
    pjson v(d);
    v.set_object();

    pjson(v.obj_insert("a")).set_int(1);
    bool ok = v.obj_erase("z");
    REQUIRE(!ok);
    REQUIRE(v.size() == 1u);

    v.free();
}

TEST_CASE("pjson: object with string values", "[pjson][object][string]")
{
    pjson_data d{};
    pjson v(d);
    v.set_object();

    pjson(v.obj_insert("name")).set_string("Alice");
    pjson(v.obj_insert("role")).set_string("engineer");

    pjson_data* name_d = v.obj_find("name");
    REQUIRE(name_d != nullptr);
    REQUIRE(std::strcmp(pjson(*name_d).get_string(), "Alice") == 0);

    v.free();
}

TEST_CASE("pjson: obj_insert triggers realloc beyond initial capacity", "[pjson][object]")
{
    pjson_data d{};
    pjson v(d);
    v.set_object();

    // Insert 10 keys — initial capacity is 4, so reallocation must happen.
    const char* keys[] = {"j", "a", "h", "c", "e", "g", "b", "f", "d", "i"};
    for( int i = 0; i < 10; i++ )
        pjson(v.obj_insert(keys[i])).set_int(i);

    REQUIRE(v.size() == 10u);
    for( int i = 0; i < 10; i++ )
        REQUIRE(v.obj_find(keys[i]) != nullptr);

    v.free();
}

// ---------------------------------------------------------------------------
// Mixed / nested value round-trip
// ---------------------------------------------------------------------------
TEST_CASE("pjson: heterogeneous array (null, bool, int, real, string)", "[pjson][mixed]")
{
    pjson_data d{};
    pjson v(d);
    v.set_array();

    pjson(v.push_back()).set_null();
    pjson(v.push_back()).set_bool(true);
    pjson(v.push_back()).set_int(-7);
    pjson(v.push_back()).set_real(2.718);
    pjson(v.push_back()).set_string("world");

    REQUIRE(v.size() == 5u);
    REQUIRE(pjson(v[0u]).is_null());
    REQUIRE(pjson(v[1u]).get_bool() == true);
    REQUIRE(pjson(v[2u]).get_int() == -7);
    REQUIRE(pjson(v[3u]).get_real() == 2.718);
    REQUIRE(std::strcmp(pjson(v[4u]).get_string(), "world") == 0);

    v.free();
}

TEST_CASE("pjson: object containing array value", "[pjson][mixed]")
{
    pjson_data d{};
    pjson v(d);
    v.set_object();

    pjson arr(v.obj_insert("items"));
    arr.set_array();
    pjson(arr.push_back()).set_int(1);
    pjson(arr.push_back()).set_int(2);

    pjson_data* items = v.obj_find("items");
    REQUIRE(items != nullptr);
    pjson arr2(*items);
    REQUIRE(arr2.is_array());
    REQUIRE(arr2.size() == 2u);
    REQUIRE(pjson(arr2[0u]).get_int() == 1);
    REQUIRE(pjson(arr2[1u]).get_int() == 2);

    v.free();
}

TEST_CASE("pjson: free releases all nested allocations", "[pjson][free]")
{
    pjson_data d{};
    pjson v(d);
    v.set_object();
    pjson(v.obj_insert("key")).set_string("value");
    pjson arr(v.obj_insert("arr"));
    arr.set_array();
    pjson(arr.push_back()).set_int(42);

    v.free();
    REQUIRE(v.is_null());
}
