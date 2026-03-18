#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <cstdio>
#include <filesystem>
#include <type_traits>
#include <vector>

#include "pam_pmm.h"

using namespace pjson;

// =============================================================================
// Тесты для pam_pmm (PMM-бэкенд персистного адресного менеджера)
//
// Адаптация тестов PersistentAddressSpace → pam_pmm_* (Задача 14.11).
//
// Проверяет:
//   Тр.4  — единое ПАП для объектов разных типов
//   Тр.10 — при загрузке образа конструкторы не вызываются
//   Тр.14 — ПАМ хранит имена объектов
//   Тр.15 — поиск по имени
//   Тр.16 — ПАМ хранит карту объектов
// =============================================================================

namespace
{
/// Вспомогательная функция: удалить временный файл.
void rm_file( const char* path )
{
    std::error_code ec;
    std::filesystem::remove( path, ec );
}
} // anonymous namespace

// ---------------------------------------------------------------------------
// Структуры — тривиально копируемы
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm_slot_info: is trivially copyable", "[pam][layout]" )
{
    REQUIRE( std::is_trivially_copyable<pam_pmm_slot_info>::value );
}

TEST_CASE( "pam_pmm_root: is trivially copyable", "[pam][layout]" )
{
    REQUIRE( std::is_trivially_copyable<pam_pmm_root>::value );
}

// ---------------------------------------------------------------------------
// Init с несуществующим файлом создаёт пустой образ
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: Init() with nonexistent file creates empty image", "[pam][init]" )
{
    const char* fname = "./test_pam_init_empty.pam";
    rm_file( fname );

    // Init создаёт пустой образ без ошибок.
    REQUIRE_NOTHROW( pam_pmm_init( fname ) );

    REQUIRE( pam_pmm_is_initialized() );

    // Поиск несуществующего объекта возвращает 0.
    REQUIRE( pam_pmm_find( "nonexistent" ) == 0u );

    pam_pmm_destroy();
    rm_file( fname );
}

// ---------------------------------------------------------------------------
// Create возвращает ненулевое смещение
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: Create<int>() returns nonzero offset", "[pam][create]" )
{
    pam_pmm_init( nullptr );

    uintptr_t offset = pam_pmm_create<int>();
    REQUIRE( offset != 0u );

    pam_pmm_delete( offset );
    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// Resolve возвращает указатель; запись и чтение работают
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: Resolve<int>() write and read", "[pam][resolve]" )
{
    pam_pmm_init( nullptr );

    uintptr_t offset = pam_pmm_create<int>();
    REQUIRE( offset != 0u );

    int* p = pam_pmm_resolve<int>( offset );
    REQUIRE( p != nullptr );

    *p = 42;
    REQUIRE( *pam_pmm_resolve<int>( offset ) == 42 );

    pam_pmm_delete( offset );
    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// Поиск по имени (Тр.14, Тр.15, Тр.16)
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: Create with name -- Find returns same offset", "[pam][find][named]" )
{
    pam_pmm_init( nullptr );

    const char* name   = "pam_test_named_counter";
    uintptr_t   offset = pam_pmm_create<int>( name );
    REQUIRE( offset != 0u );

    // Find должен вернуть то же смещение.
    uintptr_t found = pam_pmm_find( name );
    REQUIRE( found == offset );

    pam_pmm_delete( offset );

    // После удаления поиск возвращает 0.
    uintptr_t after_del = pam_pmm_find( name );
    REQUIRE( after_del == 0u );

    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// FindTyped — поиск с проверкой типа
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: FindTyped<T> finds object of correct type", "[pam][find_typed]" )
{
    pam_pmm_init( nullptr );

    const char* name   = "pam_test_typed_double";
    uintptr_t   offset = pam_pmm_create<double>( name );
    REQUIRE( offset != 0u );

    // FindTyped с верным типом — находит.
    REQUIRE( pam_pmm_find_typed<double>( name ) == offset );
    // FindTyped с неверным типом — не находит.
    REQUIRE( pam_pmm_find_typed<int>( name ) == 0u );

    pam_pmm_delete( offset );
    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// Delete освобождает слот
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: Delete frees slot", "[pam][delete]" )
{
    pam_pmm_init( nullptr );

    const char* name   = "pam_test_delete_me";
    uintptr_t   offset = pam_pmm_create<int>( name );
    REQUIRE( offset != 0u );
    REQUIRE( pam_pmm_find( name ) != 0u );

    pam_pmm_delete( offset );
    REQUIRE( pam_pmm_find( name ) == 0u );

    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// CreateArray — массив объектов
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: CreateArray<char>(100) creates 100-byte array", "[pam][array]" )
{
    pam_pmm_init( nullptr );

    uintptr_t offset = pam_pmm_create_array<char>( 100 );
    REQUIRE( offset != 0u );
    REQUIRE( pam_pmm_get_count( offset ) == 100u );

    char* arr = pam_pmm_resolve<char>( offset );
    REQUIRE( arr != nullptr );

    // Запись и чтение всех элементов.
    for ( int i = 0; i < 100; i++ )
        arr[i] = static_cast<char>( i % 127 );
    for ( int i = 0; i < 100; i++ )
        REQUIRE( arr[i] == static_cast<char>( i % 127 ) );

    pam_pmm_delete( offset );
    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// Доступ к элементу массива по индексу (через Resolve + pointer arithmetic)
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: array element access via pointer arithmetic", "[pam][array][resolve_element]" )
{
    pam_pmm_init( nullptr );

    uintptr_t offset = pam_pmm_create_array<int>( 5 );
    REQUIRE( offset != 0u );

    int* arr = pam_pmm_resolve<int>( offset );
    REQUIRE( arr != nullptr );

    for ( unsigned i = 0; i < 5; i++ )
        arr[i] = static_cast<int>( i * 10 );

    for ( unsigned i = 0; i < 5; i++ )
        REQUIRE( arr[i] == static_cast<int>( i * 10 ) );

    pam_pmm_delete( offset );
    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// Единое ПАП для разных типов (Тр.4)
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: int and double objects in unified PAP (Tr.4)", "[pam][unified_space]" )
{
    pam_pmm_init( nullptr );

    uintptr_t off_i = pam_pmm_create<int>();
    uintptr_t off_d = pam_pmm_create<double>();
    uintptr_t off_c = pam_pmm_create_array<char>( 10 );

    REQUIRE( off_i != 0u );
    REQUIRE( off_d != 0u );
    REQUIRE( off_c != 0u );

    // Все смещения уникальны.
    REQUIRE( off_i != off_d );
    REQUIRE( off_i != off_c );
    REQUIRE( off_d != off_c );

    // Запись в каждый объект независима.
    *pam_pmm_resolve<int>( off_i )    = 100;
    *pam_pmm_resolve<double>( off_d ) = 3.14;
    pam_pmm_resolve<char>( off_c )[0] = 'A';

    REQUIRE( *pam_pmm_resolve<int>( off_i ) == 100 );
    REQUIRE( *pam_pmm_resolve<double>( off_d ) == 3.14 );
    REQUIRE( pam_pmm_resolve<char>( off_c )[0] == 'A' );

    pam_pmm_delete( off_i );
    pam_pmm_delete( off_d );
    pam_pmm_delete( off_c );
    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// PtrToOffset — обратный поиск по указателю
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: PtrToOffset returns offset by pointer", "[pam][find_by_ptr]" )
{
    pam_pmm_init( nullptr );

    uintptr_t offset = pam_pmm_create<int>();
    REQUIRE( offset != 0u );

    int* p = pam_pmm_resolve<int>( offset );
    REQUIRE( p != nullptr );

    uintptr_t found = pam_pmm_ptr_to_offset( static_cast<const void*>( p ) );
    REQUIRE( found == offset );

    pam_pmm_delete( offset );
    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// Save и повторная загрузка через Init (Тр.10 — без конструкторов)
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: Save and Init -- objects are restored", "[pam][save_reload]" )
{
    const char* fname = "./test_pam_save_reload.pam";
    rm_file( fname );

    uintptr_t saved_offset = 0;
    {
        pam_pmm_init( fname );

        const char* name = "pam_persist_counter";
        saved_offset     = pam_pmm_create<int>( name );
        REQUIRE( saved_offset != 0u );

        *pam_pmm_resolve<int>( saved_offset ) = 12345;

        pam_pmm_save();
        pam_pmm_destroy();
    }

    // Перезагружаем ПАП из файла. Конструкторы НЕ вызываются (Тр.10).
    pam_pmm_init( fname );
    {
        // Ищем объект по имени.
        uintptr_t offset = pam_pmm_find( "pam_persist_counter" );
        REQUIRE( offset == saved_offset );

        // Значение должно быть восстановлено.
        int* p = pam_pmm_resolve<int>( offset );
        REQUIRE( p != nullptr );
        REQUIRE( *p == 12345 );

        pam_pmm_delete( offset );
    }

    pam_pmm_destroy();
    rm_file( fname );
}

// ---------------------------------------------------------------------------
// GetCount возвращает корректное число элементов
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: GetCount returns correct element count", "[pam][count]" )
{
    pam_pmm_init( nullptr );

    uintptr_t offset = pam_pmm_create_array<double>( 7 );
    REQUIRE( offset != 0u );
    REQUIRE( pam_pmm_get_count( offset ) == 7u );

    pam_pmm_delete( offset );
    // После удаления GetCount должен вернуть 0.
    REQUIRE( pam_pmm_get_count( offset ) == 0u );

    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// Создание нескольких массивов не приводит к наложению (алиасингу)
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: multiple arrays do not overlap", "[pam][array][no_alias]" )
{
    pam_pmm_init( nullptr );

    uintptr_t off1 = pam_pmm_create_array<double>( 3 );
    uintptr_t off2 = pam_pmm_create_array<double>( 3 );

    REQUIRE( off1 != off2 );

    double* arr1 = pam_pmm_resolve<double>( off1 );
    double* arr2 = pam_pmm_resolve<double>( off2 );

    arr1[0] = 1.1;
    arr1[1] = 2.2;
    arr1[2] = 3.3;
    arr2[0] = 4.4;
    arr2[1] = 5.5;
    arr2[2] = 6.6;

    REQUIRE( pam_pmm_resolve<double>( off1 )[0] == 1.1 );
    REQUIRE( pam_pmm_resolve<double>( off2 )[0] == 4.4 );

    pam_pmm_delete( off1 );
    pam_pmm_delete( off2 );
    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// pam_pmm_registry — тривиально копируемый
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm_registry: is trivially copyable", "[pam][layout][type_registry]" )
{
    REQUIRE( std::is_trivially_copyable<pam_pmm_registry>::value );
}

// ---------------------------------------------------------------------------
// Один тип — GetElemSize возвращает правильный размер
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: same type returns correct elem_size", "[pam][type_registry]" )
{
    pam_pmm_init( nullptr );

    // Создаём несколько объектов одного типа.
    uintptr_t off1 = pam_pmm_create<int>( "pam_type_reg_a" );
    uintptr_t off2 = pam_pmm_create<int>( "pam_type_reg_b" );
    uintptr_t off3 = pam_pmm_create<int>( "pam_type_reg_c" );

    REQUIRE( off1 != 0u );
    REQUIRE( off2 != 0u );
    REQUIRE( off3 != 0u );

    // GetElemSize должен вернуть sizeof(int) для всех трёх.
    REQUIRE( pam_pmm_get_elem_size( off1 ) == sizeof( int ) );
    REQUIRE( pam_pmm_get_elem_size( off2 ) == sizeof( int ) );
    REQUIRE( pam_pmm_get_elem_size( off3 ) == sizeof( int ) );

    pam_pmm_delete( off1 );
    pam_pmm_delete( off2 );
    pam_pmm_delete( off3 );
    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// Разные типы имеют разные размеры элементов
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: different types have different elem_size", "[pam][type_registry]" )
{
    pam_pmm_init( nullptr );

    uintptr_t off_i = pam_pmm_create<int>( "pam_type_reg_int" );
    uintptr_t off_d = pam_pmm_create<double>( "pam_type_reg_double" );

    REQUIRE( off_i != 0u );
    REQUIRE( off_d != 0u );

    // Размеры элементов должны соответствовать реальным размерам типов.
    REQUIRE( pam_pmm_get_elem_size( off_i ) == sizeof( int ) );
    REQUIRE( pam_pmm_get_elem_size( off_d ) == sizeof( double ) );

    pam_pmm_delete( off_i );
    pam_pmm_delete( off_d );
    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// GetElemSize: массив возвращает размер одного элемента
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: GetElemSize returns element size for arrays", "[pam][type_registry][array]" )
{
    pam_pmm_init( nullptr );

    uintptr_t off = pam_pmm_create_array<double>( 5 );
    REQUIRE( off != 0u );

    // GetElemSize должен вернуть sizeof(double), а не sizeof(double)*5.
    REQUIRE( pam_pmm_get_elem_size( off ) == sizeof( double ) );
    REQUIRE( pam_pmm_get_count( off ) == 5u );

    pam_pmm_delete( off );
    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// Save и повторная загрузка: реестр восстанавливается
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: Save and Init -- registry is restored", "[pam][type_registry][save_reload]" )
{
    const char* fname = "./test_pam_type_registry.pam";
    rm_file( fname );

    uintptr_t saved_int_off = 0;
    uintptr_t saved_dbl_off = 0;

    {
        pam_pmm_init( fname );

        saved_int_off = pam_pmm_create<int>( "type_reg_int_obj" );
        saved_dbl_off = pam_pmm_create<double>( "type_reg_dbl_obj" );
        REQUIRE( saved_int_off != 0u );
        REQUIRE( saved_dbl_off != 0u );

        *pam_pmm_resolve<int>( saved_int_off )    = 777;
        *pam_pmm_resolve<double>( saved_dbl_off ) = 3.14;

        pam_pmm_save();
        pam_pmm_destroy();
    }

    // Перезагружаем ПАМ.
    pam_pmm_init( fname );
    {
        uintptr_t off_i = pam_pmm_find( "type_reg_int_obj" );
        uintptr_t off_d = pam_pmm_find( "type_reg_dbl_obj" );
        REQUIRE( off_i == saved_int_off );
        REQUIRE( off_d == saved_dbl_off );

        // Тип и данные восстановлены.
        REQUIRE( pam_pmm_get_elem_size( off_i ) == sizeof( int ) );
        REQUIRE( pam_pmm_get_elem_size( off_d ) == sizeof( double ) );
        REQUIRE( *pam_pmm_resolve<int>( off_i ) == 777 );
        REQUIRE( *pam_pmm_resolve<double>( off_d ) == 3.14 );

        pam_pmm_delete( off_i );
        pam_pmm_delete( off_d );
    }

    pam_pmm_destroy();
    rm_file( fname );
}

// ---------------------------------------------------------------------------
// pam_pmm_name_key — тривиально копируемый
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm_name_key: is trivially copyable", "[pam][layout][name_registry]" )
{
    REQUIRE( std::is_trivially_copyable<pam_pmm_name_key>::value );
}

// ---------------------------------------------------------------------------
// Таблица имён: GetName возвращает имя объекта по смещению
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: GetName returns object name by offset", "[pam][name_registry]" )
{
    pam_pmm_init( nullptr );

    const char* name   = "pam_name_reg_get_name";
    uintptr_t   offset = pam_pmm_create<int>( name );
    REQUIRE( offset != 0u );

    // GetName должен вернуть то же имя (двусторонняя связь slot → name).
    const char* retrieved = pam_pmm_get_name( offset );
    REQUIRE( retrieved != nullptr );
    REQUIRE( std::strcmp( retrieved, name ) == 0 );

    // Для безымянного объекта GetName должен вернуть nullptr.
    uintptr_t anon_offset = pam_pmm_create<int>();
    REQUIRE( anon_offset != 0u );
    REQUIRE( pam_pmm_get_name( anon_offset ) == nullptr );

    pam_pmm_delete( offset );
    pam_pmm_delete( anon_offset );
    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// Таблица имён: уникальность имён — создание дубликата возвращает 0
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: duplicate name returns 0 (name uniqueness)", "[pam][name_registry][unique]" )
{
    pam_pmm_init( nullptr );

    const char* name = "pam_name_unique_test";
    uintptr_t   off1 = pam_pmm_create<int>( name );
    REQUIRE( off1 != 0u );

    // Попытка создать второй объект с тем же именем должна вернуть 0.
    uintptr_t off2 = pam_pmm_create<int>( name );
    REQUIRE( off2 == 0u );

    // После удаления первого — имя освобождается.
    pam_pmm_delete( off1 );

    // Теперь можно создать с тем же именем снова.
    uintptr_t off3 = pam_pmm_create<int>( name );
    REQUIRE( off3 != 0u );

    pam_pmm_delete( off3 );
    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// Таблица имён: Find работает через карту имён
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: Find uses name map for lookup", "[pam][name_registry]" )
{
    pam_pmm_init( nullptr );

    const char* name   = "pam_name_reg_find_test";
    uintptr_t   offset = pam_pmm_create<double>( name );
    REQUIRE( offset != 0u );

    // Find через карту имён.
    uintptr_t found = pam_pmm_find( name );
    REQUIRE( found == offset );

    pam_pmm_delete( offset );

    // После удаления запись освобождается.
    REQUIRE( pam_pmm_find( name ) == 0u );

    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// Таблица имён: Delete освобождает запись в таблице имён
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: Delete frees name table entry", "[pam][name_registry][delete]" )
{
    pam_pmm_init( nullptr );

    const char* name   = "pam_name_delete_frees_entry";
    uintptr_t   offset = pam_pmm_create<int>( name );
    REQUIRE( offset != 0u );
    REQUIRE( pam_pmm_get_name( offset ) != nullptr );
    REQUIRE( pam_pmm_find( name ) == offset );

    pam_pmm_delete( offset );

    // После удаления: Find не находит объект.
    REQUIRE( pam_pmm_find( name ) == 0u );

    // Имя теперь свободно — можно создать снова.
    uintptr_t off2 = pam_pmm_create<int>( name );
    REQUIRE( off2 != 0u );
    pam_pmm_delete( off2 );
    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// Таблица имён: Save и Init — таблица имён восстанавливается
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: Save and Init -- name registry is restored", "[pam][name_registry][save_reload]" )
{
    const char* fname = "./test_pam_name_registry.pam";
    rm_file( fname );

    uintptr_t saved_off = 0;
    {
        pam_pmm_init( fname );

        saved_off = pam_pmm_create<int>( "name_reg_saved_obj" );
        REQUIRE( saved_off != 0u );
        *pam_pmm_resolve<int>( saved_off ) = 999;

        pam_pmm_save();
        pam_pmm_destroy();
    }

    // Перезагружаем ПАМ.
    pam_pmm_init( fname );
    {
        // Find через восстановленную таблицу имён.
        uintptr_t off = pam_pmm_find( "name_reg_saved_obj" );
        REQUIRE( off == saved_off );

        // GetName через восстановленную таблицу имён.
        const char* name = pam_pmm_get_name( off );
        REQUIRE( name != nullptr );
        REQUIRE( std::strcmp( name, "name_reg_saved_obj" ) == 0 );

        // Данные восстановлены.
        REQUIRE( *pam_pmm_resolve<int>( off ) == 999 );

        pam_pmm_delete( off );
    }

    pam_pmm_destroy();
    rm_file( fname );
}

// ---------------------------------------------------------------------------
// Таблица имён: несколько объектов — разные имена, разные слоты
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: multiple named objects have independent name entries", "[pam][name_registry]" )
{
    pam_pmm_init( nullptr );

    uintptr_t off_a = pam_pmm_create<int>( "pam_name_multi_a" );
    uintptr_t off_b = pam_pmm_create<int>( "pam_name_multi_b" );
    uintptr_t off_c = pam_pmm_create<double>( "pam_name_multi_c" );

    REQUIRE( off_a != 0u );
    REQUIRE( off_b != 0u );
    REQUIRE( off_c != 0u );

    // Каждое имя ведёт к своему смещению.
    REQUIRE( pam_pmm_find( "pam_name_multi_a" ) == off_a );
    REQUIRE( pam_pmm_find( "pam_name_multi_b" ) == off_b );
    REQUIRE( pam_pmm_find( "pam_name_multi_c" ) == off_c );

    // GetName возвращает верное имя для каждого слота.
    REQUIRE( std::strcmp( pam_pmm_get_name( off_a ), "pam_name_multi_a" ) == 0 );
    REQUIRE( std::strcmp( pam_pmm_get_name( off_b ), "pam_name_multi_b" ) == 0 );
    REQUIRE( std::strcmp( pam_pmm_get_name( off_c ), "pam_name_multi_c" ) == 0 );

    pam_pmm_delete( off_a );
    pam_pmm_delete( off_b );
    pam_pmm_delete( off_c );
    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// pam_pmm_name_key: has correct size
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm_name_key: has correct size", "[pam][layout][name_registry]" )
{
    // pam_pmm_name_key должен быть ровно PAM_PMM_NAME_SIZE байт.
    REQUIRE( sizeof( pam_pmm_name_key ) == PAM_PMM_NAME_SIZE );
}

// ---------------------------------------------------------------------------
// Карта слотов: GetCount и GetElemSize работают через бинарный поиск
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: slot map -- GetCount and GetElemSize via binary search",
           "[pam][slot_map]" )
{
    pam_pmm_init( nullptr );

    uintptr_t off = pam_pmm_create_array<int>( 7 );
    REQUIRE( off != 0u );

    // GetCount использует бинарный поиск в карте слотов.
    REQUIRE( pam_pmm_get_count( off ) == 7u );
    // GetElemSize использует бинарный поиск в карте слотов.
    REQUIRE( pam_pmm_get_elem_size( off ) == sizeof( int ) );

    pam_pmm_delete( off );
    REQUIRE( pam_pmm_get_count( off ) == 0u );

    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// Карта слотов поддерживает много объектов
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: slot map -- grows correctly with many objects", "[pam][slot_map][grow]" )
{
    pam_pmm_init( nullptr );

    // Создаём 64 объекта.
    const unsigned N = 64;
    uintptr_t      offsets[N]{};
    for ( unsigned i = 0; i < N; i++ )
    {
        offsets[i] = pam_pmm_create<int>();
        REQUIRE( offsets[i] != 0u );
    }

    // Все объекты доступны через GetCount.
    for ( unsigned i = 0; i < N; i++ )
        REQUIRE( pam_pmm_get_count( offsets[i] ) == 1u );

    // Освобождаем все объекты.
    for ( unsigned i = 0; i < N; i++ )
        pam_pmm_delete( offsets[i] );

    // После удаления — карта слотов не находит объекты.
    for ( unsigned i = 0; i < N; i++ )
        REQUIRE( pam_pmm_get_count( offsets[i] ) == 0u );

    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// Save и Init сохраняют/восстанавливают карту слотов
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: slot map -- Save and Init restore slot map",
           "[pam][slot_map][save_reload]" )
{
    const char* fname = "./test_pam_slot_map.pam";

    uintptr_t saved_off = 0;
    {
        pam_pmm_init( fname );

        saved_off = pam_pmm_create<double>( "slot_map_obj" );
        REQUIRE( saved_off != 0u );
        *pam_pmm_resolve<double>( saved_off ) = 2.718;

        pam_pmm_save();
        pam_pmm_destroy();
    }

    pam_pmm_init( fname );
    {
        // Карта слотов восстановлена: Find работает через таблицу имён.
        uintptr_t off = pam_pmm_find( "slot_map_obj" );
        REQUIRE( off == saved_off );
        REQUIRE( pam_pmm_get_count( off ) == 1u );
        REQUIRE( pam_pmm_get_elem_size( off ) == sizeof( double ) );

        // Данные восстановлены.
        double* p = pam_pmm_resolve<double>( off );
        REQUIRE( p != nullptr );
        REQUIRE( *p == 2.718 );

        pam_pmm_delete( off );
    }

    pam_pmm_destroy();
    rm_file( fname );
}

// ---------------------------------------------------------------------------
// Карта имён — Find возвращает правильный offset
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: name map -- Find returns correct offset", "[pam][name_map]" )
{
    pam_pmm_init( nullptr );

    uintptr_t off = pam_pmm_create<int>( "name_map_find_obj" );
    REQUIRE( off != 0u );
    *pam_pmm_resolve<int>( off ) = 777;

    // Find работает через карту имён.
    uintptr_t found = pam_pmm_find( "name_map_find_obj" );
    REQUIRE( found == off );
    REQUIRE( *pam_pmm_resolve<int>( found ) == 777 );

    pam_pmm_delete( off );
    REQUIRE( pam_pmm_find( "name_map_find_obj" ) == 0u );

    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// Карта имён — GetName возвращает имя
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: name map -- GetName returns name", "[pam][name_map]" )
{
    pam_pmm_init( nullptr );

    uintptr_t off = pam_pmm_create<float>( "name_map_name_obj" );
    REQUIRE( off != 0u );

    const char* nm = pam_pmm_get_name( off );
    REQUIRE( nm != nullptr );
    REQUIRE( std::strcmp( nm, "name_map_name_obj" ) == 0 );

    pam_pmm_delete( off );
    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// Карта имён — уникальность имён гарантируется
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: name map -- duplicate name returns 0", "[pam][name_map]" )
{
    pam_pmm_init( nullptr );

    uintptr_t off1 = pam_pmm_create<int>( "name_map_unique_name" );
    REQUIRE( off1 != 0u );

    // Попытка создать объект с тем же именем должна вернуть 0.
    uintptr_t off2 = pam_pmm_create<int>( "name_map_unique_name" );
    REQUIRE( off2 == 0u );

    pam_pmm_delete( off1 );
    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// Карта имён — рост за пределы начальной ёмкости
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: name map -- grows correctly with many named objects",
           "[pam][name_map][grow]" )
{
    pam_pmm_init( nullptr );

    const unsigned         N = 64;
    std::vector<uintptr_t> offsets;
    offsets.reserve( N );

    for ( unsigned i = 0; i < N; i++ )
    {
        char name[32];
        std::snprintf( name, sizeof( name ), "name_map_grow_%04u", i );
        uintptr_t off = pam_pmm_create<int>( name );
        REQUIRE( off != 0u );
        *pam_pmm_resolve<int>( off ) = static_cast<int>( i );
        offsets.push_back( off );
    }

    // Проверяем поиск всех объектов.
    for ( unsigned i = 0; i < N; i++ )
    {
        char name[32];
        std::snprintf( name, sizeof( name ), "name_map_grow_%04u", i );
        uintptr_t found = pam_pmm_find( name );
        REQUIRE( found == offsets[i] );
        REQUIRE( *pam_pmm_resolve<int>( found ) == static_cast<int>( i ) );
    }

    // Удаляем все объекты.
    for ( unsigned i = 0; i < N; i++ )
    {
        char name[32];
        std::snprintf( name, sizeof( name ), "name_map_grow_%04u", i );
        pam_pmm_delete( offsets[i] );
        REQUIRE( pam_pmm_find( name ) == 0u );
    }

    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// Карта имён — Save и Init восстанавливают карту имён из файла
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: name map -- Save and Init restore name map",
           "[pam][name_map][save_reload]" )
{
    const char* fname = "./test_pam_name_map.pam";
    rm_file( fname );

    uintptr_t saved_off = 0;
    {
        pam_pmm_init( fname );

        saved_off = pam_pmm_create<int>( "name_map_save_reload_obj" );
        REQUIRE( saved_off != 0u );
        *pam_pmm_resolve<int>( saved_off ) = 42;

        pam_pmm_save();
        pam_pmm_destroy();
    }

    // Перезагружаем ПАП из файла.
    pam_pmm_init( fname );
    {
        // Карта имён восстановлена — Find работает.
        uintptr_t off = pam_pmm_find( "name_map_save_reload_obj" );
        REQUIRE( off == saved_off );

        // Данные восстановлены.
        int* p = pam_pmm_resolve<int>( off );
        REQUIRE( p != nullptr );
        REQUIRE( *p == 42 );

        // GetName работает через восстановленную карту имён.
        const char* nm = pam_pmm_get_name( off );
        REQUIRE( nm != nullptr );
        REQUIRE( std::strcmp( nm, "name_map_save_reload_obj" ) == 0 );

        pam_pmm_delete( off );
    }

    pam_pmm_destroy();
    rm_file( fname );
}

// ---------------------------------------------------------------------------
// GetElemSize возвращает правильный размер для разных типов
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: GetElemSize returns correct size", "[pam][elem_size]" )
{
    pam_pmm_init( nullptr );

    uintptr_t off_int    = pam_pmm_create<int>();
    uintptr_t off_double = pam_pmm_create<double>();

    REQUIRE( off_int != 0u );
    REQUIRE( off_double != 0u );

    // GetElemSize должен возвращать правильный размер.
    REQUIRE( pam_pmm_get_elem_size( off_int ) == sizeof( int ) );
    REQUIRE( pam_pmm_get_elem_size( off_double ) == sizeof( double ) );

    pam_pmm_delete( off_int );
    pam_pmm_delete( off_double );
    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// Много именованных объектов — рост корректен
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: grows correctly with many named objects",
           "[pam][grow]" )
{
    pam_pmm_init( nullptr );

    const unsigned         N = 48;
    std::vector<uintptr_t> offsets;
    offsets.reserve( N );

    for ( unsigned i = 0; i < N; i++ )
    {
        char name[32];
        std::snprintf( name, sizeof( name ), "grow_%04u", i );
        uintptr_t off = pam_pmm_create<int>( name );
        REQUIRE( off != 0u );
        *pam_pmm_resolve<int>( off ) = static_cast<int>( i );
        offsets.push_back( off );
    }

    // Проверяем GetElemSize для всех объектов.
    for ( unsigned i = 0; i < N; i++ )
    {
        REQUIRE( pam_pmm_get_elem_size( offsets[i] ) == sizeof( int ) );
        REQUIRE( *pam_pmm_resolve<int>( offsets[i] ) == static_cast<int>( i ) );
    }

    // Удаляем все объекты.
    for ( unsigned i = 0; i < N; i++ )
        pam_pmm_delete( offsets[i] );

    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// Save и Init восстанавливают всё состояние
// ---------------------------------------------------------------------------
TEST_CASE( "pam_pmm: Save and Init restore full state",
           "[pam][save_reload]" )
{
    const char* fname = "./test_pam_full_state.pam";
    rm_file( fname );

    uintptr_t saved_off = 0;
    {
        pam_pmm_init( fname );

        saved_off = pam_pmm_create<double>( "full_state_obj" );
        REQUIRE( saved_off != 0u );
        *pam_pmm_resolve<double>( saved_off ) = 3.14;

        pam_pmm_save();
        pam_pmm_destroy();
    }

    // Перезагружаем ПАП из файла.
    pam_pmm_init( fname );
    {
        uintptr_t off = pam_pmm_find( "full_state_obj" );
        REQUIRE( off == saved_off );
        REQUIRE( pam_pmm_get_elem_size( off ) == sizeof( double ) );

        // Данные восстановлены.
        double* p = pam_pmm_resolve<double>( off );
        REQUIRE( p != nullptr );
        REQUIRE( *p == 3.14 );

        // GetName работает.
        const char* nm = pam_pmm_get_name( off );
        REQUIRE( nm != nullptr );
        REQUIRE( std::strcmp( nm, "full_state_obj" ) == 0 );

        pam_pmm_delete( off );
    }

    pam_pmm_destroy();
    rm_file( fname );
}
