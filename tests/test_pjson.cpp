// test_pjson.cpp — Тесты персистных JSON-узлов через новый API node/node_view.
//
// Тестирует все типы узлов: null, boolean, integer, uinteger, real, string,
// array, object через node_set_* / node_view / node_object_insert.
//

#include <catch2/catch_test_macros.hpp>

#include <type_traits>
#include <cstdint>
#include <cstring>

#include "pjson_node.h"

using namespace pjson;

// Вспомогательная функция: сбросить ПАП перед каждым тестом.
namespace
{
void reset_pam()
{
    pstringview_manager::reset();
    pam_pmm_reset();
}
} // anonymous namespace

// =============================================================================
// Layout: node payload
// =============================================================================

TEST_CASE( "pjson: string_val layout is 2 * sizeof(void*)", "[pjson][layout]" )
{
    // string_val — совместим с pstring (length + chars_offset), оба поля sizeof(void*).
    REQUIRE( sizeof( node::string_val ) == 2 * sizeof( void* ) );
}

TEST_CASE( "pjson: array_val layout is 3 * sizeof(void*)", "[pjson][layout]" )
{
    // array_val — раскладка pvector<node_id> (size + capacity + data_off).
    REQUIRE( sizeof( node::array_val ) == 3 * sizeof( void* ) );
}

TEST_CASE( "pjson: object_val layout is 3 * sizeof(void*)", "[pjson][layout]" )
{
    // object_val — раскладка pmap<pstringview, node_id> (size + capacity + data_off).
    REQUIRE( sizeof( node::object_val ) == 3 * sizeof( void* ) );
}

// =============================================================================
// null value
// =============================================================================

TEST_CASE( "pjson: zero-initialised node is null", "[pjson][null]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_init_null( fn.addr() );
    REQUIRE( node_view{ fn.addr() }.is_null() );
    fn.Delete();
}

TEST_CASE( "pjson: set_null resets any value to null", "[pjson][null]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_bool( fn.addr(), true );
    REQUIRE( node_view{ fn.addr() }.is_boolean() );
    node_init_null( fn.addr() );
    REQUIRE( node_view{ fn.addr() }.is_null() );
    fn.Delete();
}

// =============================================================================
// boolean value
// =============================================================================

TEST_CASE( "pjson: set_bool(true) and as_bool()", "[pjson][boolean]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_bool( fn.addr(), true );
    node_view v{ fn.addr() };
    REQUIRE( v.is_boolean() );
    REQUIRE( v.as_bool() == true );
    fn.Delete();
}

TEST_CASE( "pjson: set_bool(false) and as_bool()", "[pjson][boolean]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_bool( fn.addr(), false );
    node_view v{ fn.addr() };
    REQUIRE( v.is_boolean() );
    REQUIRE( v.as_bool() == false );
    fn.Delete();
}

// =============================================================================
// integer value
// =============================================================================

TEST_CASE( "pjson: set_int and as_int", "[pjson][integer]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_int( fn.addr(), -42 );
    node_view v{ fn.addr() };
    REQUIRE( v.is_integer() );
    REQUIRE( v.as_int() == -42 );
    fn.Delete();
}

TEST_CASE( "pjson: set_int with INT64_MAX", "[pjson][integer]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_int( fn.addr(), INT64_MAX );
    node_view v{ fn.addr() };
    REQUIRE( v.is_integer() );
    REQUIRE( v.as_int() == INT64_MAX );
    fn.Delete();
}

// =============================================================================
// unsigned integer value
// =============================================================================

TEST_CASE( "pjson: set_uint and as_uint", "[pjson][uinteger]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_uint( fn.addr(), 99u );
    node_view v{ fn.addr() };
    REQUIRE( v.is_uinteger() );
    REQUIRE( v.as_uint() == 99u );
    fn.Delete();
}

TEST_CASE( "pjson: set_uint with UINT64_MAX", "[pjson][uinteger]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_uint( fn.addr(), UINT64_MAX );
    node_view v{ fn.addr() };
    REQUIRE( v.is_uinteger() );
    REQUIRE( v.as_uint() == UINT64_MAX );
    fn.Delete();
}

// =============================================================================
// real (double) value
// =============================================================================

TEST_CASE( "pjson: set_real and as_double", "[pjson][real]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_real( fn.addr(), 3.14 );
    node_view v{ fn.addr() };
    REQUIRE( v.is_real() );
    REQUIRE( v.as_double() == 3.14 );
    fn.Delete();
}

TEST_CASE( "pjson: is_number() true for integer, uinteger, real", "[pjson][real]" )
{
    reset_pam();
    fptr<node> fni;
    fni.New();
    node_set_int( fni.addr(), 1 );

    fptr<node> fnu;
    fnu.New();
    node_set_uint( fnu.addr(), 2u );

    fptr<node> fnr;
    fnr.New();
    node_set_real( fnr.addr(), 3.0 );

    REQUIRE( node_view{ fni.addr() }.is_number() );
    REQUIRE( node_view{ fnu.addr() }.is_number() );
    REQUIRE( node_view{ fnr.addr() }.is_number() );

    fni.Delete();
    fnu.Delete();
    fnr.Delete();
}

// =============================================================================
// string value
// =============================================================================

TEST_CASE( "pjson: set_string and as_string", "[pjson][string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_string( fn.addr(), "hello" );
    node_view v{ fn.addr() };
    REQUIRE( v.is_string() );
    REQUIRE( v.as_string() == "hello" );
    fn.Delete();
}

TEST_CASE( "pjson: set_string empty string", "[pjson][string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_string( fn.addr(), "" );
    node_view v{ fn.addr() };
    REQUIRE( v.is_string() );
    REQUIRE( v.as_string() == "" );
    fn.Delete();
}

TEST_CASE( "pjson: reassign string via node_assign_string", "[pjson][string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_string( fn.addr(), "first" );
    node_assign_string( fn.addr(), "second" );
    REQUIRE( node_view{ fn.addr() }.as_string() == "second" );
    fn.Delete();
}

TEST_CASE( "pjson: set_string size() returns string length", "[pjson][string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_string( fn.addr(), "abc" );
    REQUIRE( node_view{ fn.addr() }.size() == 3u );
    fn.Delete();
}

// =============================================================================
// array value
// =============================================================================

TEST_CASE( "pjson: set_array creates empty array", "[pjson][array]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_array( fn.addr() );
    node_view v{ fn.addr() };
    REQUIRE( v.is_array() );
    REQUIRE( v.size() == 0u );
    fn.Delete();
}

TEST_CASE( "pjson: node_array_push_back appends null elements", "[pjson][array]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_array( fn.addr() );

    node_id slot = node_array_push_back( fn.addr() );
    REQUIRE( node_view{ slot }.is_null() );
    REQUIRE( node_view{ fn.addr() }.size() == 1u );
    fn.Delete();
}

TEST_CASE( "pjson: push_back multiple elements and read back", "[pjson][array]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_array( fn.addr() );

    node_id s0 = node_array_push_back( fn.addr() );
    node_set_int( s0, 10 );
    node_id s1 = node_array_push_back( fn.addr() );
    node_set_int( s1, 20 );
    node_id s2 = node_array_push_back( fn.addr() );
    node_set_int( s2, 30 );

    node_view arr{ fn.addr() };
    REQUIRE( arr.size() == 3u );
    REQUIRE( arr.at( (uintptr_t)0 ).as_int() == 10 );
    REQUIRE( arr.at( (uintptr_t)1 ).as_int() == 20 );
    REQUIRE( arr.at( (uintptr_t)2 ).as_int() == 30 );
    fn.Delete();
}

TEST_CASE( "pjson: push_back more than initial capacity triggers realloc", "[pjson][array]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_array( fn.addr() );

    // Push 10 elements — вызывает реаллокацию несколько раз.
    for ( int i = 0; i < 10; i++ )
    {
        node_id slot = node_array_push_back( fn.addr() );
        node_set_int( slot, i );
    }

    node_view arr{ fn.addr() };
    REQUIRE( arr.size() == 10u );
    for ( int i = 0; i < 10; i++ )
        REQUIRE( arr.at( static_cast<uintptr_t>( i ) ).as_int() == i );
    fn.Delete();
}

TEST_CASE( "pjson: nested arrays", "[pjson][array]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_array( fn.addr() );

    // Внутренний массив в слоте 0.
    node_id inner_slot = node_array_push_back( fn.addr() );
    node_set_array( inner_slot );
    node_id elem_slot = node_array_push_back( inner_slot );
    node_set_int( elem_slot, 99 );

    node_view outer{ fn.addr() };
    REQUIRE( outer.size() == 1u );
    node_view inner{ outer.at( (uintptr_t)0 ).id };
    REQUIRE( inner.is_array() );
    REQUIRE( inner.size() == 1u );
    REQUIRE( inner.at( (uintptr_t)0 ).as_int() == 99 );
    fn.Delete();
}

// =============================================================================
// object value
// =============================================================================

TEST_CASE( "pjson: set_object creates empty object", "[pjson][object]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_object( fn.addr() );
    node_view v{ fn.addr() };
    REQUIRE( v.is_object() );
    REQUIRE( v.size() == 0u );
    fn.Delete();
}

TEST_CASE( "pjson: node_object_insert and at(key) single key", "[pjson][object]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_object( fn.addr() );

    node_id slot = node_object_insert( fn.addr(), "x" );
    node_set_int( slot, 42 );

    node_view obj{ fn.addr() };
    REQUIRE( obj.size() == 1u );
    REQUIRE( obj.at( "x" ).as_int() == 42 );
    fn.Delete();
}

TEST_CASE( "pjson: at(key) missing returns invalid node_view", "[pjson][object]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_object( fn.addr() );
    node_object_insert( fn.addr(), "a" );

    node_view obj{ fn.addr() };
    REQUIRE( !obj.at( "b" ).valid() );
    fn.Delete();
}

TEST_CASE( "pjson: node_object_insert maintains sorted key order", "[pjson][object]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_object( fn.addr() );

    node_id sc = node_object_insert( fn.addr(), "c" );
    node_set_int( sc, 3 );
    node_id sa = node_object_insert( fn.addr(), "a" );
    node_set_int( sa, 1 );
    node_id sb = node_object_insert( fn.addr(), "b" );
    node_set_int( sb, 2 );

    node_view obj{ fn.addr() };
    REQUIRE( obj.size() == 3u );
    REQUIRE( obj.at( "a" ).as_int() == 1 );
    REQUIRE( obj.at( "b" ).as_int() == 2 );
    REQUIRE( obj.at( "c" ).as_int() == 3 );
    fn.Delete();
}

TEST_CASE( "pjson: object with string values", "[pjson][object][string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_object( fn.addr() );

    node_id name_slot = node_object_insert( fn.addr(), "name" );
    node_set_string( name_slot, "Alice" );
    node_id role_slot = node_object_insert( fn.addr(), "role" );
    node_set_string( role_slot, "engineer" );

    node_view obj{ fn.addr() };
    REQUIRE( obj.at( "name" ).as_string() == "Alice" );
    REQUIRE( obj.at( "role" ).as_string() == "engineer" );
    fn.Delete();
}

TEST_CASE( "pjson: node_object_insert triggers realloc beyond initial capacity", "[pjson][object]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_object( fn.addr() );

    // Вставляем 10 ключей — вызывает реаллокацию.
    const char* keys[] = { "j", "a", "h", "c", "e", "g", "b", "f", "d", "i" };
    for ( int i = 0; i < 10; i++ )
    {
        node_id slot = node_object_insert( fn.addr(), keys[i] );
        node_set_int( slot, i );
    }

    node_view obj{ fn.addr() };
    REQUIRE( obj.size() == 10u );
    for ( int i = 0; i < 10; i++ )
        REQUIRE( obj.at( keys[i] ).valid() );
    fn.Delete();
}

// =============================================================================
// Mixed / nested value round-trip
// =============================================================================

TEST_CASE( "pjson: heterogeneous array (null, bool, int, real, string)", "[pjson][mixed]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_array( fn.addr() );

    node_array_push_back( fn.addr() ); // null (default)
    node_id sb = node_array_push_back( fn.addr() );
    node_set_bool( sb, true );
    node_id si = node_array_push_back( fn.addr() );
    node_set_int( si, -7 );
    node_id sr = node_array_push_back( fn.addr() );
    node_set_real( sr, 2.718 );
    node_id ss = node_array_push_back( fn.addr() );
    node_set_string( ss, "world" );

    node_view arr{ fn.addr() };
    REQUIRE( arr.size() == 5u );
    REQUIRE( arr.at( (uintptr_t)0 ).is_null() );
    REQUIRE( arr.at( (uintptr_t)1 ).as_bool() == true );
    REQUIRE( arr.at( (uintptr_t)2 ).as_int() == -7 );
    REQUIRE( arr.at( (uintptr_t)3 ).as_double() == 2.718 );
    REQUIRE( arr.at( (uintptr_t)4 ).as_string() == "world" );
    fn.Delete();
}

TEST_CASE( "pjson: object containing array value", "[pjson][mixed]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_object( fn.addr() );

    node_id items_slot = node_object_insert( fn.addr(), "items" );
    node_set_array( items_slot );
    node_id e0 = node_array_push_back( items_slot );
    node_set_int( e0, 1 );
    node_id e1 = node_array_push_back( items_slot );
    node_set_int( e1, 2 );

    node_view obj{ fn.addr() };
    node_view items = obj.at( "items" );
    REQUIRE( items.valid() );
    REQUIRE( items.is_array() );
    REQUIRE( items.size() == 2u );
    REQUIRE( items.at( (uintptr_t)0 ).as_int() == 1 );
    REQUIRE( items.at( (uintptr_t)1 ).as_int() == 2 );
    fn.Delete();
}
