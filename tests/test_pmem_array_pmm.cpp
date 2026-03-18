/**
 * @file test_pmem_array_pmm.cpp
 * @brief Тесты для pmem_array_pmm (Задача 14.2).
 *
 * Проверяют корректность работы персистного массива на базе PMM.
 */

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <type_traits>

#include "pmem_array_pmm.h"
#include "pvector_pmm.h"

using namespace pjson;

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ pmem_array_hdr_pmm — Layout
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pmem_array_hdr_pmm: is trivially copyable", "[pmem_array_pmm][layout][task14.2]" )
{
    REQUIRE( std::is_trivially_copyable<pmem_array_hdr_pmm>::value );
}

TEST_CASE( "pmem_array_hdr_pmm: struct size is 3 * sizeof(void*)", "[pmem_array_pmm][layout][task14.2]" )
{
    REQUIRE( sizeof( pmem_array_hdr_pmm ) == 3 * sizeof( void* ) );
    REQUIRE( sizeof( pmem_array_hdr_pmm::size ) == sizeof( void* ) );
    REQUIRE( sizeof( pmem_array_hdr_pmm::capacity ) == sizeof( void* ) );
    REQUIRE( sizeof( pmem_array_hdr_pmm::data_off ) == sizeof( void* ) );
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ pmem_array_pmm_init
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pmem_array_pmm_init: initialises header to zeroes", "[pmem_array_pmm][init][task14.2]" )
{
    PamManager::create( 64 * 1024 );

    // Аллоцируем заголовок через PMM
    auto hdr_pptr = PamManager::template allocate_typed<pmem_array_hdr_pmm>();
    REQUIRE( hdr_pptr );

    uintptr_t hdr_off = pptr_to_offset( hdr_pptr );

    pmem_array_pmm_init<int>( hdr_off );

    REQUIRE( hdr_pptr->size == 0u );
    REQUIRE( hdr_pptr->capacity == 0u );
    REQUIRE( hdr_pptr->data_off == 0u );

    PamManager::template deallocate_typed<pmem_array_hdr_pmm>( hdr_pptr );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ pmem_array_pmm_push_back / pmem_array_pmm_at
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pmem_array_pmm_push_back: appends elements and increases size", "[pmem_array_pmm][push_back][task14.2]" )
{
    PamManager::create( 64 * 1024 );

    auto      hdr_pptr = PamManager::template allocate_typed<pmem_array_hdr_pmm>();
    uintptr_t hdr_off  = pptr_to_offset( hdr_pptr );

    pmem_array_pmm_init<int>( hdr_off );

    pmem_array_pmm_push_back<int>( hdr_off ) = 10;
    pmem_array_pmm_push_back<int>( hdr_off ) = 20;
    pmem_array_pmm_push_back<int>( hdr_off ) = 30;

    pmem_array_hdr_pmm* hdr = pmm_resolve<pmem_array_hdr_pmm>( hdr_off );

    REQUIRE( hdr->size == 3u );
    REQUIRE( pmem_array_pmm_at<int>( hdr_off, 0 ) == 10 );
    REQUIRE( pmem_array_pmm_at<int>( hdr_off, 1 ) == 20 );
    REQUIRE( pmem_array_pmm_at<int>( hdr_off, 2 ) == 30 );

    pmem_array_pmm_free<int>( hdr_off );
    PamManager::template deallocate_typed<pmem_array_hdr_pmm>( hdr_pptr );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ pmem_array_pmm_reserve
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pmem_array_pmm_reserve: capacity grows to accommodate elements", "[pmem_array_pmm][reserve][task14.2]" )
{
    PamManager::create( 64 * 1024 );

    auto      hdr_pptr = PamManager::template allocate_typed<pmem_array_hdr_pmm>();
    uintptr_t hdr_off  = pptr_to_offset( hdr_pptr );

    pmem_array_pmm_init<int>( hdr_off );

    pmem_array_pmm_reserve<int>( hdr_off, 100 );

    pmem_array_hdr_pmm* hdr = pmm_resolve<pmem_array_hdr_pmm>( hdr_off );

    REQUIRE( hdr->capacity >= 100u );
    REQUIRE( hdr->size == 0u ); // reserve не изменяет size

    pmem_array_pmm_free<int>( hdr_off );
    PamManager::template deallocate_typed<pmem_array_hdr_pmm>( hdr_pptr );
    PamManager::destroy();
}

TEST_CASE( "pmem_array_pmm_reserve: doubling strategy", "[pmem_array_pmm][reserve][task14.2]" )
{
    PamManager::create( 64 * 1024 );

    auto      hdr_pptr = PamManager::template allocate_typed<pmem_array_hdr_pmm>();
    uintptr_t hdr_off  = pptr_to_offset( hdr_pptr );

    pmem_array_pmm_init<int>( hdr_off );

    // Первый reserve: 0 -> 4 (начальная ёмкость)
    pmem_array_pmm_reserve<int>( hdr_off, 1 );
    pmem_array_hdr_pmm* hdr = pmm_resolve<pmem_array_hdr_pmm>( hdr_off );
    REQUIRE( hdr->capacity == 4u );

    // Следующий reserve: 4 -> 8
    pmem_array_pmm_reserve<int>( hdr_off, 5 );
    hdr = pmm_resolve<pmem_array_hdr_pmm>( hdr_off );
    REQUIRE( hdr->capacity == 8u );

    // Следующий reserve: 8 -> 16
    pmem_array_pmm_reserve<int>( hdr_off, 10 );
    hdr = pmm_resolve<pmem_array_hdr_pmm>( hdr_off );
    REQUIRE( hdr->capacity == 16u );

    pmem_array_pmm_free<int>( hdr_off );
    PamManager::template deallocate_typed<pmem_array_hdr_pmm>( hdr_pptr );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ pmem_array_pmm_pop_back
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pmem_array_pmm_pop_back: decreases size", "[pmem_array_pmm][pop_back][task14.2]" )
{
    PamManager::create( 64 * 1024 );

    auto      hdr_pptr = PamManager::template allocate_typed<pmem_array_hdr_pmm>();
    uintptr_t hdr_off  = pptr_to_offset( hdr_pptr );

    pmem_array_pmm_init<int>( hdr_off );

    pmem_array_pmm_push_back<int>( hdr_off ) = 1;
    pmem_array_pmm_push_back<int>( hdr_off ) = 2;
    pmem_array_pmm_push_back<int>( hdr_off ) = 3;

    REQUIRE( pmem_array_pmm_size( hdr_off ) == 3u );

    pmem_array_pmm_pop_back<int>( hdr_off );
    REQUIRE( pmem_array_pmm_size( hdr_off ) == 2u );

    pmem_array_pmm_pop_back<int>( hdr_off );
    pmem_array_pmm_pop_back<int>( hdr_off );
    REQUIRE( pmem_array_pmm_size( hdr_off ) == 0u );

    // pop_back на пустом массиве — ничего не делает
    pmem_array_pmm_pop_back<int>( hdr_off );
    REQUIRE( pmem_array_pmm_size( hdr_off ) == 0u );

    pmem_array_pmm_free<int>( hdr_off );
    PamManager::template deallocate_typed<pmem_array_hdr_pmm>( hdr_pptr );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ pmem_array_pmm_erase_at
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pmem_array_pmm_erase_at: removes element and shifts left", "[pmem_array_pmm][erase][task14.2]" )
{
    PamManager::create( 64 * 1024 );

    auto      hdr_pptr = PamManager::template allocate_typed<pmem_array_hdr_pmm>();
    uintptr_t hdr_off  = pptr_to_offset( hdr_pptr );

    pmem_array_pmm_init<int>( hdr_off );

    pmem_array_pmm_push_back<int>( hdr_off ) = 10;
    pmem_array_pmm_push_back<int>( hdr_off ) = 20;
    pmem_array_pmm_push_back<int>( hdr_off ) = 30;
    pmem_array_pmm_push_back<int>( hdr_off ) = 40;

    // Удаляем элемент с индексом 1 (значение 20)
    pmem_array_pmm_erase_at<int>( hdr_off, 1 );

    REQUIRE( pmem_array_pmm_size( hdr_off ) == 3u );
    REQUIRE( pmem_array_pmm_at<int>( hdr_off, 0 ) == 10 );
    REQUIRE( pmem_array_pmm_at<int>( hdr_off, 1 ) == 30 );
    REQUIRE( pmem_array_pmm_at<int>( hdr_off, 2 ) == 40 );

    pmem_array_pmm_free<int>( hdr_off );
    PamManager::template deallocate_typed<pmem_array_hdr_pmm>( hdr_pptr );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ pmem_array_pmm_insert_sorted / pmem_array_pmm_find_sorted
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pmem_array_pmm_insert_sorted: maintains sorted order", "[pmem_array_pmm][sorted][task14.2]" )
{
    PamManager::create( 64 * 1024 );

    auto      hdr_pptr = PamManager::template allocate_typed<pmem_array_hdr_pmm>();
    uintptr_t hdr_off  = pptr_to_offset( hdr_pptr );

    pmem_array_pmm_init<int>( hdr_off );

    auto identity = []( const int& x ) { return x; };
    auto less     = []( int a, int b ) { return a < b; };

    pmem_array_pmm_insert_sorted<int>( hdr_off, 30, identity, less );
    pmem_array_pmm_insert_sorted<int>( hdr_off, 10, identity, less );
    pmem_array_pmm_insert_sorted<int>( hdr_off, 20, identity, less );
    pmem_array_pmm_insert_sorted<int>( hdr_off, 40, identity, less );

    REQUIRE( pmem_array_pmm_size( hdr_off ) == 4u );
    REQUIRE( pmem_array_pmm_at<int>( hdr_off, 0 ) == 10 );
    REQUIRE( pmem_array_pmm_at<int>( hdr_off, 1 ) == 20 );
    REQUIRE( pmem_array_pmm_at<int>( hdr_off, 2 ) == 30 );
    REQUIRE( pmem_array_pmm_at<int>( hdr_off, 3 ) == 40 );

    pmem_array_pmm_free<int>( hdr_off );
    PamManager::template deallocate_typed<pmem_array_hdr_pmm>( hdr_pptr );
    PamManager::destroy();
}

TEST_CASE( "pmem_array_pmm_find_sorted: finds existing elements", "[pmem_array_pmm][sorted][task14.2]" )
{
    PamManager::create( 64 * 1024 );

    auto      hdr_pptr = PamManager::template allocate_typed<pmem_array_hdr_pmm>();
    uintptr_t hdr_off  = pptr_to_offset( hdr_pptr );

    pmem_array_pmm_init<int>( hdr_off );

    auto identity = []( const int& x ) { return x; };
    auto less     = []( int a, int b ) { return a < b; };

    pmem_array_pmm_insert_sorted<int>( hdr_off, 10, identity, less );
    pmem_array_pmm_insert_sorted<int>( hdr_off, 20, identity, less );
    pmem_array_pmm_insert_sorted<int>( hdr_off, 30, identity, less );

    int* found = pmem_array_pmm_find_sorted<int, int>( hdr_off, 20, identity, less );
    REQUIRE( found != nullptr );
    REQUIRE( *found == 20 );

    int* not_found = pmem_array_pmm_find_sorted<int, int>( hdr_off, 25, identity, less );
    REQUIRE( not_found == nullptr );

    pmem_array_pmm_free<int>( hdr_off );
    PamManager::template deallocate_typed<pmem_array_hdr_pmm>( hdr_pptr );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ pmem_array_pmm_free / pmem_array_pmm_clear
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pmem_array_pmm_free: deallocates buffer and resets header", "[pmem_array_pmm][free][task14.2]" )
{
    PamManager::create( 64 * 1024 );

    auto      hdr_pptr = PamManager::template allocate_typed<pmem_array_hdr_pmm>();
    uintptr_t hdr_off  = pptr_to_offset( hdr_pptr );

    pmem_array_pmm_init<int>( hdr_off );

    pmem_array_pmm_push_back<int>( hdr_off ) = 10;
    pmem_array_pmm_push_back<int>( hdr_off ) = 20;

    REQUIRE( pmem_array_pmm_size( hdr_off ) == 2u );
    REQUIRE( pmem_array_pmm_capacity( hdr_off ) > 0u );

    pmem_array_pmm_free<int>( hdr_off );

    REQUIRE( pmem_array_pmm_size( hdr_off ) == 0u );
    REQUIRE( pmem_array_pmm_capacity( hdr_off ) == 0u );

    pmem_array_hdr_pmm* hdr = pmm_resolve<pmem_array_hdr_pmm>( hdr_off );
    REQUIRE( hdr->data_off == 0u );

    PamManager::template deallocate_typed<pmem_array_hdr_pmm>( hdr_pptr );
    PamManager::destroy();
}

TEST_CASE( "pmem_array_pmm_clear: resets size but keeps capacity", "[pmem_array_pmm][clear][task14.2]" )
{
    PamManager::create( 64 * 1024 );

    auto      hdr_pptr = PamManager::template allocate_typed<pmem_array_hdr_pmm>();
    uintptr_t hdr_off  = pptr_to_offset( hdr_pptr );

    pmem_array_pmm_init<int>( hdr_off );

    pmem_array_pmm_push_back<int>( hdr_off ) = 10;
    pmem_array_pmm_push_back<int>( hdr_off ) = 20;

    uintptr_t cap_before = pmem_array_pmm_capacity( hdr_off );
    REQUIRE( cap_before > 0u );

    pmem_array_pmm_clear<int>( hdr_off );

    REQUIRE( pmem_array_pmm_size( hdr_off ) == 0u );
    REQUIRE( pmem_array_pmm_capacity( hdr_off ) == cap_before ); // capacity не изменился

    pmem_array_pmm_free<int>( hdr_off );
    PamManager::template deallocate_typed<pmem_array_hdr_pmm>( hdr_pptr );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ pvector_pmm
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pvector_pmm: size is 3 * sizeof(void*)", "[pvector_pmm][layout][task14.2]" )
{
    REQUIRE( sizeof( pvector_pmm<int> ) == 3 * sizeof( void* ) );
    REQUIRE( sizeof( pvector_pmm<double> ) == 3 * sizeof( void* ) );
}

TEST_CASE( "pvector_pmm: push_back and element access", "[pvector_pmm][push_back][task14.2]" )
{
    PamManager::create( 64 * 1024 );

    // Аллоцируем pvector_pmm через PMM
    auto pv = PamManager::template allocate_typed<pvector_pmm<int>>();
    REQUIRE( pv );

    // Инициализируем нулями
    std::memset( pv.resolve(), 0, sizeof( pvector_pmm<int> ) );

    REQUIRE( pv->empty() );
    REQUIRE( pv->size() == 0u );

    pv->push_back( 10 );
    pv->push_back( 20 );
    pv->push_back( 30 );

    REQUIRE( pv->size() == 3u );
    REQUIRE( ( *pv )[0] == 10 );
    REQUIRE( ( *pv )[1] == 20 );
    REQUIRE( ( *pv )[2] == 30 );

    pv->free();
    PamManager::template deallocate_typed<pvector_pmm<int>>( pv );
    PamManager::destroy();
}

TEST_CASE( "pvector_pmm: front and back", "[pvector_pmm][access][task14.2]" )
{
    PamManager::create( 64 * 1024 );

    auto pv = PamManager::template allocate_typed<pvector_pmm<int>>();
    std::memset( pv.resolve(), 0, sizeof( pvector_pmm<int> ) );

    pv->push_back( 100 );
    pv->push_back( 200 );
    pv->push_back( 300 );

    REQUIRE( pv->front() == 100 );
    REQUIRE( pv->back() == 300 );

    pv->free();
    PamManager::template deallocate_typed<pvector_pmm<int>>( pv );
    PamManager::destroy();
}

TEST_CASE( "pvector_pmm: pop_back", "[pvector_pmm][pop_back][task14.2]" )
{
    PamManager::create( 64 * 1024 );

    auto pv = PamManager::template allocate_typed<pvector_pmm<int>>();
    std::memset( pv.resolve(), 0, sizeof( pvector_pmm<int> ) );

    pv->push_back( 1 );
    pv->push_back( 2 );
    pv->push_back( 3 );

    REQUIRE( pv->size() == 3u );

    pv->pop_back();
    REQUIRE( pv->size() == 2u );
    REQUIRE( pv->back() == 2 );

    pv->pop_back();
    pv->pop_back();
    REQUIRE( pv->empty() );

    pv->free();
    PamManager::template deallocate_typed<pvector_pmm<int>>( pv );
    PamManager::destroy();
}

TEST_CASE( "pvector_pmm: clear", "[pvector_pmm][clear][task14.2]" )
{
    PamManager::create( 64 * 1024 );

    auto pv = PamManager::template allocate_typed<pvector_pmm<int>>();
    std::memset( pv.resolve(), 0, sizeof( pvector_pmm<int> ) );

    pv->push_back( 1 );
    pv->push_back( 2 );
    pv->push_back( 3 );

    uintptr_t cap_before = pv->capacity();

    pv->clear();

    REQUIRE( pv->size() == 0u );
    REQUIRE( pv->capacity() == cap_before );

    pv->free();
    PamManager::template deallocate_typed<pvector_pmm<int>>( pv );
    PamManager::destroy();
}

TEST_CASE( "pvector_pmm: iterators", "[pvector_pmm][iterator][task14.2]" )
{
    PamManager::create( 64 * 1024 );

    auto pv = PamManager::template allocate_typed<pvector_pmm<int>>();
    std::memset( pv.resolve(), 0, sizeof( pvector_pmm<int> ) );

    pv->push_back( 10 );
    pv->push_back( 20 );
    pv->push_back( 30 );

    int sum = 0;
    for ( auto it = pv->begin(); it != pv->end(); ++it )
    {
        sum += *it;
    }
    REQUIRE( sum == 60 );

    pv->free();
    PamManager::template deallocate_typed<pvector_pmm<int>>( pv );
    PamManager::destroy();
}

TEST_CASE( "pvector_pmm: capacity grows with elements", "[pvector_pmm][capacity][task14.2]" )
{
    PamManager::create( 64 * 1024 );

    auto pv = PamManager::template allocate_typed<pvector_pmm<int>>();
    std::memset( pv.resolve(), 0, sizeof( pvector_pmm<int> ) );

    for ( int i = 0; i < 20; i++ )
    {
        pv->push_back( i );
    }

    REQUIRE( pv->size() == 20u );
    REQUIRE( pv->capacity() >= 20u );

    for ( int i = 0; i < 20; i++ )
    {
        REQUIRE( ( *pv )[static_cast<uintptr_t>( i )] == i );
    }

    pv->free();
    PamManager::template deallocate_typed<pvector_pmm<int>>( pv );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ ИНТЕГРАЦИИ: Большой массив
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pmem_array_pmm: handles large arrays (1000 elements)", "[pmem_array_pmm][large][task14.2]" )
{
    PamManager::create( 256 * 1024 );

    auto      hdr_pptr = PamManager::template allocate_typed<pmem_array_hdr_pmm>();
    uintptr_t hdr_off  = pptr_to_offset( hdr_pptr );

    pmem_array_pmm_init<int>( hdr_off );

    for ( int i = 0; i < 1000; i++ )
    {
        pmem_array_pmm_push_back<int>( hdr_off ) = i * i;
    }

    REQUIRE( pmem_array_pmm_size( hdr_off ) == 1000u );
    REQUIRE( pmem_array_pmm_capacity( hdr_off ) >= 1000u );

    // Проверяем несколько значений
    REQUIRE( pmem_array_pmm_at<int>( hdr_off, 0 ) == 0 );
    REQUIRE( pmem_array_pmm_at<int>( hdr_off, 10 ) == 100 );
    REQUIRE( pmem_array_pmm_at<int>( hdr_off, 99 ) == 9801 );
    REQUIRE( pmem_array_pmm_at<int>( hdr_off, 999 ) == 999 * 999 );

    pmem_array_pmm_free<int>( hdr_off );
    PamManager::template deallocate_typed<pmem_array_hdr_pmm>( hdr_pptr );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТ: смещения кратны размеру гранулы
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pmem_array_pmm: data_off is aligned to granule size", "[pmem_array_pmm][alignment][task14.2]" )
{
    PamManager::create( 64 * 1024 );

    auto      hdr_pptr = PamManager::template allocate_typed<pmem_array_hdr_pmm>();
    uintptr_t hdr_off  = pptr_to_offset( hdr_pptr );

    pmem_array_pmm_init<int>( hdr_off );
    pmem_array_pmm_push_back<int>( hdr_off ) = 42;

    pmem_array_hdr_pmm* hdr = pmm_resolve<pmem_array_hdr_pmm>( hdr_off );
    REQUIRE( is_aligned_offset( hdr->data_off ) );

    pmem_array_pmm_free<int>( hdr_off );
    PamManager::template deallocate_typed<pmem_array_hdr_pmm>( hdr_pptr );
    PamManager::destroy();
}
