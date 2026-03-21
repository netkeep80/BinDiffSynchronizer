/**
 * @file test_pmap_pmm.cpp
 * @brief Тесты для pmap_pmm.
 *
 * Проверяют корректность работы персистной карты на базе PMM.
 */

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <type_traits>

#include "pmap_pmm.h"

using namespace pjson;

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ pmap_entry_pmm — Layout
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pmap_entry_pmm<int,int>: is trivially copyable", "[pmap_pmm][layout]" )
{
    REQUIRE( std::is_trivially_copyable<pmap_entry_pmm<int, int>>::value );
    REQUIRE( std::is_trivially_copyable<pmap_entry_pmm<int, double>>::value );
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ pmap_pmm — Layout
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pmap_pmm<int,int>: size is 12 bytes (parray)", "[pmap_pmm][layout]" )
{
    // pmap_pmm оборачивает PamManager::parray<Entry> = uint32_t * 3 = 12 байт
    REQUIRE( sizeof( pmap_pmm<int, int> ) == 12u );
    REQUIRE( sizeof( pmap_pmm<int, double> ) == 12u );
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ pmap_pmm — Создание и базовые операции
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pmap_pmm<int,int>: zero-initialised gives empty map", "[pmap_pmm][construct]" )
{
    PamManager::create( 64 * 1024 );

    auto pm = PamManager::template allocate_typed<pmap_pmm<int, int>>();
    REQUIRE( pm );

    std::memset( pm.resolve(), 0, sizeof( pmap_pmm<int, int> ) );

    REQUIRE( pm->empty() );
    REQUIRE( pm->size() == 0u );

    PamManager::template deallocate_typed<pmap_pmm<int, int>>( pm );
    PamManager::destroy();
}

TEST_CASE( "pmap_pmm<int,int>: insert single entry and find it", "[pmap_pmm][insert][find]" )
{
    PamManager::create( 64 * 1024 );

    auto pm = PamManager::template allocate_typed<pmap_pmm<int, int>>();
    std::memset( pm.resolve(), 0, sizeof( pmap_pmm<int, int> ) );

    pm->insert_direct( 42, 100 );
    REQUIRE( pm->size() == 1u );
    REQUIRE( !pm->empty() );

    int* val = pm->find( 42 );
    REQUIRE( val != nullptr );
    REQUIRE( *val == 100 );

    pm->free();
    PamManager::template deallocate_typed<pmap_pmm<int, int>>( pm );
    PamManager::destroy();
}

TEST_CASE( "pmap_pmm<int,int>: find missing key returns nullptr", "[pmap_pmm][find]" )
{
    PamManager::create( 64 * 1024 );

    auto pm = PamManager::template allocate_typed<pmap_pmm<int, int>>();
    std::memset( pm.resolve(), 0, sizeof( pmap_pmm<int, int> ) );

    pm->insert_direct( 1, 10 );
    REQUIRE( pm->find( 999 ) == nullptr );

    pm->free();
    PamManager::template deallocate_typed<pmap_pmm<int, int>>( pm );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ pmap_pmm — Сортировка
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pmap_pmm<int,int>: insert multiple entries in sorted order", "[pmap_pmm][insert][sorted]" )
{
    PamManager::create( 64 * 1024 );

    auto pm = PamManager::template allocate_typed<pmap_pmm<int, int>>();
    std::memset( pm.resolve(), 0, sizeof( pmap_pmm<int, int> ) );

    pm->insert_direct( 30, 300 );
    pm->insert_direct( 10, 100 );
    pm->insert_direct( 20, 200 );

    REQUIRE( pm->size() == 3u );

    // Entries must be in ascending key order: 10, 20, 30.
    auto it = pm->begin();
    REQUIRE( it->key == 10 );
    REQUIRE( it->value == 100 );
    ++it;
    REQUIRE( it->key == 20 );
    REQUIRE( it->value == 200 );
    ++it;
    REQUIRE( it->key == 30 );
    REQUIRE( it->value == 300 );
    ++it;
    REQUIRE( it == pm->end() );

    pm->free();
    PamManager::template deallocate_typed<pmap_pmm<int, int>>( pm );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ pmap_pmm — Обновление существующего ключа
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pmap_pmm<int,int>: inserting existing key updates value", "[pmap_pmm][insert][update]" )
{
    PamManager::create( 64 * 1024 );

    auto pm = PamManager::template allocate_typed<pmap_pmm<int, int>>();
    std::memset( pm.resolve(), 0, sizeof( pmap_pmm<int, int> ) );

    pm->insert_direct( 5, 50 );
    REQUIRE( pm->size() == 1u );

    pm->insert_direct( 5, 99 );
    REQUIRE( pm->size() == 1u );

    int* val = pm->find( 5 );
    REQUIRE( val != nullptr );
    REQUIRE( *val == 99 );

    pm->free();
    PamManager::template deallocate_typed<pmap_pmm<int, int>>( pm );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ pmap_pmm — Удаление
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pmap_pmm<int,int>: erase removes the correct entry", "[pmap_pmm][erase]" )
{
    PamManager::create( 64 * 1024 );

    auto pm = PamManager::template allocate_typed<pmap_pmm<int, int>>();
    std::memset( pm.resolve(), 0, sizeof( pmap_pmm<int, int> ) );

    pm->insert_direct( 1, 10 );
    pm->insert_direct( 2, 20 );
    pm->insert_direct( 3, 30 );
    REQUIRE( pm->size() == 3u );

    bool ok = pm->erase( 2 );
    REQUIRE( ok );
    REQUIRE( pm->size() == 2u );
    REQUIRE( pm->find( 2 ) == nullptr );
    REQUIRE( pm->find( 1 ) != nullptr );
    REQUIRE( pm->find( 3 ) != nullptr );

    pm->free();
    PamManager::template deallocate_typed<pmap_pmm<int, int>>( pm );
    PamManager::destroy();
}

TEST_CASE( "pmap_pmm<int,int>: erase missing key returns false", "[pmap_pmm][erase]" )
{
    PamManager::create( 64 * 1024 );

    auto pm = PamManager::template allocate_typed<pmap_pmm<int, int>>();
    std::memset( pm.resolve(), 0, sizeof( pmap_pmm<int, int> ) );

    pm->insert_direct( 1, 10 );
    bool ok = pm->erase( 999 );
    REQUIRE( !ok );
    REQUIRE( pm->size() == 1u );

    pm->free();
    PamManager::template deallocate_typed<pmap_pmm<int, int>>( pm );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ pmap_pmm — operator[]
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pmap_pmm<int,int>: operator[] inserts default value for missing key", "[pmap_pmm][operator_index]" )
{
    PamManager::create( 64 * 1024 );

    auto pm = PamManager::template allocate_typed<pmap_pmm<int, int>>();
    std::memset( pm.resolve(), 0, sizeof( pmap_pmm<int, int> ) );

    // Key 7 does not exist; operator[] should insert it with default value 0.
    ( *pm )[7] = 77;
    REQUIRE( pm->size() == 1u );
    REQUIRE( pm->find( 7 ) != nullptr );
    REQUIRE( *pm->find( 7 ) == 77 );

    pm->free();
    PamManager::template deallocate_typed<pmap_pmm<int, int>>( pm );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ pmap_pmm — clear и free
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pmap_pmm<int,int>: clear resets size to 0", "[pmap_pmm][clear]" )
{
    PamManager::create( 64 * 1024 );

    auto pm = PamManager::template allocate_typed<pmap_pmm<int, int>>();
    std::memset( pm.resolve(), 0, sizeof( pmap_pmm<int, int> ) );

    pm->insert_direct( 1, 1 );
    pm->insert_direct( 2, 2 );
    REQUIRE( pm->size() == 2u );

    pm->clear();
    REQUIRE( pm->empty() );
    REQUIRE( pm->size() == 0u );

    pm->free();
    PamManager::template deallocate_typed<pmap_pmm<int, int>>( pm );
    PamManager::destroy();
}

TEST_CASE( "pmap_pmm<int,int>: free releases underlying allocation", "[pmap_pmm][free]" )
{
    PamManager::create( 64 * 1024 );

    auto pm = PamManager::template allocate_typed<pmap_pmm<int, int>>();
    std::memset( pm.resolve(), 0, sizeof( pmap_pmm<int, int> ) );

    pm->insert_direct( 10, 100 );
    REQUIRE( pm->size() == 1u );

    pm->free();
    REQUIRE( pm->size() == 0u );

    PamManager::template deallocate_typed<pmap_pmm<int, int>>( pm );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ pmap_pmm — Работа с double
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pmap_pmm<int,double>: insert and find double values", "[pmap_pmm][double]" )
{
    PamManager::create( 64 * 1024 );

    auto pm = PamManager::template allocate_typed<pmap_pmm<int, double>>();
    std::memset( pm.resolve(), 0, sizeof( pmap_pmm<int, double> ) );

    pm->insert_direct( 1, 1.1 );
    pm->insert_direct( 2, 2.2 );
    pm->insert_direct( 3, 3.3 );

    REQUIRE( pm->size() == 3u );
    REQUIRE( *pm->find( 1 ) == 1.1 );
    REQUIRE( *pm->find( 2 ) == 2.2 );
    REQUIRE( *pm->find( 3 ) == 3.3 );

    pm->free();
    PamManager::template deallocate_typed<pmap_pmm<int, double>>( pm );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ pmap_pmm — Итераторы
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pmap_pmm<int,int>: range-based iteration visits all entries", "[pmap_pmm][iterator]" )
{
    PamManager::create( 64 * 1024 );

    auto pm = PamManager::template allocate_typed<pmap_pmm<int, int>>();
    std::memset( pm.resolve(), 0, sizeof( pmap_pmm<int, int> ) );

    pm->insert_direct( 1, 10 );
    pm->insert_direct( 2, 20 );
    pm->insert_direct( 3, 30 );

    int key_sum = 0;
    int val_sum = 0;
    for ( auto it = pm->begin(); it != pm->end(); ++it )
    {
        key_sum += it->key;
        val_sum += it->value;
    }
    REQUIRE( key_sum == 6 );
    REQUIRE( val_sum == 60 );

    pm->free();
    PamManager::template deallocate_typed<pmap_pmm<int, int>>( pm );
    PamManager::destroy();
}

TEST_CASE( "pmap_pmm<int,int>: const iterators work correctly", "[pmap_pmm][iterator]" )
{
    PamManager::create( 64 * 1024 );

    auto pm = PamManager::template allocate_typed<pmap_pmm<int, int>>();
    std::memset( pm.resolve(), 0, sizeof( pmap_pmm<int, int> ) );

    pm->insert_direct( 100, 1000 );
    pm->insert_direct( 200, 2000 );

    const pmap_pmm<int, int>* const_pm = pm.resolve();

    int sum = 0;
    for ( auto it = const_pm->begin(); it != const_pm->end(); ++it )
    {
        sum += it->value;
    }
    REQUIRE( sum == 3000 );

    pm->free();
    PamManager::template deallocate_typed<pmap_pmm<int, int>>( pm );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ ИНТЕГРАЦИИ: Большая карта
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pmap_pmm<int,int>: handles large maps (100 elements)", "[pmap_pmm][large]" )
{
    PamManager::create( 256 * 1024 );

    auto pm = PamManager::template allocate_typed<pmap_pmm<int, int>>();
    std::memset( pm.resolve(), 0, sizeof( pmap_pmm<int, int> ) );

    // Вставляем элементы в обратном порядке
    for ( int i = 99; i >= 0; i-- )
    {
        pm->insert_direct( i, i * i );
    }

    REQUIRE( pm->size() == 100u );

    // Проверяем несколько значений
    REQUIRE( *pm->find( 0 ) == 0 );
    REQUIRE( *pm->find( 10 ) == 100 );
    REQUIRE( *pm->find( 50 ) == 2500 );
    REQUIRE( *pm->find( 99 ) == 9801 );

    // Проверяем порядок (должен быть отсортирован)
    int prev_key = -1;
    for ( auto it = pm->begin(); it != pm->end(); ++it )
    {
        REQUIRE( it->key > prev_key );
        prev_key = it->key;
    }

    pm->free();
    PamManager::template deallocate_typed<pmap_pmm<int, int>>( pm );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТ: Пустая карта
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pmap_pmm<int,int>: operations on empty map", "[pmap_pmm][empty]" )
{
    PamManager::create( 64 * 1024 );

    auto pm = PamManager::template allocate_typed<pmap_pmm<int, int>>();
    std::memset( pm.resolve(), 0, sizeof( pmap_pmm<int, int> ) );

    REQUIRE( pm->find( 1 ) == nullptr );
    REQUIRE( pm->erase( 1 ) == false );
    REQUIRE( pm->begin() == pm->end() );

    PamManager::template deallocate_typed<pmap_pmm<int, int>>( pm );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТ: Вставка с использованием insert() (с self_off)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pmap_pmm<int,int>: insert with self_off", "[pmap_pmm][insert]" )
{
    PamManager::create( 64 * 1024 );

    auto      pm     = PamManager::template allocate_typed<pmap_pmm<int, int>>();
    uintptr_t pm_off = pm.byte_offset();
    std::memset( pm.resolve(), 0, sizeof( pmap_pmm<int, int> ) );

    pm->insert( 42, 100, pm_off );

    // После insert нужно обновить указатель (может быть realloc)
    pm = PamManager::pptr_from_byte_offset<pmap_pmm<int, int>>( pm_off );

    REQUIRE( pm->size() == 1u );
    REQUIRE( *pm->find( 42 ) == 100 );

    pm->free();
    PamManager::template deallocate_typed<pmap_pmm<int, int>>( pm );
    PamManager::destroy();
}
