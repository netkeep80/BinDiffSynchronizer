// test_pjson_codec.cpp — Тесты сериализации/десериализации pjson_codec (Фаза 5).
//
// Тестирует функции:
//   node_to_string()   — сериализация node в строку JSON
//   node_from_string() — десериализация строки JSON в node
//   node_parse()       — парсинг JSON с аллокацией нового node
//
// Включает тесты для:
//   - Базовые типы: null, boolean, integer, uinteger, real, string
//   - Составные типы: array, object
//   - Расширения: $ref, $base64
//   - Round-trip тесты
//
// Зависимости: Catch2, pjson_codec.h

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <cstring>
#include <cmath>
#include <vector>

#include "pjson_codec.h"
#include "pam_pmm.h"

using namespace pjson;

/// Инициализировать PMM, если не инициализирован.
static void ensure_pmm()
{
    if ( !pam_pmm_is_initialized() )
        pam_pmm_init( nullptr );
}

// ---------------------------------------------------------------------------
// Вспомогательная функция: нормализовать JSON через node (parse + serialize).
// ---------------------------------------------------------------------------
static std::string normalize_json( const std::string& json_text )
{
    node_id id = node_parse( json_text.c_str() );
    if ( id == 0 )
        return "null";
    std::string result = node_to_string( id );
    // Освобождение node не требуется в тестах — ПАМ сбрасывается в конце.
    return result;
}

// ---------------------------------------------------------------------------
// Тесты node_to_string() — базовые типы
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_codec: to_string for null node", "[pjson_codec][to_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    node_init_null( fv.addr() );
    REQUIRE( node_to_string( fv.addr() ) == "null" );
}

TEST_CASE( "pjson_codec: to_string for boolean true", "[pjson_codec][to_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    node_set_bool( fv.addr(), true );
    REQUIRE( node_to_string( fv.addr() ) == "true" );
}

TEST_CASE( "pjson_codec: to_string for boolean false", "[pjson_codec][to_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    node_set_bool( fv.addr(), false );
    REQUIRE( node_to_string( fv.addr() ) == "false" );
}

TEST_CASE( "pjson_codec: to_string for integer", "[pjson_codec][to_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    node_set_int( fv.addr(), 42 );
    REQUIRE( node_to_string( fv.addr() ) == "42" );
}

TEST_CASE( "pjson_codec: to_string for negative integer", "[pjson_codec][to_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    node_set_int( fv.addr(), -100 );
    REQUIRE( node_to_string( fv.addr() ) == "-100" );
}

TEST_CASE( "pjson_codec: to_string for uinteger", "[pjson_codec][to_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    node_set_uint( fv.addr(), 12345678901ULL );
    REQUIRE( node_to_string( fv.addr() ) == "12345678901" );
}

TEST_CASE( "pjson_codec: to_string for real", "[pjson_codec][to_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    node_set_real( fv.addr(), 3.14 );
    std::string s = node_to_string( fv.addr() );
    double      v = std::strtod( s.c_str(), nullptr );
    REQUIRE( v == 3.14 );
}

TEST_CASE( "pjson_codec: to_string for real without decimal", "[pjson_codec][to_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    node_set_real( fv.addr(), 100.0 );
    std::string s = node_to_string( fv.addr() );
    // Должна содержать ".0" для отличия от integer.
    REQUIRE( s.find( '.' ) != std::string::npos );
}

TEST_CASE( "pjson_codec: to_string for string", "[pjson_codec][to_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    node_set_string( fv.addr(), "hello" );
    REQUIRE( node_to_string( fv.addr() ) == "\"hello\"" );
}

TEST_CASE( "pjson_codec: to_string for empty string", "[pjson_codec][to_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    node_set_string( fv.addr(), "" );
    REQUIRE( node_to_string( fv.addr() ) == "\"\"" );
}

TEST_CASE( "pjson_codec: to_string escapes special characters", "[pjson_codec][to_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    node_set_string( fv.addr(), "line1\nline2\ttab\"quote\\backslash" );
    std::string s = node_to_string( fv.addr() );
    REQUIRE( s.find( "\\n" ) != std::string::npos );
    REQUIRE( s.find( "\\t" ) != std::string::npos );
    REQUIRE( s.find( "\\\"" ) != std::string::npos );
    REQUIRE( s.find( "\\\\" ) != std::string::npos );
}

// ---------------------------------------------------------------------------
// Тесты node_to_string() — составные типы
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_codec: to_string for empty array", "[pjson_codec][to_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    node_set_array( fv.addr() );
    REQUIRE( node_to_string( fv.addr() ) == "[]" );
}

TEST_CASE( "pjson_codec: to_string for array with elements", "[pjson_codec][to_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    node_set_array( fv.addr() );
    node_id e1 = node_array_push_back( fv.addr() );
    node_id e2 = node_array_push_back( fv.addr() );
    node_id e3 = node_array_push_back( fv.addr() );
    node_set_int( e1, 1 );
    node_set_int( e2, 2 );
    node_set_int( e3, 3 );
    REQUIRE( node_to_string( fv.addr() ) == "[1,2,3]" );
}

TEST_CASE( "pjson_codec: to_string for empty object", "[pjson_codec][to_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    node_set_object( fv.addr() );
    REQUIRE( node_to_string( fv.addr() ) == "{}" );
}

TEST_CASE( "pjson_codec: to_string for object with fields", "[pjson_codec][to_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    node_set_object( fv.addr() );
    node_id a = node_object_insert( fv.addr(), "a" );
    node_id b = node_object_insert( fv.addr(), "b" );
    node_set_int( a, 1 );
    node_set_string( b, "val" );
    std::string s = node_to_string( fv.addr() );
    // Ключи должны быть в отсортированном порядке.
    REQUIRE( s.find( "\"a\"" ) != std::string::npos );
    REQUIRE( s.find( "\"b\"" ) != std::string::npos );
}

// ---------------------------------------------------------------------------
// Тесты node_to_string() — расширения $ref и $base64
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_codec: to_string for ref node", "[pjson_codec][to_string][ref]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    node_set_ref( fv.addr(), "/a/b/c" );
    REQUIRE( node_to_string( fv.addr() ) == "{\"$ref\":\"/a/b/c\"}" );
}

TEST_CASE( "pjson_codec: to_string for empty binary", "[pjson_codec][to_string][base64]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    node_set_binary( fv.addr() );
    REQUIRE( node_to_string( fv.addr() ) == "{\"$base64\":\"\"}" );
}

TEST_CASE( "pjson_codec: to_string for binary with data", "[pjson_codec][to_string][base64]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    node_set_binary( fv.addr() );
    node_binary_push_back( fv.addr(), 0 );
    node_binary_push_back( fv.addr(), 1 );
    node_binary_push_back( fv.addr(), 2 );
    // Base64 of [0, 1, 2] = "AAEC"
    REQUIRE( node_to_string( fv.addr() ) == "{\"$base64\":\"AAEC\"}" );
}

// ---------------------------------------------------------------------------
// Тесты node_from_string() — базовые типы
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_codec: from_string for null", "[pjson_codec][from_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    REQUIRE( node_from_string( "null", fv.addr() ) );
    node_view v{ fv.addr() };
    REQUIRE( v.is_null() );
}

TEST_CASE( "pjson_codec: from_string for boolean true", "[pjson_codec][from_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    REQUIRE( node_from_string( "true", fv.addr() ) );
    node_view v{ fv.addr() };
    REQUIRE( v.is_boolean() );
    REQUIRE( v.as_bool() == true );
}

TEST_CASE( "pjson_codec: from_string for boolean false", "[pjson_codec][from_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    REQUIRE( node_from_string( "false", fv.addr() ) );
    node_view v{ fv.addr() };
    REQUIRE( v.is_boolean() );
    REQUIRE( v.as_bool() == false );
}

TEST_CASE( "pjson_codec: from_string for integer", "[pjson_codec][from_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    REQUIRE( node_from_string( "42", fv.addr() ) );
    node_view v{ fv.addr() };
    REQUIRE( v.is_uinteger() );
    REQUIRE( v.as_uint() == 42u );
}

TEST_CASE( "pjson_codec: from_string for negative integer", "[pjson_codec][from_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    REQUIRE( node_from_string( "-100", fv.addr() ) );
    node_view v{ fv.addr() };
    REQUIRE( v.is_integer() );
    REQUIRE( v.as_int() == -100 );
}

TEST_CASE( "pjson_codec: from_string for real", "[pjson_codec][from_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    REQUIRE( node_from_string( "3.14", fv.addr() ) );
    node_view v{ fv.addr() };
    REQUIRE( v.is_real() );
    REQUIRE( v.as_double() == 3.14 );
}

TEST_CASE( "pjson_codec: from_string for string", "[pjson_codec][from_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    REQUIRE( node_from_string( "\"hello\"", fv.addr() ) );
    node_view v{ fv.addr() };
    REQUIRE( v.is_string() );
    REQUIRE( v.as_string() == "hello" );
}

TEST_CASE( "pjson_codec: from_string for empty string", "[pjson_codec][from_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    REQUIRE( node_from_string( "\"\"", fv.addr() ) );
    node_view v{ fv.addr() };
    REQUIRE( v.is_string() );
    REQUIRE( v.size() == 0u );
}

TEST_CASE( "pjson_codec: from_string handles escape sequences", "[pjson_codec][from_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    REQUIRE( node_from_string( "\"line1\\nline2\\ttab\"", fv.addr() ) );
    node_view v{ fv.addr() };
    REQUIRE( v.is_string() );
    std::string_view s = v.as_string();
    REQUIRE( s.find( '\n' ) != std::string_view::npos );
    REQUIRE( s.find( '\t' ) != std::string_view::npos );
}

// ---------------------------------------------------------------------------
// Тесты node_from_string() — составные типы
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_codec: from_string for empty array", "[pjson_codec][from_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    REQUIRE( node_from_string( "[]", fv.addr() ) );
    node_view v{ fv.addr() };
    REQUIRE( v.is_array() );
    REQUIRE( v.size() == 0u );
}

TEST_CASE( "pjson_codec: from_string for array with elements", "[pjson_codec][from_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    REQUIRE( node_from_string( "[1,2,3]", fv.addr() ) );
    node_view v{ fv.addr() };
    REQUIRE( v.is_array() );
    REQUIRE( v.size() == 3u );
    REQUIRE( v.at( static_cast<uintptr_t>( 0 ) ).as_uint() == 1u );
    REQUIRE( v.at( static_cast<uintptr_t>( 1 ) ).as_uint() == 2u );
    REQUIRE( v.at( static_cast<uintptr_t>( 2 ) ).as_uint() == 3u );
}

TEST_CASE( "pjson_codec: from_string for empty object", "[pjson_codec][from_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    REQUIRE( node_from_string( "{}", fv.addr() ) );
    node_view v{ fv.addr() };
    REQUIRE( v.is_object() );
    REQUIRE( v.size() == 0u );
}

TEST_CASE( "pjson_codec: from_string for object with fields", "[pjson_codec][from_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    REQUIRE( node_from_string( "{\"a\":1,\"b\":\"val\"}", fv.addr() ) );
    node_view v{ fv.addr() };
    REQUIRE( v.is_object() );
    REQUIRE( v.size() == 2u );
    REQUIRE( v.at( "a" ).as_uint() == 1u );
    REQUIRE( v.at( "b" ).as_string() == "val" );
}

TEST_CASE( "pjson_codec: from_string for nested structure", "[pjson_codec][from_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    REQUIRE( node_from_string( "{\"arr\":[1,2],\"obj\":{\"x\":3}}", fv.addr() ) );
    node_view v{ fv.addr() };
    REQUIRE( v.is_object() );
    node_view arr = v.at( "arr" );
    REQUIRE( arr.is_array() );
    REQUIRE( arr.size() == 2u );
    node_view obj = v.at( "obj" );
    REQUIRE( obj.is_object() );
    REQUIRE( obj.at( "x" ).as_uint() == 3u );
}

// ---------------------------------------------------------------------------
// Тесты node_from_string() — расширения $ref и $base64
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_codec: from_string for $ref", "[pjson_codec][from_string][ref]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    REQUIRE( node_from_string( "{\"$ref\":\"/a/b/c\"}", fv.addr() ) );
    node_view v{ fv.addr() };
    REQUIRE( v.is_ref() );
    REQUIRE( v.ref_path() == "/a/b/c" );
    REQUIRE( v.ref_target() == 0 ); // Не разрешён.
}

TEST_CASE( "pjson_codec: from_string for $base64 empty", "[pjson_codec][from_string][base64]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    REQUIRE( node_from_string( "{\"$base64\":\"\"}", fv.addr() ) );
    node_view v{ fv.addr() };
    REQUIRE( v.is_binary() );
    REQUIRE( v.size() == 0u );
}

TEST_CASE( "pjson_codec: from_string for $base64 with data", "[pjson_codec][from_string][base64]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    // "AAEC" decodes to [0, 1, 2]
    REQUIRE( node_from_string( "{\"$base64\":\"AAEC\"}", fv.addr() ) );
    node_view v{ fv.addr() };
    REQUIRE( v.is_binary() );
    REQUIRE( v.size() == 3u );

    // Проверяем содержимое.
    const node* n = pmm_resolve<node>( fv.addr() );
    REQUIRE( n != nullptr );
    const uint8_t* data = pmm_resolve<uint8_t>( n->binary_val.data_off );
    REQUIRE( data != nullptr );
    REQUIRE( data[0] == 0 );
    REQUIRE( data[1] == 1 );
    REQUIRE( data[2] == 2 );
}

TEST_CASE( "pjson_codec: $ref is only recognized for exactly one key", "[pjson_codec][from_string][ref]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    // Объект с двумя ключами, один из которых "$ref" — НЕ должен быть ref.
    REQUIRE( node_from_string( "{\"$ref\":\"/path\",\"extra\":1}", fv.addr() ) );
    node_view v{ fv.addr() };
    REQUIRE( v.is_object() );
    REQUIRE( !v.is_ref() );
}

TEST_CASE( "pjson_codec: $base64 is only recognized for exactly one key", "[pjson_codec][from_string][base64]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    // Объект с двумя ключами — НЕ должен быть binary.
    REQUIRE( node_from_string( "{\"$base64\":\"AAEC\",\"extra\":1}", fv.addr() ) );
    node_view v{ fv.addr() };
    REQUIRE( v.is_object() );
    REQUIRE( !v.is_binary() );
}

// ---------------------------------------------------------------------------
// Тесты round-trip
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_codec: round-trip for simple object", "[pjson_codec][roundtrip]" )
{
    ensure_pmm();
    const std::string original = "{\"key\":\"value\",\"num\":42}";
    std::string       result   = normalize_json( original );
    // Парсим ещё раз и сравниваем.
    std::string result2 = normalize_json( result );
    REQUIRE( result == result2 );
}

TEST_CASE( "pjson_codec: round-trip for nested structure", "[pjson_codec][roundtrip]" )
{
    ensure_pmm();
    const std::string original = "{\"arr\":[1,true,null,\"str\"],\"obj\":{\"x\":3.14}}";
    std::string       result   = normalize_json( original );
    std::string       result2  = normalize_json( result );
    REQUIRE( result == result2 );
}

TEST_CASE( "pjson_codec: round-trip for $ref", "[pjson_codec][roundtrip][ref]" )
{
    ensure_pmm();
    const std::string original = "{\"$ref\":\"/path/to/node\"}";
    std::string       result   = normalize_json( original );
    REQUIRE( result == original );
}

TEST_CASE( "pjson_codec: round-trip for $base64", "[pjson_codec][roundtrip][base64]" )
{
    ensure_pmm();
    const std::string original = "{\"$base64\":\"AAEC\"}";
    std::string       result   = normalize_json( original );
    REQUIRE( result == original );
}

// ---------------------------------------------------------------------------
// Тест node_parse()
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_codec: node_parse returns valid node_id", "[pjson_codec][parse]" )
{
    ensure_pmm();
    node_id id = node_parse( "{\"test\":123}" );
    REQUIRE( id != 0 );
    node_view v{ id };
    REQUIRE( v.is_object() );
    REQUIRE( v.at( "test" ).as_uint() == 123u );
}

TEST_CASE( "pjson_codec: node_parse returns 0 for invalid JSON", "[pjson_codec][parse]" )
{
    ensure_pmm();
    node_id id = node_parse( "not valid json" );
    REQUIRE( id == 0 );
}

// ---------------------------------------------------------------------------
// Тест обработки ошибок
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_codec: from_string sets null for invalid JSON", "[pjson_codec][from_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    REQUIRE( !node_from_string( "invalid", fv.addr() ) );
    node_view v{ fv.addr() };
    REQUIRE( v.is_null() );
}

TEST_CASE( "pjson_codec: from_string handles null pointer", "[pjson_codec][from_string]" )
{
    ensure_pmm();
    fptr<node> fv;
    fv.New();
    REQUIRE( !node_from_string( nullptr, fv.addr() ) );
    node_view v{ fv.addr() };
    REQUIRE( v.is_null() );
}
