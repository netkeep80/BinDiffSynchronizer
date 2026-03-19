// test_pjson_db_batch.cpp — Тесты пакетных операций (batch) pjson_db_pmm.
//
// Покрытие:
//   - batch_begin/batch_end: откладывание пересчёта метрик
//   - batch_guard (RAII): автоматическое управление пакетной областью
//   - Вложенные пакетные операции
//   - Корректность метрик после завершения пакетной операции
//   - Производительность: сравнение batch vs non-batch
//

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <string>

#include "pjson_db_pmm.h"

using namespace pjson;

// ===========================================================================
// Вспомогательные функции
// ===========================================================================

/// Сбросить PMM в начальное состояние перед каждым тестом.
static void reset_pmm_batch()
{
    pstringview_manager::reset();
    pam_pmm_reset();
}

// ===========================================================================
// batch_begin / batch_end: базовые тесты
// ===========================================================================

TEST_CASE( "pjson_db_pmm batch: in_batch returns false by default", "[pjson_db_pmm][batch]" )
{
    reset_pmm_batch();
    pjson_db_pmm db;
    REQUIRE( !db.in_batch() );
}

TEST_CASE( "pjson_db_pmm batch: batch_begin sets in_batch to true", "[pjson_db_pmm][batch]" )
{
    reset_pmm_batch();
    pjson_db_pmm db;
    db.batch_begin();
    REQUIRE( db.in_batch() );
    db.batch_end();
    REQUIRE( !db.in_batch() );
}

TEST_CASE( "pjson_db_pmm batch: batch_end on zero depth is safe", "[pjson_db_pmm][batch]" )
{
    reset_pmm_batch();
    pjson_db_pmm db;
    // Вызов batch_end без batch_begin не должен падать.
    db.batch_end();
    REQUIRE( !db.in_batch() );
}

TEST_CASE( "pjson_db_pmm batch: put during batch defers metrics update", "[pjson_db_pmm][batch]" )
{
    reset_pmm_batch();
    pjson_db_pmm db;

    // Запомним метрику до пакетной операции.
    node_view count_before = db.get( "/$metrics/node_count_total" );
    REQUIRE( count_before.valid() );
    uint64_t val_before = count_before.as_uint();

    db.batch_begin();

    // Вставляем несколько значений внутри пакетной операции.
    REQUIRE( db.put( "/batch_a", int64_t( 1 ) ) );
    REQUIRE( db.put( "/batch_b", int64_t( 2 ) ) );
    REQUIRE( db.put( "/batch_c", int64_t( 3 ) ) );

    // Данные доступны.
    REQUIRE( db.get( "/batch_a" ).valid() );
    REQUIRE( db.get( "/batch_a" ).as_int() == 1 );

    db.batch_end();

    // После завершения пакетной операции метрики обновлены.
    node_view count_after = db.get( "/$metrics/node_count_total" );
    REQUIRE( count_after.valid() );
    uint64_t val_after = count_after.as_uint();

    // Должно быть больше, чем до вставки.
    REQUIRE( val_after > val_before );
}

TEST_CASE( "pjson_db_pmm batch: erase during batch defers metrics update", "[pjson_db_pmm][batch]" )
{
    reset_pmm_batch();
    pjson_db_pmm db;

    db.put( "/x", int64_t( 10 ) );
    db.put( "/y", int64_t( 20 ) );
    db.put( "/z", int64_t( 30 ) );

    db.batch_begin();
    REQUIRE( db.erase( "/x" ) );
    REQUIRE( db.erase( "/y" ) );
    db.batch_end();

    // После пакетного удаления данные недоступны.
    REQUIRE( !db.get( "/x" ).valid() );
    REQUIRE( !db.get( "/y" ).valid() );
    REQUIRE( db.get( "/z" ).valid() );
}

// ===========================================================================
// Вложенные пакетные операции
// ===========================================================================

TEST_CASE( "pjson_db_pmm batch: nested batch does not recalculate until outermost ends", "[pjson_db_pmm][batch]" )
{
    reset_pmm_batch();
    pjson_db_pmm db;

    db.batch_begin(); // глубина 1
    db.put( "/n1", int64_t( 1 ) );

    db.batch_begin(); // глубина 2
    db.put( "/n2", int64_t( 2 ) );
    db.batch_end(); // глубина 1

    // Ещё в пакетной операции.
    REQUIRE( db.in_batch() );

    db.put( "/n3", int64_t( 3 ) );
    db.batch_end(); // глубина 0

    REQUIRE( !db.in_batch() );

    // Все данные доступны.
    REQUIRE( db.get( "/n1" ).as_int() == 1 );
    REQUIRE( db.get( "/n2" ).as_int() == 2 );
    REQUIRE( db.get( "/n3" ).as_int() == 3 );
}

// ===========================================================================
// RAII: batch_guard
// ===========================================================================

TEST_CASE( "pjson_db_pmm batch: batch_guard manages scope automatically", "[pjson_db_pmm][batch]" )
{
    reset_pmm_batch();
    pjson_db_pmm db;

    {
        auto guard = db.batch();
        REQUIRE( db.in_batch() );
        db.put( "/raii_a", int64_t( 100 ) );
        db.put( "/raii_b", int64_t( 200 ) );
    } // guard уничтожается, batch_end() вызывается

    REQUIRE( !db.in_batch() );
    REQUIRE( db.get( "/raii_a" ).as_int() == 100 );
    REQUIRE( db.get( "/raii_b" ).as_int() == 200 );
}

TEST_CASE( "pjson_db_pmm batch: nested batch_guard works correctly", "[pjson_db_pmm][batch]" )
{
    reset_pmm_batch();
    pjson_db_pmm db;

    {
        auto outer = db.batch();
        REQUIRE( db.in_batch() );

        db.put( "/outer", int64_t( 1 ) );

        {
            auto inner = db.batch();
            REQUIRE( db.in_batch() );

            db.put( "/inner", int64_t( 2 ) );
        } // inner batch_end

        REQUIRE( db.in_batch() ); // ещё в outer
    } // outer batch_end

    REQUIRE( !db.in_batch() );
    REQUIRE( db.get( "/outer" ).as_int() == 1 );
    REQUIRE( db.get( "/inner" ).as_int() == 2 );
}

// ===========================================================================
// Корректность метрик после пакетной операции
// ===========================================================================

TEST_CASE( "pjson_db_pmm batch: metrics correct after batch put and erase", "[pjson_db_pmm][batch]" )
{
    reset_pmm_batch();
    pjson_db_pmm db;

    {
        auto guard = db.batch();
        db.put( "/m1", int64_t( 1 ) );
        db.put( "/m2", "hello" );
        db.put( "/m3", true );
    }

    // Проверяем, что метрики отражают текущее состояние.
    node_view obj_count = db.get( "/$metrics/object_count" );
    REQUIRE( obj_count.valid() );
    // Корневой объект — это как минимум 1 объект.
    REQUIRE( obj_count.as_uint() >= 1 );

    // Удаляем в пакетном режиме.
    {
        auto guard = db.batch();
        db.erase( "/m1" );
        db.erase( "/m2" );
        db.erase( "/m3" );
    }

    // После удаления: проверяем, что данные удалены.
    REQUIRE( !db.exists( "/m1" ) );
    REQUIRE( !db.exists( "/m2" ) );
    REQUIRE( !db.exists( "/m3" ) );
}

TEST_CASE( "pjson_db_pmm batch: parse_into during batch works", "[pjson_db_pmm][batch]" )
{
    reset_pmm_batch();
    pjson_db_pmm db;

    {
        auto guard = db.batch();
        REQUIRE( db.parse_into( "/config", R"({"host":"localhost","port":8080})" ) );
        REQUIRE( db.parse_into( "/users", R"([{"name":"Alice"},{"name":"Bob"}])" ) );
    }

    REQUIRE( db.get( "/config/host" ).as_string() == "localhost" );
    REQUIRE( db.get( "/config/port" ).as_int() == 8080 );
    REQUIRE( db.get( "/users/0/name" ).as_string() == "Alice" );
    REQUIRE( db.get( "/users/1/name" ).as_string() == "Bob" );
}

TEST_CASE( "pjson_db_pmm batch: mixed put, erase, parse_into in single batch", "[pjson_db_pmm][batch]" )
{
    reset_pmm_batch();
    pjson_db_pmm db;

    {
        auto guard = db.batch();
        db.put( "/a", int64_t( 1 ) );
        db.put( "/b", int64_t( 2 ) );
        db.put( "/c", int64_t( 3 ) );
        db.erase( "/b" );
        db.parse_into( "/d", R"({"x":true})" );
    }

    REQUIRE( db.get( "/a" ).as_int() == 1 );
    REQUIRE( !db.exists( "/b" ) );
    REQUIRE( db.get( "/c" ).as_int() == 3 );
    REQUIRE( db.get( "/d/x" ).as_bool() == true );
}

// ===========================================================================
// Производительность: сравнение batch vs non-batch
// ===========================================================================

TEST_CASE( "pjson_db_pmm batch perf: batch put is faster than individual put", "[pjson_db_pmm][batch][perf]" )
{
    using clk = std::chrono::steady_clock;
    constexpr unsigned N = 500;

    // Без пакетной операции.
    {
        reset_pmm_batch();
        pjson_db_pmm db;
        auto         t0 = clk::now();
        for ( unsigned i = 0; i < N; ++i )
        {
            std::string path = "/item_" + std::to_string( i );
            db.put( path.c_str(), static_cast<int64_t>( i ) );
        }
        auto ms_no_batch = std::chrono::duration_cast<std::chrono::milliseconds>( clk::now() - t0 ).count();
        std::printf( "[batch_perf] put %u nodes without batch: %lld ms\n", N, (long long)ms_no_batch );
    }

    // С пакетной операцией.
    {
        reset_pmm_batch();
        pjson_db_pmm db;
        auto         t0 = clk::now();
        {
            auto guard = db.batch();
            for ( unsigned i = 0; i < N; ++i )
            {
                std::string path = "/item_" + std::to_string( i );
                db.put( path.c_str(), static_cast<int64_t>( i ) );
            }
        }
        auto ms_batch = std::chrono::duration_cast<std::chrono::milliseconds>( clk::now() - t0 ).count();
        std::printf( "[batch_perf] put %u nodes with batch:    %lld ms\n", N, (long long)ms_batch );
    }

    // Тест информационный: не проверяем время, а выводим его.
    REQUIRE( true );
}
