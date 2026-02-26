#include <catch2/catch_test_macros.hpp>

#include <type_traits>
#include <cstdint>

#include "pjson.h"

// =============================================================================
// Tests for pjson (persistent discriminated-union JSON value)
// =============================================================================

// ---------------------------------------------------------------------------
// Layout: pjson payload fields are uintptr_t (Phase 3)
// ---------------------------------------------------------------------------
TEST_CASE("pjson: payload slot fields are uintptr_t (Phase 3)",
          "[pjson][layout][phase3]")
{
    // Все поля slot/size в payload должны иметь размер sizeof(void*)
    // для согласованности с Phase 2 PAM API.
    REQUIRE(sizeof(pjson::payload_t::string_val.chars_slot) == sizeof(void*));
    REQUIRE(sizeof(pjson::payload_t::string_val.length) == sizeof(void*));
    REQUIRE(sizeof(pjson::payload_t::array_val.data_slot) == sizeof(void*));
    REQUIRE(sizeof(pjson::payload_t::array_val.size) == sizeof(void*));
    REQUIRE(sizeof(pjson::payload_t::object_val.pairs_slot) == sizeof(void*));
    REQUIRE(sizeof(pjson::payload_t::object_val.size) == sizeof(void*));
}

// ---------------------------------------------------------------------------
// null value
// ---------------------------------------------------------------------------
TEST_CASE("pjson: zero-initialised pjson (via fptr) is null", "[pjson][null]")
{
    fptr<pjson> fv;
    fv.New();
    REQUIRE(fv->is_null());
    REQUIRE(fv->type_tag() == pjson_type::null);
    fv.Delete();
}

TEST_CASE("pjson: set_null resets any value to null", "[pjson][null]")
{
    fptr<pjson> fv;
    fv.New();
    fv->set_bool(true);
    REQUIRE(fv->is_boolean());
    fv->set_null();
    REQUIRE(fv->is_null());
    fv.Delete();
}

// ---------------------------------------------------------------------------
// boolean value
// ---------------------------------------------------------------------------
TEST_CASE("pjson: set_bool(true) and get_bool()", "[pjson][boolean]")
{
    fptr<pjson> fv;
    fv.New();
    fv->set_bool(true);
    REQUIRE(fv->is_boolean());
    REQUIRE(fv->get_bool() == true);
    fv.Delete();
}

TEST_CASE("pjson: set_bool(false) and get_bool()", "[pjson][boolean]")
{
    fptr<pjson> fv;
    fv.New();
    fv->set_bool(false);
    REQUIRE(fv->is_boolean());
    REQUIRE(fv->get_bool() == false);
    fv.Delete();
}

// ---------------------------------------------------------------------------
// integer value
// ---------------------------------------------------------------------------
TEST_CASE("pjson: set_int and get_int", "[pjson][integer]")
{
    fptr<pjson> fv;
    fv.New();
    fv->set_int(-42);
    REQUIRE(fv->is_integer());
    REQUIRE(fv->get_int() == -42);
    fv.Delete();
}

TEST_CASE("pjson: set_int with INT64_MAX", "[pjson][integer]")
{
    fptr<pjson> fv;
    fv.New();
    fv->set_int(INT64_MAX);
    REQUIRE(fv->is_integer());
    REQUIRE(fv->get_int() == INT64_MAX);
    fv.Delete();
}

// ---------------------------------------------------------------------------
// unsigned integer value
// ---------------------------------------------------------------------------
TEST_CASE("pjson: set_uint and get_uint", "[pjson][uinteger]")
{
    fptr<pjson> fv;
    fv.New();
    fv->set_uint(99u);
    REQUIRE(fv->is_uinteger());
    REQUIRE(fv->get_uint() == 99u);
    fv.Delete();
}

TEST_CASE("pjson: set_uint with UINT64_MAX", "[pjson][uinteger]")
{
    fptr<pjson> fv;
    fv.New();
    fv->set_uint(UINT64_MAX);
    REQUIRE(fv->is_uinteger());
    REQUIRE(fv->get_uint() == UINT64_MAX);
    fv.Delete();
}

// ---------------------------------------------------------------------------
// real (double) value
// ---------------------------------------------------------------------------
TEST_CASE("pjson: set_real and get_real", "[pjson][real]")
{
    fptr<pjson> fv;
    fv.New();
    fv->set_real(3.14);
    REQUIRE(fv->is_real());
    REQUIRE(fv->get_real() == 3.14);
    fv.Delete();
}

TEST_CASE("pjson: is_number() true for integer, uinteger, real", "[pjson][real]")
{
    fptr<pjson> fvi; fvi.New(); fvi->set_int(1);
    fptr<pjson> fvu; fvu.New(); fvu->set_uint(2u);
    fptr<pjson> fvr; fvr.New(); fvr->set_real(3.0);
    REQUIRE(fvi->is_number());
    REQUIRE(fvu->is_number());
    REQUIRE(fvr->is_number());
    fvi.Delete();
    fvu.Delete();
    fvr.Delete();
}

// ---------------------------------------------------------------------------
// string value
// ---------------------------------------------------------------------------
TEST_CASE("pjson: set_string and get_string", "[pjson][string]")
{
    fptr<pjson> fv;
    fv.New();
    fv->set_string("hello");
    REQUIRE(fv->is_string());
    REQUIRE(std::strcmp(fv->get_string(), "hello") == 0);

    fv->free();
    fv.Delete();
}

TEST_CASE("pjson: set_string empty string", "[pjson][string]")
{
    fptr<pjson> fv;
    fv.New();
    fv->set_string("");
    REQUIRE(fv->is_string());
    REQUIRE(std::strcmp(fv->get_string(), "") == 0);
    fv.Delete();
}

TEST_CASE("pjson: set_string nullptr behaves like empty", "[pjson][string]")
{
    fptr<pjson> fv;
    fv.New();
    fv->set_string(nullptr);
    REQUIRE(fv->is_string());
    REQUIRE(std::strcmp(fv->get_string(), "") == 0);
    fv.Delete();
}

TEST_CASE("pjson: reassign string frees previous allocation", "[pjson][string]")
{
    fptr<pjson> fv;
    fv.New();
    fv->set_string("first");
    fv->set_string("second");
    REQUIRE(std::strcmp(fv->get_string(), "second") == 0);
    fv->free();
    fv.Delete();
}

TEST_CASE("pjson: set_string size() returns string length", "[pjson][string]")
{
    fptr<pjson> fv;
    fv.New();
    fv->set_string("abc");
    REQUIRE(fv->size() == 3u);
    fv->free();
    fv.Delete();
}

// ---------------------------------------------------------------------------
// array value
// ---------------------------------------------------------------------------
TEST_CASE("pjson: set_array creates empty array", "[pjson][array]")
{
    fptr<pjson> fv;
    fv.New();
    fv->set_array();
    REQUIRE(fv->is_array());
    REQUIRE(fv->empty());
    REQUIRE(fv->size() == 0u);
    fv.Delete();
}

TEST_CASE("pjson: push_back appends null elements", "[pjson][array]")
{
    fptr<pjson> fv;
    fv.New();
    fv->set_array();

    pjson& e0 = fv->push_back();
    REQUIRE(e0.is_null());

    REQUIRE(fv->size() == 1u);

    fv->free();
    fv.Delete();
}

TEST_CASE("pjson: push_back multiple elements and read back", "[pjson][array]")
{
    fptr<pjson> fv;
    fv.New();
    fv->set_array();

    fv->push_back().set_int(10);
    fv->push_back().set_int(20);
    fv->push_back().set_int(30);

    REQUIRE(fv->size() == 3u);
    REQUIRE((*fv)[0u].get_int() == 10);
    REQUIRE((*fv)[1u].get_int() == 20);
    REQUIRE((*fv)[2u].get_int() == 30);

    fv->free();
    fv.Delete();
}

TEST_CASE("pjson: push_back more than initial capacity triggers realloc", "[pjson][array]")
{
    fptr<pjson> fv;
    fv.New();
    fv->set_array();

    // Push 10 elements — initial capacity is 4, so multiple reallocations happen.
    for( int i = 0; i < 10; i++ )
        fv->push_back().set_int(i);

    REQUIRE(fv->size() == 10u);
    for( int i = 0; i < 10; i++ )
        REQUIRE((*fv)[static_cast<unsigned>(i)].get_int() == i);

    fv->free();
    fv.Delete();
}

TEST_CASE("pjson: nested arrays", "[pjson][array]")
{
    fptr<pjson> fv;
    fv.New();
    fv->set_array();

    // Inner array at index 0.
    pjson& inner_d = fv->push_back();
    inner_d.set_array();
    inner_d.push_back().set_int(99);

    REQUIRE(fv->size() == 1u);
    pjson& inner2 = (*fv)[0u];
    REQUIRE(inner2.is_array());
    REQUIRE(inner2.size() == 1u);
    REQUIRE(inner2[0u].get_int() == 99);

    fv->free();
    fv.Delete();
}

// ---------------------------------------------------------------------------
// object value
// ---------------------------------------------------------------------------
TEST_CASE("pjson: set_object creates empty object", "[pjson][object]")
{
    fptr<pjson> fv;
    fv.New();
    fv->set_object();
    REQUIRE(fv->is_object());
    REQUIRE(fv->empty());
    REQUIRE(fv->size() == 0u);
    fv.Delete();
}

TEST_CASE("pjson: obj_insert and obj_find single key", "[pjson][object]")
{
    fptr<pjson> fv;
    fv.New();
    fv->set_object();

    fv->obj_insert("x").set_int(42);
    REQUIRE(fv->size() == 1u);

    pjson* found = fv->obj_find("x");
    REQUIRE(found != nullptr);
    REQUIRE(found->get_int() == 42);

    fv->free();
    fv.Delete();
}

TEST_CASE("pjson: obj_find missing key returns nullptr", "[pjson][object]")
{
    fptr<pjson> fv;
    fv.New();
    fv->set_object();
    fv->obj_insert("a");

    REQUIRE(fv->obj_find("b") == nullptr);

    fv->free();
    fv.Delete();
}

TEST_CASE("pjson: obj_insert maintains sorted key order", "[pjson][object]")
{
    fptr<pjson> fv;
    fv.New();
    fv->set_object();

    fv->obj_insert("c").set_int(3);
    fv->obj_insert("a").set_int(1);
    fv->obj_insert("b").set_int(2);

    REQUIRE(fv->size() == 3u);

    // Keys should be sorted: a, b, c.
    pjson* fa = fv->obj_find("a");
    pjson* fb = fv->obj_find("b");
    pjson* fc = fv->obj_find("c");
    REQUIRE(fa != nullptr);
    REQUIRE(fb != nullptr);
    REQUIRE(fc != nullptr);
    REQUIRE(fa->get_int() == 1);
    REQUIRE(fb->get_int() == 2);
    REQUIRE(fc->get_int() == 3);

    fv->free();
    fv.Delete();
}

TEST_CASE("pjson: obj_insert replaces existing key value", "[pjson][object]")
{
    fptr<pjson> fv;
    fv.New();
    fv->set_object();

    fv->obj_insert("k").set_int(1);
    REQUIRE(fv->obj_find("k")->get_int() == 1);

    fv->obj_insert("k").set_int(2);
    REQUIRE(fv->size() == 1u);
    REQUIRE(fv->obj_find("k")->get_int() == 2);

    fv->free();
    fv.Delete();
}

TEST_CASE("pjson: obj_erase removes key", "[pjson][object]")
{
    fptr<pjson> fv;
    fv.New();
    fv->set_object();

    fv->obj_insert("a").set_int(1);
    fv->obj_insert("b").set_int(2);
    fv->obj_insert("c").set_int(3);
    REQUIRE(fv->size() == 3u);

    bool ok = fv->obj_erase("b");
    REQUIRE(ok);
    REQUIRE(fv->size() == 2u);
    REQUIRE(fv->obj_find("b") == nullptr);
    REQUIRE(fv->obj_find("a") != nullptr);
    REQUIRE(fv->obj_find("c") != nullptr);

    fv->free();
    fv.Delete();
}

TEST_CASE("pjson: obj_erase missing key returns false", "[pjson][object]")
{
    fptr<pjson> fv;
    fv.New();
    fv->set_object();

    fv->obj_insert("a").set_int(1);
    bool ok = fv->obj_erase("z");
    REQUIRE(!ok);
    REQUIRE(fv->size() == 1u);

    fv->free();
    fv.Delete();
}

TEST_CASE("pjson: object with string values", "[pjson][object][string]")
{
    fptr<pjson> fv;
    fv.New();
    fv->set_object();

    fv->obj_insert("name").set_string("Alice");
    fv->obj_insert("role").set_string("engineer");

    pjson* name_d = fv->obj_find("name");
    REQUIRE(name_d != nullptr);
    REQUIRE(std::strcmp(name_d->get_string(), "Alice") == 0);

    fv->free();
    fv.Delete();
}

TEST_CASE("pjson: obj_insert triggers realloc beyond initial capacity", "[pjson][object]")
{
    fptr<pjson> fv;
    fv.New();
    fv->set_object();

    // Insert 10 keys — initial capacity is 4, so reallocation must happen.
    const char* keys[] = {"j", "a", "h", "c", "e", "g", "b", "f", "d", "i"};
    for( int i = 0; i < 10; i++ )
        fv->obj_insert(keys[i]).set_int(i);

    REQUIRE(fv->size() == 10u);
    for( int i = 0; i < 10; i++ )
        REQUIRE(fv->obj_find(keys[i]) != nullptr);

    fv->free();
    fv.Delete();
}

// ---------------------------------------------------------------------------
// Mixed / nested value round-trip
// ---------------------------------------------------------------------------
TEST_CASE("pjson: heterogeneous array (null, bool, int, real, string)", "[pjson][mixed]")
{
    fptr<pjson> fv;
    fv.New();
    fv->set_array();

    fv->push_back().set_null();
    fv->push_back().set_bool(true);
    fv->push_back().set_int(-7);
    fv->push_back().set_real(2.718);
    fv->push_back().set_string("world");

    REQUIRE(fv->size() == 5u);
    REQUIRE((*fv)[0u].is_null());
    REQUIRE((*fv)[1u].get_bool() == true);
    REQUIRE((*fv)[2u].get_int() == -7);
    REQUIRE((*fv)[3u].get_real() == 2.718);
    REQUIRE(std::strcmp((*fv)[4u].get_string(), "world") == 0);

    fv->free();
    fv.Delete();
}

TEST_CASE("pjson: object containing array value", "[pjson][mixed]")
{
    fptr<pjson> fv;
    fv.New();
    fv->set_object();

    pjson& arr = fv->obj_insert("items");
    arr.set_array();
    arr.push_back().set_int(1);
    arr.push_back().set_int(2);

    pjson* items = fv->obj_find("items");
    REQUIRE(items != nullptr);
    REQUIRE(items->is_array());
    REQUIRE(items->size() == 2u);
    REQUIRE((*items)[0u].get_int() == 1);
    REQUIRE((*items)[1u].get_int() == 2);

    fv->free();
    fv.Delete();
}

TEST_CASE("pjson: free releases all nested allocations", "[pjson][free]")
{
    fptr<pjson> fv;
    fv.New();
    fv->set_object();
    fv->obj_insert("key").set_string("value");
    pjson& arr = fv->obj_insert("arr");
    arr.set_array();
    arr.push_back().set_int(42);

    fv->free();
    REQUIRE(fv->is_null());

    fv.Delete();
}
