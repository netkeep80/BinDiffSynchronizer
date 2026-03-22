/**
 * @file test_pam_pmm.cpp
 * @brief Тесты фасада pam_pmm.h.
 *
 * Тесты проверяют API pam_pmm:
 *   - Инициализация и уничтожение
 *   - Создание именованных и безымянных объектов
 *   - Поиск объектов по имени
 *   - Удаление объектов
 *   - Сохранение и загрузка из файла
 *   - Метрики
 */

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstring>

#include "pam_pmm.h"

using namespace pjson;

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ ИНИЦИАЛИЗАЦИИ И УНИЧТОЖЕНИЯ
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pam_pmm: init and destroy (in-memory)", "[pam_pmm]" )
{
    pam_pmm_init( nullptr );

    REQUIRE( pam_pmm_is_initialized() );
    REQUIRE( pam_pmm_get_data_size() > 0 );

    pam_pmm_destroy();

    REQUIRE_FALSE( pam_pmm_is_initialized() );
}

TEST_CASE( "pam_pmm: reset", "[pam_pmm]" )
{
    pam_pmm_init( nullptr );

    // Создаём объект.
    uintptr_t off = pam_pmm_create<int>( "test_obj" );
    REQUIRE( off != 0 );
    REQUIRE( pam_pmm_slot_count() > 0 );

    // Сбрасываем.
    pam_pmm_reset();

    // После сброса объект недоступен.
    REQUIRE( pam_pmm_find( "test_obj" ) == 0 );

    pam_pmm_destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ СОЗДАНИЯ ОБЪЕКТОВ
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pam_pmm: create unnamed object", "[pam_pmm]" )
{
    pam_pmm_init( nullptr );

    uintptr_t off = pam_pmm_create<int>();
    REQUIRE( off != 0 );

    // Получаем указатель и записываем значение.
    int* p = pam_pmm_resolve<int>( off );
    REQUIRE( p != nullptr );
    *p = 42;
    REQUIRE( *p == 42 );

    // Безымянный объект не имеет имени.
    REQUIRE( pam_pmm_get_name( off ) == nullptr );

    // Но есть в slot_map_.
    REQUIRE( pam_pmm_slot_count() > 0 );

    pam_pmm_destroy();
}

TEST_CASE( "pam_pmm: create named object", "[pam_pmm]" )
{
    pam_pmm_init( nullptr );

    uintptr_t off = pam_pmm_create<double>( "my_double" );
    REQUIRE( off != 0 );

    // Записываем значение.
    double* p = pam_pmm_resolve<double>( off );
    REQUIRE( p != nullptr );
    *p = 3.14159;
    REQUIRE( *p == 3.14159 );

    // Найти по имени.
    REQUIRE( pam_pmm_find( "my_double" ) == off );
    REQUIRE( pam_pmm_find_typed<double>( "my_double" ) == off );
    REQUIRE( pam_pmm_find_typed<int>( "my_double" ) == 0 ); // Размер не совпадает.

    // Получить имя.
    const char* name = pam_pmm_get_name( off );
    REQUIRE( name != nullptr );
    REQUIRE( std::strcmp( name, "my_double" ) == 0 );

    // Метрики.
    REQUIRE( pam_pmm_named_count() > 0 );

    pam_pmm_destroy();
}

TEST_CASE( "pam_pmm: create named object - duplicate name fails", "[pam_pmm]" )
{
    pam_pmm_init( nullptr );

    uintptr_t off1 = pam_pmm_create<int>( "unique_name" );
    REQUIRE( off1 != 0 );

    // Попытка создать объект с тем же именем должна вернуть 0.
    uintptr_t off2 = pam_pmm_create<int>( "unique_name" );
    REQUIRE( off2 == 0 );

    pam_pmm_destroy();
}

TEST_CASE( "pam_pmm: create array", "[pam_pmm]" )
{
    pam_pmm_init( nullptr );

    uintptr_t off = pam_pmm_create_array<int>( 10, "my_array" );
    REQUIRE( off != 0 );

    // Заполняем массив.
    int* arr = pam_pmm_resolve<int>( off );
    REQUIRE( arr != nullptr );
    for ( int i = 0; i < 10; i++ )
        arr[i] = i * i;

    // Проверяем значения.
    for ( int i = 0; i < 10; i++ )
        REQUIRE( arr[i] == i * i );

    // Метрики.
    REQUIRE( pam_pmm_get_count( off ) == 10 );
    REQUIRE( pam_pmm_get_elem_size( off ) == sizeof( int ) );

    pam_pmm_destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ УДАЛЕНИЯ ОБЪЕКТОВ
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pam_pmm: delete unnamed object", "[pam_pmm]" )
{
    pam_pmm_init( nullptr );

    uintptr_t off = pam_pmm_create<int>();
    REQUIRE( off != 0 );

    uintptr_t slot_count_before = pam_pmm_slot_count();

    pam_pmm_delete( off );

    // Слот должен быть удалён.
    REQUIRE( pam_pmm_slot_count() < slot_count_before );

    pam_pmm_destroy();
}

TEST_CASE( "pam_pmm: delete named object", "[pam_pmm]" )
{
    pam_pmm_init( nullptr );

    uintptr_t off = pam_pmm_create<int>( "to_delete" );
    REQUIRE( off != 0 );
    REQUIRE( pam_pmm_find( "to_delete" ) == off );

    pam_pmm_delete( off );

    // Объект больше не найден по имени.
    REQUIRE( pam_pmm_find( "to_delete" ) == 0 );

    pam_pmm_destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ СОХРАНЕНИЯ И ЗАГРУЗКИ
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pam_pmm: save and load", "[pam_pmm]" )
{
    const char* filename = "test_pam_pmm_saveload.dat";

    // Фаза 1: создаём данные и сохраняем.
    {
        pam_pmm_init( filename );

        uintptr_t off = pam_pmm_create<int>( "saved_int" );
        REQUIRE( off != 0 );

        int* p = pam_pmm_resolve<int>( off );
        *p     = 123456;

        pam_pmm_save();
        pam_pmm_destroy();
    }

    // Фаза 2: загружаем и проверяем данные.
    {
        pam_pmm_init( filename );

        uintptr_t off = pam_pmm_find( "saved_int" );
        REQUIRE( off != 0 );

        int* p = pam_pmm_resolve<int>( off );
        REQUIRE( p != nullptr );
        REQUIRE( *p == 123456 );

        pam_pmm_destroy();
    }

    // Удаляем тестовый файл.
    std::remove( filename );
}

TEST_CASE( "pam_pmm: save and load array", "[pam_pmm]" )
{
    const char* filename = "test_pam_pmm_array.dat";

    // Фаза 1: создаём массив и сохраняем.
    {
        pam_pmm_init( filename );

        uintptr_t off = pam_pmm_create_array<double>( 5, "saved_arr" );
        REQUIRE( off != 0 );

        double* arr = pam_pmm_resolve<double>( off );
        for ( int i = 0; i < 5; i++ )
            arr[i] = 1.1 * ( i + 1 );

        pam_pmm_save();
        pam_pmm_destroy();
    }

    // Фаза 2: загружаем и проверяем.
    {
        pam_pmm_init( filename );

        uintptr_t off = pam_pmm_find( "saved_arr" );
        REQUIRE( off != 0 );
        REQUIRE( pam_pmm_get_count( off ) == 5 );

        double* arr = pam_pmm_resolve<double>( off );
        REQUIRE( arr != nullptr );
        for ( int i = 0; i < 5; i++ )
            REQUIRE( arr[i] == 1.1 * ( i + 1 ) );

        pam_pmm_destroy();
    }

    std::remove( filename );
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ МЕТРИК
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pam_pmm: metrics", "[pam_pmm]" )
{
    pam_pmm_init( nullptr );

    REQUIRE( pam_pmm_get_data_size() > 0 );
    REQUIRE( pam_pmm_get_bump() > 0 ); // После создания реестра.

    uintptr_t initial_slots = pam_pmm_slot_count();

    // Создаём объекты.
    pam_pmm_create<int>( "named1" );
    pam_pmm_create<double>( "named2" );
    pam_pmm_create<char>();

    REQUIRE( pam_pmm_slot_count() == initial_slots + 3 );
    REQUIRE( pam_pmm_named_count() == 2 );

    pam_pmm_destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ РАЗЫМЕНОВАНИЯ
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pam_pmm: resolve and ptr_to_offset", "[pam_pmm]" )
{
    pam_pmm_init( nullptr );

    uintptr_t off = pam_pmm_create<int>();
    REQUIRE( off != 0 );

    int* p = pam_pmm_resolve<int>( off );
    REQUIRE( p != nullptr );

    // Преобразование обратно.
    uintptr_t off2 = pam_pmm_ptr_to_offset( p );
    REQUIRE( off2 == off );

    // nullptr возвращает 0.
    REQUIRE( pam_pmm_ptr_to_offset( nullptr ) == 0 );

    // Указатель вне PMM возвращает 0.
    int stack_var = 10;
    REQUIRE( pam_pmm_ptr_to_offset( &stack_var ) == 0 );

    pam_pmm_destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ REALLOC
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pam_pmm: realloc", "[pam_pmm]" )
{
    pam_pmm_init( nullptr );

    // Создаём массив из 4 элементов.
    uintptr_t off = pam_pmm_create_array<int>( 4 );
    REQUIRE( off != 0 );

    int* arr = pam_pmm_resolve<int>( off );
    for ( int i = 0; i < 4; i++ )
        arr[i] = i + 1;

    // Расширяем до 8 элементов.
    uintptr_t new_off = pam_pmm_realloc<int>( off, 4, 8 );
    REQUIRE( new_off != 0 );

    // Старые данные сохранены.
    int* new_arr = pam_pmm_resolve<int>( new_off );
    REQUIRE( new_arr != nullptr );
    for ( int i = 0; i < 4; i++ )
        REQUIRE( new_arr[i] == i + 1 );

    // Новые элементы инициализированы нулями.
    for ( int i = 4; i < 8; i++ )
        REQUIRE( new_arr[i] == 0 );

    pam_pmm_destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ СТРУКТУР
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pam_pmm: create custom struct", "[pam_pmm]" )
{
    struct TestStruct
    {
        int    id;
        double value;
        char   name[32];
    };

    static_assert( std::is_trivially_copyable<TestStruct>::value, "TestStruct должен быть тривиально копируемым" );

    pam_pmm_init( nullptr );

    uintptr_t off = pam_pmm_create<TestStruct>( "my_struct" );
    REQUIRE( off != 0 );

    TestStruct* s = pam_pmm_resolve<TestStruct>( off );
    REQUIRE( s != nullptr );

    s->id    = 100;
    s->value = 2.718;
    std::strncpy( s->name, "Test", 31 );

    // Проверяем.
    REQUIRE( s->id == 100 );
    REQUIRE( s->value == 2.718 );
    REQUIRE( std::strcmp( s->name, "Test" ) == 0 );

    // Метрики.
    REQUIRE( pam_pmm_get_elem_size( off ) == sizeof( TestStruct ) );

    pam_pmm_destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ МНОЖЕСТВЕННЫХ ОБЪЕКТОВ
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pam_pmm: multiple objects", "[pam_pmm]" )
{
    pam_pmm_init( nullptr );

    // Создаём много объектов.
    constexpr int N = 100;
    uintptr_t     offsets[N];

    for ( int i = 0; i < N; i++ )
    {
        char name[64];
        std::snprintf( name, sizeof( name ), "obj_%d", i );

        offsets[i] = pam_pmm_create<int>( name );
        REQUIRE( offsets[i] != 0 );

        int* p = pam_pmm_resolve<int>( offsets[i] );
        *p     = i * 10;
    }

    // Проверяем все объекты.
    for ( int i = 0; i < N; i++ )
    {
        char name[64];
        std::snprintf( name, sizeof( name ), "obj_%d", i );

        uintptr_t found_off = pam_pmm_find( name );
        REQUIRE( found_off == offsets[i] );

        int* p = pam_pmm_resolve<int>( found_off );
        REQUIRE( *p == i * 10 );
    }

    REQUIRE( pam_pmm_named_count() == N );

    pam_pmm_destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ ГРАНИЧНЫХ СЛУЧАЕВ
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pam_pmm: edge cases", "[pam_pmm]" )
{
    pam_pmm_init( nullptr );

    // Поиск несуществующего имени.
    REQUIRE( pam_pmm_find( "nonexistent" ) == 0 );
    REQUIRE( pam_pmm_find_typed<int>( "nonexistent" ) == 0 );

    // Поиск с nullptr/пустым именем.
    REQUIRE( pam_pmm_find( nullptr ) == 0 );
    REQUIRE( pam_pmm_find( "" ) == 0 );

    // Удаление с offset = 0 (должно быть безопасно).
    pam_pmm_delete( 0 );

    // Resolve с offset = 0.
    REQUIRE( pam_pmm_resolve<int>( 0 ) == nullptr );

    // Создание массива с count = 0.
    REQUIRE( pam_pmm_create_array<int>( 0 ) == 0 );

    pam_pmm_destroy();
}

TEST_CASE( "pam_pmm: validate", "[pam_pmm]" )
{
    pam_pmm_init( nullptr );
    REQUIRE( pam_pmm_validate() );
    pam_pmm_destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТ СОВМЕСТИМОСТИ ТИПОВ
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pam_pmm: trivial copyable check", "[pam_pmm]" )
{
    REQUIRE( std::is_trivially_copyable<pam_pmm_name_key>::value );
    REQUIRE( std::is_trivially_copyable<pam_pmm_slot_info>::value );
    REQUIRE( std::is_trivially_copyable<pam_pmm_registry>::value );
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТ КОРНЕВОГО ОБЪЕКТА PMM (Issue #163, Plan Stage 1.2)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pam_pmm: root object stored in PMM header", "[pam_pmm]" )
{
    const char* filename = "test_pam_pmm_root_api.dat";

    // Phase 1: create, verify root is set in PMM header, save.
    {
        pam_pmm_init( filename );

        // Root should be stored in PMM header via set_root().
        auto root_pptr = PamManager::template get_root<pam_pmm_root>();
        REQUIRE( !root_pptr.is_null() );

        pam_pmm_root* root = root_pptr.resolve();
        REQUIRE( root != nullptr );
        REQUIRE( root->magic == PAM_PMM_MAGIC );
        REQUIRE( root->version == PAM_PMM_VERSION );

        // Create some data to verify full cycle.
        uintptr_t off = pam_pmm_create<int>( "root_test_val" );
        REQUIRE( off != 0 );
        int* p = pam_pmm_resolve<int>( off );
        *p     = 999;

        pam_pmm_save();
        pam_pmm_destroy();
    }

    // Phase 2: load from file — root found via get_root (no magic scan).
    {
        pam_pmm_init( filename );

        auto root_pptr = PamManager::template get_root<pam_pmm_root>();
        REQUIRE( !root_pptr.is_null() );

        uintptr_t off = pam_pmm_find( "root_test_val" );
        REQUIRE( off != 0 );
        int* p = pam_pmm_resolve<int>( off );
        REQUIRE( p != nullptr );
        REQUIRE( *p == 999 );

        pam_pmm_destroy();
    }

    std::remove( filename );
}

TEST_CASE( "pam_pmm: root object survives reset", "[pam_pmm]" )
{
    pam_pmm_init( nullptr );

    auto root1 = PamManager::template get_root<pam_pmm_root>();
    REQUIRE( !root1.is_null() );

    // Reset should create a new root and set it in PMM header.
    pam_pmm_reset();

    auto root2 = PamManager::template get_root<pam_pmm_root>();
    REQUIRE( !root2.is_null() );

    pam_pmm_root* root = root2.resolve();
    REQUIRE( root != nullptr );
    REQUIRE( root->magic == PAM_PMM_MAGIC );

    pam_pmm_destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ pam_pmm_state (Этап 10.1: инкапсуляция глобального состояния)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pam_pmm_state: default construction zeroed", "[pam_pmm][pam_pmm_state]" )
{
    pam_pmm_state s{};
    REQUIRE( s.filename[0] == '\0' );
    REQUIRE( s.root_offset == 0 );
    REQUIRE( s.initialized == false );
}

TEST_CASE( "pam_pmm_state: reset clears all fields", "[pam_pmm][pam_pmm_state]" )
{
    pam_pmm_state s{};
    std::strncpy( s.filename, "test.pam", sizeof( s.filename ) - 1 );
    s.root_offset = 42;
    s.initialized = true;

    s.reset();

    REQUIRE( s.filename[0] == '\0' );
    REQUIRE( s.root_offset == 0 );
    REQUIRE( s.initialized == false );
}

TEST_CASE( "pam_pmm_state: global_state singleton is consistent with detail:: accessors", "[pam_pmm][pam_pmm_state]" )
{
    pam_pmm_state& gs = pam_pmm_global_state();

    // detail:: functions should return pointers/references into the same state.
    REQUIRE( detail::pam_pmm_filename() == gs.filename );
    REQUIRE( &detail::pam_pmm_root_offset() == &gs.root_offset );
    REQUIRE( &detail::pam_pmm_initialized() == &gs.initialized );
}

TEST_CASE( "pam_pmm_state: init/destroy cycle reflected in global state", "[pam_pmm][pam_pmm_state]" )
{
    pam_pmm_init( nullptr );

    pam_pmm_state& gs = pam_pmm_global_state();
    REQUIRE( gs.initialized == true );
    REQUIRE( gs.root_offset != 0 );

    pam_pmm_destroy();

    REQUIRE( gs.initialized == false );
    REQUIRE( gs.root_offset == 0 );
    REQUIRE( gs.filename[0] == '\0' );
}
