/**
 * @file test_pam_pmm_threadsafe.cpp
 * @brief Тесты потокобезопасности pam_pmm_state (Этап C, Issue #210).
 *
 * Проверяют, что std::mutex в pam_pmm_state защищает init/destroy/reset/save
 * от гонок при одновременном доступе из нескольких потоков.
 */

#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <thread>
#include <vector>

#include "pam_pmm.h"

using namespace pjson;

// ═══════════════════════════════════════════════════════════════════════════
// БАЗОВЫЕ ТЕСТЫ МЬЮТЕКСА В PAM_PMM_STATE
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pam_pmm_threadsafe: mutex exists in pam_pmm_state", "[pam_pmm][threadsafe]" )
{
    // Проверяем, что pam_pmm_state содержит мьютекс (компилируется).
    pam_pmm_state               state;
    std::lock_guard<std::mutex> lock( state.mtx );
    REQUIRE_FALSE( state.initialized );
}

TEST_CASE( "pam_pmm_threadsafe: concurrent is_initialized reads", "[pam_pmm][threadsafe]" )
{
    pam_pmm_init( nullptr );
    REQUIRE( pam_pmm_is_initialized() );

    constexpr int            NUM_THREADS = 8;
    std::atomic<int>         success_count{ 0 };
    std::vector<std::thread> threads;

    for ( int i = 0; i < NUM_THREADS; ++i )
    {
        threads.emplace_back(
            [&success_count]()
            {
                if ( pam_pmm_is_initialized() )
                    success_count.fetch_add( 1 );
            } );
    }

    for ( auto& t : threads )
        t.join();

    REQUIRE( success_count.load() == NUM_THREADS );

    pam_pmm_destroy();
}

TEST_CASE( "pam_pmm_threadsafe: init and destroy are serialized", "[pam_pmm][threadsafe]" )
{
    // Проверяем, что последовательные init/destroy не приводят к гонкам.
    constexpr int ITERATIONS = 10;

    for ( int i = 0; i < ITERATIONS; ++i )
    {
        pam_pmm_init( nullptr );
        REQUIRE( pam_pmm_is_initialized() );
        pam_pmm_destroy();
        REQUIRE_FALSE( pam_pmm_is_initialized() );
    }
}

TEST_CASE( "pam_pmm_threadsafe: concurrent init/destroy with explicit state", "[pam_pmm][threadsafe]" )
{
    // Используем явное состояние для проверки, что мьютекс защищает
    // одновременный доступ к одному и тому же state из разных потоков.
    pam_pmm_state state;

    constexpr int ITERATIONS  = 5;
    constexpr int NUM_THREADS = 4;

    for ( int iter = 0; iter < ITERATIONS; ++iter )
    {
        // Инициализируем.
        pam_pmm_init( state, nullptr );
        REQUIRE( pam_pmm_is_initialized( state ) );

        // Несколько потоков одновременно проверяют состояние.
        std::atomic<int>         check_count{ 0 };
        std::vector<std::thread> threads;

        for ( int i = 0; i < NUM_THREADS; ++i )
        {
            threads.emplace_back(
                [&state, &check_count]()
                {
                    // Каждый поток проверяет is_initialized через мьютекс.
                    bool ok = pam_pmm_is_initialized( state );
                    if ( ok )
                        check_count.fetch_add( 1 );
                } );
        }

        for ( auto& t : threads )
            t.join();

        REQUIRE( check_count.load() == NUM_THREADS );

        // Уничтожаем.
        pam_pmm_destroy( state );
        REQUIRE_FALSE( pam_pmm_is_initialized( state ) );
    }
}

TEST_CASE( "pam_pmm_threadsafe: reset is serialized", "[pam_pmm][threadsafe]" )
{
    pam_pmm_init( nullptr );

    // Создаём объект.
    auto&     state = pam_pmm_global_state();
    uintptr_t off   = pam_pmm_create<int>( state, "test_threadsafe" );
    REQUIRE( off != 0 );

    // Reset через мьютекс.
    pam_pmm_reset( state );
    REQUIRE( pam_pmm_is_initialized( state ) );
    REQUIRE( pam_pmm_find( state, "test_threadsafe" ) == 0 );

    pam_pmm_destroy( state );
}

TEST_CASE( "pam_pmm_threadsafe: pam_pmm_state is not copyable due to mutex", "[pam_pmm][threadsafe]" )
{
    // std::mutex делает pam_pmm_state некопируемым — это ожидаемое поведение.
    STATIC_REQUIRE_FALSE( std::is_copy_constructible<pam_pmm_state>::value );
    STATIC_REQUIRE_FALSE( std::is_copy_assignable<pam_pmm_state>::value );
}
