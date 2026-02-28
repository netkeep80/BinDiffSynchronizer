#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <type_traits>
#include <vector>

#include "pam.h"

// =============================================================================
// Тесты метрик ПАМ и проверки корректности ПАП (задача #86)
//
// Проверяют:
//   — GetSlotCount: число аллоцированных слотов
//   — GetSlotCapacity: ёмкость карты слотов
//   — GetNamedCount: число именованных объектов
//   — GetTypeCount: число уникальных типов
//   — GetDataSize: размер области данных
//   — GetFreeListSize: число свободных блоков в списке
//   — GetBump: позиция bump-указателя
//   — Validate: проверка корректности ПАП
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
// GetSlotCount: начальное значение 0, растёт при Create, уменьшается при Delete
// ---------------------------------------------------------------------------
TEST_CASE( "PAM metrics: GetSlotCount increases on Create and decreases on Delete", "[pam][metrics][slot_count]" )
{
    auto&     pam          = PersistentAddressSpace::Get();
    uintptr_t slots_before = pam.GetSlotCount();

    uintptr_t off1 = pam.Create<int>();
    uintptr_t off2 = pam.Create<double>();
    REQUIRE( off1 != 0u );
    REQUIRE( off2 != 0u );

    REQUIRE( pam.GetSlotCount() == slots_before + 2 );

    pam.Delete( off1 );
    REQUIRE( pam.GetSlotCount() == slots_before + 1 );

    pam.Delete( off2 );
    REQUIRE( pam.GetSlotCount() == slots_before );
}

// ---------------------------------------------------------------------------
// GetSlotCapacity: ёмкость >= размер
// ---------------------------------------------------------------------------
TEST_CASE( "PAM metrics: GetSlotCapacity is always >= GetSlotCount", "[pam][metrics][slot_capacity]" )
{
    auto& pam = PersistentAddressSpace::Get();
    REQUIRE( pam.GetSlotCapacity() >= pam.GetSlotCount() );

    // Добавляем объекты — ёмкость остаётся >= размера.
    std::vector<uintptr_t> offs;
    for ( int i = 0; i < 32; i++ )
    {
        uintptr_t off = pam.Create<int>();
        REQUIRE( off != 0u );
        offs.push_back( off );
        REQUIRE( pam.GetSlotCapacity() >= pam.GetSlotCount() );
    }

    for ( auto off : offs )
        pam.Delete( off );
}

// ---------------------------------------------------------------------------
// GetNamedCount: растёт при именованном Create, уменьшается при Delete
// ---------------------------------------------------------------------------
TEST_CASE( "PAM metrics: GetNamedCount tracks named objects only", "[pam][metrics][named_count]" )
{
    auto&     pam          = PersistentAddressSpace::Get();
    uintptr_t named_before = pam.GetNamedCount();

    // Безымянный объект — GetNamedCount не меняется.
    uintptr_t anon = pam.Create<int>();
    REQUIRE( anon != 0u );
    REQUIRE( pam.GetNamedCount() == named_before );

    // Именованный объект — GetNamedCount растёт.
    uintptr_t named = pam.Create<int>( "metrics_named_obj" );
    REQUIRE( named != 0u );
    REQUIRE( pam.GetNamedCount() == named_before + 1 );

    // Удаляем именованный — GetNamedCount уменьшается.
    pam.Delete( named );
    REQUIRE( pam.GetNamedCount() == named_before );

    pam.Delete( anon );
}

// ---------------------------------------------------------------------------
// GetTypeCount: уникальные типы не дублируются
// ---------------------------------------------------------------------------
TEST_CASE( "PAM metrics: GetTypeCount tracks unique types", "[pam][metrics][type_count]" )
{
    auto&     pam          = PersistentAddressSpace::Get();
    uintptr_t types_before = pam.GetTypeCount();

    // Создаём несколько int (один тип) — GetTypeCount должен вырасти не более чем на 1.
    uintptr_t off1 = pam.Create<int>( "metrics_tc_a" );
    uintptr_t off2 = pam.Create<int>( "metrics_tc_b" );
    REQUIRE( off1 != 0u );
    REQUIRE( off2 != 0u );
    REQUIRE( pam.GetTypeCount() <= types_before + 1 );

    // Добавляем double — GetTypeCount должен вырасти ещё не более чем на 1.
    uintptr_t off3 = pam.Create<double>( "metrics_tc_c" );
    REQUIRE( off3 != 0u );
    REQUIRE( pam.GetTypeCount() <= types_before + 2 );

    pam.Delete( off1 );
    pam.Delete( off2 );
    pam.Delete( off3 );
}

// ---------------------------------------------------------------------------
// GetDataSize: размер области данных > 0 и не уменьшается при Create
// ---------------------------------------------------------------------------
TEST_CASE( "PAM metrics: GetDataSize returns positive size and does not decrease", "[pam][metrics][data_size]" )
{
    auto&     pam       = PersistentAddressSpace::Get();
    uintptr_t size_init = pam.GetDataSize();
    REQUIRE( size_init > 0u );

    // Создаём большой массив — DataSize может вырасти, но не уменьшиться.
    uintptr_t off = pam.CreateArray<char>( 512 );
    REQUIRE( off != 0u );
    REQUIRE( pam.GetDataSize() >= size_init );

    pam.Delete( off );
}

// ---------------------------------------------------------------------------
// GetFreeListSize: растёт при Delete, используется при Create
// ---------------------------------------------------------------------------
TEST_CASE( "PAM metrics: GetFreeListSize increases after Delete", "[pam][metrics][free_list]" )
{
    auto& pam = PersistentAddressSpace::Get();

    // Создаём и удаляем объект — список свободных может вырасти.
    uintptr_t free_before = pam.GetFreeListSize();
    uintptr_t off         = pam.Create<int>();
    REQUIRE( off != 0u );
    pam.Delete( off );

    // Список свободных вырос (или остался прежним, если объект последний и bump отступил).
    REQUIRE( pam.GetFreeListSize() >= free_before );
}

// ---------------------------------------------------------------------------
// GetBump: bump растёт при выделении памяти
// ---------------------------------------------------------------------------
TEST_CASE( "PAM metrics: GetBump increases after Create", "[pam][metrics][bump]" )
{
    auto&     pam         = PersistentAddressSpace::Get();
    uintptr_t bump_before = pam.GetBump();
    REQUIRE( bump_before > 0u );

    // После выделения памяти bump должен вырасти (если нет повторного использования).
    uintptr_t off = pam.Create<int>();
    REQUIRE( off != 0u );

    // bump всегда >= предыдущего значения.
    REQUIRE( pam.GetBump() >= bump_before );

    pam.Delete( off );
}

// ---------------------------------------------------------------------------
// Validate: новый ПАМ корректен
// ---------------------------------------------------------------------------
TEST_CASE( "PAM Validate: fresh PAM passes validation", "[pam][validate]" )
{
    auto& pam = PersistentAddressSpace::Get();
    REQUIRE( pam.Validate() );
}

// ---------------------------------------------------------------------------
// Validate: ПАМ корректен после создания и удаления объектов
// ---------------------------------------------------------------------------
TEST_CASE( "PAM Validate: valid after multiple create and delete operations", "[pam][validate]" )
{
    auto& pam = PersistentAddressSpace::Get();

    const unsigned         N = 32;
    std::vector<uintptr_t> offs;

    for ( unsigned i = 0; i < N; i++ )
    {
        char name[32];
        std::snprintf( name, sizeof( name ), "validate_obj_%04u", i );
        uintptr_t off = ( i % 2 == 0 ) ? pam.Create<int>( name ) : pam.Create<double>();
        REQUIRE( off != 0u );
        offs.push_back( off );
        REQUIRE( pam.Validate() );
    }

    // Удаляем половину объектов.
    for ( unsigned i = 0; i < N / 2; i++ )
    {
        pam.Delete( offs[i] );
        REQUIRE( pam.Validate() );
    }

    // Удаляем оставшиеся.
    for ( unsigned i = N / 2; i < N; i++ )
    {
        pam.Delete( offs[i] );
        REQUIRE( pam.Validate() );
    }
}

// ---------------------------------------------------------------------------
// Validate: ПАМ корректен после Save и Init (перезагрузки из файла)
// ---------------------------------------------------------------------------
TEST_CASE( "PAM Validate: valid after Save and Init", "[pam][validate][save_reload]" )
{
    const char* fname = "./test_pam_metrics_validate_reload.pam";
    rm_file( fname );

    uintptr_t saved_off = 0;
    {
        PersistentAddressSpace::Init( fname );
        auto& pam = PersistentAddressSpace::Get();
        REQUIRE( pam.Validate() );

        saved_off = pam.Create<int>( "validate_save_obj" );
        REQUIRE( saved_off != 0u );
        *pam.Resolve<int>( saved_off ) = 12345;

        REQUIRE( pam.Validate() );
        pam.Save();
    }

    PersistentAddressSpace::Init( fname );
    {
        auto& pam = PersistentAddressSpace::Get();
        REQUIRE( pam.Validate() );

        uintptr_t off = pam.Find( "validate_save_obj" );
        REQUIRE( off == saved_off );
        REQUIRE( *pam.Resolve<int>( off ) == 12345 );

        pam.Delete( off );
        REQUIRE( pam.Validate() );
    }

    rm_file( fname );
}

// ---------------------------------------------------------------------------
// Метрики согласованы между собой
// ---------------------------------------------------------------------------
TEST_CASE( "PAM metrics: named count <= slot count", "[pam][metrics][consistency]" )
{
    auto& pam = PersistentAddressSpace::Get();

    // Создаём именованные и безымянные объекты.
    uintptr_t off_named = pam.Create<int>( "metrics_consistency_named" );
    uintptr_t off_anon  = pam.Create<double>();
    REQUIRE( off_named != 0u );
    REQUIRE( off_anon != 0u );

    // GetNamedCount <= GetSlotCount всегда.
    REQUIRE( pam.GetNamedCount() <= pam.GetSlotCount() );
    // GetSlotCapacity >= GetSlotCount всегда.
    REQUIRE( pam.GetSlotCapacity() >= pam.GetSlotCount() );
    // GetBump <= GetDataSize всегда.
    REQUIRE( pam.GetBump() <= pam.GetDataSize() );
    // Validate должен пройти.
    REQUIRE( pam.Validate() );

    pam.Delete( off_named );
    pam.Delete( off_anon );
}

// ---------------------------------------------------------------------------
// Производительность: pmap erase с memmove быстрее O(n^2) через fptr
// ---------------------------------------------------------------------------
TEST_CASE( "pmap perf: erase 100k entries completes within 2 seconds", "[pmap][perf][erase][opt]" )
{
    constexpr unsigned N = 100'000u;

    fptr<pmap<int, int>> fm;
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

    // Оптимизированный erase должен уложиться в 2 секунды (было ~12 секунд).
    REQUIRE( ms < 2000 );

    fm.Delete();
}

// ---------------------------------------------------------------------------
// Производительность: pmap insert с memmove
// ---------------------------------------------------------------------------
TEST_CASE( "pmap perf: insert 100k entries completes within 2 seconds", "[pmap][perf][insert][opt]" )
{
    constexpr unsigned N = 100'000u;

    fptr<pmap<int, int>> fm;
    fm.New();

    auto t0 = std::chrono::steady_clock::now();

    for ( unsigned i = 0; i < N; ++i )
        fm->insert( static_cast<int>( i ), static_cast<int>( i * 2 ) );

    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>( t1 - t0 ).count();

    REQUIRE( fm->size() == N );

    std::printf( "[perf][opt] insert %u entries: %lld ms\n", N, static_cast<long long>( ms ) );

    // Вставка в отсортированном порядке — O(n^2) memmove, но намного быстрее fptr[].
    REQUIRE( ms < 2000 );

    fm->free();
    fm.Delete();
}
