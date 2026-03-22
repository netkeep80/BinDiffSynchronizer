#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>

#include "pjson_db_pmm.h"

using namespace pjson;

// Вспомогательная функция: сбросить PMM перед каждым тестом.
namespace
{
void reset_pam()
{
    pstringview_manager::reset();
    pam_pmm_reset();
}
} // anonymous namespace

// =============================================================================
// Этап 10.4: const-корректность _walk_path — тесты
// =============================================================================

// ---------------------------------------------------------------------------
// const get(): read-only обход пути работает на const-ссылке
// ---------------------------------------------------------------------------
TEST_CASE( "const_correctness: get works on const db reference", "[pjson_db_pmm][const]" )
{
    reset_pam();
    auto db = pjson_db_pmm::open( "" );
    db.put( "/a/b", 42 );

    const pjson_db_pmm& cdb = db;
    node_view           v   = cdb.get( "/a/b" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_integer() );
    REQUIRE( v.as_int() == 42 );
}

TEST_CASE( "const_correctness: get returns error on const db for missing path", "[pjson_db_pmm][const]" )
{
    reset_pam();
    auto db = pjson_db_pmm::open( "" );
    db.put( "/x", 1 );

    const pjson_db_pmm& cdb = db;
    node_view           v   = cdb.get( "/nonexistent" );
    REQUIRE( v.is_error() );
    REQUIRE( v.error() == node_error::not_found );
}

// ---------------------------------------------------------------------------
// const exists(): работает на const-ссылке
// ---------------------------------------------------------------------------
TEST_CASE( "const_correctness: exists works on const db reference", "[pjson_db_pmm][const]" )
{
    reset_pam();
    auto db = pjson_db_pmm::open( "" );
    db.put( "/foo/bar", "hello" );

    const pjson_db_pmm& cdb = db;
    REQUIRE( cdb.exists( "/foo/bar" ) );
    REQUIRE_FALSE( cdb.exists( "/foo/missing" ) );
}

// ---------------------------------------------------------------------------
// const find(): работает на const-ссылке
// ---------------------------------------------------------------------------
TEST_CASE( "const_correctness: find works on const db reference", "[pjson_db_pmm][const]" )
{
    reset_pam();
    auto db = pjson_db_pmm::open( "" );
    db.put( "/k", 99 );

    const pjson_db_pmm& cdb = db;
    node_view           v   = cdb.find( "/k" );
    REQUIRE( v.valid() );
    REQUIRE( v.as_int() == 99 );
}

// ---------------------------------------------------------------------------
// const root() / root_id(): работают на const-ссылке
// ---------------------------------------------------------------------------
TEST_CASE( "const_correctness: root and root_id work on const db reference", "[pjson_db_pmm][const]" )
{
    reset_pam();
    auto db = pjson_db_pmm::open( "" );
    db.put( "/v", 1 );

    const pjson_db_pmm& cdb = db;
    REQUIRE( cdb.root_id() != 0 );
    REQUIRE( cdb.root().valid() );
}

// ---------------------------------------------------------------------------
// const metrics(): работает на const-ссылке
// ---------------------------------------------------------------------------
TEST_CASE( "const_correctness: metrics works on const db reference", "[pjson_db_pmm][const]" )
{
    reset_pam();
    auto db = pjson_db_pmm::open( "" );
    db.put( "/m", 1 );

    const pjson_db_pmm& cdb = db;
    node_view           nc  = cdb.get( "/$metrics/node_count_total" );
    REQUIRE( nc.valid() );
    REQUIRE( nc.is_uinteger() );
    REQUIRE( nc.as_uint() > 0 );
}

// ---------------------------------------------------------------------------
// non-const put: мутирующие методы работают на non-const ссылке
// ---------------------------------------------------------------------------
TEST_CASE( "const_correctness: put creates intermediate nodes correctly", "[pjson_db_pmm][const]" )
{
    reset_pam();
    auto db = pjson_db_pmm::open( "" );

    REQUIRE( db.put( "/deep/nested/path", 123 ) );
    node_view v = db.get( "/deep/nested/path" );
    REQUIRE( v.valid() );
    REQUIRE( v.as_int() == 123 );
}

// ---------------------------------------------------------------------------
// _walk_path_read: навигация по массивам
// ---------------------------------------------------------------------------
TEST_CASE( "const_correctness: const get navigates arrays correctly", "[pjson_db_pmm][const]" )
{
    reset_pam();
    auto db = pjson_db_pmm::open( "" );
    db.put( "/arr/0", 10 );
    db.put( "/arr/1", 20 );
    db.put( "/arr/2", 30 );

    const pjson_db_pmm& cdb = db;
    REQUIRE( cdb.get( "/arr/0" ).as_int() == 10 );
    REQUIRE( cdb.get( "/arr/1" ).as_int() == 20 );
    REQUIRE( cdb.get( "/arr/2" ).as_int() == 30 );

    // Out-of-range index
    node_view oob = cdb.get( "/arr/5" );
    REQUIRE( oob.is_error() );
    REQUIRE( oob.error() == node_error::index_out_of_range );
}

// ---------------------------------------------------------------------------
// _walk_path_read: ошибка wrong_type при навигации через скалярный узел
// ---------------------------------------------------------------------------
TEST_CASE( "const_correctness: const get returns wrong_type for scalar traversal", "[pjson_db_pmm][const]" )
{
    reset_pam();
    auto db = pjson_db_pmm::open( "" );
    db.put( "/scalar", 42 );

    const pjson_db_pmm& cdb = db;
    node_view           v   = cdb.get( "/scalar/child" );
    REQUIRE( v.is_error() );
    REQUIRE( v.error() == node_error::wrong_type );
}

// ---------------------------------------------------------------------------
// Разделение read/create: create создаёт, read не создаёт
// ---------------------------------------------------------------------------
TEST_CASE( "const_correctness: get does not create intermediate nodes", "[pjson_db_pmm][const]" )
{
    reset_pam();
    auto db = pjson_db_pmm::open( "" );

    // Чтение несуществующего пути не должно создавать узлов
    const pjson_db_pmm& cdb = db;
    node_view           v   = cdb.get( "/should/not/exist" );
    REQUIRE( v.is_error() );

    // Проверяем, что путь действительно не создан
    REQUIRE_FALSE( cdb.exists( "/should" ) );
}
