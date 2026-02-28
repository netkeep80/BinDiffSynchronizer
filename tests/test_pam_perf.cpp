#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>

#include "pmap.h"

// =============================================================================
// Тесты производительности управления ПАП (задача #65)
//
// Оценивают эффективность управления персистной памятью:
//   — создание pmap<int, int>
//   — вставка 100 000 записей
//   — поиск значений (100 000 запросов)
//   — удаление значений (100 000 операций erase)
// =============================================================================

namespace
{
constexpr unsigned PERF_N = 100'000u;
} // anonymous namespace

// ---------------------------------------------------------------------------
// Вставка 100 000 записей в pmap<int, int>
// ---------------------------------------------------------------------------
TEST_CASE( "pmap<int,int>: insert 100 000 entries", "[pmap][perf][insert]" )
{
    fptr<pmap<int, int>> fm;
    fm.New();

    auto t0 = std::chrono::steady_clock::now();

    for ( unsigned i = 0; i < PERF_N; ++i )
        fm->insert( static_cast<int>( i ), static_cast<int>( i * 2 ) );

    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>( t1 - t0 ).count();

    REQUIRE( fm->size() == PERF_N );

    std::printf( "[perf] insert %u entries: %lld ms\n", PERF_N, static_cast<long long>( ms ) );

    fm->free();
    fm.Delete();
}

// ---------------------------------------------------------------------------
// Поиск 100 000 значений в pmap<int, int>
// ---------------------------------------------------------------------------
TEST_CASE( "pmap<int,int>: find 100 000 entries", "[pmap][perf][find]" )
{
    fptr<pmap<int, int>> fm;
    fm.New();

    for ( unsigned i = 0; i < PERF_N; ++i )
        fm->insert( static_cast<int>( i ), static_cast<int>( i * 3 ) );

    REQUIRE( fm->size() == PERF_N );

    auto t0 = std::chrono::steady_clock::now();

    unsigned found = 0;
    for ( unsigned i = 0; i < PERF_N; ++i )
    {
        const int* v = fm->find( static_cast<int>( i ) );
        if ( v != nullptr )
            ++found;
    }

    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>( t1 - t0 ).count();

    REQUIRE( found == PERF_N );

    std::printf( "[perf] find %u entries: %lld ms\n", PERF_N, static_cast<long long>( ms ) );

    fm->free();
    fm.Delete();
}

// ---------------------------------------------------------------------------
// Проверка корректности значений после вставки 100 000 записей
// ---------------------------------------------------------------------------
TEST_CASE( "pmap<int,int>: verify 100 000 values after insert", "[pmap][perf][verify]" )
{
    fptr<pmap<int, int>> fm;
    fm.New();

    for ( unsigned i = 0; i < PERF_N; ++i )
        fm->insert( static_cast<int>( i ), static_cast<int>( i * 5 ) );

    REQUIRE( fm->size() == PERF_N );

    // Выборочная проверка: все значения должны совпадать с i * 5.
    bool all_correct = true;
    for ( unsigned i = 0; i < PERF_N; ++i )
    {
        const int* v = fm->find( static_cast<int>( i ) );
        if ( v == nullptr || *v != static_cast<int>( i * 5 ) )
        {
            all_correct = false;
            break;
        }
    }
    REQUIRE( all_correct );

    fm->free();
    fm.Delete();
}

// ---------------------------------------------------------------------------
// Удаление 100 000 записей из pmap<int, int>
// ---------------------------------------------------------------------------
TEST_CASE( "pmap<int,int>: erase 100 000 entries", "[pmap][perf][erase]" )
{
    fptr<pmap<int, int>> fm;
    fm.New();

    for ( unsigned i = 0; i < PERF_N; ++i )
        fm->insert( static_cast<int>( i ), static_cast<int>( i ) );

    REQUIRE( fm->size() == PERF_N );

    auto t0 = std::chrono::steady_clock::now();

    for ( unsigned i = 0; i < PERF_N; ++i )
        fm->erase( static_cast<int>( i ) );

    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>( t1 - t0 ).count();

    REQUIRE( fm->empty() );

    std::printf( "[perf] erase %u entries: %lld ms\n", PERF_N, static_cast<long long>( ms ) );

    fm.Delete();
}

// ---------------------------------------------------------------------------
// Полный цикл: создание, вставка 100k, поиск, удаление
// ---------------------------------------------------------------------------
TEST_CASE( "pmap<int,int>: full lifecycle with 100 000 entries", "[pmap][perf][lifecycle]" )
{
    auto t_total_start = std::chrono::steady_clock::now();

    // 1. Создание pmap
    fptr<pmap<int, int>> fm;
    fm.New();
    REQUIRE( fm->empty() );

    // 2. Вставка 100 000 записей
    {
        auto t0 = std::chrono::steady_clock::now();
        for ( unsigned i = 0; i < PERF_N; ++i )
            fm->insert( static_cast<int>( i ), static_cast<int>( i * 7 ) );
        auto t1 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>( t1 - t0 ).count();
        std::printf( "[perf][lifecycle] insert: %lld ms\n", static_cast<long long>( ms ) );
    }

    REQUIRE( fm->size() == PERF_N );

    // 3. Проверка: каждое значение соответствует i * 7
    {
        bool ok = true;
        for ( unsigned i = 0; i < PERF_N; ++i )
        {
            const int* v = fm->find( static_cast<int>( i ) );
            if ( v == nullptr || *v != static_cast<int>( i * 7 ) )
            {
                ok = false;
                break;
            }
        }
        REQUIRE( ok );
    }

    // 4. Поиск: все ключи должны быть найдены
    {
        auto     t0    = std::chrono::steady_clock::now();
        unsigned found = 0;
        for ( unsigned i = 0; i < PERF_N; ++i )
        {
            if ( fm->find( static_cast<int>( i ) ) != nullptr )
                ++found;
        }
        auto t1 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>( t1 - t0 ).count();
        std::printf( "[perf][lifecycle] find:   %lld ms\n", static_cast<long long>( ms ) );
        REQUIRE( found == PERF_N );
    }

    // 5. Удаление всех записей
    {
        auto t0 = std::chrono::steady_clock::now();
        for ( unsigned i = 0; i < PERF_N; ++i )
            fm->erase( static_cast<int>( i ) );
        auto t1 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>( t1 - t0 ).count();
        std::printf( "[perf][lifecycle] erase:  %lld ms\n", static_cast<long long>( ms ) );
    }

    REQUIRE( fm->empty() );

    auto t_total_end = std::chrono::steady_clock::now();
    auto ms_total    = std::chrono::duration_cast<std::chrono::milliseconds>( t_total_end - t_total_start ).count();
    std::printf( "[perf][lifecycle] TOTAL:  %lld ms\n", static_cast<long long>( ms_total ) );

    fm.Delete();
}

// ---------------------------------------------------------------------------
// pmap<int,int>: поиск несуществующих ключей (false negative — 0 найдено)
// ---------------------------------------------------------------------------
TEST_CASE( "pmap<int,int>: find 100 000 non-existent keys returns nullptr", "[pmap][perf][find_miss]" )
{
    fptr<pmap<int, int>> fm;
    fm.New();

    // Вставляем ключи 0..PERF_N-1
    for ( unsigned i = 0; i < PERF_N; ++i )
        fm->insert( static_cast<int>( i ), static_cast<int>( i ) );

    // Ищем ключи PERF_N..2*PERF_N-1 (их нет)
    auto t0 = std::chrono::steady_clock::now();

    unsigned misses = 0;
    for ( unsigned i = PERF_N; i < 2 * PERF_N; ++i )
    {
        if ( fm->find( static_cast<int>( i ) ) == nullptr )
            ++misses;
    }

    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>( t1 - t0 ).count();

    REQUIRE( misses == PERF_N );

    std::printf( "[perf] find %u non-existent keys: %lld ms\n", PERF_N, static_cast<long long>( ms ) );

    fm->free();
    fm.Delete();
}
