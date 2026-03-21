/**
 * @file test_pmem_array_pmm.cpp
 * @brief Тесты для PamManager::parray.
 *
 * Проверяют корректность работы персистного массива на базе PMM.
 */

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <type_traits>

#include "pam_pmm_config.h"

using namespace pjson;

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ PamManager::parray — Layout
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "parray: is trivially copyable", "[parray][layout]" )
{
    REQUIRE( std::is_trivially_copyable<PamManager::parray<int>>::value );
}

TEST_CASE( "parray: struct size is 12 bytes (uint32 + uint32 + uint32)", "[parray][layout]" )
{
    // parray содержит: _size (uint32_t) + _capacity (uint32_t) + _data_idx (uint32_t)
    REQUIRE( sizeof( PamManager::parray<int> ) == 12u );
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ parray — Инициализация
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "parray: default construction initialises to zeroes", "[parray][init]" )
{
    PamManager::create( 64 * 1024 );

    // Аллоцируем parray через PMM
    auto arr_pptr = PamManager::template allocate_typed<PamManager::parray<int>>();
    REQUIRE( arr_pptr );

    // Инициализируем нулями (имитация персистного хранения)
    std::memset( arr_pptr.resolve(), 0, sizeof( PamManager::parray<int> ) );

    REQUIRE( arr_pptr->size() == 0u );
    REQUIRE( arr_pptr->capacity() == 0u );
    REQUIRE( arr_pptr->empty() );

    PamManager::template deallocate_typed<PamManager::parray<int>>( arr_pptr );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ parray — push_back / data access
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "parray: push_back appends elements and increases size", "[parray][push_back]" )
{
    PamManager::create( 64 * 1024 );

    auto arr_pptr = PamManager::template allocate_typed<PamManager::parray<int>>();
    std::memset( arr_pptr.resolve(), 0, sizeof( PamManager::parray<int> ) );

    arr_pptr->push_back( 10 );
    arr_pptr->push_back( 20 );
    arr_pptr->push_back( 30 );

    REQUIRE( arr_pptr->size() == 3u );

    int* d = arr_pptr->data();
    REQUIRE( d != nullptr );
    REQUIRE( d[0] == 10 );
    REQUIRE( d[1] == 20 );
    REQUIRE( d[2] == 30 );

    arr_pptr->free_data();
    PamManager::template deallocate_typed<PamManager::parray<int>>( arr_pptr );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ parray — reserve
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "parray: reserve capacity grows to accommodate elements", "[parray][reserve]" )
{
    PamManager::create( 64 * 1024 );

    auto arr_pptr = PamManager::template allocate_typed<PamManager::parray<int>>();
    std::memset( arr_pptr.resolve(), 0, sizeof( PamManager::parray<int> ) );

    arr_pptr->reserve( 100 );

    REQUIRE( arr_pptr->capacity() >= 100u );
    REQUIRE( arr_pptr->size() == 0u ); // reserve не изменяет size

    arr_pptr->free_data();
    PamManager::template deallocate_typed<PamManager::parray<int>>( arr_pptr );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ parray — pop_back
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "parray: pop_back decreases size", "[parray][pop_back]" )
{
    PamManager::create( 64 * 1024 );

    auto arr_pptr = PamManager::template allocate_typed<PamManager::parray<int>>();
    std::memset( arr_pptr.resolve(), 0, sizeof( PamManager::parray<int> ) );

    arr_pptr->push_back( 1 );
    arr_pptr->push_back( 2 );
    arr_pptr->push_back( 3 );

    REQUIRE( arr_pptr->size() == 3u );

    arr_pptr->pop_back();
    REQUIRE( arr_pptr->size() == 2u );

    arr_pptr->pop_back();
    arr_pptr->pop_back();
    REQUIRE( arr_pptr->size() == 0u );

    // pop_back на пустом массиве — ничего не делает
    arr_pptr->pop_back();
    REQUIRE( arr_pptr->size() == 0u );

    arr_pptr->free_data();
    PamManager::template deallocate_typed<PamManager::parray<int>>( arr_pptr );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ parray — set
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "parray: set modifies element at index", "[parray][set]" )
{
    PamManager::create( 64 * 1024 );

    auto arr_pptr = PamManager::template allocate_typed<PamManager::parray<int>>();
    std::memset( arr_pptr.resolve(), 0, sizeof( PamManager::parray<int> ) );

    arr_pptr->push_back( 10 );
    arr_pptr->push_back( 20 );
    arr_pptr->push_back( 30 );

    REQUIRE( arr_pptr->set( 1, 99 ) );

    int* d = arr_pptr->data();
    REQUIRE( d[1] == 99 );

    // set вне границ — возвращает false
    REQUIRE_FALSE( arr_pptr->set( 5, 42 ) );

    arr_pptr->free_data();
    PamManager::template deallocate_typed<PamManager::parray<int>>( arr_pptr );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ parray — free_data / clear
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "parray: free_data deallocates buffer and resets header", "[parray][free]" )
{
    PamManager::create( 64 * 1024 );

    auto arr_pptr = PamManager::template allocate_typed<PamManager::parray<int>>();
    std::memset( arr_pptr.resolve(), 0, sizeof( PamManager::parray<int> ) );

    arr_pptr->push_back( 10 );
    arr_pptr->push_back( 20 );

    REQUIRE( arr_pptr->size() == 2u );
    REQUIRE( arr_pptr->capacity() > 0u );

    arr_pptr->free_data();

    REQUIRE( arr_pptr->size() == 0u );
    REQUIRE( arr_pptr->capacity() == 0u );
    REQUIRE( arr_pptr->data() == nullptr );

    PamManager::template deallocate_typed<PamManager::parray<int>>( arr_pptr );
    PamManager::destroy();
}

TEST_CASE( "parray: clear resets size but keeps capacity", "[parray][clear]" )
{
    PamManager::create( 64 * 1024 );

    auto arr_pptr = PamManager::template allocate_typed<PamManager::parray<int>>();
    std::memset( arr_pptr.resolve(), 0, sizeof( PamManager::parray<int> ) );

    arr_pptr->push_back( 10 );
    arr_pptr->push_back( 20 );

    std::size_t cap_before = arr_pptr->capacity();
    REQUIRE( cap_before > 0u );

    arr_pptr->clear();

    REQUIRE( arr_pptr->size() == 0u );
    REQUIRE( arr_pptr->capacity() == cap_before ); // capacity не изменился

    arr_pptr->free_data();
    PamManager::template deallocate_typed<PamManager::parray<int>>( arr_pptr );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ ИНТЕГРАЦИИ: Большой массив
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "parray: handles large arrays (1000 elements)", "[parray][large]" )
{
    PamManager::create( 256 * 1024 );

    auto arr_pptr = PamManager::template allocate_typed<PamManager::parray<int>>();
    std::memset( arr_pptr.resolve(), 0, sizeof( PamManager::parray<int> ) );

    for ( int i = 0; i < 1000; i++ )
    {
        arr_pptr->push_back( i * i );
    }

    REQUIRE( arr_pptr->size() == 1000u );
    REQUIRE( arr_pptr->capacity() >= 1000u );

    // Проверяем несколько значений
    int* d = arr_pptr->data();
    REQUIRE( d[0] == 0 );
    REQUIRE( d[10] == 100 );
    REQUIRE( d[99] == 9801 );
    REQUIRE( d[999] == 999 * 999 );

    arr_pptr->free_data();
    PamManager::template deallocate_typed<PamManager::parray<int>>( arr_pptr );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТ: данные корректно резолвятся через data()
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "parray: data() returns valid pointer after push_back", "[parray][alignment]" )
{
    PamManager::create( 64 * 1024 );

    auto arr_pptr = PamManager::template allocate_typed<PamManager::parray<int>>();
    std::memset( arr_pptr.resolve(), 0, sizeof( PamManager::parray<int> ) );

    arr_pptr->push_back( 42 );

    int* d = arr_pptr->data();
    REQUIRE( d != nullptr );
    REQUIRE( d[0] == 42 );

    // Проверяем, что указатель данных находится внутри PMM области
    std::uint8_t* base = PamManager::backend().base_ptr();
    REQUIRE( base != nullptr );
    REQUIRE( reinterpret_cast<std::uint8_t*>( d ) >= base );

    arr_pptr->free_data();
    PamManager::template deallocate_typed<PamManager::parray<int>>( arr_pptr );
    PamManager::destroy();
}
