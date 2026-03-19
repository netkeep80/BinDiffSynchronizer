#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <type_traits>

#include "fptr_pmm.h"

// =============================================================================
// Тесты для fptr_pmm<T> (Задача 14.7)
//
// Эти тесты проверяют PMM-версию персистного указателя.
// =============================================================================

using namespace pjson;

// ---------------------------------------------------------------------------
// fptr_pmm<T> — статические проверки размера (Тр.5)
// ---------------------------------------------------------------------------
TEST_CASE( "fptr_pmm<T>: sizeof(fptr_pmm<T>) == sizeof(void*) (Tr.5)", "[task14.7][fptr_pmm][layout]" )
{
    REQUIRE( sizeof( fptr_pmm<int> ) == sizeof( void* ) );
    REQUIRE( sizeof( fptr_pmm<double> ) == sizeof( void* ) );
    REQUIRE( sizeof( fptr_pmm<char> ) == sizeof( void* ) );
}

// ---------------------------------------------------------------------------
// fptr_pmm<T> — тривиально копируем
// ---------------------------------------------------------------------------
TEST_CASE( "fptr_pmm<T>: is trivially copyable", "[task14.7][fptr_pmm][layout]" )
{
    REQUIRE( std::is_trivially_copyable<fptr_pmm<int>>::value );
    REQUIRE( std::is_trivially_copyable<fptr_pmm<double>>::value );
    REQUIRE( std::is_trivially_copyable<fptr_pmm<char>>::value );
}

// ---------------------------------------------------------------------------
// fptr_pmm<T> — инициализация по умолчанию
// ---------------------------------------------------------------------------
TEST_CASE( "fptr_pmm<int>: default constructor creates null pointer", "[task14.7][fptr_pmm]" )
{
    fptr_pmm<int> p;
    REQUIRE( p.addr() == 0u );
    REQUIRE( p.is_null() );
    REQUIRE( p == nullptr );
}

// ---------------------------------------------------------------------------
// fptr_pmm<T> — set_addr и addr
// ---------------------------------------------------------------------------
TEST_CASE( "fptr_pmm<int>: set_addr and addr -- get and set offset", "[task14.7][fptr_pmm]" )
{
    fptr_pmm<int> p;
    REQUIRE( p.addr() == 0u );

    // Используем выровненное значение (кратное размеру гранулы PMM = 16).
    p.set_addr( 16u );
    REQUIRE( p.addr() == 16u );

    p.set_addr( 0u );
    REQUIRE( p.addr() == 0u );
}

// ---------------------------------------------------------------------------
// fptr_pmm<double> — New / разыменование / Delete
// ---------------------------------------------------------------------------
TEST_CASE( "fptr_pmm<double>: New / dereference / Delete", "[task14.7][fptr_pmm]" )
{
    // Инициализируем PMM перед тестом.
    if ( !pam_pmm_is_initialized() )
        pam_pmm_init( nullptr );

    fptr_pmm<double> p;
    REQUIRE( p.addr() == 0u );

    p.New();
    REQUIRE( p.addr() != 0u );

    *p = 2.718;
    REQUIRE( *p == 2.718 );

    p.Delete();
    REQUIRE( p.addr() == 0u );
}

// ---------------------------------------------------------------------------
// fptr_pmm<int> — NewArray / operator[] / count / DeleteArray
// ---------------------------------------------------------------------------
TEST_CASE( "fptr_pmm<int>: NewArray / operator[] / count / DeleteArray", "[task14.7][fptr_pmm]" )
{
    if ( !pam_pmm_is_initialized() )
        pam_pmm_init( nullptr );

    fptr_pmm<int> arr;
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
}

// ---------------------------------------------------------------------------
// fptr_pmm — создание именованного объекта и поиск
// ---------------------------------------------------------------------------
TEST_CASE( "fptr_pmm<double>: New with name / pam_pmm_find", "[task14.7][fptr_pmm][named]" )
{
    if ( !pam_pmm_is_initialized() )
        pam_pmm_init( nullptr );

    const char*      name = "test_fptr_pmm_named_double";
    fptr_pmm<double> dp;

    dp.New( name );
    REQUIRE( dp.addr() != 0u );

    *dp        = 3.14159;
    double val = *dp;
    REQUIRE( val == 3.14159 );

    // Поиск по имени.
    uintptr_t found = pam_pmm_find( name );
    REQUIRE( found == dp.addr() );

    // Поиск с проверкой типа.
    uintptr_t found_typed = pam_pmm_find_typed<double>( name );
    REQUIRE( found_typed == dp.addr() );

    // Удаляем и проверяем, что слот освобождён.
    dp.Delete();
    uintptr_t found2 = pam_pmm_find( name );
    REQUIRE( found2 == 0u );
}

// ---------------------------------------------------------------------------
// fptr_pmm — создание именованного массива
// ---------------------------------------------------------------------------
TEST_CASE( "fptr_pmm<int>: NewArray with name / pam_pmm_get_count", "[task14.7][fptr_pmm][named]" )
{
    if ( !pam_pmm_is_initialized() )
        pam_pmm_init( nullptr );

    const char*   name = "test_fptr_pmm_array_v2";
    fptr_pmm<int> arr;

    arr.NewArray( 5, name );
    REQUIRE( arr.addr() != 0u );
    REQUIRE( pam_pmm_get_count( arr.addr() ) == 5u );

    // Записываем элементы.
    for ( unsigned i = 0; i < 5; i++ )
        arr[i] = static_cast<int>( i * 10 );

    // Читаем обратно.
    for ( unsigned i = 0; i < 5; i++ )
        REQUIRE( arr[i] == static_cast<int>( i * 10 ) );

    arr.DeleteArray();
    REQUIRE( pam_pmm_find( name ) == 0u );
}

// ---------------------------------------------------------------------------
// fptr_pmm::find — инициализация по имени объекта (Тр.15)
// ---------------------------------------------------------------------------
TEST_CASE( "fptr_pmm<int>: find() initializes by object name (Tr.15)", "[task14.7][fptr_pmm][find]" )
{
    if ( !pam_pmm_is_initialized() )
        pam_pmm_init( nullptr );

    // Создаём объект с именем.
    const char* name = "find_test_fptr_pmm_named_int";

    fptr_pmm<int> p1;
    p1.New( name );
    REQUIRE( p1.addr() != 0u );
    *p1 = 999;

    // Инициализируем другой fptr_pmm по имени.
    fptr_pmm<int> p2;
    p2.find( name );
    REQUIRE( p2.addr() != 0u );
    REQUIRE( *p2 == 999 );

    p1.Delete();
}

// ---------------------------------------------------------------------------
// fptr_pmm — конструктор с именем объекта
// ---------------------------------------------------------------------------
TEST_CASE( "fptr_pmm<int>: constructor with name finds object", "[task14.7][fptr_pmm][find]" )
{
    if ( !pam_pmm_is_initialized() )
        pam_pmm_init( nullptr );

    const char* name = "ctor_test_fptr_pmm_int";

    // Создаём объект с именем.
    fptr_pmm<int> p1;
    p1.New( name );
    *p1 = 777;

    // Создаём fptr_pmm через конструктор с именем.
    fptr_pmm<int> p2( name );
    REQUIRE( p2.addr() != 0u );
    REQUIRE( *p2 == 777 );

    p1.Delete();
}

// ---------------------------------------------------------------------------
// fptr_pmm — сравнение с nullptr
// ---------------------------------------------------------------------------
TEST_CASE( "fptr_pmm<int>: nullptr comparison", "[task14.7][fptr_pmm]" )
{
    fptr_pmm<int> p;
    REQUIRE( p == nullptr );
    REQUIRE( !( p != nullptr ) );

    if ( !pam_pmm_is_initialized() )
        pam_pmm_init( nullptr );

    p.New();
    REQUIRE( p != nullptr );
    REQUIRE( !( p == nullptr ) );

    p.Delete();
    REQUIRE( p == nullptr );
}

// ---------------------------------------------------------------------------
// fptr_pmm — сравнение двух указателей
// ---------------------------------------------------------------------------
TEST_CASE( "fptr_pmm<int>: comparison between pointers", "[task14.7][fptr_pmm]" )
{
    if ( !pam_pmm_is_initialized() )
        pam_pmm_init( nullptr );

    fptr_pmm<int> p1, p2;
    REQUIRE( p1 == p2 ); // Оба null.

    p1.New();
    REQUIRE( p1 != p2 );

    p2.set_addr( p1.addr() );
    REQUIRE( p1 == p2 );

    p1.Delete();
}

// ---------------------------------------------------------------------------
// fptr_pmm — объекты разных типов в едином ПАП (Тр.4)
// ---------------------------------------------------------------------------
TEST_CASE( "fptr_pmm: int and double objects in unified PAP (Tr.4)", "[task14.7][fptr_pmm][unified_space]" )
{
    if ( !pam_pmm_is_initialized() )
        pam_pmm_init( nullptr );

    fptr_pmm<int>    pi;
    fptr_pmm<double> pd;

    pi.New( "test_unified_fptr_pmm_int" );
    pd.New( "test_unified_fptr_pmm_double" );

    REQUIRE( pi.addr() != 0u );
    REQUIRE( pd.addr() != 0u );
    REQUIRE( pi.addr() != pd.addr() );

    *pi = 42;
    *pd = 3.14;

    REQUIRE( *pi == 42 );
    REQUIRE( *pd == 3.14 );

    pi.Delete();
    pd.Delete();
}

// ---------------------------------------------------------------------------
// pjson::fptr алиас = fptr_pmm
// ---------------------------------------------------------------------------
TEST_CASE( "pjson::fptr<T> alias works as fptr_pmm<T>", "[task14.7][fptr_pmm][alias]" )
{
    if ( !pam_pmm_is_initialized() )
        pam_pmm_init( nullptr );

    pjson::fptr<int> p;
    REQUIRE( p.is_null() );

    p.New();
    REQUIRE( !p.is_null() );
    *p = 123;
    REQUIRE( *p == 123 );

    p.Delete();
}

