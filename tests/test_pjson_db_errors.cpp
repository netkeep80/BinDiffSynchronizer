// test_pjson_db_errors.cpp — Тесты для кодов ошибок node_view (Фаза 11).
//
// Покрытие:
//   - node_error enum: все коды ошибок
//   - node_view::is_error() / node_view::error()
//   - node_view_error(): фабричная функция
//   - Обратная совместимость: node_view{} (id==0) не является ошибкой
//   - pjson_db::get() возвращает типизированные ошибки:
//     - not_found при отсутствующем пути
//     - wrong_type при навигации через скалярный узел
//     - index_out_of_range при выходе индекса массива за границы
//     - ref_cycle при обнаружении цикла в $ref
//
// Все комментарии — на русском языке (Тр.6).

#include <catch2/catch_test_macros.hpp>

#include "pjson_db.h"

// ===========================================================================
// Вспомогательные функции
// ===========================================================================

/// Сбросить ПАП в начальное состояние перед каждым тестом.
static void reset_pam()
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();
}

// ===========================================================================
// Задача 11.2: node_view_error() и is_error() / error()
// ===========================================================================

TEST_CASE( "node_error: node_view{} is not an error", "[phase11][error]" )
{
    node_view v{};
    REQUIRE( v.is_error() == false );
    REQUIRE( v.error() == node_error::none );
    REQUIRE( v.is_null() == true );
    REQUIRE( v.valid() == false );
}

TEST_CASE( "node_error: node_view_error(not_found) is_error true", "[phase11][error]" )
{
    node_view v = node_view_error( node_error::not_found );
    REQUIRE( v.is_error() == true );
    REQUIRE( v.error() == node_error::not_found );
    REQUIRE( v.valid() == false );
    REQUIRE( v.is_null() == false );
}

TEST_CASE( "node_error: node_view_error returns correct code for each error", "[phase11][error]" )
{
    REQUIRE( node_view_error( node_error::none ).error() == node_error::none );
    REQUIRE( node_view_error( node_error::not_found ).error() == node_error::not_found );
    REQUIRE( node_view_error( node_error::wrong_type ).error() == node_error::wrong_type );
    REQUIRE( node_view_error( node_error::index_out_of_range ).error() == node_error::index_out_of_range );
    REQUIRE( node_view_error( node_error::readonly ).error() == node_error::readonly );
    REQUIRE( node_view_error( node_error::ref_cycle ).error() == node_error::ref_cycle );
    REQUIRE( node_view_error( node_error::parse_error ).error() == node_error::parse_error );
}

TEST_CASE( "node_error: all error views have is_error true", "[phase11][error]" )
{
    // node_error::none кодируется как NODE_ERROR_BASE + 0, что >= NODE_ERROR_BASE — ошибка.
    // Все значения node_error должны давать is_error() == true через node_view_error().
    REQUIRE( node_view_error( node_error::not_found ).is_error() );
    REQUIRE( node_view_error( node_error::wrong_type ).is_error() );
    REQUIRE( node_view_error( node_error::index_out_of_range ).is_error() );
    REQUIRE( node_view_error( node_error::readonly ).is_error() );
    REQUIRE( node_view_error( node_error::ref_cycle ).is_error() );
    REQUIRE( node_view_error( node_error::parse_error ).is_error() );
}

TEST_CASE( "node_error: valid node_view is not an error", "[phase11][error]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/x", int64_t( 42 ) );
    node_view v = db.get( "/x" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_error() == false );
    REQUIRE( v.error() == node_error::none );
}

// ===========================================================================
// Задача 11.3: pjson_db::get() с типизированными ошибками
// ===========================================================================

TEST_CASE( "node_error: get nonexistent path returns not_found", "[phase11][error]" )
{
    reset_pam();
    pjson_db  db;
    node_view v = db.get( "/nonexistent/path" );
    REQUIRE( v.is_error() );
    REQUIRE( v.error() == node_error::not_found );
}

TEST_CASE( "node_error: get through scalar returns wrong_type", "[phase11][error]" )
{
    reset_pam();
    pjson_db db;
    // Записываем скалярное значение по пути /flag.
    REQUIRE( db.put( "/flag", true ) );
    // Попытка перейти в /flag/sub через булевый узел — неверный тип.
    node_view v = db.get( "/flag/sub" );
    REQUIRE( v.is_error() );
    REQUIRE( v.error() == node_error::wrong_type );
}

TEST_CASE( "node_error: get array index out of range returns index_out_of_range", "[phase11][error]" )
{
    reset_pam();
    pjson_db db;
    // Создаём массив из одного элемента.
    db.parse_into( "/arr", "[10]" );
    // Запрашиваем несуществующий индекс 99.
    node_view v = db.get( "/arr/99" );
    REQUIRE( v.is_error() );
    REQUIRE( v.error() == node_error::index_out_of_range );
}

TEST_CASE( "node_error: get ref cycle returns ref_cycle", "[phase11][error]" )
{
    reset_pam();
    pjson_db db;
    // Создаём два ref-узла, образующих цикл: a -> b -> a.
    db.put_ref( "/cycleA", "/cycleB" );
    db.put_ref( "/cycleB", "/cycleA" );
    db.resolve_all_refs();
    // Попытка получить /cycleA с разыменованием должна вернуть ref_cycle.
    node_view v = db.get( "/cycleA" );
    REQUIRE( v.is_error() );
    REQUIRE( v.error() == node_error::ref_cycle );
}

TEST_CASE( "node_error: get existing path returns valid node without error", "[phase11][error]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/config/host", "localhost" );
    node_view v = db.get( "/config/host" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_error() == false );
    REQUIRE( v.is_string() );
    REQUIRE( v.as_string() == "localhost" );
}
