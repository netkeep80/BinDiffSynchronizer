#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <cstdio>
#include <filesystem>
#include <type_traits>

#include "pam.h"

// =============================================================================
// Тесты для PersistentAddressSpace (ПАМ — персистный адресный менеджер)
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
// Структуры заголовка и дескриптора — тривиально копируемы
// ---------------------------------------------------------------------------
TEST_CASE( "slot_descriptor: is trivially copyable", "[pam][layout]" )
{
    REQUIRE( std::is_trivially_copyable<slot_descriptor>::value );
}

TEST_CASE( "pam_header: is trivially copyable", "[pam][layout]" )
{
    REQUIRE( std::is_trivially_copyable<pam_header>::value );
}

// ---------------------------------------------------------------------------
// Init с несуществующим файлом создаёт пустой образ
// ---------------------------------------------------------------------------
TEST_CASE( "PersistentAddressSpace: Init() with nonexistent file creates empty image", "[pam][init]" )
{
    const char* fname = "./test_pam_init_empty.pam";
    rm_file( fname );

    // Init создаёт пустой образ без ошибок.
    REQUIRE_NOTHROW( PersistentAddressSpace::Init( fname ) );

    auto& pam = PersistentAddressSpace::Get();

    // Поиск несуществующего объекта возвращает 0.
    REQUIRE( pam.Find( "nonexistent" ) == 0u );

    rm_file( fname );
}

// ---------------------------------------------------------------------------
// Create возвращает ненулевое смещение
// ---------------------------------------------------------------------------
TEST_CASE( "PersistentAddressSpace: Create<int>() returns nonzero offset", "[pam][create]" )
{
    auto& pam = PersistentAddressSpace::Get();

    uintptr_t offset = pam.Create<int>();
    REQUIRE( offset != 0u );

    pam.Delete( offset );
}

// ---------------------------------------------------------------------------
// Resolve возвращает указатель; запись и чтение работают
// ---------------------------------------------------------------------------
TEST_CASE( "PersistentAddressSpace: Resolve<int>() write and read", "[pam][resolve]" )
{
    auto& pam = PersistentAddressSpace::Get();

    uintptr_t offset = pam.Create<int>();
    REQUIRE( offset != 0u );

    int* p = pam.Resolve<int>( offset );
    REQUIRE( p != nullptr );

    *p = 42;
    REQUIRE( *pam.Resolve<int>( offset ) == 42 );

    pam.Delete( offset );
}

// ---------------------------------------------------------------------------
// Поиск по имени (Тр.14, Тр.15, Тр.16)
// ---------------------------------------------------------------------------
TEST_CASE( "PersistentAddressSpace: Create with name -- Find returns same offset", "[pam][find][named]" )
{
    auto& pam = PersistentAddressSpace::Get();

    const char* name   = "pam_test_named_counter";
    uintptr_t   offset = pam.Create<int>( name );
    REQUIRE( offset != 0u );

    // Find должен вернуть то же смещение.
    uintptr_t found = pam.Find( name );
    REQUIRE( found == offset );

    pam.Delete( offset );

    // После удаления поиск возвращает 0.
    uintptr_t after_del = pam.Find( name );
    REQUIRE( after_del == 0u );
}

// ---------------------------------------------------------------------------
// FindTyped — поиск с проверкой типа
// ---------------------------------------------------------------------------
TEST_CASE( "PersistentAddressSpace: FindTyped<T> finds object of correct type", "[pam][find_typed]" )
{
    auto& pam = PersistentAddressSpace::Get();

    const char* name   = "pam_test_typed_double";
    uintptr_t   offset = pam.Create<double>( name );
    REQUIRE( offset != 0u );

    // FindTyped с верным типом — находит.
    REQUIRE( pam.FindTyped<double>( name ) == offset );
    // FindTyped с неверным типом — не находит.
    REQUIRE( pam.FindTyped<int>( name ) == 0u );

    pam.Delete( offset );
}

// ---------------------------------------------------------------------------
// Delete освобождает слот
// ---------------------------------------------------------------------------
TEST_CASE( "PersistentAddressSpace: Delete frees slot", "[pam][delete]" )
{
    auto& pam = PersistentAddressSpace::Get();

    const char* name   = "pam_test_delete_me";
    uintptr_t   offset = pam.Create<int>( name );
    REQUIRE( offset != 0u );
    REQUIRE( pam.Find( name ) != 0u );

    pam.Delete( offset );
    REQUIRE( pam.Find( name ) == 0u );
}

// ---------------------------------------------------------------------------
// CreateArray — массив объектов
// ---------------------------------------------------------------------------
TEST_CASE( "PersistentAddressSpace: CreateArray<char>(100) creates 100-byte array", "[pam][array]" )
{
    auto& pam = PersistentAddressSpace::Get();

    uintptr_t offset = pam.CreateArray<char>( 100 );
    REQUIRE( offset != 0u );
    REQUIRE( pam.GetCount( offset ) == 100u );

    char* arr = pam.Resolve<char>( offset );
    REQUIRE( arr != nullptr );

    // Запись и чтение всех элементов.
    for ( int i = 0; i < 100; i++ )
        arr[i] = static_cast<char>( i % 127 );
    for ( int i = 0; i < 100; i++ )
        REQUIRE( arr[i] == static_cast<char>( i % 127 ) );

    pam.Delete( offset );
}

// ---------------------------------------------------------------------------
// ResolveElement — доступ к элементу массива по индексу
// ---------------------------------------------------------------------------
TEST_CASE( "PersistentAddressSpace: ResolveElement<int> accesses array elements", "[pam][array][resolve_element]" )
{
    auto& pam = PersistentAddressSpace::Get();

    uintptr_t offset = pam.CreateArray<int>( 5 );
    REQUIRE( offset != 0u );

    for ( unsigned i = 0; i < 5; i++ )
        pam.ResolveElement<int>( offset, i ) = static_cast<int>( i * 10 );

    for ( unsigned i = 0; i < 5; i++ )
        REQUIRE( pam.ResolveElement<int>( offset, i ) == static_cast<int>( i * 10 ) );

    pam.Delete( offset );
}

// ---------------------------------------------------------------------------
// Единое ПАП для разных типов (Тр.4)
// ---------------------------------------------------------------------------
TEST_CASE( "PersistentAddressSpace: int and double objects in unified PAP (Tr.4)", "[pam][unified_space]" )
{
    auto& pam = PersistentAddressSpace::Get();

    uintptr_t off_i = pam.Create<int>();
    uintptr_t off_d = pam.Create<double>();
    uintptr_t off_c = pam.CreateArray<char>( 10 );

    REQUIRE( off_i != 0u );
    REQUIRE( off_d != 0u );
    REQUIRE( off_c != 0u );

    // Все смещения уникальны.
    REQUIRE( off_i != off_d );
    REQUIRE( off_i != off_c );
    REQUIRE( off_d != off_c );

    // Запись в каждый объект независима.
    *pam.Resolve<int>( off_i )    = 100;
    *pam.Resolve<double>( off_d ) = 3.14;
    pam.Resolve<char>( off_c )[0] = 'A';

    REQUIRE( *pam.Resolve<int>( off_i ) == 100 );
    REQUIRE( *pam.Resolve<double>( off_d ) == 3.14 );
    REQUIRE( pam.Resolve<char>( off_c )[0] == 'A' );

    pam.Delete( off_i );
    pam.Delete( off_d );
    pam.Delete( off_c );
}

// ---------------------------------------------------------------------------
// FindByPtr — обратный поиск по указателю
// ---------------------------------------------------------------------------
TEST_CASE( "PersistentAddressSpace: FindByPtr returns offset by pointer", "[pam][find_by_ptr]" )
{
    auto& pam = PersistentAddressSpace::Get();

    uintptr_t offset = pam.Create<int>();
    REQUIRE( offset != 0u );

    int* p = pam.Resolve<int>( offset );
    REQUIRE( p != nullptr );

    uintptr_t found = pam.FindByPtr( static_cast<const void*>( p ) );
    REQUIRE( found == offset );

    pam.Delete( offset );
}

// ---------------------------------------------------------------------------
// Save и повторная загрузка через Init (Тр.10 — без конструкторов)
// ---------------------------------------------------------------------------
TEST_CASE( "PersistentAddressSpace: Save and Init -- objects are restored", "[pam][save_reload]" )
{
    const char* fname = "./test_pam_save_reload.pam";
    rm_file( fname );

    uintptr_t saved_offset = 0;
    {
        PersistentAddressSpace::Init( fname );
        auto& pam = PersistentAddressSpace::Get();

        const char* name = "pam_persist_counter";
        saved_offset     = pam.Create<int>( name );
        REQUIRE( saved_offset != 0u );

        *pam.Resolve<int>( saved_offset ) = 12345;

        pam.Save();
    }

    // Перезагружаем ПАП из файла. Конструкторы НЕ вызываются (Тр.10).
    PersistentAddressSpace::Init( fname );
    {
        auto& pam = PersistentAddressSpace::Get();

        // Ищем объект по имени.
        uintptr_t offset = pam.Find( "pam_persist_counter" );
        REQUIRE( offset == saved_offset );

        // Значение должно быть восстановлено.
        int* p = pam.Resolve<int>( offset );
        REQUIRE( p != nullptr );
        REQUIRE( *p == 12345 );

        pam.Delete( offset );
    }

    rm_file( fname );
}

// ---------------------------------------------------------------------------
// GetCount возвращает корректное число элементов
// ---------------------------------------------------------------------------
TEST_CASE( "PersistentAddressSpace: GetCount returns correct element count", "[pam][count]" )
{
    auto& pam = PersistentAddressSpace::Get();

    uintptr_t offset = pam.CreateArray<double>( 7 );
    REQUIRE( offset != 0u );
    REQUIRE( pam.GetCount( offset ) == 7u );

    pam.Delete( offset );
    // После удаления GetCount должен вернуть 0.
    REQUIRE( pam.GetCount( offset ) == 0u );
}

// ---------------------------------------------------------------------------
// Создание нескольких массивов не приводит к наложению (алиасингу)
// ---------------------------------------------------------------------------
TEST_CASE( "PersistentAddressSpace: multiple arrays do not overlap", "[pam][array][no_alias]" )
{
    auto& pam = PersistentAddressSpace::Get();

    uintptr_t off1 = pam.CreateArray<double>( 3 );
    uintptr_t off2 = pam.CreateArray<double>( 3 );

    REQUIRE( off1 != off2 );

    pam.ResolveElement<double>( off1, 0 ) = 1.1;
    pam.ResolveElement<double>( off1, 1 ) = 2.2;
    pam.ResolveElement<double>( off1, 2 ) = 3.3;
    pam.ResolveElement<double>( off2, 0 ) = 4.4;
    pam.ResolveElement<double>( off2, 1 ) = 5.5;
    pam.ResolveElement<double>( off2, 2 ) = 6.6;

    REQUIRE( pam.ResolveElement<double>( off1, 0 ) == 1.1 );
    REQUIRE( pam.ResolveElement<double>( off2, 0 ) == 4.4 );

    pam.Delete( off1 );
    pam.Delete( off2 );
}

// ---------------------------------------------------------------------------
// type_info_entry — тривиально копируемый
// ---------------------------------------------------------------------------
TEST_CASE( "type_info_entry: is trivially copyable", "[pam][layout][type_registry]" )
{
    REQUIRE( std::is_trivially_copyable<type_info_entry>::value );
}

// ---------------------------------------------------------------------------
// Таблица типов: один тип не дублируется в таблице (задача #58)
// ---------------------------------------------------------------------------
TEST_CASE( "PersistentAddressSpace: same type not duplicated in type registry", "[pam][type_registry]" )
{
    auto& pam = PersistentAddressSpace::Get();

    // Создаём несколько объектов одного типа.
    uintptr_t off1 = pam.Create<int>( "pam_type_reg_a" );
    uintptr_t off2 = pam.Create<int>( "pam_type_reg_b" );
    uintptr_t off3 = pam.Create<int>( "pam_type_reg_c" );

    REQUIRE( off1 != 0u );
    REQUIRE( off2 != 0u );
    REQUIRE( off3 != 0u );

    // GetElemSize должен вернуть sizeof(int) для всех трёх.
    REQUIRE( pam.GetElemSize( off1 ) == sizeof( int ) );
    REQUIRE( pam.GetElemSize( off2 ) == sizeof( int ) );
    REQUIRE( pam.GetElemSize( off3 ) == sizeof( int ) );

    pam.Delete( off1 );
    pam.Delete( off2 );
    pam.Delete( off3 );
}

// ---------------------------------------------------------------------------
// Таблица типов: разные типы имеют разные записи
// ---------------------------------------------------------------------------
TEST_CASE( "PersistentAddressSpace: different types have different registry entries", "[pam][type_registry]" )
{
    auto& pam = PersistentAddressSpace::Get();

    uintptr_t off_i = pam.Create<int>( "pam_type_reg_int" );
    uintptr_t off_d = pam.Create<double>( "pam_type_reg_double" );

    REQUIRE( off_i != 0u );
    REQUIRE( off_d != 0u );

    // Размеры элементов должны соответствовать реальным размерам типов.
    REQUIRE( pam.GetElemSize( off_i ) == sizeof( int ) );
    REQUIRE( pam.GetElemSize( off_d ) == sizeof( double ) );

    pam.Delete( off_i );
    pam.Delete( off_d );
}

// ---------------------------------------------------------------------------
// GetElemSize: массив возвращает размер одного элемента
// ---------------------------------------------------------------------------
TEST_CASE( "PersistentAddressSpace: GetElemSize returns element size for arrays", "[pam][type_registry][array]" )
{
    auto& pam = PersistentAddressSpace::Get();

    uintptr_t off = pam.CreateArray<double>( 5 );
    REQUIRE( off != 0u );

    // GetElemSize должен вернуть sizeof(double), а не sizeof(double)*5.
    REQUIRE( pam.GetElemSize( off ) == sizeof( double ) );
    REQUIRE( pam.GetCount( off ) == 5u );

    pam.Delete( off );
}

// ---------------------------------------------------------------------------
// Save и повторная загрузка: таблица типов восстанавливается
// ---------------------------------------------------------------------------
TEST_CASE( "PersistentAddressSpace: Save and Init -- type registry is restored", "[pam][type_registry][save_reload]" )
{
    const char* fname = "./test_pam_type_registry.pam";
    rm_file( fname );

    uintptr_t saved_int_off = 0;
    uintptr_t saved_dbl_off = 0;

    {
        PersistentAddressSpace::Init( fname );
        auto& pam = PersistentAddressSpace::Get();

        saved_int_off = pam.Create<int>( "type_reg_int_obj" );
        saved_dbl_off = pam.Create<double>( "type_reg_dbl_obj" );
        REQUIRE( saved_int_off != 0u );
        REQUIRE( saved_dbl_off != 0u );

        *pam.Resolve<int>( saved_int_off )    = 777;
        *pam.Resolve<double>( saved_dbl_off ) = 3.14;

        pam.Save();
    }

    // Перезагружаем ПАМ.
    PersistentAddressSpace::Init( fname );
    {
        auto& pam = PersistentAddressSpace::Get();

        uintptr_t off_i = pam.Find( "type_reg_int_obj" );
        uintptr_t off_d = pam.Find( "type_reg_dbl_obj" );
        REQUIRE( off_i == saved_int_off );
        REQUIRE( off_d == saved_dbl_off );

        // Тип и данные восстановлены.
        REQUIRE( pam.GetElemSize( off_i ) == sizeof( int ) );
        REQUIRE( pam.GetElemSize( off_d ) == sizeof( double ) );
        REQUIRE( *pam.Resolve<int>( off_i ) == 777 );
        REQUIRE( *pam.Resolve<double>( off_d ) == 3.14 );

        pam.Delete( off_i );
        pam.Delete( off_d );
    }

    rm_file( fname );
}

// ---------------------------------------------------------------------------
// name_info_entry — тривиально копируемый (задача #58, фаза 5)
// ---------------------------------------------------------------------------
TEST_CASE( "name_info_entry: is trivially copyable", "[pam][layout][name_registry]" )
{
    REQUIRE( std::is_trivially_copyable<name_info_entry>::value );
}

// ---------------------------------------------------------------------------
// Таблица имён: GetName возвращает имя объекта по смещению (двусторонняя связь)
// ---------------------------------------------------------------------------
TEST_CASE( "PersistentAddressSpace: GetName returns object name by offset", "[pam][name_registry]" )
{
    auto& pam = PersistentAddressSpace::Get();

    const char* name   = "pam_name_reg_get_name";
    uintptr_t   offset = pam.Create<int>( name );
    REQUIRE( offset != 0u );

    // GetName должен вернуть то же имя (двусторонняя связь slot → name).
    const char* retrieved = pam.GetName( offset );
    REQUIRE( retrieved != nullptr );
    REQUIRE( std::strcmp( retrieved, name ) == 0 );

    // Для безымянного объекта GetName должен вернуть nullptr.
    uintptr_t anon_offset = pam.Create<int>();
    REQUIRE( anon_offset != 0u );
    REQUIRE( pam.GetName( anon_offset ) == nullptr );

    pam.Delete( offset );
    pam.Delete( anon_offset );
}

// ---------------------------------------------------------------------------
// Таблица имён: уникальность имён — создание дубликата возвращает 0
// ---------------------------------------------------------------------------
TEST_CASE( "PersistentAddressSpace: duplicate name returns 0 (name uniqueness)", "[pam][name_registry][unique]" )
{
    auto& pam = PersistentAddressSpace::Get();

    const char* name = "pam_name_unique_test";
    uintptr_t   off1 = pam.Create<int>( name );
    REQUIRE( off1 != 0u );

    // Попытка создать второй объект с тем же именем должна вернуть 0.
    uintptr_t off2 = pam.Create<int>( name );
    REQUIRE( off2 == 0u );

    // После удаления первого — имя освобождается.
    pam.Delete( off1 );

    // Теперь можно создать с тем же именем снова.
    uintptr_t off3 = pam.Create<int>( name );
    REQUIRE( off3 != 0u );

    pam.Delete( off3 );
}

// ---------------------------------------------------------------------------
// Таблица имён: Find использует таблицу имён (двусторонняя связь name → slot)
// ---------------------------------------------------------------------------
TEST_CASE( "PersistentAddressSpace: Find uses name table for lookup", "[pam][name_registry]" )
{
    auto& pam = PersistentAddressSpace::Get();

    const char* name   = "pam_name_reg_find_test";
    uintptr_t   offset = pam.Create<double>( name );
    REQUIRE( offset != 0u );

    // Find через таблицу имён: name_info_entry.slot_offset → карта слотов (фаза 8.2).
    uintptr_t found = pam.Find( name );
    REQUIRE( found == offset );

    pam.Delete( offset );

    // После удаления запись в таблице имён освобождается.
    REQUIRE( pam.Find( name ) == 0u );
}

// ---------------------------------------------------------------------------
// Таблица имён: Delete освобождает запись в таблице имён
// ---------------------------------------------------------------------------
TEST_CASE( "PersistentAddressSpace: Delete frees name table entry", "[pam][name_registry][delete]" )
{
    auto& pam = PersistentAddressSpace::Get();

    const char* name   = "pam_name_delete_frees_entry";
    uintptr_t   offset = pam.Create<int>( name );
    REQUIRE( offset != 0u );
    REQUIRE( pam.GetName( offset ) != nullptr );
    REQUIRE( pam.Find( name ) == offset );

    pam.Delete( offset );

    // После удаления: Find не находит объект.
    REQUIRE( pam.Find( name ) == 0u );
    // offset больше не является валидным слотом → GetName вернёт nullptr.
    REQUIRE( pam.GetName( offset ) == nullptr );

    // Имя теперь свободно — можно создать снова.
    uintptr_t off2 = pam.Create<int>( name );
    REQUIRE( off2 != 0u );
    pam.Delete( off2 );
}

// ---------------------------------------------------------------------------
// Таблица имён: Save и Init — таблица имён восстанавливается
// ---------------------------------------------------------------------------
TEST_CASE( "PersistentAddressSpace: Save and Init -- name registry is restored", "[pam][name_registry][save_reload]" )
{
    const char* fname = "./test_pam_name_registry.pam";
    rm_file( fname );

    uintptr_t saved_off = 0;
    {
        PersistentAddressSpace::Init( fname );
        auto& pam = PersistentAddressSpace::Get();

        saved_off = pam.Create<int>( "name_reg_saved_obj" );
        REQUIRE( saved_off != 0u );
        *pam.Resolve<int>( saved_off ) = 999;

        pam.Save();
    }

    // Перезагружаем ПАМ.
    PersistentAddressSpace::Init( fname );
    {
        auto& pam = PersistentAddressSpace::Get();

        // Find через восстановленную таблицу имён.
        uintptr_t off = pam.Find( "name_reg_saved_obj" );
        REQUIRE( off == saved_off );

        // GetName через восстановленную таблицу имён (двусторонняя связь).
        const char* name = pam.GetName( off );
        REQUIRE( name != nullptr );
        REQUIRE( std::strcmp( name, "name_reg_saved_obj" ) == 0 );

        // Данные восстановлены.
        REQUIRE( *pam.Resolve<int>( off ) == 999 );

        pam.Delete( off );
    }

    rm_file( fname );
}

// ---------------------------------------------------------------------------
// Таблица имён: несколько объектов — разные имена, разные слоты
// ---------------------------------------------------------------------------
TEST_CASE( "PersistentAddressSpace: multiple named objects have independent name entries", "[pam][name_registry]" )
{
    auto& pam = PersistentAddressSpace::Get();

    uintptr_t off_a = pam.Create<int>( "pam_name_multi_a" );
    uintptr_t off_b = pam.Create<int>( "pam_name_multi_b" );
    uintptr_t off_c = pam.Create<double>( "pam_name_multi_c" );

    REQUIRE( off_a != 0u );
    REQUIRE( off_b != 0u );
    REQUIRE( off_c != 0u );

    // Каждое имя ведёт к своему смещению.
    REQUIRE( pam.Find( "pam_name_multi_a" ) == off_a );
    REQUIRE( pam.Find( "pam_name_multi_b" ) == off_b );
    REQUIRE( pam.Find( "pam_name_multi_c" ) == off_c );

    // GetName возвращает верное имя для каждого слота.
    REQUIRE( std::strcmp( pam.GetName( off_a ), "pam_name_multi_a" ) == 0 );
    REQUIRE( std::strcmp( pam.GetName( off_b ), "pam_name_multi_b" ) == 0 );
    REQUIRE( std::strcmp( pam.GetName( off_c ), "pam_name_multi_c" ) == 0 );

    pam.Delete( off_a );
    pam.Delete( off_b );
    pam.Delete( off_c );
}

// ---------------------------------------------------------------------------
// Фаза 8.2: карта слотов внутри ПАП (pmap<uintptr_t, SlotInfo>)
// ---------------------------------------------------------------------------

// SlotInfo — тривиально копируемый
TEST_CASE( "SlotInfo: is trivially copyable", "[pam][layout][phase82]" )
{
    REQUIRE( std::is_trivially_copyable<SlotInfo>::value );
}

// slot_entry — тривиально копируемый
TEST_CASE( "slot_entry: is trivially copyable", "[pam][layout][phase82]" )
{
    REQUIRE( std::is_trivially_copyable<slot_entry>::value );
}

// SlotInfo содержит count, type_idx, name_idx
TEST_CASE( "SlotInfo: has correct fields", "[pam][layout][phase82]" )
{
    SlotInfo si{};
    si.count    = 42u;
    si.type_idx = 1u;
    si.name_idx = PAM_INVALID_IDX;
    REQUIRE( si.count == 42u );
    REQUIRE( si.type_idx == 1u );
    REQUIRE( si.name_idx == PAM_INVALID_IDX );
}

// GetCount и GetElemSize работают после вставки в карту слотов
TEST_CASE( "PersistentAddressSpace: phase 8.2 slot map -- GetCount and GetElemSize via binary search",
           "[pam][phase82]" )
{
    auto& pam = PersistentAddressSpace::Get();

    uintptr_t off = pam.CreateArray<int>( 7 );
    REQUIRE( off != 0u );

    // GetCount использует бинарный поиск в карте слотов (O(log n)).
    REQUIRE( pam.GetCount( off ) == 7u );
    // GetElemSize использует бинарный поиск в карте слотов (O(log n)).
    REQUIRE( pam.GetElemSize( off ) == sizeof( int ) );

    pam.Delete( off );
    REQUIRE( pam.GetCount( off ) == 0u );
}

// Карта слотов поддерживает много объектов (тест растущей карты внутри ПАП)
TEST_CASE( "PersistentAddressSpace: phase 8.2 slot map -- grows correctly with many objects", "[pam][phase82][grow]" )
{
    auto& pam = PersistentAddressSpace::Get();

    // Создаём больше объектов, чем начальная ёмкость карты слотов (16).
    const unsigned N = 64;
    uintptr_t      offsets[N]{};
    for ( unsigned i = 0; i < N; i++ )
    {
        offsets[i] = pam.Create<int>();
        REQUIRE( offsets[i] != 0u );
    }

    // Все объекты доступны через GetCount (бинарный поиск в карте слотов).
    for ( unsigned i = 0; i < N; i++ )
        REQUIRE( pam.GetCount( offsets[i] ) == 1u );

    // Освобождаем все объекты.
    for ( unsigned i = 0; i < N; i++ )
        pam.Delete( offsets[i] );

    // После удаления — карта слотов не находит объекты.
    for ( unsigned i = 0; i < N; i++ )
        REQUIRE( pam.GetCount( offsets[i] ) == 0u );
}

// Save и Init сохраняют/восстанавливают карту слотов внутри ПАП
TEST_CASE( "PersistentAddressSpace: phase 8.2 slot map -- Save and Init restore slot map",
           "[pam][phase82][save_reload]" )
{
    const char* fname = "./test_pam_phase82_slot_map.pam";

    uintptr_t saved_off = 0;
    {
        PersistentAddressSpace::Init( fname );
        auto& pam = PersistentAddressSpace::Get();

        saved_off = pam.Create<double>( "phase82_slot_map_obj" );
        REQUIRE( saved_off != 0u );
        *pam.Resolve<double>( saved_off ) = 2.718;

        pam.Save();
    }

    PersistentAddressSpace::Init( fname );
    {
        auto& pam = PersistentAddressSpace::Get();

        // Карта слотов восстановлена: Find работает через таблицу имён.
        uintptr_t off = pam.Find( "phase82_slot_map_obj" );
        REQUIRE( off == saved_off );
        REQUIRE( pam.GetCount( off ) == 1u );
        REQUIRE( pam.GetElemSize( off ) == sizeof( double ) );

        // Данные восстановлены.
        double* p = pam.Resolve<double>( off );
        REQUIRE( p != nullptr );
        REQUIRE( *p == 2.718 );

        pam.Delete( off );
    }

    std::error_code ec;
    std::filesystem::remove( fname, ec );
}
