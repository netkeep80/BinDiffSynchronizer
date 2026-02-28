// test_pjson_bench.cpp — Бенчмарки для оптимизаций pjson из задачи #84.
//
// Сравнивает производительность:
//   - to_string (прямой) vs nlohmann::json::dump (через pjson_to_nlohmann)
//   - from_string (прямой) vs nlohmann::json::parse (через pjson_from_nlohmann)
//   - set_string vs set_string_interned (дедупликация)
//   - pjson_node_pool::alloc vs fptr<pjson>::New
//
// Все бенчмарки выводят измеренное время.
// Тест ПРОХОДИТ всегда — бенчмарки информационные, не требования.
//
// Все комментарии — на русском языке (Тр.6).

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>
#include <chrono>
#include <vector>
#include <cstdio>
#include <fstream>

#include "nlohmann/json.hpp"
#include "pjson.h"

using json      = nlohmann::json;
using bench_clk = std::chrono::high_resolution_clock;
using bench_ms  = std::chrono::milliseconds;

// Вспомогательная функция: вычислить прошедшие миллисекунды.
template <typename T> static long long bench_elapsed_ms( const T& start )
{
    return std::chrono::duration_cast<bench_ms>( bench_clk::now() - start ).count();
}

// Вспомогательная функция: конвертировать pjson → nlohmann::json (тест-копия из test_pjson_large.cpp).
static json bench_pjson_to_nlohmann( const pjson& src )
{
    switch ( src.type_tag() )
    {
    case pjson_type::null:
        return json( nullptr );
    case pjson_type::boolean:
        return json( src.get_bool() );
    case pjson_type::integer:
        return json( src.get_int() );
    case pjson_type::uinteger:
        return json( src.get_uint() );
    case pjson_type::real:
        return json( src.get_real() );
    case pjson_type::string:
        return json( std::string( src.get_string() ) );
    case pjson_type::array:
    {
        json arr = json::array();
        for ( uintptr_t i = 0; i < src.size(); i++ )
            arr.push_back( bench_pjson_to_nlohmann( src[i] ) );
        return arr;
    }
    case pjson_type::object:
    {
        json      obj      = json::object();
        uintptr_t sz       = src.size();
        uintptr_t data_adr = src.payload.object_val.data.addr();
        for ( uintptr_t i = 0; i < sz; i++ )
        {
            const pjson_kv_entry& pair = AddressManager<pjson_kv_entry>::GetArrayElement( data_adr, i );
            obj[pair.key.c_str()]      = bench_pjson_to_nlohmann( pair.value );
        }
        return obj;
    }
    default:
        return json( nullptr );
    }
}

// Вспомогательная функция: конвертировать nlohmann::json → pjson (тест-копия из test_pjson_large.cpp).
static void bench_nlohmann_to_pjson( const json& src, uintptr_t dst_offset )
{
    auto&  pam = PersistentAddressSpace::Get();
    pjson* dst = pam.Resolve<pjson>( dst_offset );
    if ( dst == nullptr )
        return;

    switch ( src.type() )
    {
    case json::value_t::null:
        pam.Resolve<pjson>( dst_offset )->set_null();
        break;
    case json::value_t::boolean:
        pam.Resolve<pjson>( dst_offset )->set_bool( src.get<bool>() );
        break;
    case json::value_t::number_integer:
        pam.Resolve<pjson>( dst_offset )->set_int( src.get<int64_t>() );
        break;
    case json::value_t::number_unsigned:
        pam.Resolve<pjson>( dst_offset )->set_uint( src.get<uint64_t>() );
        break;
    case json::value_t::number_float:
        pam.Resolve<pjson>( dst_offset )->set_real( src.get<double>() );
        break;
    case json::value_t::string:
        pam.Resolve<pjson>( dst_offset )->set_string( src.get<std::string>().c_str() );
        break;
    case json::value_t::array:
    {
        pam.Resolve<pjson>( dst_offset )->set_array();
        for ( const auto& elem : src )
        {
            pjson&    new_elem = pam.Resolve<pjson>( dst_offset )->push_back();
            uintptr_t elem_off = pam.PtrToOffset( &new_elem );
            bench_nlohmann_to_pjson( elem, elem_off );
        }
        break;
    }
    case json::value_t::object:
    {
        pam.Resolve<pjson>( dst_offset )->set_object();
        for ( const auto& [key, val] : src.items() )
        {
            pjson&    new_val = pam.Resolve<pjson>( dst_offset )->obj_insert( key.c_str() );
            uintptr_t val_off = pam.PtrToOffset( &new_val );
            bench_nlohmann_to_pjson( val, val_off );
        }
        break;
    }
    default:
        pam.Resolve<pjson>( dst_offset )->set_null();
        break;
    }
}

// ============================================================================
// Бенчмарк: to_string (прямой F6) vs nlohmann dump
// ============================================================================

TEST_CASE( "pjson bench: to_string direct vs nlohmann dump", "[pjson][bench][serial]" )
{
    // Создаём достаточно сложный JSON-документ.
    // Важно: после каждой операции аллокации в ПАМ указатели могут инвалидироваться.
    // Поэтому сохраняем смещение и каждый раз переразрешаем через obj_find.
    auto&       pam = PersistentAddressSpace::Get();
    fptr<pjson> froot;
    froot.New();
    froot->set_object();
    for ( int i = 0; i < 100; i++ )
    {
        char key[16];
        std::snprintf( key, sizeof( key ), "key_%03d", i );
        froot->obj_insert( key );
        // Переразрешаем указатель после возможной реаллокации ПАМ.
        pjson* val = pam.Resolve<pjson>( froot.addr() )->obj_find( key );
        if ( i % 5 == 0 )
        {
            uintptr_t val_off = pam.PtrToOffset( val );
            pam.Resolve<pjson>( val_off )->set_array();
            for ( int j = 0; j < 10; j++ )
            {
                // Переразрешаем val_off после каждого push_back.
                pam.Resolve<pjson>( val_off )->push_back().set_int( j );
            }
        }
        else if ( i % 3 == 0 )
        {
            val->set_string( "hello world string value" );
        }
        else if ( i % 2 == 0 )
        {
            val->set_real( 3.14159 * i );
        }
        else
        {
            val->set_int( i * 42 );
        }
    }

    constexpr int ITERATIONS = 500;

    // Прямая сериализация (F6).
    auto t1 = bench_clk::now();
    for ( int n = 0; n < ITERATIONS; n++ )
    {
        std::string s = froot->to_string();
        (void)s;
    }
    long long direct_ms = bench_elapsed_ms( t1 );

    // Сериализация через nlohmann (копия pjson → nlohmann → dump).
    auto t2 = bench_clk::now();
    for ( int n = 0; n < ITERATIONS; n++ )
    {
        json        j = bench_pjson_to_nlohmann( *froot );
        std::string s = j.dump();
        (void)s;
    }
    long long nlohmann_ms = bench_elapsed_ms( t2 );

    std::printf( "[bench] to_string %d iter: direct=%lld ms, via_nlohmann=%lld ms\n", ITERATIONS, direct_ms,
                 nlohmann_ms );

    froot->free();
    froot.Delete();

    // Бенчмарк информационный — тест всегда проходит.
    REQUIRE( true );
}

// ============================================================================
// Бенчмарк: from_string (прямой F6) vs nlohmann parse + pjson copy
// ============================================================================

TEST_CASE( "pjson bench: from_string direct vs nlohmann parse", "[pjson][bench][parse]" )
{
    const std::string test_json = "{\"name\":\"Alice\",\"age\":30,\"scores\":[95,87,92,88,100],"
                                  "\"address\":{\"city\":\"Moscow\",\"zip\":\"123456\"},"
                                  "\"tags\":[\"developer\",\"architect\",\"speaker\"],"
                                  "\"active\":true,\"balance\":12345.67}";

    constexpr int ITERATIONS = 1000;

    // Прямой парсер (F6).
    auto t1 = bench_clk::now();
    for ( int n = 0; n < ITERATIONS; n++ )
    {
        fptr<pjson> fv;
        fv.New();
        pjson::from_string( test_json.c_str(), fv.addr() );
        fv->free();
        fv.Delete();
    }
    long long direct_ms = bench_elapsed_ms( t1 );

    // Парсинг через nlohmann + копия в pjson.
    auto t2 = bench_clk::now();
    for ( int n = 0; n < ITERATIONS; n++ )
    {
        json        j = json::parse( test_json );
        fptr<pjson> fv;
        fv.New();
        bench_nlohmann_to_pjson( j, fv.addr() );
        fv->free();
        fv.Delete();
    }
    long long nlohmann_ms = bench_elapsed_ms( t2 );

    std::printf( "[bench] from_string %d iter: direct=%lld ms, via_nlohmann=%lld ms\n", ITERATIONS, direct_ms,
                 nlohmann_ms );

    REQUIRE( true );
}

// ============================================================================
// Бенчмарк: set_string vs set_string_interned
// ============================================================================

TEST_CASE( "pjson bench: set_string vs set_string_interned", "[pjson][bench][interning]" )
{
    const char*   keys[] = { "name", "age", "city", "country", "email", "phone", "address", "zip", "active", "score" };
    constexpr int N_KEYS = 10;
    constexpr int REPEAT = 100;
    constexpr int TOTAL  = N_KEYS * REPEAT;

    auto& pam = PersistentAddressSpace::Get();

    // Бенчмарк set_string (обычный).
    {
        auto                   t1 = bench_clk::now();
        std::vector<uintptr_t> offsets;
        offsets.reserve( TOTAL );
        for ( int i = 0; i < TOTAL; i++ )
        {
            fptr<pjson> fv;
            fv.New();
            fv->set_string( keys[i % N_KEYS] );
            offsets.push_back( fv.addr() );
        }
        long long alloc_ms = bench_elapsed_ms( t1 );

        for ( uintptr_t off : offsets )
        {
            fptr<pjson> fv;
            fv.set_addr( off );
            fv->free();
            fv.Delete();
        }
        std::printf( "[bench] set_string %d nodes: %lld ms\n", TOTAL, alloc_ms );
    }

    // Бенчмарк set_string_interned (с дедупликацией).
    {
        fptr<pjson_string_table> tbl;
        tbl.New();
        uintptr_t tbl_off = tbl.addr();

        auto                   t2 = bench_clk::now();
        std::vector<uintptr_t> offsets;
        offsets.reserve( TOTAL );
        for ( int i = 0; i < TOTAL; i++ )
        {
            fptr<pjson> fv;
            fv.New();
            fv->set_string_interned( keys[i % N_KEYS], tbl_off );
            offsets.push_back( fv.addr() );
        }
        long long interned_ms = bench_elapsed_ms( t2 );

        for ( uintptr_t off : offsets )
        {
            fptr<pjson> fv;
            fv.set_addr( off );
            fv->free();
            fv.Delete();
        }
        tbl.Delete();
        std::printf( "[bench] set_string_interned %d nodes: %lld ms\n", TOTAL, interned_ms );
    }

    REQUIRE( true );
}

// ============================================================================
// Бенчмарк: pjson_node_pool::alloc vs fptr<pjson>::New
// ============================================================================

TEST_CASE( "pjson bench: pool alloc vs fptr New", "[pjson][bench][pool]" )
{
    constexpr int N = 500;

    auto& pam = PersistentAddressSpace::Get();

    // Бенчмарк fptr<pjson>::New (обычная аллокация через ПАМ).
    {
        auto                   t1 = bench_clk::now();
        std::vector<uintptr_t> offsets;
        offsets.reserve( N );
        for ( int i = 0; i < N; i++ )
        {
            fptr<pjson> fv;
            fv.New();
            offsets.push_back( fv.addr() );
        }
        long long alloc_ms = bench_elapsed_ms( t1 );

        for ( uintptr_t off : offsets )
        {
            fptr<pjson> fv;
            fv.set_addr( off );
            fv.Delete();
        }
        std::printf( "[bench] fptr<pjson>::New %d allocs: %lld ms\n", N, alloc_ms );
    }

    // Бенчмарк pjson_node_pool::alloc (пул узлов F2).
    {
        fptr<pjson_node_pool> pool;
        pool.New();

        auto                   t2 = bench_clk::now();
        std::vector<uintptr_t> offsets;
        offsets.reserve( N );
        for ( int i = 0; i < N; i++ )
        {
            uintptr_t off = pool->alloc();
            offsets.push_back( off );
        }
        long long pool_alloc_ms = bench_elapsed_ms( t2 );

        for ( uintptr_t off : offsets )
            pool->dealloc( off );
        pool.Delete();

        std::printf( "[bench] pjson_node_pool::alloc %d allocs: %lld ms\n", N, pool_alloc_ms );
    }

    REQUIRE( true );
}

// ============================================================================
// Бенчмарк: round-trip с крупным JSON-файлом (test.json)
// ============================================================================

#ifndef TEST_JSON_PATH
#define TEST_JSON_PATH "tests/test.json"
#endif

TEST_CASE( "pjson bench: round-trip test.json direct", "[pjson][bench][roundtrip]" )
{
    std::ifstream fin( TEST_JSON_PATH );
    if ( !fin.is_open() )
    {
        std::printf( "[bench] Cannot open %s — skipping\n", TEST_JSON_PATH );
        REQUIRE( true );
        return;
    }
    std::string json_text( ( std::istreambuf_iterator<char>( fin ) ), std::istreambuf_iterator<char>() );
    fin.close();

    std::printf( "[bench] test.json size: %zu bytes\n", json_text.size() );

    // Прямой парсер + прямой сериализатор (F6).
    {
        auto        t1 = bench_clk::now();
        fptr<pjson> fv;
        fv.New();
        pjson::from_string( json_text.c_str(), fv.addr() );
        long long parse_ms = bench_elapsed_ms( t1 );

        auto        t2     = bench_clk::now();
        std::string s_out  = fv->to_string();
        long long   ser_ms = bench_elapsed_ms( t2 );

        fv->free();
        fv.Delete();
        std::printf( "[bench] direct F6: parse=%lld ms, serialize=%lld ms, output_size=%zu\n", parse_ms, ser_ms,
                     s_out.size() );
    }

    // Nlohmann-based: parse + copy into pjson + dump via pjson_to_nlohmann.
    {
        auto      t1                = bench_clk::now();
        json      nlohmann_doc      = json::parse( json_text );
        long long nlohmann_parse_ms = bench_elapsed_ms( t1 );

        fptr<pjson> fv;
        fv.New();

        auto t2 = bench_clk::now();
        bench_nlohmann_to_pjson( nlohmann_doc, fv.addr() );
        long long copy_ms = bench_elapsed_ms( t2 );

        auto        t3      = bench_clk::now();
        json        jout    = bench_pjson_to_nlohmann( *fv );
        std::string s_out2  = jout.dump();
        long long   dump_ms = bench_elapsed_ms( t3 );

        fv->free();
        fv.Delete();

        std::printf( "[bench] nlohmann+copy: parse=%lld ms, copy_to_pjson=%lld ms, pjson_to_nlohmann+dump=%lld ms\n",
                     nlohmann_parse_ms, copy_ms, dump_ms );
    }

    REQUIRE( true );
}
