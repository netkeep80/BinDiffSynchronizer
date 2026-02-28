// test_pjson_serial.cpp — Тесты сериализации/десериализации pjson (задача #84, F6).
//
// Тестирует методы:
//   pjson::to_string()     — сериализация pjson в строку JSON
//   pjson::from_string()   — десериализация строки JSON в pjson
//
// Зависимости: Catch2, pjson.h

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <cstring>
#include <fstream>

#include "pjson.h"

// ---------------------------------------------------------------------------
// Вспомогательная функция: нормализовать JSON через pjson (parse + serialize).
// Используется для сравнения JSON, игнорируя различия в пробелах и порядке ключей.
// ---------------------------------------------------------------------------
static std::string normalize_json( const std::string& json_text )
{
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( json_text.c_str(), fv.addr() );
    std::string result = fv->to_string();
    fv->free();
    fv.Delete();
    return result;
}

// ---------------------------------------------------------------------------
// Тесты to_string()
// ---------------------------------------------------------------------------

TEST_CASE( "pjson serial: to_string for null", "[pjson][serial][to_string]" )
{
    fptr<pjson> fv;
    fv.New();
    REQUIRE( fv->is_null() );
    REQUIRE( fv->to_string() == "null" );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson serial: to_string for boolean true", "[pjson][serial][to_string]" )
{
    fptr<pjson> fv;
    fv.New();
    fv->set_bool( true );
    REQUIRE( fv->to_string() == "true" );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson serial: to_string for boolean false", "[pjson][serial][to_string]" )
{
    fptr<pjson> fv;
    fv.New();
    fv->set_bool( false );
    REQUIRE( fv->to_string() == "false" );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson serial: to_string for integer", "[pjson][serial][to_string]" )
{
    fptr<pjson> fv;
    fv.New();
    fv->set_int( 42 );
    REQUIRE( fv->to_string() == "42" );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson serial: to_string for negative integer", "[pjson][serial][to_string]" )
{
    fptr<pjson> fv;
    fv.New();
    fv->set_int( -100 );
    REQUIRE( fv->to_string() == "-100" );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson serial: to_string for uinteger", "[pjson][serial][to_string]" )
{
    fptr<pjson> fv;
    fv.New();
    fv->set_uint( 12345678901ULL );
    std::string s = fv->to_string();
    REQUIRE( s == "12345678901" );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson serial: to_string for real", "[pjson][serial][to_string]" )
{
    fptr<pjson> fv;
    fv.New();
    fv->set_real( 3.14 );
    std::string s = fv->to_string();
    // Проверяем, что строка корректно восстанавливается через strtod.
    double v = std::strtod( s.c_str(), nullptr );
    REQUIRE( v == 3.14 );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson serial: to_string for string", "[pjson][serial][to_string]" )
{
    fptr<pjson> fv;
    fv.New();
    fv->set_string( "hello" );
    REQUIRE( fv->to_string() == "\"hello\"" );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson serial: to_string for empty string", "[pjson][serial][to_string]" )
{
    fptr<pjson> fv;
    fv.New();
    fv->set_string( "" );
    REQUIRE( fv->to_string() == "\"\"" );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson serial: to_string for array", "[pjson][serial][to_string]" )
{
    fptr<pjson> fv;
    fv.New();
    fv->set_array();
    fv->push_back().set_int( 1 );
    fv->push_back().set_int( 2 );
    fv->push_back().set_int( 3 );
    std::string s = fv->to_string();
    // Парсим обратно через pjson и проверяем структуру.
    fptr<pjson> parsed;
    parsed.New();
    pjson::from_string( s.c_str(), parsed.addr() );
    REQUIRE( parsed->is_array() );
    REQUIRE( parsed->size() == 3u );
    REQUIRE( ( *parsed )[0].get_uint() == 1u );
    REQUIRE( ( *parsed )[1].get_uint() == 2u );
    REQUIRE( ( *parsed )[2].get_uint() == 3u );
    parsed->free();
    parsed.Delete();
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson serial: to_string for empty array", "[pjson][serial][to_string]" )
{
    fptr<pjson> fv;
    fv.New();
    fv->set_array();
    REQUIRE( fv->to_string() == "[]" );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson serial: to_string for object", "[pjson][serial][to_string]" )
{
    fptr<pjson> fv;
    fv.New();
    fv->set_object();
    fv->obj_insert( "a" ).set_int( 1 );
    fv->obj_insert( "b" ).set_string( "val" );
    std::string s = fv->to_string();
    // Парсим обратно и проверяем значения.
    fptr<pjson> parsed;
    parsed.New();
    pjson::from_string( s.c_str(), parsed.addr() );
    REQUIRE( parsed->is_object() );
    pjson* a = parsed->obj_find( "a" );
    pjson* b = parsed->obj_find( "b" );
    REQUIRE( a != nullptr );
    REQUIRE( b != nullptr );
    REQUIRE( a->get_uint() == 1u );
    REQUIRE( std::strcmp( b->get_string(), "val" ) == 0 );
    parsed->free();
    parsed.Delete();
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson serial: to_string for empty object", "[pjson][serial][to_string]" )
{
    fptr<pjson> fv;
    fv.New();
    fv->set_object();
    REQUIRE( fv->to_string() == "{}" );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson serial: to_string for nested structure", "[pjson][serial][to_string]" )
{
    fptr<pjson> fv;
    fv.New();
    fv->set_object();
    fv->obj_insert( "nums" ).set_array();
    pjson* arr = fv->obj_find( "nums" );
    REQUIRE( arr != nullptr );
    arr->push_back().set_int( 10 );
    arr->push_back().set_bool( true );
    std::string s = fv->to_string();
    // Парсим обратно и проверяем структуру.
    fptr<pjson> parsed;
    parsed.New();
    pjson::from_string( s.c_str(), parsed.addr() );
    REQUIRE( parsed->is_object() );
    pjson* nums = parsed->obj_find( "nums" );
    REQUIRE( nums != nullptr );
    REQUIRE( nums->is_array() );
    REQUIRE( ( *nums )[0].get_uint() == 10u );
    REQUIRE( ( *nums )[1].get_bool() == true );
    parsed->free();
    parsed.Delete();
    fv->free();
    fv.Delete();
}

// ---------------------------------------------------------------------------
// Тесты from_string()
// ---------------------------------------------------------------------------

TEST_CASE( "pjson serial: from_string for null", "[pjson][serial][from_string]" )
{
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( "null", fv.addr() );
    REQUIRE( fv->is_null() );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson serial: from_string for boolean true", "[pjson][serial][from_string]" )
{
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( "true", fv.addr() );
    REQUIRE( fv->is_boolean() );
    REQUIRE( fv->get_bool() == true );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson serial: from_string for boolean false", "[pjson][serial][from_string]" )
{
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( "false", fv.addr() );
    REQUIRE( fv->is_boolean() );
    REQUIRE( fv->get_bool() == false );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson serial: from_string for integer", "[pjson][serial][from_string]" )
{
    fptr<pjson> fv;
    fv.New();
    // Неотрицательное целое → pjson_type::uinteger.
    pjson::from_string( "42", fv.addr() );
    REQUIRE( fv->is_uinteger() );
    REQUIRE( fv->get_uint() == 42u );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson serial: from_string for negative integer", "[pjson][serial][from_string]" )
{
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( "-100", fv.addr() );
    REQUIRE( fv->is_integer() );
    REQUIRE( fv->get_int() == -100 );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson serial: from_string for real", "[pjson][serial][from_string]" )
{
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( "3.14", fv.addr() );
    REQUIRE( fv->is_real() );
    REQUIRE( fv->get_real() == 3.14 );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson serial: from_string for string", "[pjson][serial][from_string]" )
{
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( "\"hello\"", fv.addr() );
    REQUIRE( fv->is_string() );
    REQUIRE( std::strcmp( fv->get_string(), "hello" ) == 0 );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson serial: from_string for empty string", "[pjson][serial][from_string]" )
{
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( "\"\"", fv.addr() );
    REQUIRE( fv->is_string() );
    REQUIRE( fv->size() == 0u );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson serial: from_string for array", "[pjson][serial][from_string]" )
{
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( "[1,2,3]", fv.addr() );
    REQUIRE( fv->is_array() );
    REQUIRE( fv->size() == 3u );
    REQUIRE( ( *fv )[0].get_uint() == 1u );
    REQUIRE( ( *fv )[1].get_uint() == 2u );
    REQUIRE( ( *fv )[2].get_uint() == 3u );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson serial: from_string for empty array", "[pjson][serial][from_string]" )
{
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( "[]", fv.addr() );
    REQUIRE( fv->is_array() );
    REQUIRE( fv->size() == 0u );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson serial: from_string for object", "[pjson][serial][from_string]" )
{
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( "{\"a\":1,\"b\":\"val\"}", fv.addr() );
    REQUIRE( fv->is_object() );
    REQUIRE( fv->size() == 2u );
    pjson* a = fv->obj_find( "a" );
    REQUIRE( a != nullptr );
    REQUIRE( a->get_uint() == 1u );
    pjson* b = fv->obj_find( "b" );
    REQUIRE( b != nullptr );
    REQUIRE( std::strcmp( b->get_string(), "val" ) == 0 );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson serial: from_string for empty object", "[pjson][serial][from_string]" )
{
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( "{}", fv.addr() );
    REQUIRE( fv->is_object() );
    REQUIRE( fv->size() == 0u );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson serial: from_string ignores invalid JSON", "[pjson][serial][from_string]" )
{
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( "not valid json", fv.addr() );
    REQUIRE( fv->is_null() );
    fv->free();
    fv.Delete();
}

// ---------------------------------------------------------------------------
// Round-trip тесты (from_string → to_string)
// ---------------------------------------------------------------------------

TEST_CASE( "pjson serial: round-trip simple object", "[pjson][serial][roundtrip]" )
{
    // pjson хранит ключи в отсортированном порядке, поэтому нормализуем оригинал.
    const std::string original = "{\"key\":\"value\",\"num\":42}";

    fptr<pjson> fv;
    fv.New();
    pjson::from_string( original.c_str(), fv.addr() );

    std::string restored = fv->to_string();
    // Нормализуем оба через pjson для сравнения.
    REQUIRE( normalize_json( original ) == restored );

    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson serial: round-trip nested structure", "[pjson][serial][roundtrip]" )
{
    const std::string original = "{\"arr\":[1,true,null,\"str\"],\"obj\":{\"x\":3.14}}";

    fptr<pjson> fv;
    fv.New();
    pjson::from_string( original.c_str(), fv.addr() );

    std::string restored = fv->to_string();
    // После parse+serialize структура должна совпадать.
    REQUIRE( normalize_json( original ) == restored );

    fv->free();
    fv.Delete();
}

#ifndef TEST_JSON_PATH
#define TEST_JSON_PATH "tests/test.json"
#endif

TEST_CASE( "pjson serial: round-trip test.json via from_string and to_string", "[pjson][serial][roundtrip][large]" )
{
    // Предварительно резервируем ёмкость карты слотов ПАМ.
    // test.json содержит ~100k+ узлов; резервирование устраняет многократные
    // реаллокации и ускоряет разбор (задача #88).
    PersistentAddressSpace::Get().ReserveSlots( 200000 );

    // Загружаем test.json как строку.
    std::ifstream fin( TEST_JSON_PATH );
    REQUIRE( fin.is_open() );
    std::string json_text( ( std::istreambuf_iterator<char>( fin ) ), std::istreambuf_iterator<char>() );
    fin.close();

    // Загружаем в pjson через from_string.
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( json_text.c_str(), fv.addr() );

    REQUIRE( !fv->is_null() );

    // Сериализуем обратно в строку.
    std::string restored_str = fv->to_string();

    // Нормализуем оригинал через pjson (parse + serialize).
    std::string original_normalized = normalize_json( json_text );

    // Сравниваем нормализованные версии.
    REQUIRE( original_normalized == restored_str );

    // Сбрасываем ПАМ целиком — быстрее O(1) vs O(n²) поэлементной очистки.
    PersistentAddressSpace::Get().Reset();
}
