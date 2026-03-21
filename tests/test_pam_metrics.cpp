#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <type_traits>
#include <vector>

#include "pam_pmm.h"
#include "pmap_pmm.h"
#include "fptr_pmm.h"

using namespace pjson;

// =============================================================================
// Тесты метрик PMM и проверки корректности ПАП (задача #86)
//
// Проверяют:
//   — pam_pmm_slot_count: число аллоцированных слотов
//   — pam_pmm_named_count: число именованных объектов
//   — pam_pmm_get_data_size: размер области данных
//   — pam_pmm_get_bump: позиция bump-указателя
//   — pam_pmm_validate: проверка корректности ПАП
// =============================================================================

namespace
{
void rm_file( const char* path )
{
    std::error_code ec;
    std::filesystem::remove( path, ec );
}
} // anonymous namespace

// ---------------------------------------------------------------------------
// pam_pmm_slot_count: растёт при Create, уменьшается при Delete
// ---------------------------------------------------------------------------
TEST_CASE( "PAM metrics: slot_count increases on Create and decreases on Delete", "[pam][metrics][slot_count]" )
{
    pam_pmm_init( nullptr );

    uintptr_t slots_before = pam_pmm_slot_count();

    uintptr_t off1 = pam_pmm_create<int>();
    uintptr_t off2 = pam_pmm_create<double>();
    REQUIRE( off1 != 0u );
    REQUIRE( off2 != 0u );

    REQUIRE( pam_pmm_slot_count() == slots_before + 2 );

    pam_pmm_delete( off1 );
    REQUIRE( pam_pmm_slot_count() == slots_before + 1 );

    pam_pmm_delete( off2 );
    REQUIRE( pam_pmm_slot_count() == slots_before );

    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// pam_pmm_named_count: растёт при именованном Create, уменьшается при Delete
// ---------------------------------------------------------------------------
TEST_CASE( "PAM metrics: named_count tracks named objects only", "[pam][metrics][named_count]" )
{
    pam_pmm_init( nullptr );

    uintptr_t named_before = pam_pmm_named_count();

    // Безымянный объект — named_count не меняется.
    uintptr_t anon = pam_pmm_create<int>();
    REQUIRE( anon != 0u );
    REQUIRE( pam_pmm_named_count() == named_before );

    // Именованный объект — named_count растёт.
    uintptr_t named = pam_pmm_create<int>( "metrics_named_obj" );
    REQUIRE( named != 0u );
    REQUIRE( pam_pmm_named_count() == named_before + 1 );

    // Удаляем именованный — named_count уменьшается.
    pam_pmm_delete( named );
    REQUIRE( pam_pmm_named_count() == named_before );

    pam_pmm_delete( anon );
    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// pam_pmm_get_data_size: размер области данных > 0
// ---------------------------------------------------------------------------
TEST_CASE( "PAM metrics: get_data_size returns positive size", "[pam][metrics][data_size]" )
{
    pam_pmm_init( nullptr );

    uintptr_t size_init = pam_pmm_get_data_size();
    REQUIRE( size_init > 0u );

    // Создаём массив — DataSize может вырасти, но не уменьшиться.
    uintptr_t off = pam_pmm_create_array<char>( 512 );
    REQUIRE( off != 0u );
    REQUIRE( pam_pmm_get_data_size() >= size_init );

    pam_pmm_delete( off );
    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// pam_pmm_get_bump: bump растёт при выделении памяти
// ---------------------------------------------------------------------------
TEST_CASE( "PAM metrics: get_bump increases after Create", "[pam][metrics][bump]" )
{
    pam_pmm_init( nullptr );

    uintptr_t bump_before = pam_pmm_get_bump();
    REQUIRE( bump_before > 0u );

    // После выделения памяти bump должен вырасти.
    uintptr_t off = pam_pmm_create<int>();
    REQUIRE( off != 0u );

    // bump всегда >= предыдущего значения.
    REQUIRE( pam_pmm_get_bump() >= bump_before );

    pam_pmm_delete( off );
    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// pam_pmm_validate: новый PMM корректен
// ---------------------------------------------------------------------------
TEST_CASE( "PAM Validate: fresh PMM passes validation", "[pam][validate]" )
{
    pam_pmm_init( nullptr );

    REQUIRE( pam_pmm_validate() );

    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// pam_pmm_validate: ПАМ корректен после создания и удаления объектов
// ---------------------------------------------------------------------------
TEST_CASE( "PAM Validate: valid after multiple create and delete operations", "[pam][validate]" )
{
    pam_pmm_init( nullptr );

    const unsigned         N = 32;
    std::vector<uintptr_t> offs;

    for ( unsigned i = 0; i < N; i++ )
    {
        char name[32];
        std::snprintf( name, sizeof( name ), "validate_obj_%04u", i );
        uintptr_t off = ( i % 2 == 0 ) ? pam_pmm_create<int>( name ) : pam_pmm_create<double>();
        REQUIRE( off != 0u );
        offs.push_back( off );
        REQUIRE( pam_pmm_validate() );
    }

    // Удаляем половину объектов.
    for ( unsigned i = 0; i < N / 2; i++ )
    {
        pam_pmm_delete( offs[i] );
        REQUIRE( pam_pmm_validate() );
    }

    // Удаляем оставшиеся.
    for ( unsigned i = N / 2; i < N; i++ )
    {
        pam_pmm_delete( offs[i] );
        REQUIRE( pam_pmm_validate() );
    }

    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// pam_pmm_validate: ПАМ корректен после Save и Init
// ---------------------------------------------------------------------------
TEST_CASE( "PAM Validate: valid after Save and Init", "[pam][validate][save_reload]" )
{
    const char* fname = "./test_pam_metrics_validate_reload.pam";
    rm_file( fname );

    uintptr_t saved_off = 0;
    {
        pam_pmm_init( fname );
        REQUIRE( pam_pmm_validate() );

        saved_off = pam_pmm_create<int>( "validate_save_obj" );
        REQUIRE( saved_off != 0u );
        *pam_pmm_resolve<int>( saved_off ) = 12345;

        REQUIRE( pam_pmm_validate() );
        pam_pmm_save();
        pam_pmm_destroy();
    }

    pam_pmm_init( fname );
    {
        REQUIRE( pam_pmm_validate() );

        uintptr_t off = pam_pmm_find( "validate_save_obj" );
        REQUIRE( off == saved_off );
        REQUIRE( *pam_pmm_resolve<int>( off ) == 12345 );

        pam_pmm_delete( off );
        REQUIRE( pam_pmm_validate() );
    }

    pam_pmm_destroy();
    rm_file( fname );
}

// ---------------------------------------------------------------------------
// Метрики согласованы между собой
// ---------------------------------------------------------------------------
TEST_CASE( "PAM metrics: named count <= slot count", "[pam][metrics][consistency]" )
{
    pam_pmm_init( nullptr );

    // Создаём именованные и безымянные объекты.
    uintptr_t off_named = pam_pmm_create<int>( "metrics_consistency_named" );
    uintptr_t off_anon  = pam_pmm_create<double>();
    REQUIRE( off_named != 0u );
    REQUIRE( off_anon != 0u );

    // named_count <= slot_count всегда.
    REQUIRE( pam_pmm_named_count() <= pam_pmm_slot_count() );
    // bump <= data_size всегда.
    REQUIRE( pam_pmm_get_bump() <= pam_pmm_get_data_size() );
    // Validate должен пройти.
    REQUIRE( pam_pmm_validate() );

    pam_pmm_delete( off_named );
    pam_pmm_delete( off_anon );
    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// Производительность: pmap_pmm erase 100k entries
// ---------------------------------------------------------------------------
TEST_CASE( "pmap_pmm perf: erase 100k entries completes within 2 seconds", "[pmap][perf][erase][opt]" )
{
    constexpr unsigned N = 100'000u;

    pam_pmm_init( nullptr );

    fptr<pmap_pmm<int, int>> fm;
    fm.New();

    for ( unsigned i = 0; i < N; ++i )
        fm->insert( static_cast<int>( i ), static_cast<int>( i ) );

    REQUIRE( fm->size() == N );

    auto t0 = std::chrono::steady_clock::now();

    for ( unsigned i = 0; i < N; ++i )
        fm->erase( static_cast<int>( i ) );

    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>( t1 - t0 ).count();

    REQUIRE( fm->empty() );

    std::printf( "[perf][opt] erase %u entries: %lld ms\n", N, static_cast<long long>( ms ) );

    // Оптимизированный erase должен уложиться в 2 секунды.
    REQUIRE( ms < 2000 );

    fm.Delete();
    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// Производительность: pmap_pmm insert 100k entries
// ---------------------------------------------------------------------------
TEST_CASE( "pmap_pmm perf: insert 100k entries completes within 2 seconds", "[pmap][perf][insert][opt]" )
{
    constexpr unsigned N = 100'000u;

    pam_pmm_init( nullptr );

    fptr<pmap_pmm<int, int>> fm;
    fm.New();

    auto t0 = std::chrono::steady_clock::now();

    for ( unsigned i = 0; i < N; ++i )
        fm->insert( static_cast<int>( i ), static_cast<int>( i * 2 ) );

    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>( t1 - t0 ).count();

    REQUIRE( fm->size() == N );

    std::printf( "[perf][opt] insert %u entries: %lld ms\n", N, static_cast<long long>( ms ) );

    REQUIRE( ms < 2000 );

    fm->free();
    fm.Delete();
    pam_pmm_destroy();
}
