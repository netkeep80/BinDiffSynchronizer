// test_pjson_serial.cpp - Tests for pjson serialization/deserialization (Phase 7).
//
// Tests methods:
//   pjson::to_string()     - serialize pjson to JSON string
//   pjson::from_string()   - deserialize JSON string into pjson
//
// Dependencies: nlohmann/json.hpp (via third_party/), Catch2

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <cstring>
#include <fstream>

#include "nlohmann/json.hpp"
#include "pjson.h"

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Tests for to_string()
// ---------------------------------------------------------------------------

TEST_CASE( "pjson serial: to_string for null", "[pjson][serial][to_string]" )
{
    fptr<pjson> fv;
    fv.New();
    // By default pjson = null.
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
    // nlohmann::json serializes uint64_t as number without quotes.
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
    // nlohmann::json serializes 3.14 as "3.14".
    // Verify the string parses back to the same value.
    double v = json::parse( s ).get<double>();
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
    // nlohmann::json dump() with default separators (no spaces).
    json parsed = json::parse( s );
    REQUIRE( parsed.is_array() );
    REQUIRE( parsed.size() == 3u );
    REQUIRE( parsed[0].get<int>() == 1 );
    REQUIRE( parsed[1].get<int>() == 2 );
    REQUIRE( parsed[2].get<int>() == 3 );
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
    std::string s      = fv->to_string();
    json        parsed = json::parse( s );
    REQUIRE( parsed.is_object() );
    REQUIRE( parsed["a"].get<int>() == 1 );
    REQUIRE( parsed["b"].get<std::string>() == "val" );
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
    std::string s      = fv->to_string();
    json        parsed = json::parse( s );
    REQUIRE( parsed.is_object() );
    REQUIRE( parsed["nums"].is_array() );
    REQUIRE( parsed["nums"][0].get<int>() == 10 );
    REQUIRE( parsed["nums"][1].get<bool>() == true );
    fv->free();
    fv.Delete();
}

// ---------------------------------------------------------------------------
// Tests for from_string()
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
    // nlohmann::json parses non-negative integer as number_unsigned,
    // so we expect pjson_type::uinteger for "42".
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
    REQUIRE( ( *fv )[0].get_int() == 1 );
    REQUIRE( ( *fv )[1].get_int() == 2 );
    REQUIRE( ( *fv )[2].get_int() == 3 );
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
    REQUIRE( a->get_int() == 1 );
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
    // Before from_string pjson = null.
    // With invalid JSON nothing should change (is_discarded -> return).
    pjson::from_string( "not valid json", fv.addr() );
    // Type should remain null (from_string was not applied).
    REQUIRE( fv->is_null() );
    fv->free();
    fv.Delete();
}

// ---------------------------------------------------------------------------
// Round-trip tests (from_string -> to_string)
// ---------------------------------------------------------------------------

TEST_CASE( "pjson serial: round-trip simple object", "[pjson][serial][roundtrip]" )
{
    const std::string original        = "{\"key\":\"value\",\"num\":42}";
    json              original_parsed = json::parse( original );

    fptr<pjson> fv;
    fv.New();
    pjson::from_string( original.c_str(), fv.addr() );

    std::string restored        = fv->to_string();
    json        restored_parsed = json::parse( restored );

    // Compare via dump() to normalize key order.
    REQUIRE( original_parsed.dump() == restored_parsed.dump() );

    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson serial: round-trip nested structure", "[pjson][serial][roundtrip]" )
{
    const std::string original        = "{\"arr\":[1,true,null,\"str\"],\"obj\":{\"x\":3.14}}";
    json              original_parsed = json::parse( original );

    fptr<pjson> fv;
    fv.New();
    pjson::from_string( original.c_str(), fv.addr() );

    std::string restored        = fv->to_string();
    json        restored_parsed = json::parse( restored );

    REQUIRE( original_parsed.dump() == restored_parsed.dump() );

    fv->free();
    fv.Delete();
}

#ifndef TEST_JSON_PATH
#define TEST_JSON_PATH "tests/test.json"
#endif

TEST_CASE( "pjson serial: round-trip test.json via from_string and to_string", "[pjson][serial][roundtrip][large]" )
{
    // Load test.json as string.
    std::ifstream fin( TEST_JSON_PATH );
    REQUIRE( fin.is_open() );
    std::string json_text( ( std::istreambuf_iterator<char>( fin ) ), std::istreambuf_iterator<char>() );
    fin.close();

    // Parse original via nlohmann for subsequent comparison.
    json original = json::parse( json_text );

    // Load into pjson via from_string.
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( json_text.c_str(), fv.addr() );

    REQUIRE( !fv->is_null() );

    // Serialize back to string.
    std::string restored_str = fv->to_string();
    json        restored     = json::parse( restored_str );

    // Compare original and restored JSON.
    REQUIRE( original.dump() == restored.dump() );

    fv->free();
    fv.Delete();
}
