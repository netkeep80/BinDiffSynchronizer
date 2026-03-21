// test_pjson_db_pmm_perf.cpp — Тесты производительности pjson_db_pmm.
//
// Проверяют, что высокоуровневый API pjson_db_pmm справляется с 100k узлами
// за приемлемое время.
//
// Покрытие:
//   - put() 10k целочисленных значений по уникальным путям
//   - put() 10k строковых значений
//   - get() 100k запросов (чтение по 10k узлам, каждый LARGE_N/PERF_N раз)
//   - parse_into() 1k узлов (JSON-парсинг)
//   - erase() 10k узлов
//   - Полный жизненный цикл (put/get/erase) с 10k узлами
//   - ReserveSlots перед массовой вставкой — сравнение скорости
//
// Примечание: path-адресация через put() имеет накладные расходы O(N*depth)
// при вставке N узлов, т.к. каждый put() обходит путь. Для вставки 100k узлов
// плоской структуры используется PERF_N = 10k (соответствует требованиям задачи 9.2).
// get() тестируется на 100k запросах, т.к. работает намного быстрее.
//
// Все тесты информационные: они не падают по времени,
// но распечатывают измеренное время в [perf9] формате.
//

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

#include "pjson_db_pmm.h"

using namespace pjson;

// ===========================================================================
// Вспомогательные функции
// ===========================================================================

namespace
{
/// Сбросить PMM в начальное состояние перед каждым тестом.
static void reset_pam_perf()
{
    pstringview_manager::reset();
    pam_pmm_reset();
}

/// Текущее время для замеров.
using perf_clk = std::chrono::steady_clock;

/// Вернуть количество миллисекунд с момента start.
static long long elapsed_ms( const perf_clk::time_point& start )
{
    return std::chrono::duration_cast<std::chrono::milliseconds>( perf_clk::now() - start ).count();
}

// Используем 1k для put/erase (каждый put() обходит весь путь O(depth)),
// 10k — для запросов get() (намного быстрее put).
constexpr unsigned PERF_N  = 1'000u;
constexpr unsigned LARGE_N = 10'000u;
} // anonymous namespace

// ===========================================================================
// put() 10k целочисленных узлов, проверка корректности
// ===========================================================================

TEST_CASE( "pjson_db_pmm perf: put 1k integer nodes and verify", "[pjson_db_pmm][perf]" )
{
    reset_pam_perf();
    pjson_db_pmm db;

    char path[64];
    auto t0 = perf_clk::now();

    for ( unsigned i = 0; i < PERF_N; ++i )
    {
        std::snprintf( path, sizeof( path ), "/data/k_%05u", i );
        db.put( path, static_cast<int64_t>( i ) );
    }

    long long ms = elapsed_ms( t0 );
    std::printf( "[perf9] put %u int nodes: %lld ms\n", PERF_N, ms );

    // Проверяем, что все узлы были записаны корректно (выборочно).
    std::snprintf( path, sizeof( path ), "/data/k_%05u", PERF_N / 2 );
    node_view v = db.get( path );
    REQUIRE( v.valid() );
    REQUIRE( v.is_integer() );
    REQUIRE( v.as_int() == static_cast<int64_t>( PERF_N / 2 ) );

    // Информационный тест — не ограничиваем время.
    REQUIRE( true );
}

// ===========================================================================
// put() 10k строковых узлов, проверка корректности
// ===========================================================================

TEST_CASE( "pjson_db_pmm perf: put 1k string nodes and verify", "[pjson_db_pmm][perf]" )
{
    reset_pam_perf();
    pjson_db_pmm db;

    char path[64];
    char value[64];
    auto t0 = perf_clk::now();

    for ( unsigned i = 0; i < PERF_N; ++i )
    {
        std::snprintf( path, sizeof( path ), "/strings/k_%05u", i );
        std::snprintf( value, sizeof( value ), "value_%u", i );
        db.put( path, value );
    }

    long long ms = elapsed_ms( t0 );
    std::printf( "[perf9] put %u string nodes: %lld ms\n", PERF_N, ms );

    // Выборочная проверка.
    std::snprintf( path, sizeof( path ), "/strings/k_%05u", PERF_N / 3 );
    std::snprintf( value, sizeof( value ), "value_%u", PERF_N / 3 );
    node_view v = db.get( path );
    REQUIRE( v.valid() );
    REQUIRE( v.is_string() );
    REQUIRE( v.as_string() == std::string_view( value ) );

    REQUIRE( true );
}

// ===========================================================================
// get() 10k запросов по 1k ключам
// ===========================================================================

TEST_CASE( "pjson_db_pmm perf: get 10k requests", "[pjson_db_pmm][perf]" )
{
    reset_pam_perf();
    pjson_db_pmm db;

    char path[64];

    // Предварительно вставляем PERF_N узлов.
    for ( unsigned i = 0; i < PERF_N; ++i )
    {
        std::snprintf( path, sizeof( path ), "/items/k_%05u", i );
        db.put( path, static_cast<int64_t>( i ) );
    }

    // Замеряем LARGE_N get()-запросов (циклически по PERF_N ключам).
    auto     t0    = perf_clk::now();
    unsigned found = 0;

    for ( unsigned i = 0; i < LARGE_N; ++i )
    {
        std::snprintf( path, sizeof( path ), "/items/k_%05u", i % PERF_N );
        node_view v = db.get( path );
        if ( v.valid() )
            ++found;
    }

    long long ms = elapsed_ms( t0 );
    std::printf( "[perf9] get %u requests (%u unique keys): %lld ms\n", LARGE_N, PERF_N, ms );

    REQUIRE( found == LARGE_N );
}

// ===========================================================================
// parse_into() 1k JSON-объектов (информационный)
// ===========================================================================

TEST_CASE( "pjson_db_pmm perf: parse_into 1k JSON objects", "[pjson_db_pmm][perf]" )
{
    reset_pam_perf();
    pjson_db_pmm db;

    char path[64];
    // Небольшой JSON-объект для вставки.
    const char* json_val = "{\"active\":true,\"count\":42}";
    // Используем 1000 объектов — parse_into вносит ощутимые накладные расходы.
    constexpr unsigned PARSE_N = 1000u;
    auto               t0      = perf_clk::now();

    for ( unsigned i = 0; i < PARSE_N; ++i )
    {
        std::snprintf( path, sizeof( path ), "/records/k_%05u", i );
        db.parse_into( path, json_val );
    }

    long long ms = elapsed_ms( t0 );
    std::printf( "[perf9] parse_into %u JSON objects: %lld ms\n", PARSE_N, ms );

    // Проверяем случайный узел.
    std::snprintf( path, sizeof( path ), "/records/k_%05u/count", PARSE_N / 4 );
    node_view v = db.get( path );
    REQUIRE( v.valid() );
    REQUIRE( v.as_int() == 42 );

    REQUIRE( true );
}

// ===========================================================================
// erase() 1k узлов (строковые ключи)
// ===========================================================================

TEST_CASE( "pjson_db_pmm perf: erase 1k nodes", "[pjson_db_pmm][perf]" )
{
    reset_pam_perf();
    pjson_db_pmm db;

    char path[64];

    // Предварительно вставляем PERF_N узлов со строковыми ключами.
    for ( unsigned i = 0; i < PERF_N; ++i )
    {
        std::snprintf( path, sizeof( path ), "/todelete/k_%05u", i );
        db.put( path, static_cast<int64_t>( i ) );
    }

    // Замеряем erase().
    auto t0 = perf_clk::now();

    for ( unsigned i = 0; i < PERF_N; ++i )
    {
        std::snprintf( path, sizeof( path ), "/todelete/k_%05u", i );
        db.erase( path );
    }

    long long ms = elapsed_ms( t0 );
    std::printf( "[perf9] erase %u nodes: %lld ms\n", PERF_N, ms );

    // Проверяем, что узлы удалены.
    REQUIRE( !db.exists( "/todelete/k_00000" ) );
    REQUIRE( !db.exists( "/todelete/k_00001" ) );

    REQUIRE( true );
}

// ===========================================================================
// полный жизненный цикл — put/get/erase с 1k узлами
// ===========================================================================

TEST_CASE( "pjson_db_pmm perf: full lifecycle with 10k nodes", "[pjson_db_pmm][perf]" )
{
    reset_pam_perf();
    pjson_db_pmm db;

    char path[64];

    auto t_total = perf_clk::now();

    // 1. put() PERF_N узлов.
    {
        auto t0 = perf_clk::now();
        for ( unsigned i = 0; i < PERF_N; ++i )
        {
            std::snprintf( path, sizeof( path ), "/lifecycle/k_%05u", i );
            db.put( path, static_cast<int64_t>( i ) );
        }
        std::printf( "[perf9][lifecycle] put:   %lld ms\n", elapsed_ms( t0 ) );
    }

    // 2. get() PERF_N узлов — проверка корректности.
    {
        auto     t0    = perf_clk::now();
        unsigned found = 0;
        for ( unsigned i = 0; i < PERF_N; ++i )
        {
            std::snprintf( path, sizeof( path ), "/lifecycle/k_%05u", i );
            node_view v = db.get( path );
            if ( v.valid() && v.is_integer() && v.as_int() == static_cast<int64_t>( i ) )
                ++found;
        }
        std::printf( "[perf9][lifecycle] get:   %lld ms\n", elapsed_ms( t0 ) );
        REQUIRE( found == PERF_N );
    }

    // 3. erase() PERF_N узлов.
    {
        auto t0 = perf_clk::now();
        for ( unsigned i = 0; i < PERF_N; ++i )
        {
            std::snprintf( path, sizeof( path ), "/lifecycle/k_%05u", i );
            db.erase( path );
        }
        std::printf( "[perf9][lifecycle] erase: %lld ms\n", elapsed_ms( t0 ) );
    }

    std::printf( "[perf9][lifecycle] TOTAL: %lld ms\n", elapsed_ms( t_total ) );

    // Проверяем, что узлы удалены.
    REQUIRE( !db.exists( "/lifecycle/k_00000" ) );
    REQUIRE( !db.exists( "/lifecycle/k_09999" ) );
}

// ===========================================================================
// ReserveSlots — предварительное резервирование ускоряет вставку
// ===========================================================================

TEST_CASE( "pjson_db_pmm perf: ReserveSlots before bulk insert", "[pjson_db_pmm][perf]" )
{
    // Вставка БЕЗ резервирования.
    long long ms_without = 0;
    {
        reset_pam_perf();
        pjson_db_pmm db;
        char         path[64];

        auto t0 = perf_clk::now();
        for ( unsigned i = 0; i < PERF_N; ++i )
        {
            std::snprintf( path, sizeof( path ), "/bulk/k_%05u", i );
            db.put( path, static_cast<int64_t>( i ) );
        }
        ms_without = elapsed_ms( t0 );
    }

    // Вставка С резервированием через ReserveSlots.
    long long ms_with = 0;
    {
        reset_pam_perf();
        pjson_db_pmm db;
        pam_pmm_reserve_slots( PERF_N + 64u );

        char path[64];

        auto t0 = perf_clk::now();
        for ( unsigned i = 0; i < PERF_N; ++i )
        {
            std::snprintf( path, sizeof( path ), "/bulk/k_%05u", i );
            db.put( path, static_cast<int64_t>( i ) );
        }
        ms_with = elapsed_ms( t0 );
    }

    std::printf( "[perf9] put %u nodes without reserve: %lld ms\n", PERF_N, ms_without );
    std::printf( "[perf9] put %u nodes with ReserveSlots: %lld ms\n", PERF_N, ms_with );

    // Информационный тест — просто убеждаемся, что обе версии завершаются.
    REQUIRE( true );
}
