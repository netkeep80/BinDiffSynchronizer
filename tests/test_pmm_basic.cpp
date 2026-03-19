/**
 * @file test_pmm_basic.cpp
 * @brief Базовые тесты интеграции PersistMemoryManager.
 *
 * Тесты проверяют корректность подключения PMM и базовые операции:
 *   - Создание и уничтожение менеджера
 *   - Аллокация и деаллокация памяти
 *   - Типизированная аллокация (allocate_typed<T>)
 *   - Проверка конфигурации PamManager
 *   - Конверсия pptr<T> <-> uintptr_t
 */

#include <catch2/catch_test_macros.hpp>
#include <cstring>

// Включаем конфигурацию pjson_db для PMM
#include "pam_pmm_config.h"

// Включаем адаптер pptr <-> uintptr_t
#include "pam_adapter.h"

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ ИНТЕГРАЦИИ PMM
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "PMM: create and destroy manager", "[pmm]" )
{
    // Создание менеджера с начальным размером 64 КБ
    pjson::PamManager::create( 64 * 1024 );

    // Проверяем, что менеджер создан
    REQUIRE( pjson::PamManager::total_size() > 0 );

    // Уничтожение менеджера
    pjson::PamManager::destroy();
}

TEST_CASE( "PMM: allocate and deallocate raw memory", "[pmm]" )
{
    pjson::PamManager::create( 64 * 1024 );

    // Аллоцируем блок памяти
    void* ptr = pjson::PamManager::allocate( 256 );
    REQUIRE( ptr != nullptr );

    // Можем записать данные
    std::memset( ptr, 0xAB, 256 );

    // Деаллоцируем
    pjson::PamManager::deallocate( ptr );

    pjson::PamManager::destroy();
}

TEST_CASE( "PMM: allocate_typed for POD types", "[pmm]" )
{
    pjson::PamManager::create( 64 * 1024 );

    // Аллоцируем int
    auto p_int = pjson::PamManager::template allocate_typed<int>();
    REQUIRE( p_int ); // pptr имеет operator bool

    // Присваиваем значение через operator*
    *p_int = 42;
    REQUIRE( *p_int == 42 );

    // Аллоцируем double
    auto p_double = pjson::PamManager::template allocate_typed<double>();
    REQUIRE( p_double );
    *p_double = 3.14159;
    REQUIRE( *p_double == 3.14159 );

    // Деаллоцируем
    pjson::PamManager::template deallocate_typed<int>( p_int );
    pjson::PamManager::template deallocate_typed<double>( p_double );

    pjson::PamManager::destroy();
}

TEST_CASE( "PMM: allocate_typed for array", "[pmm]" )
{
    pjson::PamManager::create( 64 * 1024 );

    // Аллоцируем массив из 10 int
    auto p_arr = pjson::PamManager::template allocate_typed<int>( 10 );
    REQUIRE( p_arr );

    // Получаем сырой указатель для доступа к элементам
    int* arr = p_arr.resolve();
    REQUIRE( arr != nullptr );

    // Заполняем массив
    for ( size_t i = 0; i < 10; ++i )
    {
        arr[i] = static_cast<int>( i * i );
    }

    // Проверяем значения
    for ( size_t i = 0; i < 10; ++i )
    {
        REQUIRE( arr[i] == static_cast<int>( i * i ) );
    }

    pjson::PamManager::template deallocate_typed<int>( p_arr );
    pjson::PamManager::destroy();
}

TEST_CASE( "PMM: pptr resolve and dereference", "[pmm]" )
{
    pjson::PamManager::create( 64 * 1024 );

    struct TestStruct
    {
        int    a;
        double b;
        char   c;
    };

    // Аллоцируем структуру
    auto p = pjson::PamManager::template allocate_typed<TestStruct>();
    REQUIRE( p );

    // Заполняем поля через ->
    p->a = 100;
    p->b = 2.718;
    p->c = 'X';

    // Проверяем через resolve
    TestStruct* raw = p.resolve();
    REQUIRE( raw != nullptr );
    REQUIRE( raw->a == 100 );
    REQUIRE( raw->b == 2.718 );
    REQUIRE( raw->c == 'X' );

    pjson::PamManager::template deallocate_typed<TestStruct>( p );
    pjson::PamManager::destroy();
}

TEST_CASE( "PMM: manager statistics", "[pmm]" )
{
    pjson::PamManager::create( 64 * 1024 );

    // Начальные статистики
    size_t initial_total = pjson::PamManager::total_size();
    size_t initial_used  = pjson::PamManager::used_size();

    REQUIRE( initial_total >= 64 * 1024 );

    // Аллоцируем несколько блоков
    auto p1 = pjson::PamManager::template allocate_typed<int>();
    auto p2 = pjson::PamManager::template allocate_typed<double>();
    auto p3 = pjson::PamManager::template allocate_typed<char>( 100 );

    size_t after_alloc_used = pjson::PamManager::used_size();
    REQUIRE( after_alloc_used > initial_used );

    // Освобождаем
    pjson::PamManager::template deallocate_typed<int>( p1 );
    pjson::PamManager::template deallocate_typed<double>( p2 );
    pjson::PamManager::template deallocate_typed<char>( p3 );

    pjson::PamManager::destroy();
}

TEST_CASE( "PMM: PamManager uses NoLock thread policy", "[pmm]" )
{
    // Проверяем, что PamManager использует thread_policy (exposed lock_policy)
    // CacheManagerConfig = однопоточный, NoLock
    using thread_policy = typename pjson::PamManager::thread_policy;

    // NoLock должен быть trivial и не иметь накладных расходов
    static_assert( std::is_empty_v<thread_policy> || sizeof( thread_policy ) <= sizeof( void* ),
                   "Lock policy should be lightweight for single-threaded use" );

    SUCCEED( "PamManager configured for single-threaded use" );
}

// Определяем тип pptr на уровне файла, чтобы избежать проблем с template в using
template <typename T> using pjson_pptr = typename pjson::PamManager::template pptr<T>;

TEST_CASE( "PMM: pptr<T> type alias works", "[pmm]" )
{
    pjson::PamManager::create( 64 * 1024 );

    // Используем allocate_typed напрямую
    pjson_pptr<int> p = pjson::PamManager::template allocate_typed<int>();
    REQUIRE( p );

    *p = 123;
    REQUIRE( *p == 123 );

    // Проверяем is_null
    REQUIRE_FALSE( p.is_null() );

    // Проверяем что дефолтный pptr is_null
    pjson_pptr<int> null_ptr{};
    REQUIRE( null_ptr.is_null() );

    pjson::PamManager::template deallocate_typed<int>( p );
    pjson::PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ АДАПТЕРА pptr <-> uintptr_t
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pam_adapter: pptr_to_offset for null pptr returns 0", "[pmm][adapter]" )
{
    pjson::PamManager::create( 64 * 1024 );

    // Null pptr должен конвертироваться в 0
    pjson_pptr<int> null_p{};
    REQUIRE( null_p.is_null() );

    uintptr_t off = pjson::pptr_to_offset( null_p );
    REQUIRE( off == 0 );

    pjson::PamManager::destroy();
}

TEST_CASE( "pam_adapter: offset_to_pptr for 0 returns null pptr", "[pmm][adapter]" )
{
    pjson::PamManager::create( 64 * 1024 );

    // Смещение 0 должно конвертироваться в null pptr
    auto p = pjson::offset_to_pptr<int>( 0 );
    REQUIRE( p.is_null() );

    pjson::PamManager::destroy();
}

TEST_CASE( "pam_adapter: roundtrip pptr -> offset -> pptr", "[pmm][adapter]" )
{
    pjson::PamManager::create( 64 * 1024 );

    // Аллоцируем структуру
    auto p_orig = pjson::PamManager::template allocate_typed<int>();
    REQUIRE( p_orig );
    *p_orig = 42;

    // Конвертируем в байтовое смещение
    uintptr_t off = pjson::pptr_to_offset( p_orig );
    REQUIRE( off != 0 );
    REQUIRE( pjson::is_aligned_offset( off ) );

    // Конвертируем обратно в pptr
    auto p_restored = pjson::offset_to_pptr<int>( off );
    REQUIRE_FALSE( p_restored.is_null() );

    // Проверяем что оба указывают на один и тот же объект
    REQUIRE( p_restored.offset() == p_orig.offset() );
    REQUIRE( *p_restored == 42 );

    pjson::PamManager::template deallocate_typed<int>( p_orig );
    pjson::PamManager::destroy();
}

TEST_CASE( "pam_adapter: multiple allocations have different offsets", "[pmm][adapter]" )
{
    pjson::PamManager::create( 64 * 1024 );

    // Аллоцируем несколько объектов
    auto p1 = pjson::PamManager::template allocate_typed<int>();
    auto p2 = pjson::PamManager::template allocate_typed<int>();
    auto p3 = pjson::PamManager::template allocate_typed<int>();

    *p1 = 100;
    *p2 = 200;
    *p3 = 300;

    // Конвертируем в смещения
    uintptr_t off1 = pjson::pptr_to_offset( p1 );
    uintptr_t off2 = pjson::pptr_to_offset( p2 );
    uintptr_t off3 = pjson::pptr_to_offset( p3 );

    // Все смещения должны быть разными
    REQUIRE( off1 != off2 );
    REQUIRE( off2 != off3 );
    REQUIRE( off1 != off3 );

    // Все должны быть выровнены
    REQUIRE( pjson::is_aligned_offset( off1 ) );
    REQUIRE( pjson::is_aligned_offset( off2 ) );
    REQUIRE( pjson::is_aligned_offset( off3 ) );

    // Конвертируем обратно и проверяем значения
    REQUIRE( *pjson::offset_to_pptr<int>( off1 ) == 100 );
    REQUIRE( *pjson::offset_to_pptr<int>( off2 ) == 200 );
    REQUIRE( *pjson::offset_to_pptr<int>( off3 ) == 300 );

    pjson::PamManager::template deallocate_typed<int>( p1 );
    pjson::PamManager::template deallocate_typed<int>( p2 );
    pjson::PamManager::template deallocate_typed<int>( p3 );
    pjson::PamManager::destroy();
}

TEST_CASE( "pam_adapter: offset is granule_index * granule_size", "[pmm][adapter]" )
{
    pjson::PamManager::create( 64 * 1024 );

    auto p = pjson::PamManager::template allocate_typed<double>();
    REQUIRE( p );

    // Получаем гранульный индекс напрямую
    auto granule_idx = p.offset();
    REQUIRE( granule_idx != 0 );

    // Получаем байтовое смещение через адаптер
    uintptr_t byte_off = pjson::pptr_to_offset( p );

    // Проверяем формулу: byte_off = granule_idx * granule_size
    REQUIRE( byte_off == static_cast<uintptr_t>( granule_idx ) * pjson::PMM_GRANULE_SIZE );

    pjson::PamManager::template deallocate_typed<double>( p );
    pjson::PamManager::destroy();
}

TEST_CASE( "pam_adapter: granule_size constant", "[pmm][adapter]" )
{
    // Проверяем что размер гранулы соответствует конфигурации
    // CacheManagerConfig использует DefaultAddressTraits с 16-байтовой гранулой
    REQUIRE( pjson::PMM_GRANULE_SIZE == 16 );
    REQUIRE( pjson::get_granule_size() == 16 );
}

TEST_CASE( "pam_adapter: is_aligned_offset", "[pmm][adapter]" )
{
    // 0 всегда выровнен
    REQUIRE( pjson::is_aligned_offset( 0 ) );

    // Кратные размеру гранулы выровнены
    REQUIRE( pjson::is_aligned_offset( 16 ) );
    REQUIRE( pjson::is_aligned_offset( 32 ) );
    REQUIRE( pjson::is_aligned_offset( 64 ) );
    REQUIRE( pjson::is_aligned_offset( 1024 ) );

    // Не кратные — не выровнены
    REQUIRE_FALSE( pjson::is_aligned_offset( 1 ) );
    REQUIRE_FALSE( pjson::is_aligned_offset( 15 ) );
    REQUIRE_FALSE( pjson::is_aligned_offset( 17 ) );
    REQUIRE_FALSE( pjson::is_aligned_offset( 100 ) );
}

TEST_CASE( "pam_adapter: align_offset_up", "[pmm][adapter]" )
{
    // 0 остаётся 0
    REQUIRE( pjson::align_offset_up( 0 ) == 0 );

    // Уже выровненные значения не меняются
    REQUIRE( pjson::align_offset_up( 16 ) == 16 );
    REQUIRE( pjson::align_offset_up( 32 ) == 32 );
    REQUIRE( pjson::align_offset_up( 64 ) == 64 );

    // Не выровненные округляются вверх
    REQUIRE( pjson::align_offset_up( 1 ) == 16 );
    REQUIRE( pjson::align_offset_up( 15 ) == 16 );
    REQUIRE( pjson::align_offset_up( 17 ) == 32 );
    REQUIRE( pjson::align_offset_up( 31 ) == 32 );
    REQUIRE( pjson::align_offset_up( 33 ) == 48 );
}

TEST_CASE( "pam_adapter: works with struct types", "[pmm][adapter]" )
{
    pjson::PamManager::create( 64 * 1024 );

    struct TestNode
    {
        int    tag;
        double value;
        char   name[8];
    };

    // Аллоцируем структуру
    auto p = pjson::PamManager::template allocate_typed<TestNode>();
    REQUIRE( p );

    p->tag   = 42;
    p->value = 3.14;
    std::strcpy( p->name, "test" );

    // Конвертируем в uintptr_t (как node_id)
    uintptr_t node_id = pjson::pptr_to_offset( p );
    REQUIRE( node_id != 0 );

    // Восстанавливаем из node_id
    auto p2 = pjson::offset_to_pptr<TestNode>( node_id );
    REQUIRE_FALSE( p2.is_null() );

    // Проверяем данные
    REQUIRE( p2->tag == 42 );
    REQUIRE( p2->value == 3.14 );
    REQUIRE( std::strcmp( p2->name, "test" ) == 0 );

    pjson::PamManager::template deallocate_typed<TestNode>( p );
    pjson::PamManager::destroy();
}

TEST_CASE( "pam_adapter: pmm_index_type alias", "[pmm][adapter]" )
{
    // Проверяем что pmm_index_type соответствует типу индекса из pptr
    static_assert( std::is_same_v<pjson::pmm_index_type, uint32_t>,
                   "pmm_index_type should be uint32_t for CacheManagerConfig" );

    SUCCEED( "pmm_index_type is uint32_t" );
}
