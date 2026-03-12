/**
 * @file test_pmm_basic.cpp
 * @brief Базовые тесты интеграции PersistMemoryManager (Задача 14.0).
 *
 * Тесты проверяют корректность подключения PMM и базовые операции:
 *   - Создание и уничтожение менеджера
 *   - Аллокация и деаллокация памяти
 *   - Типизированная аллокация (allocate_typed<T>)
 *   - Проверка конфигурации PamManager
 */

#include <catch2/catch_test_macros.hpp>
#include <cstring>

// Включаем конфигурацию pjson_db для PMM
#include "pam_pmm_config.h"

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ ИНТЕГРАЦИИ PMM (Задача 14.0)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "PMM: create and destroy manager", "[pmm][task14.0]" )
{
    // Создание менеджера с начальным размером 64 КБ
    pjson::PamManager::create( 64 * 1024 );

    // Проверяем, что менеджер создан
    REQUIRE( pjson::PamManager::total_size() > 0 );

    // Уничтожение менеджера
    pjson::PamManager::destroy();
}

TEST_CASE( "PMM: allocate and deallocate raw memory", "[pmm][task14.0]" )
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

TEST_CASE( "PMM: allocate_typed for POD types", "[pmm][task14.0]" )
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

TEST_CASE( "PMM: allocate_typed for array", "[pmm][task14.0]" )
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

TEST_CASE( "PMM: pptr resolve and dereference", "[pmm][task14.0]" )
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

TEST_CASE( "PMM: manager statistics", "[pmm][task14.0]" )
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

TEST_CASE( "PMM: PamManager uses NoLock thread policy", "[pmm][task14.0]" )
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

TEST_CASE( "PMM: pptr<T> type alias works", "[pmm][task14.0]" )
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
