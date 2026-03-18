// test_pjson_serial.cpp — Тесты сериализации/десериализации через новый API pjson_codec (Задача 9.5).
//
// Мигрировано с pjson.h на pjson_codec.h в рамках Задачи 9.5.
// Тестирует функции:
//   node_to_string(node_id)          — сериализация узла в строку JSON
//   node_from_string(json, node_off) — десериализация строки JSON в узел
//
// Все комментарии — на русском языке (Тр.6).

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <cstring>
#include <fstream>

#include "pjson_codec.h"

using namespace pjson;

// Вспомогательная функция: сбросить PMM перед каждым тестом.
static void reset_pam()
{
    pstringview_manager::reset();
    pam_pmm_reset();
}

// Вспомогательная функция: нормализовать JSON через node_from_string + node_to_string.
static std::string normalize_json( const std::string& json_text )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( json_text.c_str(), fn.addr() );
    return node_to_string( fn.addr() );
}

// ===========================================================================
// Тесты node_to_string()
// ===========================================================================

TEST_CASE( "pjson serial: to_string for null", "[pjson][serial][to_string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_init_null( fn.addr() );
    REQUIRE( node_to_string( fn.addr() ) == "null" );
    fn.Delete();
}

TEST_CASE( "pjson serial: to_string for boolean true", "[pjson][serial][to_string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_bool( fn.addr(), true );
    REQUIRE( node_to_string( fn.addr() ) == "true" );
    fn.Delete();
}

TEST_CASE( "pjson serial: to_string for boolean false", "[pjson][serial][to_string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_bool( fn.addr(), false );
    REQUIRE( node_to_string( fn.addr() ) == "false" );
    fn.Delete();
}

TEST_CASE( "pjson serial: to_string for integer", "[pjson][serial][to_string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_int( fn.addr(), 42 );
    REQUIRE( node_to_string( fn.addr() ) == "42" );
    fn.Delete();
}

TEST_CASE( "pjson serial: to_string for negative integer", "[pjson][serial][to_string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_int( fn.addr(), -100 );
    REQUIRE( node_to_string( fn.addr() ) == "-100" );
    fn.Delete();
}

TEST_CASE( "pjson serial: to_string for uinteger", "[pjson][serial][to_string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_uint( fn.addr(), 12345678901ULL );
    REQUIRE( node_to_string( fn.addr() ) == "12345678901" );
    fn.Delete();
}

TEST_CASE( "pjson serial: to_string for real", "[pjson][serial][to_string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_real( fn.addr(), 3.14 );
    std::string s = node_to_string( fn.addr() );
    // Проверяем, что строка корректно восстанавливается через strtod.
    double v = std::strtod( s.c_str(), nullptr );
    REQUIRE( v == 3.14 );
    fn.Delete();
}

TEST_CASE( "pjson serial: to_string for string", "[pjson][serial][to_string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_string( fn.addr(), "hello" );
    REQUIRE( node_to_string( fn.addr() ) == "\"hello\"" );
    fn.Delete();
}

TEST_CASE( "pjson serial: to_string for empty string", "[pjson][serial][to_string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_string( fn.addr(), "" );
    REQUIRE( node_to_string( fn.addr() ) == "\"\"" );
    fn.Delete();
}

TEST_CASE( "pjson serial: to_string for array", "[pjson][serial][to_string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_array( fn.addr() );
    node_id s0 = node_array_push_back( fn.addr() );
    node_set_int( s0, 1 );
    node_id s1 = node_array_push_back( fn.addr() );
    node_set_int( s1, 2 );
    node_id s2 = node_array_push_back( fn.addr() );
    node_set_int( s2, 3 );
    std::string s = node_to_string( fn.addr() );

    // Парсим обратно и проверяем структуру.
    fptr<node> parsed;
    parsed.New();
    node_from_string( s.c_str(), parsed.addr() );
    node_view arr{ parsed.addr() };
    REQUIRE( arr.is_array() );
    REQUIRE( arr.size() == 3u );
    REQUIRE( arr.at( (uintptr_t)0 ).as_uint() == 1u );
    REQUIRE( arr.at( (uintptr_t)1 ).as_uint() == 2u );
    REQUIRE( arr.at( (uintptr_t)2 ).as_uint() == 3u );
    fn.Delete();
    parsed.Delete();
}

TEST_CASE( "pjson serial: to_string for empty array", "[pjson][serial][to_string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_array( fn.addr() );
    REQUIRE( node_to_string( fn.addr() ) == "[]" );
    fn.Delete();
}

TEST_CASE( "pjson serial: to_string for object", "[pjson][serial][to_string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_object( fn.addr() );
    node_id sa = node_object_insert( fn.addr(), "a" );
    node_set_int( sa, 1 );
    node_id sb = node_object_insert( fn.addr(), "b" );
    node_set_string( sb, "val" );
    std::string s = node_to_string( fn.addr() );

    // Парсим обратно и проверяем значения.
    fptr<node> parsed;
    parsed.New();
    node_from_string( s.c_str(), parsed.addr() );
    node_view obj{ parsed.addr() };
    REQUIRE( obj.is_object() );
    REQUIRE( obj.at( "a" ).as_uint() == 1u );
    REQUIRE( obj.at( "b" ).as_string() == "val" );
    fn.Delete();
    parsed.Delete();
}

TEST_CASE( "pjson serial: to_string for empty object", "[pjson][serial][to_string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_object( fn.addr() );
    REQUIRE( node_to_string( fn.addr() ) == "{}" );
    fn.Delete();
}

TEST_CASE( "pjson serial: to_string for nested structure", "[pjson][serial][to_string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_object( fn.addr() );
    node_id nums_slot = node_object_insert( fn.addr(), "nums" );
    node_set_array( nums_slot );
    node_id e0 = node_array_push_back( nums_slot );
    node_set_int( e0, 10 );
    node_id e1 = node_array_push_back( nums_slot );
    node_set_bool( e1, true );
    std::string s = node_to_string( fn.addr() );

    // Парсим обратно и проверяем структуру.
    fptr<node> parsed;
    parsed.New();
    node_from_string( s.c_str(), parsed.addr() );
    node_view obj{ parsed.addr() };
    REQUIRE( obj.is_object() );
    node_view nums = obj.at( "nums" );
    REQUIRE( nums.valid() );
    REQUIRE( nums.is_array() );
    REQUIRE( nums.at( (uintptr_t)0 ).as_uint() == 10u );
    REQUIRE( nums.at( (uintptr_t)1 ).as_bool() == true );
    fn.Delete();
    parsed.Delete();
}

// ===========================================================================
// Тесты node_from_string()
// ===========================================================================

TEST_CASE( "pjson serial: from_string for null", "[pjson][serial][from_string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( "null", fn.addr() );
    REQUIRE( node_view{ fn.addr() }.is_null() );
    fn.Delete();
}

TEST_CASE( "pjson serial: from_string for boolean true", "[pjson][serial][from_string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( "true", fn.addr() );
    node_view v{ fn.addr() };
    REQUIRE( v.is_boolean() );
    REQUIRE( v.as_bool() == true );
    fn.Delete();
}

TEST_CASE( "pjson serial: from_string for boolean false", "[pjson][serial][from_string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( "false", fn.addr() );
    node_view v{ fn.addr() };
    REQUIRE( v.is_boolean() );
    REQUIRE( v.as_bool() == false );
    fn.Delete();
}

TEST_CASE( "pjson serial: from_string for integer", "[pjson][serial][from_string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    // Неотрицательное целое → node_tag::uinteger.
    node_from_string( "42", fn.addr() );
    node_view v{ fn.addr() };
    REQUIRE( v.is_uinteger() );
    REQUIRE( v.as_uint() == 42u );
    fn.Delete();
}

TEST_CASE( "pjson serial: from_string for negative integer", "[pjson][serial][from_string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( "-100", fn.addr() );
    node_view v{ fn.addr() };
    REQUIRE( v.is_integer() );
    REQUIRE( v.as_int() == -100 );
    fn.Delete();
}

TEST_CASE( "pjson serial: from_string for real", "[pjson][serial][from_string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( "3.14", fn.addr() );
    node_view v{ fn.addr() };
    REQUIRE( v.is_real() );
    REQUIRE( v.as_double() == 3.14 );
    fn.Delete();
}

TEST_CASE( "pjson serial: from_string for string", "[pjson][serial][from_string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( "\"hello\"", fn.addr() );
    node_view v{ fn.addr() };
    REQUIRE( v.is_string() );
    REQUIRE( v.as_string() == "hello" );
    fn.Delete();
}

TEST_CASE( "pjson serial: from_string for empty string", "[pjson][serial][from_string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( "\"\"", fn.addr() );
    node_view v{ fn.addr() };
    REQUIRE( v.is_string() );
    REQUIRE( v.size() == 0u );
    fn.Delete();
}

TEST_CASE( "pjson serial: from_string for array", "[pjson][serial][from_string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( "[1,2,3]", fn.addr() );
    node_view v{ fn.addr() };
    REQUIRE( v.is_array() );
    REQUIRE( v.size() == 3u );
    REQUIRE( v.at( (uintptr_t)0 ).as_uint() == 1u );
    REQUIRE( v.at( (uintptr_t)1 ).as_uint() == 2u );
    REQUIRE( v.at( (uintptr_t)2 ).as_uint() == 3u );
    fn.Delete();
}

TEST_CASE( "pjson serial: from_string for empty array", "[pjson][serial][from_string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( "[]", fn.addr() );
    node_view v{ fn.addr() };
    REQUIRE( v.is_array() );
    REQUIRE( v.size() == 0u );
    fn.Delete();
}

TEST_CASE( "pjson serial: from_string for object", "[pjson][serial][from_string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( "{\"a\":1,\"b\":\"val\"}", fn.addr() );
    node_view v{ fn.addr() };
    REQUIRE( v.is_object() );
    REQUIRE( v.size() == 2u );
    REQUIRE( v.at( "a" ).as_uint() == 1u );
    REQUIRE( v.at( "b" ).as_string() == "val" );
    fn.Delete();
}

TEST_CASE( "pjson serial: from_string for empty object", "[pjson][serial][from_string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( "{}", fn.addr() );
    node_view v{ fn.addr() };
    REQUIRE( v.is_object() );
    REQUIRE( v.size() == 0u );
    fn.Delete();
}

TEST_CASE( "pjson serial: from_string ignores invalid JSON", "[pjson][serial][from_string]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( "not valid json", fn.addr() );
    REQUIRE( node_view{ fn.addr() }.is_null() );
    fn.Delete();
}

// ===========================================================================
// Round-trip тесты (from_string → to_string)
// ===========================================================================

TEST_CASE( "pjson serial: round-trip simple object", "[pjson][serial][roundtrip]" )
{
    // Ключи хранятся в отсортированном порядке, поэтому нормализуем оригинал.
    const std::string original = "{\"key\":\"value\",\"num\":42}";

    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( original.c_str(), fn.addr() );
    std::string restored = node_to_string( fn.addr() );
    REQUIRE( normalize_json( original ) == restored );
    fn.Delete();
}

TEST_CASE( "pjson serial: round-trip nested structure", "[pjson][serial][roundtrip]" )
{
    const std::string original = "{\"arr\":[1,true,null,\"str\"],\"obj\":{\"x\":3.14}}";

    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( original.c_str(), fn.addr() );
    std::string restored = node_to_string( fn.addr() );
    REQUIRE( normalize_json( original ) == restored );
    fn.Delete();
}

#ifndef TEST_JSON_PATH
#define TEST_JSON_PATH "tests/test.json"
#endif

TEST_CASE( "pjson serial: round-trip test.json via node_from_string and node_to_string",
           "[pjson][serial][roundtrip][large]" )
{
    // Предварительно резервируем ёмкость карты слотов ПАМ.
    reset_pam();
    pam_pmm_reserve_slots( 200000 );

    // Загружаем test.json как строку.
    std::ifstream fin( TEST_JSON_PATH );
    REQUIRE( fin.is_open() );
    std::string json_text( ( std::istreambuf_iterator<char>( fin ) ), std::istreambuf_iterator<char>() );
    fin.close();

    // Загружаем в узел через node_from_string.
    fptr<node> fn;
    fn.New();
    node_from_string( json_text.c_str(), fn.addr() );
    REQUIRE( !node_view{ fn.addr() }.is_null() );

    // Сериализуем обратно в строку.
    std::string restored_str = node_to_string( fn.addr() );

    // Нормализуем оригинал через parse + serialize.
    fptr<node> forig;
    forig.New();
    node_from_string( json_text.c_str(), forig.addr() );
    std::string original_normalized = node_to_string( forig.addr() );

    // Сравниваем нормализованные версии.
    REQUIRE( original_normalized == restored_str );

    // Сбрасываем ПАМ целиком — быстрее O(1) vs O(n²) поэлементной очистки.
    pam_pmm_reset();
}
