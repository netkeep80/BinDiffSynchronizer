// test_pjson_bench.cpp — Бенчмарки для новых оптимизаций pjson_db (Задача 9.5).
//
// Мигрировано с pjson.h на pjson_codec.h и pjson_pool.h в рамках Задачи 9.5.
//
// Измеряет производительность:
//   - node_to_string — сериализация узла в строку (pjson_codec)
//   - node_from_string — десериализация строки в узел (pjson_codec)
//   - pam_intern_string — интернирование строк (pstringview_table)
//   - pjson_pool::alloc vs fptr<node>::New
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

#include "pjson_codec.h"
#include "pjson_pool.h"

using bench_clk = std::chrono::high_resolution_clock;
using bench_ms  = std::chrono::milliseconds;

// Вспомогательная функция: вычислить прошедшие миллисекунды.
template <typename T> static long long bench_elapsed_ms( const T& start )
{
    return std::chrono::duration_cast<bench_ms>( bench_clk::now() - start ).count();
}

// Вспомогательная функция: сбросить ПАП перед каждым тестом.
static void reset_pam()
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();
}

// ============================================================================
// Бенчмарк: node_to_string
// ============================================================================

TEST_CASE( "pjson bench: to_string direct", "[pjson][bench][serial]" )
{
    reset_pam();

    // Создаём достаточно сложный JSON-документ.
    // После каждой аллокации указатели в ПАМ могут инвалидироваться.
    // Сохраняем смещение и работаем через node_id.
    fptr<node> froot;
    froot.New();
    node_set_object( froot.addr() );
    for ( int i = 0; i < 100; i++ )
    {
        char key[16];
        std::snprintf( key, sizeof( key ), "key_%03d", i );
        node_id val_slot = node_object_insert( froot.addr(), key );
        if ( i % 5 == 0 )
        {
            node_set_array( val_slot );
            for ( int j = 0; j < 10; j++ )
            {
                node_id e = node_array_push_back( val_slot );
                node_set_int( e, j );
            }
        }
        else if ( i % 3 == 0 )
        {
            node_set_string( val_slot, "hello world string value" );
        }
        else if ( i % 2 == 0 )
        {
            node_set_real( val_slot, 3.14159 * i );
        }
        else
        {
            node_set_int( val_slot, i * 42 );
        }
    }

    constexpr int ITERATIONS = 500;

    auto t1 = bench_clk::now();
    for ( int n = 0; n < ITERATIONS; n++ )
    {
        std::string s = node_to_string( froot.addr() );
        (void)s;
    }
    long long direct_ms = bench_elapsed_ms( t1 );

    std::printf( "[bench] node_to_string %d iter: direct=%lld ms\n", ITERATIONS, direct_ms );

    froot.Delete();

    // Бенчмарк информационный — тест всегда проходит.
    REQUIRE( true );
}

// ============================================================================
// Бенчмарк: node_from_string
// ============================================================================

TEST_CASE( "pjson bench: from_string direct", "[pjson][bench][parse]" )
{
    const std::string test_json = "{\"name\":\"Alice\",\"age\":30,\"scores\":[95,87,92,88,100],"
                                  "\"address\":{\"city\":\"Moscow\",\"zip\":\"123456\"},"
                                  "\"tags\":[\"developer\",\"architect\",\"speaker\"],"
                                  "\"active\":true,\"balance\":12345.67}";

    constexpr int ITERATIONS = 1000;

    auto t1 = bench_clk::now();
    for ( int n = 0; n < ITERATIONS; n++ )
    {
        reset_pam();
        fptr<node> fn;
        fn.New();
        node_from_string( test_json.c_str(), fn.addr() );
        fn.Delete();
    }
    long long direct_ms = bench_elapsed_ms( t1 );

    std::printf( "[bench] node_from_string %d iter: direct=%lld ms\n", ITERATIONS, direct_ms );

    REQUIRE( true );
}

// ============================================================================
// Бенчмарк: pam_intern_string (интернирование строк)
// ============================================================================

TEST_CASE( "pjson bench: pam_intern_string vs plain node_set_string", "[pjson][bench][interning]" )
{
    const char*   keys[] = { "name", "age", "city", "country", "email", "phone", "address", "zip", "active", "score" };
    constexpr int N_KEYS = 10;
    constexpr int REPEAT = 100;
    constexpr int TOTAL  = N_KEYS * REPEAT;

    // Бенчмарк node_set_string (обычный, без дедупликации).
    {
        reset_pam();
        auto                   t1 = bench_clk::now();
        std::vector<uintptr_t> offsets;
        offsets.reserve( TOTAL );
        for ( int i = 0; i < TOTAL; i++ )
        {
            fptr<node> fn;
            fn.New();
            node_set_string( fn.addr(), keys[i % N_KEYS] );
            offsets.push_back( fn.addr() );
        }
        long long alloc_ms = bench_elapsed_ms( t1 );
        std::printf( "[bench] node_set_string %d nodes: %lld ms\n", TOTAL, alloc_ms );
    }

    // Бенчмарк pam_intern_string (с дедупликацией через pstringview_table).
    {
        reset_pam();
        auto t2 = bench_clk::now();
        for ( int i = 0; i < TOTAL; i++ )
            pam_intern_string( keys[i % N_KEYS] );
        long long interned_ms = bench_elapsed_ms( t2 );
        std::printf( "[bench] pam_intern_string %d calls: %lld ms\n", TOTAL, interned_ms );
    }

    REQUIRE( true );
}

// ============================================================================
// Бенчмарк: pjson_pool::alloc vs fptr<node>::New
// ============================================================================

TEST_CASE( "pjson bench: pool alloc vs fptr New", "[pjson][bench][pool]" )
{
    constexpr int N = 500;

    // Бенчмарк fptr<node>::New (обычная аллокация через ПАМ).
    {
        reset_pam();
        auto                   t1 = bench_clk::now();
        std::vector<uintptr_t> offsets;
        offsets.reserve( N );
        for ( int i = 0; i < N; i++ )
        {
            fptr<node> fn;
            fn.New();
            offsets.push_back( fn.addr() );
        }
        long long alloc_ms = bench_elapsed_ms( t1 );

        for ( uintptr_t off : offsets )
        {
            fptr<node> fn;
            fn.set_addr( off );
            fn.Delete();
        }
        std::printf( "[bench] fptr<node>::New %d allocs: %lld ms\n", N, alloc_ms );
    }

    // Бенчмарк pjson_pool::alloc (пул узлов).
    {
        reset_pam();
        fptr<pjson_pool> pool;
        pool.New();

        auto                 t2 = bench_clk::now();
        std::vector<node_id> offsets;
        offsets.reserve( N );
        for ( int i = 0; i < N; i++ )
        {
            node_id off = pool->alloc();
            offsets.push_back( off );
        }
        long long pool_alloc_ms = bench_elapsed_ms( t2 );

        for ( node_id off : offsets )
            pool->free( off );
        pool.Delete();

        std::printf( "[bench] pjson_pool::alloc %d allocs: %lld ms\n", N, pool_alloc_ms );
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

    reset_pam();
    PersistentAddressSpace::Get().ReserveSlots( 200000 );

    // Парсинг через node_from_string.
    auto       t1 = bench_clk::now();
    fptr<node> fn;
    fn.New();
    node_from_string( json_text.c_str(), fn.addr() );
    long long parse_ms = bench_elapsed_ms( t1 );

    // Сериализация через node_to_string.
    auto        t2     = bench_clk::now();
    std::string s_out  = node_to_string( fn.addr() );
    long long   ser_ms = bench_elapsed_ms( t2 );

    fn.Delete();
    std::printf( "[bench] direct: parse=%lld ms, serialize=%lld ms, output_size=%zu\n", parse_ms, ser_ms,
                 s_out.size() );

    PersistentAddressSpace::Get().Reset();
    REQUIRE( true );
}
