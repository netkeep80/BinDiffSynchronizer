// test_pjson_rfc6901.cpp — Тесты для поддержки RFC 6901 (JSON Pointer) в путях.
//
// Покрытие:
//   - Декодирование сегментов: ~1 → /, ~0 → ~
//   - put/get с ключами, содержащими '/' и '~'
//   - erase с RFC 6901 путями
//   - exists с RFC 6901 путями
//   - clone с RFC 6901 путями
//   - pjson_split_path с RFC 6901 декодированием
//   - parse_into с RFC 6901 путями
//

#include <catch2/catch_test_macros.hpp>
#include <cstdio>

#include "pjson_db_pmm.h"

using namespace pjson;

// ===========================================================================
// Вспомогательные функции
// ===========================================================================

static void reset_pmm()
{
    pstringview_manager::reset();
    pam_pmm_reset();
}

// ===========================================================================
// Декодирование сегментов: pjson_decode_rfc6901_segment
// ===========================================================================

TEST_CASE( "rfc6901: decode segment - plain text unchanged", "[rfc6901]" )
{
    REQUIRE( pjson_decode_rfc6901_segment( "hello" ) == "hello" );
}

TEST_CASE( "rfc6901: decode segment - ~1 becomes /", "[rfc6901]" )
{
    REQUIRE( pjson_decode_rfc6901_segment( "a~1b" ) == "a/b" );
}

TEST_CASE( "rfc6901: decode segment - ~0 becomes ~", "[rfc6901]" )
{
    REQUIRE( pjson_decode_rfc6901_segment( "a~0b" ) == "a~b" );
}

TEST_CASE( "rfc6901: decode segment - multiple escapes", "[rfc6901]" )
{
    REQUIRE( pjson_decode_rfc6901_segment( "a~1b~0c~1d" ) == "a/b~c/d" );
}

TEST_CASE( "rfc6901: decode segment - ~0 before ~1 decodes correctly", "[rfc6901]" )
{
    // ~01 → ~1 (not /), т.к. ~0 декодируется в ~, а 1 остаётся.
    REQUIRE( pjson_decode_rfc6901_segment( "~01" ) == "~1" );
}

TEST_CASE( "rfc6901: decode segment - trailing ~ unchanged", "[rfc6901]" )
{
    REQUIRE( pjson_decode_rfc6901_segment( "abc~" ) == "abc~" );
}

TEST_CASE( "rfc6901: decode segment - empty string", "[rfc6901]" )
{
    REQUIRE( pjson_decode_rfc6901_segment( "" ) == "" );
}

TEST_CASE( "rfc6901: decode segment - ~2 unknown escape stays as-is", "[rfc6901]" )
{
    REQUIRE( pjson_decode_rfc6901_segment( "a~2b" ) == "a~2b" );
}

// ===========================================================================
// put/get с ключами, содержащими '/'
// ===========================================================================

TEST_CASE( "rfc6901: put/get key with slash via ~1 escaping", "[rfc6901][pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;

    // Ключ "a/b" экранируется как "a~1b" в пути
    REQUIRE( db.put( "/config/a~1b", int64_t( 42 ) ) );

    node_view v = db.get( "/config/a~1b" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_integer() );
    REQUIRE( v.as_int() == 42 );

    // Проверяем, что ключ в объекте действительно содержит '/'
    node_view config = db.get( "/config" );
    REQUIRE( config.valid() );
    REQUIRE( config.is_object() );

    node_view child = config.at( "a/b" );
    REQUIRE( child.valid() );
    REQUIRE( child.as_int() == 42 );
}

TEST_CASE( "rfc6901: put/get key with tilde via ~0 escaping", "[rfc6901][pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;

    // Ключ "a~b" экранируется как "a~0b" в пути
    REQUIRE( db.put( "/data/a~0b", int64_t( 7 ) ) );

    node_view v = db.get( "/data/a~0b" );
    REQUIRE( v.valid() );
    REQUIRE( v.as_int() == 7 );

    // Проверяем, что ключ в объекте действительно содержит '~'
    node_view data  = db.get( "/data" );
    node_view child = data.at( "a~b" );
    REQUIRE( child.valid() );
    REQUIRE( child.as_int() == 7 );
}

TEST_CASE( "rfc6901: put/get key with both slash and tilde", "[rfc6901][pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;

    // Ключ "x/y~z" экранируется как "x~1y~0z"
    REQUIRE( db.put( "/obj/x~1y~0z", "hello" ) );

    node_view v = db.get( "/obj/x~1y~0z" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_string() );
    REQUIRE( v.as_string() == "hello" );

    node_view obj   = db.get( "/obj" );
    node_view child = obj.at( "x/y~z" );
    REQUIRE( child.valid() );
    REQUIRE( child.as_string() == "hello" );
}

// ===========================================================================
// exists с RFC 6901 путями
// ===========================================================================

TEST_CASE( "rfc6901: exists with escaped slash key", "[rfc6901][pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.put( "/items/a~1b", true ) );
    REQUIRE( db.exists( "/items/a~1b" ) );
    REQUIRE_FALSE( db.exists( "/items/a~1c" ) );
}

// ===========================================================================
// erase с RFC 6901 путями
// ===========================================================================

TEST_CASE( "rfc6901: erase key with escaped slash", "[rfc6901][pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;

    REQUIRE( db.put( "/items/a~1b", int64_t( 1 ) ) );
    REQUIRE( db.put( "/items/normal", int64_t( 2 ) ) );

    REQUIRE( db.exists( "/items/a~1b" ) );
    REQUIRE( db.erase( "/items/a~1b" ) );
    REQUIRE_FALSE( db.exists( "/items/a~1b" ) );

    // Другой ключ не затронут
    REQUIRE( db.exists( "/items/normal" ) );
}

// ===========================================================================
// clone с RFC 6901 путями
// ===========================================================================

TEST_CASE( "rfc6901: clone with escaped slash in source and destination", "[rfc6901][pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;

    REQUIRE( db.put( "/src/a~1b", int64_t( 99 ) ) );
    REQUIRE( db.clone( "/src/a~1b", "/dst/c~1d" ) );

    node_view v = db.get( "/dst/c~1d" );
    REQUIRE( v.valid() );
    REQUIRE( v.as_int() == 99 );

    // Проверяем, что ключ назначения содержит '/'
    node_view dst   = db.get( "/dst" );
    node_view child = dst.at( "c/d" );
    REQUIRE( child.valid() );
    REQUIRE( child.as_int() == 99 );
}

// ===========================================================================
// parse_into с RFC 6901 путями
// ===========================================================================

TEST_CASE( "rfc6901: parse_into with escaped path", "[rfc6901][pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;

    REQUIRE( db.parse_into( "/conf/a~1b", R"({"x":1})" ) );
    node_view v = db.get( "/conf/a~1b" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_object() );

    // Навигация внутрь: /conf/a~1b/x → вложенный ключ "x" внутри объекта с ключом "a/b"
    node_view x = db.get( "/conf/a~1b/x" );
    REQUIRE( x.valid() );
    REQUIRE( x.as_int() == 1 );
}

// ===========================================================================
// pjson_split_path с RFC 6901 декодированием
// ===========================================================================

TEST_CASE( "rfc6901: pjson_split_path decodes last segment", "[rfc6901]" )
{
    std::string parent, last;

    pjson_split_path( "/foo/a~1b", parent, last );
    REQUIRE( parent == "/foo" );
    REQUIRE( last == "a/b" );

    pjson_split_path( "/x~0y", parent, last );
    REQUIRE( parent == "/" );
    REQUIRE( last == "x~y" );
}

// ===========================================================================
// Пути без escaping по-прежнему работают
// ===========================================================================

TEST_CASE( "rfc6901: plain paths without escaping still work", "[rfc6901][pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;

    REQUIRE( db.put( "/a/b/c", int64_t( 123 ) ) );
    node_view v = db.get( "/a/b/c" );
    REQUIRE( v.valid() );
    REQUIRE( v.as_int() == 123 );
}
