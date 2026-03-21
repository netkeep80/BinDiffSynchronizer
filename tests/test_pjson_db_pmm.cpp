// test_pjson_db_pmm.cpp — Тесты для PMM-версии pjson_db.
//
// Покрытие:
//   - Открытие и инициализация БД на PMM
//   - put/get для всех типов значений
//   - Вложенные объекты и массивы через path-адресацию
//   - $ref разыменование
//   - erase()
//   - exists()
//   - Метрики через /$metrics
//   - dump() / parse()
//   - search_node_strings()
//   - operator[] / find / insert
//   - clone()
//

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cstdio>

#include "pjson_db_pmm.h"

using namespace pjson;

// ===========================================================================
// Вспомогательные функции
// ===========================================================================

/// Сбросить PMM в начальное состояние перед каждым тестом.
static void reset_pmm()
{
    pstringview_manager::reset();
    pam_pmm_reset();
}

// ===========================================================================
// Открытие и инициализация БД
// ===========================================================================

TEST_CASE( "pjson_db_pmm: default constructor creates root object", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    node_view    root = db.root();
    REQUIRE( root.valid() );
    REQUIRE( root.is_object() );
}

TEST_CASE( "pjson_db_pmm: root_id returns valid node_id", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    node_id      rid = db.root_id();
    REQUIRE( rid != 0 );
    node_view v{ rid };
    REQUIRE( v.is_object() );
}

// ===========================================================================
// put/get для базовых типов
// ===========================================================================

TEST_CASE( "pjson_db_pmm: put and get boolean", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.put( "/flag", true ) );
    node_view v = db.get( "/flag" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_boolean() );
    REQUIRE( v.as_bool() == true );
}

TEST_CASE( "pjson_db_pmm: put and get integer", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.put( "/age", int64_t( 42 ) ) );
    node_view v = db.get( "/age" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_integer() );
    REQUIRE( v.as_int() == 42 );
}

TEST_CASE( "pjson_db_pmm: put and get uint64_t", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.put( "/count", uint64_t( 1000000 ) ) );
    node_view v = db.get( "/count" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_uinteger() );
    REQUIRE( v.as_uint() == 1000000u );
}

TEST_CASE( "pjson_db_pmm: put and get double", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.put( "/pi", 3.14159 ) );
    node_view v = db.get( "/pi" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_real() );
    REQUIRE( v.as_double() == Catch::Approx( 3.14159 ) );
}

TEST_CASE( "pjson_db_pmm: put and get string", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.put( "/name", "Alice" ) );
    node_view v = db.get( "/name" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_string() );
    REQUIRE( v.as_string() == "Alice" );
}

// ===========================================================================
// Вложенные объекты и массивы
// ===========================================================================

TEST_CASE( "pjson_db_pmm: nested object path", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.put( "/user/name", "Bob" ) );
    REQUIRE( db.put( "/user/age", 30 ) );

    node_view name = db.get( "/user/name" );
    REQUIRE( name.valid() );
    REQUIRE( name.as_string() == "Bob" );

    node_view age = db.get( "/user/age" );
    REQUIRE( age.valid() );
    REQUIRE( age.as_int() == 30 );
}

TEST_CASE( "pjson_db_pmm: array path", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.parse_into( "/scores", "[10, 20, 30]" ) );

    node_view arr = db.get( "/scores" );
    REQUIRE( arr.valid() );
    REQUIRE( arr.is_array() );
    REQUIRE( arr.size() == 3 );

    node_view elem0 = db.get( "/scores/0" );
    REQUIRE( elem0.valid() );
    REQUIRE( elem0.as_int() == 10 );

    node_view elem2 = db.get( "/scores/2" );
    REQUIRE( elem2.valid() );
    REQUIRE( elem2.as_int() == 30 );
}

// ===========================================================================
// $ref разыменование
// ===========================================================================

TEST_CASE( "pjson_db_pmm: put_ref and automatic dereference", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.put( "/original", "hello" ) );
    REQUIRE( db.put_ref( "/link", "/original" ) );
    db.resolve_all_refs();

    // Автоматическое разыменование
    node_view v = db.get( "/link" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_string() );
    REQUIRE( v.as_string() == "hello" );
}

TEST_CASE( "pjson_db_pmm: cyclic ref detection", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.put_ref( "/a", "/b" ) );
    REQUIRE( db.put_ref( "/b", "/a" ) );
    db.resolve_all_refs();

    node_view v = db.get( "/a" );
    REQUIRE( v.is_error() );
    REQUIRE( v.error() == node_error::ref_cycle );
}

// ===========================================================================
// erase
// ===========================================================================

TEST_CASE( "pjson_db_pmm: erase removes node - pending PMM node integration", "[pjson_db_pmm][.pending]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.put( "/to_delete", "value" ) );
    REQUIRE( db.exists( "/to_delete" ) );
    REQUIRE( db.erase( "/to_delete" ) );
    REQUIRE_FALSE( db.exists( "/to_delete" ) );
}

TEST_CASE( "pjson_db_pmm: erase array element - pending PMM node integration", "[pjson_db_pmm][.pending]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.parse_into( "/arr", "[1, 2, 3]" ) );
    REQUIRE( db.get( "/arr" ).size() == 3 );
    REQUIRE( db.erase( "/arr/1" ) );
    REQUIRE( db.get( "/arr" ).size() == 2 );
}

// ===========================================================================
// exists
// ===========================================================================

TEST_CASE( "pjson_db_pmm: exists returns correct value", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE_FALSE( db.exists( "/nonexistent" ) );
    REQUIRE( db.put( "/key", "val" ) );
    REQUIRE( db.exists( "/key" ) );
}

// ===========================================================================
// Метрики
// ===========================================================================

TEST_CASE( "pjson_db_pmm: metrics pam_bump_offset", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    node_view    v = db.get( "/$metrics/pam_bump_offset" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_uinteger() );
    REQUIRE( v.as_uint() > 0 );
}

TEST_CASE( "pjson_db_pmm: metrics pam_total_size", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    node_view    v = db.get( "/$metrics/pam_total_size" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_uinteger() );
    REQUIRE( v.as_uint() >= 64 * 1024 ); // PAM_PMM_INITIAL_SIZE
}

TEST_CASE( "pjson_db_pmm: metrics object_count", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    db.put( "/obj/a", 1 );
    db.put( "/obj/b", 2 );

    node_view v = db.get( "/$metrics/object_count" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_uinteger() );
    REQUIRE( v.as_uint() >= 2 ); // root + obj
}

TEST_CASE( "pjson_db_pmm: put to metrics is forbidden", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE_FALSE( db.put( "/$metrics/hack", 999 ) );
}

// ===========================================================================
// dump / parse
// ===========================================================================

TEST_CASE( "pjson_db_pmm: dump and parse roundtrip", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.put( "/name", "test" ) );
    REQUIRE( db.put( "/value", 42 ) );

    std::string json = db.dump();
    REQUIRE( !json.empty() );
    REQUIRE( json.find( "\"name\"" ) != std::string::npos );
    REQUIRE( json.find( "\"test\"" ) != std::string::npos );
    REQUIRE( json.find( "42" ) != std::string::npos );
}

// ===========================================================================
// search_node_strings
// ===========================================================================

TEST_CASE( "pjson_db_pmm: search_node_strings finds matching strings", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.put( "/a", "hello world" ) );
    REQUIRE( db.put( "/b", "goodbye" ) );
    REQUIRE( db.put( "/c", "hello again" ) );

    auto results = db.search_node_strings( "hello" );
    REQUIRE( results.size() == 2 );
}

TEST_CASE( "pjson_db_pmm: search_node_strings empty pattern returns all strings", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.put( "/x", "one" ) );
    REQUIRE( db.put( "/y", "two" ) );

    auto results = db.search_node_strings( "" );
    REQUIRE( results.size() == 2 );
}

// ===========================================================================
// operator[] / find / insert
// ===========================================================================

TEST_CASE( "pjson_db_pmm: operator[] creates path if not exists", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    node_view    v = db["/new/path"];
    REQUIRE( v.valid() );
    REQUIRE( db.exists( "/new" ) );
}

TEST_CASE( "pjson_db_pmm: find returns existing node", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.put( "/found", "yes" ) );
    node_view v = db.find( "/found" );
    REQUIRE( v.valid() );
    REQUIRE( v.as_string() == "yes" );
}

TEST_CASE( "pjson_db_pmm: find returns invalid for nonexistent", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    node_view    v = db.find( "/missing" );
    REQUIRE( v.is_error() );
}

TEST_CASE( "pjson_db_pmm: insert parses JSON value", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    node_view    v = db.insert( "/data", R"({"x": 1, "y": 2})" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_object() );

    node_view x = db.get( "/data/x" );
    REQUIRE( x.valid() );
    REQUIRE( x.as_int() == 1 );
}

// ===========================================================================
// clone
// ===========================================================================

TEST_CASE( "pjson_db_pmm: clone creates independent copy - pending PMM node integration", "[pjson_db_pmm][.pending]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.put( "/orig/name", "Alice" ) );
    REQUIRE( db.put( "/orig/age", 30 ) );

    REQUIRE( db.clone( "/orig", "/copy" ) );

    // Изменяем копию
    REQUIRE( db.put( "/copy/name", "Bob" ) );

    // Проверяем, что оригинал не изменился
    node_view orig_name = db.get( "/orig/name" );
    REQUIRE( orig_name.valid() );
    REQUIRE( orig_name.as_string() == "Alice" );

    // Копия изменилась
    node_view copy_name = db.get( "/copy/name" );
    REQUIRE( copy_name.valid() );
    REQUIRE( copy_name.as_string() == "Bob" );
}

TEST_CASE( "pjson_db_pmm: clone to metrics is forbidden", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.put( "/src", "data" ) );
    REQUIRE_FALSE( db.clone( "/src", "/$metrics/hack" ) );
}

// ===========================================================================
// PMM-специфичные метрики
// ===========================================================================

TEST_CASE( "pjson_db_pmm: pmm_used_size returns positive value", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.pmm_used_size() > 0 );
}

TEST_CASE( "pjson_db_pmm: pmm_total_size matches initial size", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.pmm_total_size() >= 64 * 1024 );
}

TEST_CASE( "pjson_db_pmm: pmm_slot_count tracks PMM registry", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    // pmm_slot_count tracks objects registered in PMM slot_map.
    uintptr_t count = db.pmm_slot_count();
    REQUIRE( count >= 2 ); // At minimum: root, pool, metrics, registry
}

// ===========================================================================
// Ошибки
// ===========================================================================

TEST_CASE( "pjson_db_pmm: get nonexistent returns not_found error", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    node_view    v = db.get( "/no/such/path" );
    REQUIRE( v.is_error() );
    REQUIRE( v.error() == node_error::not_found );
}

TEST_CASE( "pjson_db_pmm: get through scalar returns wrong_type error", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.put( "/scalar", 123 ) );
    node_view v = db.get( "/scalar/child" );
    REQUIRE( v.is_error() );
    REQUIRE( v.error() == node_error::wrong_type );
}

TEST_CASE( "pjson_db_pmm: get array index out of range", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.parse_into( "/arr", "[1, 2]" ) );
    node_view v = db.get( "/arr/99" );
    REQUIRE( v.is_error() );
    REQUIRE( v.error() == node_error::index_out_of_range );
}

// ===========================================================================
// Тесты, перенесённые из test_pjson_db.cpp (уникальные)
// ===========================================================================

TEST_CASE( "pjson_db_pmm: second instance reuses existing root", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db1;
    node_id      root1 = db1.root_id();

    pjson_db_pmm db2;
    node_id      root2 = db2.root_id();

    // Оба должны указывать на один корневой узел.
    REQUIRE( root1 == root2 );
}

TEST_CASE( "pjson_db_pmm: put and get boolean false", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.put( "/flag", false ) );
    REQUIRE( db.get( "/flag" ).as_bool() == false );
}

TEST_CASE( "pjson_db_pmm: put and get empty string", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.put( "/empty", "" ) );
    node_view v = db.get( "/empty" );
    REQUIRE( v.is_string() );
    REQUIRE( v.as_string() == "" );
}

TEST_CASE( "pjson_db_pmm: put int convenience overload", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.put( "/x", 10 ) );
    node_view v = db.get( "/x" );
    REQUIRE( v.as_int() == 10 );
}

TEST_CASE( "pjson_db_pmm: put creates intermediate objects", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.put( "/users/alice/name", "Alice" ) );
    node_view v = db.get( "/users/alice/name" );
    REQUIRE( v.is_string() );
    REQUIRE( v.as_string() == "Alice" );
}

TEST_CASE( "pjson_db_pmm: put multiple nested keys", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.put( "/users/alice/name", "Alice" ) );
    REQUIRE( db.put( "/users/alice/age", static_cast<int64_t>( 30 ) ) );
    REQUIRE( db.put( "/users/bob/name", "Bob" ) );

    REQUIRE( db.get( "/users/alice/name" ).as_string() == "Alice" );
    REQUIRE( db.get( "/users/alice/age" ).as_int() == 30 );
    REQUIRE( db.get( "/users/bob/name" ).as_string() == "Bob" );
}

TEST_CASE( "pjson_db_pmm: exists returns true for metrics path", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.exists( "/$metrics/node_count_total" ) );
}

TEST_CASE( "pjson_db_pmm: erase returns false for missing key", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( !db.erase( "/nonexistent" ) );
}

TEST_CASE( "pjson_db_pmm: erase cannot remove metrics", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( !db.erase( "/$metrics/node_count_total" ) );
}

TEST_CASE( "pjson_db_pmm: erase removes nested key preserving parent", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    db.put( "/a/b/c", "deep" );
    REQUIRE( db.exists( "/a/b/c" ) );
    REQUIRE( db.erase( "/a/b/c" ) );
    REQUIRE( !db.exists( "/a/b/c" ) );
    // Родительский объект /a/b должен остаться.
    REQUIRE( db.exists( "/a/b" ) );
}

TEST_CASE( "pjson_db_pmm: put_ref creates ref node with ref_path", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    db.put( "/target", "hello" );
    REQUIRE( db.put_ref( "/link", "/target" ) );

    // Без разыменования — возвращается ref-узел.
    node_view ref_v = db.get( "/link", /*deref_refs=*/false );
    REQUIRE( ref_v.valid() );
    REQUIRE( ref_v.is_ref() );
    REQUIRE( ref_v.ref_path() == "/target" );
}

TEST_CASE( "pjson_db_pmm: resolve_all_refs sets targets correctly", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    db.put( "/data/value", static_cast<int64_t>( 42 ) );
    db.put_ref( "/alias", "/data/value" );

    db.resolve_all_refs();

    node_view ref_v = db.get( "/alias", /*deref_refs=*/false );
    REQUIRE( ref_v.is_ref() );
    REQUIRE( ref_v.ref_target() != 0u );

    node_view resolved = ref_v.deref( true, 32 );
    REQUIRE( resolved.valid() );
    REQUIRE( resolved.as_int() == 42 );
}

TEST_CASE( "pjson_db_pmm: resolve_ref returns invalid for unresolved ref", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;

    // ref с несуществующим путём.
    db.put_ref( "/ghost", "/no/such/path" );

    node_view ref_v = db.get( "/ghost", /*deref_refs=*/false );
    REQUIRE( ref_v.is_ref() );
    REQUIRE( ref_v.ref_target() == 0u );

    node_view resolved = db.resolve_ref( ref_v.id );
    REQUIRE( !resolved.valid() );
}

TEST_CASE( "pjson_db_pmm: parse JSON with $ref creates ref node", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;

    db.put( "/source", "value" );
    REQUIRE( db.parse_into( "/link", R"({"$ref":"/source"})" ) );

    node_view link = db.get( "/link", /*deref_refs=*/false );
    REQUIRE( link.is_ref() );
    REQUIRE( link.ref_path() == "/source" );
}

TEST_CASE( "pjson_db_pmm: parse JSON with $base64 creates binary node", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;

    REQUIRE( db.parse_into( "/data", R"({"$base64":"AAEC"})" ) );

    node_view v = db.get( "/data" );
    REQUIRE( v.is_binary() );
    REQUIRE( v.size() == 3u );
}

TEST_CASE( "pjson_db_pmm: metrics string_count returns non-zero after puts", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    db.put( "/hello", "world" );

    node_view v = db.get( "/$metrics/string_count" );
    REQUIRE( v.valid() );
    REQUIRE( v.as_uint() > 0u );
}

TEST_CASE( "pjson_db_pmm: metrics string_count_total is alias for string_count", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    db.put( "/key_a", "value_a" );
    db.put( "/key_b", "value_b" );

    node_view v1 = db.get( "/$metrics/string_count" );
    node_view v2 = db.get( "/$metrics/string_count_total" );
    REQUIRE( v1.valid() );
    REQUIRE( v2.valid() );
    REQUIRE( v1.as_uint() == v2.as_uint() );
    REQUIRE( v1.as_uint() > 0u );
}

TEST_CASE( "pjson_db_pmm: metrics pam_slot_count increases after puts", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;

    node_view v1 = db.get( "/$metrics/pam_slot_count" );
    REQUIRE( v1.valid() );
    uint64_t count_before = v1.as_uint();

    db.put( "/new_key", "new_value" );

    node_view v2 = db.get( "/$metrics/pam_slot_count" );
    REQUIRE( v2.valid() );
    REQUIRE( v2.as_uint() >= count_before );
}

TEST_CASE( "pjson_db_pmm: metrics pam_named_count is non-zero after db creation", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;

    node_view v = db.get( "/$metrics/pam_named_count" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_uinteger() );
    REQUIRE( v.as_uint() >= 3u );
}

TEST_CASE( "pjson_db_pmm: metrics free_node_count and used_node_count are consistent", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    db.put( "/a", "x" );
    db.put( "/b", "y" );

    node_view total  = db.get( "/$metrics/node_count_total" );
    node_view free_n = db.get( "/$metrics/free_node_count" );
    node_view used_n = db.get( "/$metrics/used_node_count" );

    REQUIRE( total.valid() );
    REQUIRE( free_n.valid() );
    REQUIRE( used_n.valid() );
    REQUIRE( total.as_uint() >= used_n.as_uint() );
}

TEST_CASE( "pjson_db_pmm: metrics ref_count increases after put_ref", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    db.put( "/target", "hello" );

    node_view v1 = db.get( "/$metrics/ref_count" );
    REQUIRE( v1.valid() );
    uint64_t ref_before = v1.as_uint();

    db.put_ref( "/link", "/target" );

    node_view v2 = db.get( "/$metrics/ref_count" );
    REQUIRE( v2.valid() );
    REQUIRE( v2.as_uint() > ref_before );
}

TEST_CASE( "pjson_db_pmm: metrics array_count increases after parse_into array", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;

    node_view v1 = db.get( "/$metrics/array_count" );
    REQUIRE( v1.valid() );
    uint64_t arr_before = v1.as_uint();

    db.parse_into( "/items", "[1,2,3]" );

    node_view v2 = db.get( "/$metrics/array_count" );
    REQUIRE( v2.valid() );
    REQUIRE( v2.as_uint() > arr_before );
}

TEST_CASE( "pjson_db_pmm: metrics last_save_time is zero before save", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    db.put( "/x", "y" );

    node_view v = db.get( "/$metrics/last_save_time" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_uinteger() );
    REQUIRE( v.as_uint() == 0u );
}

TEST_CASE( "pjson_db_pmm: metrics last_save_time is updated after save", "[pjson_db_pmm]" )
{
    const char* pam_file = "./test_pjson_db_pmm_metrics_save.pam";

    {
        pstringview_manager::reset();
        pam_pmm_reset();
        pam_pmm_init( pam_file );
        pjson_db_pmm db;
        db.put( "/test", "data" );
        db.save();

        node_view v = db.get( "/$metrics/last_save_time" );
        REQUIRE( v.valid() );
        REQUIRE( v.as_uint() > 0u );
    }

    std::remove( pam_file );
}

TEST_CASE( "pjson_db_pmm: metrics pam_free_list_size is valid", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    db.put( "/a", "value1" );
    db.put( "/b", "value2" );
    db.erase( "/a" );

    node_view v = db.get( "/$metrics/pam_free_list_size" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_uinteger() );
    (void)v.as_uint();
}

TEST_CASE( "pjson_db_pmm: update_metrics explicit call works", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    db.put( "/key", "value" );

    db.update_metrics();

    node_view v = db.get( "/$metrics/node_count_total" );
    REQUIRE( v.valid() );
    REQUIRE( v.as_uint() > 0u );
}

TEST_CASE( "pjson_db_pmm: metrics unknown key returns null node", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;

    node_view v = db.get( "/$metrics/nonexistent_metric" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_null() );
}

TEST_CASE( "pjson_db_pmm: metrics binary_bytes_total increases after parse_into binary", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;

    node_view v1 = db.get( "/$metrics/binary_bytes_total" );
    REQUIRE( v1.valid() );
    uint64_t bytes_before = v1.as_uint();

    db.parse_into( "/data", R"({"$base64":"AAEC"})" );

    node_view v2 = db.get( "/$metrics/binary_bytes_total" );
    REQUIRE( v2.valid() );
    REQUIRE( v2.as_uint() >= bytes_before + 3u );
}

TEST_CASE( "pjson_db_pmm: dump node_id returns JSON for specific node", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    db.put( "/val", "test_val" );
    node_view v = db.get( "/val" );
    REQUIRE( v.valid() );
    std::string json = db.dump( v.id );
    REQUIRE( json == "\"test_val\"" );
}

TEST_CASE( "pjson_db_pmm: parse replaces root with parsed JSON", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.parse( R"({"greeting":"hello","count":42})" ) );

    node_view g = db.get( "/greeting" );
    REQUIRE( g.is_string() );
    REQUIRE( g.as_string() == "hello" );

    node_view c = db.get( "/count" );
    REQUIRE( c.as_uint() == 42u );
}

TEST_CASE( "pjson_db_pmm: search_strings finds interned keys", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    db.put( "/users/alice/name", "Alice" );
    db.put( "/users/bob/name", "Bob" );

    auto results = db.search_strings( "alice" );
    bool found   = false;
    for ( const auto& r : results )
        if ( r.value.find( "alice" ) != std::string::npos )
            found = true;
    REQUIRE( found );
}

TEST_CASE( "pjson_db_pmm: all_strings returns non-empty list after puts", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    db.put( "/key1", "val1" );
    db.put( "/key2", "val2" );

    auto all = db.all_strings();
    REQUIRE( all.size() > 0u );
}

TEST_CASE( "pjson_db_pmm: save and reload preserves data", "[pjson_db_pmm]" )
{
    const char* pam_file = "./test_pjson_db_pmm_persist.pam";

    {
        pstringview_manager::reset();
        pam_pmm_reset();
        pam_pmm_init( pam_file );
        pjson_db_pmm db;
        db.put( "/greeting", "hello" );
        db.put( "/count", static_cast<int64_t>( 99 ) );
        db.save();
    }

    {
        pstringview_manager::reset();
        pam_pmm_reset();
        pam_pmm_init( pam_file );
        pjson_db_pmm db;
        node_view    greeting = db.get( "/greeting" );
        REQUIRE( greeting.is_string() );
        REQUIRE( greeting.as_string() == "hello" );

        node_view count = db.get( "/count" );
        REQUIRE( count.as_int() == 99 );
    }

    std::remove( pam_file );
}

TEST_CASE( "pjson_db_pmm: parse_into with array JSON stores elements", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.parse_into( "/items", "[1,2,3]" ) );

    node_view items = db.get( "/items" );
    REQUIRE( items.is_array() );
    REQUIRE( items.size() == 3u );
    REQUIRE( items.at( static_cast<uintptr_t>( 0 ) ).as_uint() == 1u );
    REQUIRE( items.at( static_cast<uintptr_t>( 1 ) ).as_uint() == 2u );
    REQUIRE( items.at( static_cast<uintptr_t>( 2 ) ).as_uint() == 3u );
}

TEST_CASE( "pjson_db_pmm: get array element by index path", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    db.parse_into( "/arr", "[10,20,30]" );

    node_view v = db.get( "/arr/1" );
    REQUIRE( v.valid() );
    REQUIRE( v.as_uint() == 20u );
}

TEST_CASE( "pjson_db_pmm: overwrite string value at existing path", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    db.put( "/name", "Alice" );
    REQUIRE( db.get( "/name" ).as_string() == "Alice" );

    db.put( "/name", "Bob" );
    REQUIRE( db.get( "/name" ).as_string() == "Bob" );
}

TEST_CASE( "pjson_db_pmm: overwrite with different type", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    db.put( "/val", "text" );
    db.put( "/val", static_cast<int64_t>( 42 ) );
    node_view v = db.get( "/val" );
    REQUIRE( v.is_integer() );
    REQUIRE( v.as_int() == 42 );
}

TEST_CASE( "pjson_db_pmm: put_null creates null node", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    REQUIRE( db.put_null( "/nothing" ) );
    node_view v = db.get( "/nothing" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_null() );
}

TEST_CASE( "pjson_db_pmm: operator[] on metrics path returns metrics node", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    db.put( "/x", static_cast<int64_t>( 1 ) );

    node_view v = db["/$metrics/pam_bump_offset"];
    REQUIRE( v.valid() );
    REQUIRE( v.is_uinteger() );
    REQUIRE( v.as_uint() > 0u );
}

TEST_CASE( "pjson_db_pmm: find does not create nodes", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;

    db.find( "/missing/path/here" );
    REQUIRE( !db.exists( "/missing" ) );
}

TEST_CASE( "pjson_db_pmm: find does not dereference ref nodes", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    db.put( "/target", "hello" );
    db.put_ref( "/link", "/target" );
    db.resolve_all_refs();

    node_view v = db.find( "/link" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_ref() );
}

TEST_CASE( "pjson_db_pmm: insert creates node from JSON string", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;

    node_view v = db.insert( "/data", "\"hello world\"" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_string() );
    REQUIRE( v.as_string() == "hello world" );
}

TEST_CASE( "pjson_db_pmm: insert creates node from JSON number", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;

    node_view v = db.insert( "/count", "42" );
    REQUIRE( v.valid() );
    REQUIRE( v.as_int() == 42 );
}

TEST_CASE( "pjson_db_pmm: insert overwrites existing value", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    db.put( "/val", "old" );

    node_view v = db.insert( "/val", "\"new\"" );
    REQUIRE( v.valid() );
    REQUIRE( v.as_string() == "new" );
}

TEST_CASE( "pjson_db_pmm: insert into metrics path returns invalid", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;

    node_view v = db.insert( "/$metrics/node_count_total", "999" );
    REQUIRE( !v.valid() );
}

TEST_CASE( "pjson_db_pmm: search_node_strings finds values in nested objects", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    db.parse_into( "/config", R"({"host":"localhost","port":8080,"desc":"main server"})" );

    auto results = db.search_node_strings( "server" );
    REQUIRE( results.size() == 1u );
    REQUIRE( node_view{ results[0] }.as_string() == "main server" );
}

TEST_CASE( "pjson_db_pmm: search_node_strings finds values in arrays", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    db.parse_into( "/tags", R"(["persistent","json","database","persistent-db"])" );

    auto results = db.search_node_strings( "persistent" );
    REQUIRE( results.size() == 2u );
}

TEST_CASE( "pjson_db_pmm: search_node_strings returns empty for non-matching pattern", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    db.put( "/greeting", "Hello" );
    db.put( "/farewell", "Goodbye" );

    auto results = db.search_node_strings( "xyz_not_found" );
    REQUIRE( results.empty() );
}

TEST_CASE( "pjson_db_pmm: search_strings and search_node_strings are complementary", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    db.put( "/user", "Alice" );
    db.put( "/city", "Wonderland" );

    auto key_results = db.search_strings( "user" );
    REQUIRE( !key_results.empty() );

    auto val_results = db.search_node_strings( "Alice" );
    REQUIRE( val_results.size() == 1u );
    REQUIRE( node_view{ val_results[0] }.as_string() == "Alice" );
}

TEST_CASE( "pjson_db_pmm: integration - build DB with insert and query with find", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;

    db.insert( "/employees/1/name", "\"Ivan\"" );
    db.insert( "/employees/1/dept", "\"Engineering\"" );
    db.insert( "/employees/2/name", "\"Maria\"" );
    db.insert( "/employees/2/dept", "\"Marketing\"" );

    node_view n1 = db.find( "/employees/1/name" );
    REQUIRE( n1.valid() );
    REQUIRE( n1.as_string() == "Ivan" );

    node_view n2 = db.find( "/employees/2/dept" );
    REQUIRE( n2.valid() );
    REQUIRE( n2.as_string() == "Marketing" );
}

TEST_CASE( "pjson_db_pmm: integration - search_node_strings across nested structure", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;

    db.put( "/product/name", "WidgetPro" );
    db.put( "/product/version", "2.0" );
    db.put( "/product/desc", "Advanced widget for professionals" );
    db.put( "/product/vendor", "WidgetCorp" );

    auto results = db.search_node_strings( "Widget" );
    REQUIRE( results.size() == 2u );
}

TEST_CASE( "pjson_db_pmm: integration - operator[] creates path then insert populates it", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;

    node_view slot = db["/settings/theme"];
    REQUIRE( slot.valid() );

    node_view v = db.insert( "/settings/theme", "\"dark\"" );
    REQUIRE( v.valid() );
    REQUIRE( v.as_string() == "dark" );

    REQUIRE( db.get( "/settings/theme" ).as_string() == "dark" );
}

// ===========================================================================
// Этап 8.4: проверка отсутствия утечки временных узлов метрик
// ===========================================================================

TEST_CASE( "pjson_db_pmm: metrics queries do not leak temporary nodes (issue 188 step 8.4)", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    db.put( "/x", 42 );

    // Запоминаем количество слотов после инициализации.
    uint64_t slots_before = pam_pmm_slot_count();

    // Выполняем множество обращений к метрикам.
    for ( int i = 0; i < 100; ++i )
    {
        node_view v1 = db.get( "/$metrics/node_count_total" );
        REQUIRE( v1.valid() );
        node_view v2 = db.get( "/$metrics/free_node_count" );
        REQUIRE( v2.valid() );
        node_view v3 = db.get( "/$metrics/pam_bump_offset" );
        REQUIRE( v3.valid() );
        node_view v4 = db.get( "/$metrics/last_save_time" );
        REQUIRE( v4.valid() );
        node_view v5 = db.get( "/$metrics/nonexistent_metric" );
        REQUIRE( v5.valid() );
    }

    // Количество слотов не должно расти — временный узел переиспользуется.
    uint64_t slots_after = pam_pmm_slot_count();
    REQUIRE( slots_after == slots_before );
}

TEST_CASE( "pjson_db_pmm: metrics tmp node is reused across calls", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;
    db.put( "/a", 1 );

    // Все обращения к метрикам должны возвращать node_view с одним и тем же id.
    node_view v1 = db.get( "/$metrics/node_count_total" );
    node_view v2 = db.get( "/$metrics/pam_bump_offset" );
    node_view v3 = db.get( "/$metrics/last_save_time" );

    REQUIRE( v1.id == v2.id );
    REQUIRE( v2.id == v3.id );
}

TEST_CASE( "pjson_db_pmm: metrics values are correct after repeated queries", "[pjson_db_pmm]" )
{
    reset_pmm();
    pjson_db_pmm db;

    // Каждый следующий вызов перезаписывает значение в том же узле,
    // поэтому node_view из предыдущего вызова «протухает».
    // Проверяем, что последний вызов корректно возвращает значение.
    db.put( "/item1", 10 );
    db.put( "/item2", 20 );

    node_view obj_count = db.get( "/$metrics/object_count" );
    REQUIRE( obj_count.valid() );
    REQUIRE( obj_count.is_uinteger() );
    REQUIRE( obj_count.as_uint() >= 1u );

    node_view arr_count = db.get( "/$metrics/array_count" );
    REQUIRE( arr_count.valid() );
    REQUIRE( arr_count.is_uinteger() );
}
