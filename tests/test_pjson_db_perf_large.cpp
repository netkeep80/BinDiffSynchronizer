// test_pjson_db_perf_large.cpp — Большой тест производительности pjson_db_pmm.
//
// Объективная оценка скорости БД на масштабах 50k–100k узлов.
// Покрывает все основные операции высокоуровневого API:
//
//   - put() 50k узлов разных типов (int, string, double, bool)
//   - get() 100k запросов чтения
//   - exists() 100k проверок существования
//   - erase() 50k узлов
//   - parse_into() 5k JSON-объектов (многоуровневые)
//   - dump() сериализация большого дерева
//   - clone() глубокое копирование поддерева
//   - put_ref() + resolve_all_refs() — ссылки на масштабе
//   - search_strings() / search_node_strings() — полнотекстовый поиск
//   - update_metrics() — пересчёт статистики
//   - save() — персистентность в файл
//   - Полный жизненный цикл: put/get/update/erase 50k узлов
//
// Все тесты информационные: не падают по времени,
// но распечатывают измеренное время в [perf_large] формате.
//

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "pjson_db_pmm.h"

using namespace pjson;

// ===========================================================================
// Вспомогательные функции
// ===========================================================================

namespace
{
/// Сбросить PMM в начальное состояние перед каждым тестом.
static void reset_pam_large()
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

/// Вернуть количество микросекунд с момента start.
static long long elapsed_us( const perf_clk::time_point& start )
{
    return std::chrono::duration_cast<std::chrono::microseconds>( perf_clk::now() - start ).count();
}

// Масштабы для разных операций.
// put/erase — O(N*depth), поэтому 50k (баланс скорости и объёма).
// get/exists — O(depth), поэтому 100k запросов.
constexpr unsigned LARGE_PUT_N   = 50'000u;
constexpr unsigned LARGE_GET_N   = 100'000u;
constexpr unsigned LARGE_PARSE_N = 5'000u;
constexpr unsigned LARGE_REF_N   = 10'000u;
constexpr unsigned LARGE_CLONE_N = 5'000u;
} // anonymous namespace

// ===========================================================================
// 1. put() 50k узлов разных типов (int, string, double, bool)
// ===========================================================================

TEST_CASE( "perf_large: put 50k mixed-type nodes", "[pjson_db_pmm][perf_large]" )
{
    reset_pam_large();
    pjson_db_pmm db;
    pam_pmm_reserve_slots( LARGE_PUT_N * 2 + 64u );

    char path[64];
    char value[64];
    auto t0 = perf_clk::now();

    for ( unsigned i = 0; i < LARGE_PUT_N; ++i )
    {
        std::snprintf( path, sizeof( path ), "/mixed/k_%06u", i );
        switch ( i % 4 )
        {
        case 0:
            db.put( path, static_cast<int64_t>( i ) );
            break;
        case 1:
            std::snprintf( value, sizeof( value ), "val_%u", i );
            db.put( path, value );
            break;
        case 2:
            db.put( path, static_cast<double>( i ) * 0.123 );
            break;
        case 3:
            db.put( path, ( i % 2 ) == 0 );
            break;
        }
    }

    long long ms = elapsed_ms( t0 );
    std::printf( "[perf_large] put %u mixed-type nodes: %lld ms\n", LARGE_PUT_N, ms );

    // Выборочная проверка корректности.
    node_view v0 = db.get( "/mixed/k_000000" );
    REQUIRE( v0.valid() );
    REQUIRE( v0.is_integer() );
    REQUIRE( v0.as_int() == 0 );

    std::snprintf( path, sizeof( path ), "/mixed/k_%06u", LARGE_PUT_N / 2 + 1 );
    node_view v1 = db.get( path );
    REQUIRE( v1.valid() );
    REQUIRE( v1.is_string() );

    REQUIRE( true );
}

// ===========================================================================
// 2. get() 100k запросов по 50k ключам
// ===========================================================================

TEST_CASE( "perf_large: get 100k requests over 50k keys", "[pjson_db_pmm][perf_large]" )
{
    reset_pam_large();
    pjson_db_pmm db;
    pam_pmm_reserve_slots( LARGE_PUT_N * 2 + 64u );

    char path[64];
    for ( unsigned i = 0; i < LARGE_PUT_N; ++i )
    {
        std::snprintf( path, sizeof( path ), "/items/k_%06u", i );
        db.put( path, static_cast<int64_t>( i ) );
    }

    auto     t0    = perf_clk::now();
    unsigned found = 0;

    for ( unsigned i = 0; i < LARGE_GET_N; ++i )
    {
        std::snprintf( path, sizeof( path ), "/items/k_%06u", i % LARGE_PUT_N );
        node_view v = db.get( path );
        if ( v.valid() )
            ++found;
    }

    long long ms = elapsed_ms( t0 );
    std::printf( "[perf_large] get %u requests (%u unique keys): %lld ms\n", LARGE_GET_N, LARGE_PUT_N, ms );

    REQUIRE( found == LARGE_GET_N );
}

// ===========================================================================
// 3. exists() 100k проверок (50k существующих + 50k несуществующих)
// ===========================================================================

TEST_CASE( "perf_large: exists 100k checks (hit + miss)", "[pjson_db_pmm][perf_large]" )
{
    reset_pam_large();
    pjson_db_pmm db;
    pam_pmm_reserve_slots( LARGE_PUT_N * 2 + 64u );

    char path[64];
    for ( unsigned i = 0; i < LARGE_PUT_N; ++i )
    {
        std::snprintf( path, sizeof( path ), "/exist/k_%06u", i );
        db.put( path, static_cast<int64_t>( i ) );
    }

    auto     t0     = perf_clk::now();
    unsigned hits   = 0;
    unsigned misses = 0;

    // Первая половина — существующие ключи.
    for ( unsigned i = 0; i < LARGE_PUT_N; ++i )
    {
        std::snprintf( path, sizeof( path ), "/exist/k_%06u", i );
        if ( db.exists( path ) )
            ++hits;
    }

    // Вторая половина — несуществующие ключи.
    for ( unsigned i = LARGE_PUT_N; i < 2 * LARGE_PUT_N; ++i )
    {
        std::snprintf( path, sizeof( path ), "/exist/k_%06u", i );
        if ( !db.exists( path ) )
            ++misses;
    }

    long long ms = elapsed_ms( t0 );
    std::printf( "[perf_large] exists %u checks (hits=%u, misses=%u): %lld ms\n", 2 * LARGE_PUT_N, hits, misses, ms );

    REQUIRE( hits == LARGE_PUT_N );
    REQUIRE( misses == LARGE_PUT_N );
}

// ===========================================================================
// 4. erase() 50k узлов
// ===========================================================================

TEST_CASE( "perf_large: erase 50k nodes", "[pjson_db_pmm][perf_large]" )
{
    reset_pam_large();
    pjson_db_pmm db;
    pam_pmm_reserve_slots( LARGE_PUT_N * 2 + 64u );

    char path[64];
    for ( unsigned i = 0; i < LARGE_PUT_N; ++i )
    {
        std::snprintf( path, sizeof( path ), "/todel/k_%06u", i );
        db.put( path, static_cast<int64_t>( i ) );
    }

    auto t0 = perf_clk::now();

    for ( unsigned i = 0; i < LARGE_PUT_N; ++i )
    {
        std::snprintf( path, sizeof( path ), "/todel/k_%06u", i );
        db.erase( path );
    }

    long long ms = elapsed_ms( t0 );
    std::printf( "[perf_large] erase %u nodes: %lld ms\n", LARGE_PUT_N, ms );

    REQUIRE( !db.exists( "/todel/k_000000" ) );
    REQUIRE( !db.exists( "/todel/k_049999" ) );
}

// ===========================================================================
// 5. parse_into() 5k JSON-объектов (многоуровневые)
// ===========================================================================

TEST_CASE( "perf_large: parse_into 5k nested JSON objects", "[pjson_db_pmm][perf_large]" )
{
    reset_pam_large();
    pjson_db_pmm db;
    pam_pmm_reserve_slots( LARGE_PARSE_N * 20 + 64u );

    const char* json_val = "{\"name\":\"item\",\"active\":true,\"count\":42,"
                           "\"tags\":[\"a\",\"b\",\"c\"],"
                           "\"meta\":{\"version\":1,\"flag\":false}}";

    char path[64];
    auto t0 = perf_clk::now();

    for ( unsigned i = 0; i < LARGE_PARSE_N; ++i )
    {
        std::snprintf( path, sizeof( path ), "/records/r_%06u", i );
        db.parse_into( path, json_val );
    }

    long long ms = elapsed_ms( t0 );
    std::printf( "[perf_large] parse_into %u nested JSON objects: %lld ms\n", LARGE_PARSE_N, ms );

    // Проверка глубокого доступа.
    std::snprintf( path, sizeof( path ), "/records/r_%06u/meta/version", LARGE_PARSE_N / 2 );
    node_view v = db.get( path );
    REQUIRE( v.valid() );
    REQUIRE( v.as_int() == 1 );

    std::snprintf( path, sizeof( path ), "/records/r_%06u/tags/1", LARGE_PARSE_N / 3 );
    node_view v2 = db.get( path );
    REQUIRE( v2.valid() );
    REQUIRE( v2.as_string() == std::string_view( "b" ) );
}

// ===========================================================================
// 6. dump() — сериализация дерева с 10k узлами
// ===========================================================================

TEST_CASE( "perf_large: dump large tree to JSON string", "[pjson_db_pmm][perf_large]" )
{
    reset_pam_large();
    pjson_db_pmm db;

    constexpr unsigned DUMP_N = 10'000u;
    pam_pmm_reserve_slots( DUMP_N * 4 + 64u );

    char path[64];
    char value[64];
    for ( unsigned i = 0; i < DUMP_N; ++i )
    {
        std::snprintf( path, sizeof( path ), "/dump/k_%06u", i );
        std::snprintf( value, sizeof( value ), "value_%u", i );
        db.put( path, value );
    }

    auto        t0     = perf_clk::now();
    std::string result = db.dump();
    long long   ms     = elapsed_ms( t0 );

    std::printf( "[perf_large] dump tree with %u string nodes: %lld ms (output %zu bytes)\n", DUMP_N, ms,
                 result.size() );

    REQUIRE( result.size() > 0 );
}

// ===========================================================================
// 7. clone() — глубокое копирование поддерева с 5k узлами
// ===========================================================================

TEST_CASE( "perf_large: clone subtree with 5k nodes", "[pjson_db_pmm][perf_large]" )
{
    reset_pam_large();
    pjson_db_pmm db;
    pam_pmm_reserve_slots( LARGE_CLONE_N * 6 + 64u );

    char path[64];
    for ( unsigned i = 0; i < LARGE_CLONE_N; ++i )
    {
        std::snprintf( path, sizeof( path ), "/source/k_%06u", i );
        db.put( path, static_cast<int64_t>( i ) );
    }

    auto      t0 = perf_clk::now();
    bool      ok = db.clone( "/source", "/dest" );
    long long ms = elapsed_ms( t0 );

    std::printf( "[perf_large] clone subtree with %u nodes: %lld ms\n", LARGE_CLONE_N, ms );

    REQUIRE( ok );

    // Проверка, что копия содержит корректные данные.
    std::snprintf( path, sizeof( path ), "/dest/k_%06u", LARGE_CLONE_N / 2 );
    node_view v = db.get( path );
    REQUIRE( v.valid() );
    REQUIRE( v.is_integer() );
    REQUIRE( v.as_int() == static_cast<int64_t>( LARGE_CLONE_N / 2 ) );
}

// ===========================================================================
// 8. put_ref() + resolve_all_refs() — ссылки на масштабе
// ===========================================================================

TEST_CASE( "perf_large: put_ref and resolve_all_refs with 10k refs", "[pjson_db_pmm][perf_large]" )
{
    reset_pam_large();
    pjson_db_pmm db;
    pam_pmm_reserve_slots( LARGE_REF_N * 4 + 64u );

    char target_path[64];
    char ref_path[64];

    // Создаём LARGE_REF_N целевых узлов.
    for ( unsigned i = 0; i < LARGE_REF_N; ++i )
    {
        std::snprintf( target_path, sizeof( target_path ), "/targets/t_%06u", i );
        db.put( target_path, static_cast<int64_t>( i ) );
    }

    // Создаём LARGE_REF_N ссылок на целевые узлы.
    auto t_put = perf_clk::now();
    for ( unsigned i = 0; i < LARGE_REF_N; ++i )
    {
        std::snprintf( ref_path, sizeof( ref_path ), "/refs/r_%06u", i );
        std::snprintf( target_path, sizeof( target_path ), "/targets/t_%06u", i );
        db.put_ref( ref_path, target_path );
    }
    long long put_ms = elapsed_ms( t_put );

    // Разрешаем все ссылки.
    auto t_resolve = perf_clk::now();
    db.resolve_all_refs();
    long long resolve_ms = elapsed_ms( t_resolve );

    std::printf( "[perf_large] put_ref %u refs: %lld ms\n", LARGE_REF_N, put_ms );
    std::printf( "[perf_large] resolve_all_refs %u refs: %lld ms\n", LARGE_REF_N, resolve_ms );

    // Проверка: ссылка должна разрешаться к целевому значению.
    std::snprintf( ref_path, sizeof( ref_path ), "/refs/r_%06u", LARGE_REF_N / 2 );
    node_view v = db.get( ref_path, true );
    REQUIRE( v.valid() );
    REQUIRE( v.is_integer() );
    REQUIRE( v.as_int() == static_cast<int64_t>( LARGE_REF_N / 2 ) );
}

// ===========================================================================
// 9. search_strings() — полнотекстовый поиск по ключам
// ===========================================================================

TEST_CASE( "perf_large: search_strings over 50k keys", "[pjson_db_pmm][perf_large]" )
{
    reset_pam_large();
    pjson_db_pmm db;
    pam_pmm_reserve_slots( LARGE_PUT_N * 2 + 64u );

    char path[64];
    for ( unsigned i = 0; i < LARGE_PUT_N; ++i )
    {
        std::snprintf( path, sizeof( path ), "/search/k_%06u", i );
        db.put( path, static_cast<int64_t>( i ) );
    }

    // Поиск подстроки в интернированных ключах.
    auto                                   t0      = perf_clk::now();
    std::vector<pstringview_search_result> result1 = db.search_strings( "k_025" );
    long long                              ms1     = elapsed_ms( t0 );

    std::printf( "[perf_large] search_strings \"k_025\" over %u keys: %lld ms (%zu matches)\n", LARGE_PUT_N, ms1,
                 result1.size() );

    REQUIRE( result1.size() > 0 );
}

// ===========================================================================
// 10. search_node_strings() — поиск по строковым значениям
// ===========================================================================

TEST_CASE( "perf_large: search_node_strings over 10k string nodes", "[pjson_db_pmm][perf_large]" )
{
    reset_pam_large();
    pjson_db_pmm db;

    constexpr unsigned STR_N = 10'000u;
    pam_pmm_reserve_slots( STR_N * 4 + 64u );

    char path[64];
    char value[64];
    for ( unsigned i = 0; i < STR_N; ++i )
    {
        std::snprintf( path, sizeof( path ), "/strvals/s_%06u", i );
        std::snprintf( value, sizeof( value ), "text_data_%06u_end", i );
        db.put( path, value );
    }

    auto                 t0     = perf_clk::now();
    std::vector<node_id> result = db.search_node_strings( "data_005" );
    long long            ms     = elapsed_ms( t0 );

    std::printf( "[perf_large] search_node_strings \"data_005\" over %u nodes: %lld ms (%zu matches)\n", STR_N, ms,
                 result.size() );

    REQUIRE( result.size() > 0 );
}

// ===========================================================================
// 11. update_metrics() — пересчёт статистики после 50k узлов
// ===========================================================================

TEST_CASE( "perf_large: update_metrics after 50k nodes", "[pjson_db_pmm][perf_large]" )
{
    reset_pam_large();
    pjson_db_pmm db;
    pam_pmm_reserve_slots( LARGE_PUT_N * 2 + 64u );

    char path[64];
    for ( unsigned i = 0; i < LARGE_PUT_N; ++i )
    {
        std::snprintf( path, sizeof( path ), "/metrics_test/k_%06u", i );
        db.put( path, static_cast<int64_t>( i ) );
    }

    auto t0 = perf_clk::now();
    db.update_metrics();
    long long ms = elapsed_ms( t0 );

    std::printf( "[perf_large] update_metrics after %u nodes: %lld ms\n", LARGE_PUT_N, ms );

    // Проверка: метрики должны отражать реальное состояние.
    node_view node_cnt = db.get( "/$metrics/node_count_total" );
    REQUIRE( node_cnt.valid() );
    REQUIRE( node_cnt.as_uint() > 0 );
}

// ===========================================================================
// 12. save() — персистентность в файл после 10k узлов
// ===========================================================================

TEST_CASE( "perf_large: save to file after 10k nodes", "[pjson_db_pmm][perf_large]" )
{
    reset_pam_large();

    const char*  filename = "perf_large_save_test.pjson";
    pjson_db_pmm db;
    db.open( filename );

    constexpr unsigned SAVE_N = 10'000u;
    pam_pmm_reserve_slots( SAVE_N * 2 + 64u );

    char path[64];
    for ( unsigned i = 0; i < SAVE_N; ++i )
    {
        std::snprintf( path, sizeof( path ), "/savetest/k_%06u", i );
        db.put( path, static_cast<int64_t>( i ) );
    }

    auto t0 = perf_clk::now();
    db.save();
    long long ms = elapsed_ms( t0 );

    std::printf( "[perf_large] save %u nodes to file: %lld ms\n", SAVE_N, ms );

    // Очистка: удаляем файл.
    std::remove( filename );

    REQUIRE( true );
}

// ===========================================================================
// 13. put() с обновлением существующих значений (overwrite)
// ===========================================================================

TEST_CASE( "perf_large: overwrite 50k existing nodes", "[pjson_db_pmm][perf_large]" )
{
    reset_pam_large();
    pjson_db_pmm db;
    pam_pmm_reserve_slots( LARGE_PUT_N * 2 + 64u );

    char path[64];

    // Создаём 50k узлов.
    for ( unsigned i = 0; i < LARGE_PUT_N; ++i )
    {
        std::snprintf( path, sizeof( path ), "/overwrite/k_%06u", i );
        db.put( path, static_cast<int64_t>( i ) );
    }

    // Перезаписываем все 50k.
    auto t0 = perf_clk::now();
    for ( unsigned i = 0; i < LARGE_PUT_N; ++i )
    {
        std::snprintf( path, sizeof( path ), "/overwrite/k_%06u", i );
        db.put( path, static_cast<int64_t>( i * 2 ) );
    }
    long long ms = elapsed_ms( t0 );

    std::printf( "[perf_large] overwrite %u existing nodes: %lld ms\n", LARGE_PUT_N, ms );

    // Проверка: значение обновлено.
    node_view v = db.get( "/overwrite/k_000100" );
    REQUIRE( v.valid() );
    REQUIRE( v.as_int() == 200 );
}

// ===========================================================================
// 14. Массовая вставка: без ReserveSlots vs с ReserveSlots
// ===========================================================================

TEST_CASE( "perf_large: ReserveSlots impact on 50k insert", "[pjson_db_pmm][perf_large]" )
{
    // Вставка БЕЗ резервирования.
    long long ms_without = 0;
    {
        reset_pam_large();
        pjson_db_pmm db;
        char         path[64];

        auto t0 = perf_clk::now();
        for ( unsigned i = 0; i < LARGE_PUT_N; ++i )
        {
            std::snprintf( path, sizeof( path ), "/noreserve/k_%06u", i );
            db.put( path, static_cast<int64_t>( i ) );
        }
        ms_without = elapsed_ms( t0 );
    }

    // Вставка С резервированием.
    long long ms_with = 0;
    {
        reset_pam_large();
        pjson_db_pmm db;
        pam_pmm_reserve_slots( LARGE_PUT_N * 2 + 64u );
        char path[64];

        auto t0 = perf_clk::now();
        for ( unsigned i = 0; i < LARGE_PUT_N; ++i )
        {
            std::snprintf( path, sizeof( path ), "/reserved/k_%06u", i );
            db.put( path, static_cast<int64_t>( i ) );
        }
        ms_with = elapsed_ms( t0 );
    }

    std::printf( "[perf_large] put %u nodes without reserve: %lld ms\n", LARGE_PUT_N, ms_without );
    std::printf( "[perf_large] put %u nodes with ReserveSlots: %lld ms\n", LARGE_PUT_N, ms_with );

    REQUIRE( true );
}

// ===========================================================================
// 15. Глубокие пути — производительность path-адресации
// ===========================================================================

TEST_CASE( "perf_large: deep path addressing (depth=10)", "[pjson_db_pmm][perf_large]" )
{
    reset_pam_large();
    pjson_db_pmm db;

    constexpr unsigned DEEP_N = 5'000u;
    pam_pmm_reserve_slots( DEEP_N * 20 + 64u );

    char path[128];

    // Создаём узлы с глубиной 10.
    auto t_put = perf_clk::now();
    for ( unsigned i = 0; i < DEEP_N; ++i )
    {
        std::snprintf( path, sizeof( path ), "/d1/d2/d3/d4/d5/d6/d7/d8/d9/k_%06u", i );
        db.put( path, static_cast<int64_t>( i ) );
    }
    long long put_ms = elapsed_ms( t_put );

    // Читаем все узлы с глубиной 10.
    auto     t_get = perf_clk::now();
    unsigned found = 0;
    for ( unsigned i = 0; i < DEEP_N; ++i )
    {
        std::snprintf( path, sizeof( path ), "/d1/d2/d3/d4/d5/d6/d7/d8/d9/k_%06u", i );
        node_view v = db.get( path );
        if ( v.valid() )
            ++found;
    }
    long long get_ms = elapsed_ms( t_get );

    std::printf( "[perf_large] deep path (depth=10) put %u: %lld ms\n", DEEP_N, put_ms );
    std::printf( "[perf_large] deep path (depth=10) get %u: %lld ms\n", DEEP_N, get_ms );

    REQUIRE( found == DEEP_N );
}

// ===========================================================================
// 16. Полный жизненный цикл — put/get/update/erase 50k узлов
// ===========================================================================

TEST_CASE( "perf_large: full lifecycle with 50k nodes", "[pjson_db_pmm][perf_large]" )
{
    reset_pam_large();
    pjson_db_pmm db;
    pam_pmm_reserve_slots( LARGE_PUT_N * 2 + 64u );

    char path[64];
    auto t_total = perf_clk::now();

    // 1. put() LARGE_PUT_N узлов.
    {
        auto t0 = perf_clk::now();
        for ( unsigned i = 0; i < LARGE_PUT_N; ++i )
        {
            std::snprintf( path, sizeof( path ), "/lifecycle/k_%06u", i );
            db.put( path, static_cast<int64_t>( i ) );
        }
        std::printf( "[perf_large][lifecycle] put:      %lld ms\n", elapsed_ms( t0 ) );
    }

    // 2. get() LARGE_PUT_N узлов с проверкой корректности.
    {
        auto     t0    = perf_clk::now();
        unsigned found = 0;
        for ( unsigned i = 0; i < LARGE_PUT_N; ++i )
        {
            std::snprintf( path, sizeof( path ), "/lifecycle/k_%06u", i );
            node_view v = db.get( path );
            if ( v.valid() && v.is_integer() && v.as_int() == static_cast<int64_t>( i ) )
                ++found;
        }
        std::printf( "[perf_large][lifecycle] get:      %lld ms\n", elapsed_ms( t0 ) );
        REQUIRE( found == LARGE_PUT_N );
    }

    // 3. overwrite() — обновление всех значений.
    {
        auto t0 = perf_clk::now();
        for ( unsigned i = 0; i < LARGE_PUT_N; ++i )
        {
            std::snprintf( path, sizeof( path ), "/lifecycle/k_%06u", i );
            db.put( path, static_cast<int64_t>( i + 1000000 ) );
        }
        std::printf( "[perf_large][lifecycle] update:   %lld ms\n", elapsed_ms( t0 ) );
    }

    // 4. exists() — проверка существования.
    {
        auto     t0    = perf_clk::now();
        unsigned found = 0;
        for ( unsigned i = 0; i < LARGE_PUT_N; ++i )
        {
            std::snprintf( path, sizeof( path ), "/lifecycle/k_%06u", i );
            if ( db.exists( path ) )
                ++found;
        }
        std::printf( "[perf_large][lifecycle] exists:   %lld ms\n", elapsed_ms( t0 ) );
        REQUIRE( found == LARGE_PUT_N );
    }

    // 5. update_metrics().
    {
        auto t0 = perf_clk::now();
        db.update_metrics();
        long long us = elapsed_us( t0 );
        std::printf( "[perf_large][lifecycle] metrics:  %lld us\n", us );
    }

    // 6. dump() — сериализация.
    {
        auto        t0  = perf_clk::now();
        std::string out = db.dump();
        std::printf( "[perf_large][lifecycle] dump:     %lld ms (%zu bytes)\n", elapsed_ms( t0 ), out.size() );
        REQUIRE( out.size() > 0 );
    }

    // 7. erase() LARGE_PUT_N узлов.
    {
        auto t0 = perf_clk::now();
        for ( unsigned i = 0; i < LARGE_PUT_N; ++i )
        {
            std::snprintf( path, sizeof( path ), "/lifecycle/k_%06u", i );
            db.erase( path );
        }
        std::printf( "[perf_large][lifecycle] erase:    %lld ms\n", elapsed_ms( t0 ) );
    }

    std::printf( "[perf_large][lifecycle] TOTAL:    %lld ms\n", elapsed_ms( t_total ) );

    // Проверка: все узлы удалены.
    REQUIRE( !db.exists( "/lifecycle/k_000000" ) );
    REQUIRE( !db.exists( "/lifecycle/k_049999" ) );
}
