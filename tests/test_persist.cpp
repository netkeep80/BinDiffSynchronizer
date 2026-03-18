#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <type_traits>

#include "fptr_pmm.h"
#include "persist_pmm.h"
#include "pam_pmm.h"

using namespace pjson;

// =============================================================================
// Тесты для persist<T>, fptr<T> (обновлено для PMM — Задача 14.11)
//
// persist<T> = persist_pmm<T> (Тр.8).
// fptr<T> = fptr_pmm<T> (Тр.5, Тр.12).
// =============================================================================

// ---------------------------------------------------------------------------
// persist<T> — статические проверки размера (Тр.8)
// ---------------------------------------------------------------------------
TEST_CASE( "persist<T>: sizeof(persist<T>) == sizeof(T) (Tr.8)", "[persist][layout]" )
{
    // Требование Тр.8: размер persist<T> должен равняться sizeof(T).
    REQUIRE( sizeof( persist<int> ) == sizeof( int ) );
    REQUIRE( sizeof( persist<double> ) == sizeof( double ) );
    REQUIRE( sizeof( persist<char> ) == sizeof( char ) );
    REQUIRE( sizeof( persist<float> ) == sizeof( float ) );
}

// ---------------------------------------------------------------------------
// fptr<T> — статические проверки размера (Тр.5)
// ---------------------------------------------------------------------------
TEST_CASE( "fptr<T>: sizeof(fptr<T>) == sizeof(void*) (Tr.5)", "[fptr][layout]" )
{
    // Требование Тр.5: размер fptr<T> должен равняться sizeof(void*).
    REQUIRE( sizeof( fptr<int> ) == sizeof( void* ) );
    REQUIRE( sizeof( fptr<double> ) == sizeof( void* ) );
    REQUIRE( sizeof( fptr<char> ) == sizeof( void* ) );
}

// ---------------------------------------------------------------------------
// fptr<T> — тривиально копируем
// ---------------------------------------------------------------------------
TEST_CASE( "fptr<T>: is trivially copyable", "[fptr][layout]" )
{
    REQUIRE( std::is_trivially_copyable<fptr<int>>::value );
    REQUIRE( std::is_trivially_copyable<fptr<double>>::value );
    REQUIRE( std::is_trivially_copyable<fptr<char>>::value );
}

// ---------------------------------------------------------------------------
// fptr<T> — инициализация по умолчанию и set_addr/addr
// ---------------------------------------------------------------------------
TEST_CASE( "fptr<int>: set_addr and addr -- get and set offset", "[fptr]" )
{
    fptr<int> p;
    REQUIRE( p.addr() == 0u );

    // В PMM смещения кратны размеру гранулы (16 байт).
    p.set_addr( 16u );
    REQUIRE( p.addr() == 16u );

    p.set_addr( 0u );
    REQUIRE( p.addr() == 0u );
}

// ---------------------------------------------------------------------------
// fptr<double> — New / разыменование / Delete
// ---------------------------------------------------------------------------
TEST_CASE( "fptr<double>: New / dereference / Delete", "[fptr]" )
{
    pam_pmm_init( nullptr );

    fptr<double> p;
    REQUIRE( p.addr() == 0u );

    p.New();
    REQUIRE( p.addr() != 0u );

    *p = 2.718;
    REQUIRE( *p == 2.718 );

    p.Delete();
    REQUIRE( p.addr() == 0u );

    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// fptr<int> — NewArray / operator[] / count / DeleteArray
// ---------------------------------------------------------------------------
TEST_CASE( "fptr<int>: NewArray / operator[] / count / DeleteArray", "[fptr]" )
{
    pam_pmm_init( nullptr );

    fptr<int> arr;
    arr.NewArray( 6 );
    REQUIRE( arr.addr() != 0u );
    REQUIRE( arr.count() == 6u );

    for ( unsigned i = 0; i < 6; i++ )
        arr[i] = static_cast<int>( i + 1 );

    for ( unsigned i = 0; i < 6; i++ )
        REQUIRE( arr[i] == static_cast<int>( i + 1 ) );

    arr.DeleteArray();
    REQUIRE( arr.addr() == 0u );
    REQUIRE( arr.count() == 0u );

    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// Create / доступ / Delete через PMM (замена AddressManager)
// ---------------------------------------------------------------------------
TEST_CASE( "fptr<double>: Create with name / access / Delete", "[fptr][named]" )
{
    pam_pmm_init( nullptr );

    // Создаём объект с именем для поиска.
    const char* name   = "test_am_create_delete_v2";
    uintptr_t   offset = pam_pmm_create<double>( name );
    REQUIRE( offset != 0u );

    // Записываем значение через fptr.
    fptr<double> dp;
    dp.set_addr( offset );
    *dp        = 3.14;
    double val = *dp;
    REQUIRE( val == 3.14 );

    // Поиск по имени.
    uintptr_t found = pam_pmm_find( name );
    REQUIRE( found == offset );

    // Удаляем и проверяем, что слот освобождён.
    pam_pmm_delete( offset );
    dp.set_addr( 0 );
    uintptr_t found2 = pam_pmm_find( name );
    REQUIRE( found2 == 0u );

    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// CreateArray / GetCount / GetArrayElement / DeleteArray через PMM
// ---------------------------------------------------------------------------
TEST_CASE( "fptr<int>: CreateArray with name / access array", "[fptr][array]" )
{
    pam_pmm_init( nullptr );

    const char* name   = "test_am_array_v2";
    uintptr_t   offset = pam_pmm_create_array<int>( 5, name );
    REQUIRE( offset != 0u );
    REQUIRE( pam_pmm_get_count( offset ) == 5u );

    // Записываем элементы через fptr.
    fptr<int> arr;
    arr.set_addr( offset );
    for ( unsigned i = 0; i < 5; i++ )
        arr[i] = static_cast<int>( i * 10 );

    // Читаем обратно.
    for ( unsigned i = 0; i < 5; i++ )
        REQUIRE( arr[i] == static_cast<int>( i * 10 ) );

    pam_pmm_delete( offset );
    arr.set_addr( 0 );
    REQUIRE( pam_pmm_find( name ) == 0u );

    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// PtrToOffset — обратный поиск по указателю
// ---------------------------------------------------------------------------
TEST_CASE( "fptr<int>: PtrToOffset returns offset by pointer", "[fptr][find_by_ptr]" )
{
    pam_pmm_init( nullptr );

    uintptr_t offset = pam_pmm_create_array<int>( 3 );
    REQUIRE( offset != 0u );

    int* p = pam_pmm_resolve<int>( offset );
    REQUIRE( p != nullptr );

    uintptr_t found = pam_pmm_ptr_to_offset( p );
    REQUIRE( found == offset );

    pam_pmm_delete( offset );
    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// Единое ПАП — объекты разных типов (Тр.4)
// ---------------------------------------------------------------------------
TEST_CASE( "fptr: int and double objects in unified PAP (Tr.4)", "[fptr][unified_space]" )
{
    pam_pmm_init( nullptr );

    // Создаём объекты разных типов.
    uintptr_t off_i = pam_pmm_create<int>( "test_unified_int" );
    uintptr_t off_d = pam_pmm_create<double>( "test_unified_double" );

    REQUIRE( off_i != 0u );
    REQUIRE( off_d != 0u );
    // Смещения должны быть разными.
    REQUIRE( off_i != off_d );

    // Записываем значения.
    fptr<int> pi;
    pi.set_addr( off_i );
    *pi = 42;
    fptr<double> pd;
    pd.set_addr( off_d );
    *pd = 3.14;

    REQUIRE( *pi == 42 );
    REQUIRE( *pd == 3.14 );

    // Убираем за собой.
    pam_pmm_delete( off_i );
    pam_pmm_delete( off_d );
    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// fptr::find — инициализация по имени объекта (Тр.15)
// ---------------------------------------------------------------------------
TEST_CASE( "fptr<int>: find() initializes by object name (Tr.15)", "[fptr][find]" )
{
    pam_pmm_init( nullptr );

    // Создаём объект с именем.
    uintptr_t offset = pam_pmm_create<int>( "find_test_named_int" );
    REQUIRE( offset != 0u );

    fptr<int> p1;
    p1.set_addr( offset );
    *p1 = 999;

    // Инициализируем другой fptr по имени.
    fptr<int> p2;
    p2.find( "find_test_named_int" );
    REQUIRE( p2.addr() != 0u );
    REQUIRE( *p2 == 999 );

    pam_pmm_delete( offset );
    pam_pmm_destroy();
}
