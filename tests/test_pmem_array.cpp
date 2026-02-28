#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <type_traits>

#include "pmem_array.h"

// =============================================================================
// Tests for pmem_array (persistent array primitive)
// =============================================================================

// ---------------------------------------------------------------------------
// pmem_array_hdr — layout checks
// ---------------------------------------------------------------------------
TEST_CASE( "pmem_array_hdr: is trivially copyable", "[pmem_array][layout]" )
{
    REQUIRE( std::is_trivially_copyable<pmem_array_hdr>::value );
}

TEST_CASE( "pmem_array_hdr: struct size is 3 * sizeof(void*)", "[pmem_array][layout]" )
{
    REQUIRE( sizeof( pmem_array_hdr ) == 3 * sizeof( void* ) );
    REQUIRE( sizeof( pmem_array_hdr::size ) == sizeof( void* ) );
    REQUIRE( sizeof( pmem_array_hdr::capacity ) == sizeof( void* ) );
    REQUIRE( sizeof( pmem_array_hdr::data_off ) == sizeof( void* ) );
}

// ---------------------------------------------------------------------------
// pmem_array_init — инициализация
// ---------------------------------------------------------------------------
TEST_CASE( "pmem_array_init: initialises header to zeroes", "[pmem_array][init]" )
{
    PersistentAddressSpace::Get().Reset();

    fptr<pmem_array_hdr> fhdr;
    fhdr.New();
    uintptr_t hdr_off = fhdr.addr();

    pmem_array_init<int>( hdr_off );

    REQUIRE( fhdr->size == 0u );
    REQUIRE( fhdr->capacity == 0u );
    REQUIRE( fhdr->data_off == 0u );

    fhdr.Delete();
}

// ---------------------------------------------------------------------------
// pmem_array_push_back / pmem_array_at — добавление и доступ
// ---------------------------------------------------------------------------
TEST_CASE( "pmem_array_push_back: appends elements and increases size", "[pmem_array][push_back]" )
{
    PersistentAddressSpace::Get().Reset();

    fptr<pmem_array_hdr> fhdr;
    fhdr.New();
    uintptr_t hdr_off = fhdr.addr();

    pmem_array_init<int>( hdr_off );

    pmem_array_push_back<int>( hdr_off ) = 10;
    pmem_array_push_back<int>( hdr_off ) = 20;
    pmem_array_push_back<int>( hdr_off ) = 30;

    auto&           pam = PersistentAddressSpace::Get();
    pmem_array_hdr* hdr = pam.Resolve<pmem_array_hdr>( hdr_off );

    REQUIRE( hdr->size == 3u );
    REQUIRE( pmem_array_at<int>( hdr_off, 0 ) == 10 );
    REQUIRE( pmem_array_at<int>( hdr_off, 1 ) == 20 );
    REQUIRE( pmem_array_at<int>( hdr_off, 2 ) == 30 );

    pmem_array_free<int>( hdr_off );
    fhdr.Delete();
}

// ---------------------------------------------------------------------------
// pmem_array_reserve — резервирование ёмкости
// ---------------------------------------------------------------------------
TEST_CASE( "pmem_array_reserve: capacity grows to accommodate elements", "[pmem_array][reserve]" )
{
    PersistentAddressSpace::Get().Reset();

    fptr<pmem_array_hdr> fhdr;
    fhdr.New();
    uintptr_t hdr_off = fhdr.addr();

    pmem_array_init<int>( hdr_off );

    pmem_array_reserve<int>( hdr_off, 100 );

    auto&           pam = PersistentAddressSpace::Get();
    pmem_array_hdr* hdr = pam.Resolve<pmem_array_hdr>( hdr_off );

    REQUIRE( hdr->capacity >= 100u );
    REQUIRE( hdr->size == 0u ); // reserve не изменяет size

    pmem_array_free<int>( hdr_off );
    fhdr.Delete();
}

// ---------------------------------------------------------------------------
// pmem_array_pop_back — удаление последнего элемента
// ---------------------------------------------------------------------------
TEST_CASE( "pmem_array_pop_back: decreases size by one", "[pmem_array][pop_back]" )
{
    PersistentAddressSpace::Get().Reset();

    fptr<pmem_array_hdr> fhdr;
    fhdr.New();
    uintptr_t hdr_off = fhdr.addr();

    pmem_array_init<int>( hdr_off );
    pmem_array_push_back<int>( hdr_off ) = 1;
    pmem_array_push_back<int>( hdr_off ) = 2;
    pmem_array_push_back<int>( hdr_off ) = 3;

    REQUIRE( pmem_array_size( hdr_off ) == 3u );

    pmem_array_pop_back<int>( hdr_off );
    REQUIRE( pmem_array_size( hdr_off ) == 2u );

    pmem_array_free<int>( hdr_off );
    fhdr.Delete();
}

// ---------------------------------------------------------------------------
// pmem_array_erase_at — удаление по индексу
// ---------------------------------------------------------------------------
TEST_CASE( "pmem_array_erase_at: removes element at given index and shifts remaining", "[pmem_array][erase]" )
{
    PersistentAddressSpace::Get().Reset();

    fptr<pmem_array_hdr> fhdr;
    fhdr.New();
    uintptr_t hdr_off = fhdr.addr();

    pmem_array_init<int>( hdr_off );
    pmem_array_push_back<int>( hdr_off ) = 10;
    pmem_array_push_back<int>( hdr_off ) = 20;
    pmem_array_push_back<int>( hdr_off ) = 30;
    pmem_array_push_back<int>( hdr_off ) = 40;

    // Удаляем элемент с индексом 1 (значение 20).
    pmem_array_erase_at<int>( hdr_off, 1 );

    REQUIRE( pmem_array_size( hdr_off ) == 3u );
    REQUIRE( pmem_array_at<int>( hdr_off, 0 ) == 10 );
    REQUIRE( pmem_array_at<int>( hdr_off, 1 ) == 30 );
    REQUIRE( pmem_array_at<int>( hdr_off, 2 ) == 40 );

    pmem_array_free<int>( hdr_off );
    fhdr.Delete();
}

// ---------------------------------------------------------------------------
// pmem_array_insert_sorted / pmem_array_find_sorted — сортированная вставка и поиск
// ---------------------------------------------------------------------------

// Простая структура для теста sorted операций.
struct TestEntry
{
    int key;
    int value;
};

static_assert( std::is_trivially_copyable<TestEntry>::value, "TestEntry должен быть тривиально копируемым" );

// Функтор: извлечь ключ из TestEntry.
struct TestKeyOf
{
    const int& operator()( const TestEntry& e ) const { return e.key; }
};

// Функтор: сравнение ключей.
struct TestLess
{
    bool operator()( const int& a, const int& b ) const { return a < b; }
};

TEST_CASE( "pmem_array_insert_sorted: inserts in sorted key order", "[pmem_array][sorted]" )
{
    PersistentAddressSpace::Get().Reset();

    fptr<pmem_array_hdr> fhdr;
    fhdr.New();
    uintptr_t hdr_off = fhdr.addr();

    pmem_array_init<TestEntry>( hdr_off );

    // Вставляем в неотсортированном порядке.
    pmem_array_insert_sorted<TestEntry, TestKeyOf, TestLess>( hdr_off, { 30, 300 }, TestKeyOf{}, TestLess{} );
    pmem_array_insert_sorted<TestEntry, TestKeyOf, TestLess>( hdr_off, { 10, 100 }, TestKeyOf{}, TestLess{} );
    pmem_array_insert_sorted<TestEntry, TestKeyOf, TestLess>( hdr_off, { 20, 200 }, TestKeyOf{}, TestLess{} );
    pmem_array_insert_sorted<TestEntry, TestKeyOf, TestLess>( hdr_off, { 5, 50 }, TestKeyOf{}, TestLess{} );

    REQUIRE( pmem_array_size( hdr_off ) == 4u );

    // Проверяем отсортированный порядок.
    REQUIRE( pmem_array_at<TestEntry>( hdr_off, 0 ).key == 5 );
    REQUIRE( pmem_array_at<TestEntry>( hdr_off, 1 ).key == 10 );
    REQUIRE( pmem_array_at<TestEntry>( hdr_off, 2 ).key == 20 );
    REQUIRE( pmem_array_at<TestEntry>( hdr_off, 3 ).key == 30 );

    pmem_array_free<TestEntry>( hdr_off );
    fhdr.Delete();
}

TEST_CASE( "pmem_array_insert_sorted: updates existing key without growing", "[pmem_array][sorted]" )
{
    PersistentAddressSpace::Get().Reset();

    fptr<pmem_array_hdr> fhdr;
    fhdr.New();
    uintptr_t hdr_off = fhdr.addr();

    pmem_array_init<TestEntry>( hdr_off );

    pmem_array_insert_sorted<TestEntry, TestKeyOf, TestLess>( hdr_off, { 10, 100 }, TestKeyOf{}, TestLess{} );
    pmem_array_insert_sorted<TestEntry, TestKeyOf, TestLess>( hdr_off, { 20, 200 }, TestKeyOf{}, TestLess{} );

    REQUIRE( pmem_array_size( hdr_off ) == 2u );

    // Обновляем существующий ключ.
    pmem_array_insert_sorted<TestEntry, TestKeyOf, TestLess>( hdr_off, { 10, 999 }, TestKeyOf{}, TestLess{} );

    // Размер не должен увеличиться.
    REQUIRE( pmem_array_size( hdr_off ) == 2u );

    // Значение должно обновиться.
    TestEntry* found =
        pmem_array_find_sorted<TestEntry, int, TestKeyOf, TestLess>( hdr_off, 10, TestKeyOf{}, TestLess{} );
    REQUIRE( found != nullptr );
    REQUIRE( found->value == 999 );

    pmem_array_free<TestEntry>( hdr_off );
    fhdr.Delete();
}

TEST_CASE( "pmem_array_find_sorted: finds existing element", "[pmem_array][sorted]" )
{
    PersistentAddressSpace::Get().Reset();

    fptr<pmem_array_hdr> fhdr;
    fhdr.New();
    uintptr_t hdr_off = fhdr.addr();

    pmem_array_init<TestEntry>( hdr_off );

    pmem_array_insert_sorted<TestEntry, TestKeyOf, TestLess>( hdr_off, { 10, 100 }, TestKeyOf{}, TestLess{} );
    pmem_array_insert_sorted<TestEntry, TestKeyOf, TestLess>( hdr_off, { 20, 200 }, TestKeyOf{}, TestLess{} );
    pmem_array_insert_sorted<TestEntry, TestKeyOf, TestLess>( hdr_off, { 30, 300 }, TestKeyOf{}, TestLess{} );

    TestEntry* found =
        pmem_array_find_sorted<TestEntry, int, TestKeyOf, TestLess>( hdr_off, 20, TestKeyOf{}, TestLess{} );
    REQUIRE( found != nullptr );
    REQUIRE( found->key == 20 );
    REQUIRE( found->value == 200 );

    pmem_array_free<TestEntry>( hdr_off );
    fhdr.Delete();
}

TEST_CASE( "pmem_array_find_sorted: returns nullptr for missing element", "[pmem_array][sorted]" )
{
    PersistentAddressSpace::Get().Reset();

    fptr<pmem_array_hdr> fhdr;
    fhdr.New();
    uintptr_t hdr_off = fhdr.addr();

    pmem_array_init<TestEntry>( hdr_off );

    pmem_array_insert_sorted<TestEntry, TestKeyOf, TestLess>( hdr_off, { 10, 100 }, TestKeyOf{}, TestLess{} );
    pmem_array_insert_sorted<TestEntry, TestKeyOf, TestLess>( hdr_off, { 30, 300 }, TestKeyOf{}, TestLess{} );

    TestEntry* found =
        pmem_array_find_sorted<TestEntry, int, TestKeyOf, TestLess>( hdr_off, 20, TestKeyOf{}, TestLess{} );
    REQUIRE( found == nullptr );

    pmem_array_free<TestEntry>( hdr_off );
    fhdr.Delete();
}

// ---------------------------------------------------------------------------
// pmem_array_clear — очистка без освобождения буфера
// ---------------------------------------------------------------------------
TEST_CASE( "pmem_array_clear: resets size to 0 without freeing buffer", "[pmem_array][clear]" )
{
    PersistentAddressSpace::Get().Reset();

    fptr<pmem_array_hdr> fhdr;
    fhdr.New();
    uintptr_t hdr_off = fhdr.addr();

    pmem_array_init<int>( hdr_off );
    pmem_array_push_back<int>( hdr_off ) = 1;
    pmem_array_push_back<int>( hdr_off ) = 2;
    pmem_array_push_back<int>( hdr_off ) = 3;

    uintptr_t cap_before = pmem_array_capacity( hdr_off );
    pmem_array_clear<int>( hdr_off );

    REQUIRE( pmem_array_size( hdr_off ) == 0u );
    REQUIRE( pmem_array_capacity( hdr_off ) == cap_before ); // буфер не освобождён

    pmem_array_free<int>( hdr_off );
    fhdr.Delete();
}

// ---------------------------------------------------------------------------
// pmem_array_free — полное освобождение буфера
// ---------------------------------------------------------------------------
TEST_CASE( "pmem_array_free: releases allocation and resets all fields", "[pmem_array][free]" )
{
    PersistentAddressSpace::Get().Reset();

    fptr<pmem_array_hdr> fhdr;
    fhdr.New();
    uintptr_t hdr_off = fhdr.addr();

    pmem_array_init<int>( hdr_off );
    pmem_array_push_back<int>( hdr_off ) = 42;
    pmem_array_push_back<int>( hdr_off ) = 43;

    pmem_array_free<int>( hdr_off );

    auto&           pam = PersistentAddressSpace::Get();
    pmem_array_hdr* hdr = pam.Resolve<pmem_array_hdr>( hdr_off );

    REQUIRE( hdr->size == 0u );
    REQUIRE( hdr->capacity == 0u );
    REQUIRE( hdr->data_off == 0u );

    fhdr.Delete();
}

// ---------------------------------------------------------------------------
// pmem_array_size / pmem_array_capacity — геттеры
// ---------------------------------------------------------------------------
TEST_CASE( "pmem_array_size and pmem_array_capacity return correct values", "[pmem_array][getters]" )
{
    PersistentAddressSpace::Get().Reset();

    fptr<pmem_array_hdr> fhdr;
    fhdr.New();
    uintptr_t hdr_off = fhdr.addr();

    pmem_array_init<int>( hdr_off );

    REQUIRE( pmem_array_size( hdr_off ) == 0u );
    REQUIRE( pmem_array_capacity( hdr_off ) == 0u );

    pmem_array_push_back<int>( hdr_off ) = 1;
    pmem_array_push_back<int>( hdr_off ) = 2;

    REQUIRE( pmem_array_size( hdr_off ) == 2u );
    REQUIRE( pmem_array_capacity( hdr_off ) >= 2u );

    pmem_array_free<int>( hdr_off );
    fhdr.Delete();
}

// ---------------------------------------------------------------------------
// Большой тест: 10000 элементов
// ---------------------------------------------------------------------------
TEST_CASE( "pmem_array_push_back: 10000 elements stored and retrieved correctly", "[pmem_array][large]" )
{
    PersistentAddressSpace::Get().Reset();

    fptr<pmem_array_hdr> fhdr;
    fhdr.New();
    uintptr_t hdr_off = fhdr.addr();

    pmem_array_init<int>( hdr_off );

    for ( int i = 0; i < 10000; i++ )
        pmem_array_push_back<int>( hdr_off ) = i;

    REQUIRE( pmem_array_size( hdr_off ) == 10000u );

    for ( int i = 0; i < 10000; i++ )
        REQUIRE( pmem_array_at<int>( hdr_off, static_cast<uintptr_t>( i ) ) == i );

    pmem_array_free<int>( hdr_off );
    fhdr.Delete();
}
