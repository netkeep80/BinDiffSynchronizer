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
// Этап 10.3: Оптимизация tag-проверок — тесты корректности
// =============================================================================

// ---------------------------------------------------------------------------
// is_number: единственный resolve вместо трёх
// ---------------------------------------------------------------------------
TEST_CASE( "tag_opt: is_number returns true for integer", "[pjson_node][tag_opt]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_int( fn.addr(), 42 );
    node_view v{ fn.addr() };
    REQUIRE( v.is_number() );
    REQUIRE( v.is_integer() );
    REQUIRE_FALSE( v.is_uinteger() );
    REQUIRE_FALSE( v.is_real() );
    fn.Delete();
}

TEST_CASE( "tag_opt: is_number returns true for uinteger", "[pjson_node][tag_opt]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_uint( fn.addr(), 99u );
    node_view v{ fn.addr() };
    REQUIRE( v.is_number() );
    REQUIRE_FALSE( v.is_integer() );
    REQUIRE( v.is_uinteger() );
    REQUIRE_FALSE( v.is_real() );
    fn.Delete();
}

TEST_CASE( "tag_opt: is_number returns true for real", "[pjson_node][tag_opt]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_real( fn.addr(), 3.14 );
    node_view v{ fn.addr() };
    REQUIRE( v.is_number() );
    REQUIRE_FALSE( v.is_integer() );
    REQUIRE_FALSE( v.is_uinteger() );
    REQUIRE( v.is_real() );
    fn.Delete();
}

TEST_CASE( "tag_opt: is_number returns false for non-numeric types", "[pjson_node][tag_opt]" )
{
    reset_pam();

    // null
    {
        node_view v{};
        REQUIRE_FALSE( v.is_number() );
    }

    // boolean
    {
        fptr<node> fn;
        fn.New();
        node_set_bool( fn.addr(), true );
        REQUIRE_FALSE( node_view{ fn.addr() }.is_number() );
        fn.Delete();
    }

    // string
    {
        reset_pam();
        fptr<node> fn;
        fn.New();
        node_set_string( fn.addr(), "hello" );
        REQUIRE_FALSE( node_view{ fn.addr() }.is_number() );
        fn.Delete();
    }

    // array
    {
        reset_pam();
        fptr<node> fn;
        fn.New();
        node_set_array( fn.addr() );
        REQUIRE_FALSE( node_view{ fn.addr() }.is_number() );
        fn.Delete();
    }

    // object
    {
        reset_pam();
        fptr<node> fn;
        fn.New();
        node_set_object( fn.addr() );
        REQUIRE_FALSE( node_view{ fn.addr() }.is_number() );
        fn.Delete();
    }
}

// ---------------------------------------------------------------------------
// is_null: корректное поведение для null, error и валидных узлов
// ---------------------------------------------------------------------------
TEST_CASE( "tag_opt: is_null returns true for default node_view", "[pjson_node][tag_opt]" )
{
    node_view v{};
    REQUIRE( v.is_null() );
    REQUIRE_FALSE( v.is_error() );
}

TEST_CASE( "tag_opt: is_null returns false for error node_view", "[pjson_node][tag_opt]" )
{
    node_view v = node_view_error( node_error::not_found );
    REQUIRE_FALSE( v.is_null() );
    REQUIRE( v.is_error() );
}

TEST_CASE( "tag_opt: is_null returns true for freshly allocated node (tag==null by default)", "[pjson_node][tag_opt]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    // Свежевыделенный узел имеет tag==null по умолчанию.
    node_view v{ fn.addr() };
    REQUIRE( v.is_null() );
    REQUIRE_FALSE( v.is_error() );
    fn.Delete();
}

TEST_CASE( "tag_opt: is_null returns false for non-null node", "[pjson_node][tag_opt]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_int( fn.addr(), 7 );
    REQUIRE_FALSE( node_view{ fn.addr() }.is_null() );
    fn.Delete();
}

// ---------------------------------------------------------------------------
// deref: оптимизированный единственный resolve за итерацию
// ---------------------------------------------------------------------------
TEST_CASE( "tag_opt: deref returns self for non-ref node", "[pjson_node][tag_opt]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_int( fn.addr(), 100 );
    node_view v{ fn.addr() };
    node_view d = v.deref();
    REQUIRE( d.id == v.id );
    REQUIRE( d.as_int() == 100 );
    fn.Delete();
}

TEST_CASE( "tag_opt: deref resolves ref chain", "[pjson_node][tag_opt]" )
{
    reset_pam();
    pjson_db_pmm db;

    // Создаём целевой узел и ref на него.
    db.put( "/target", static_cast<int64_t>( 42 ) );
    db.put_ref( "/link", "/target" );
    db.resolve_all_refs();

    node_view link = db.get( "/link", /*deref_refs=*/false );
    REQUIRE( link.is_ref() );
    REQUIRE( link.ref_target() != 0u );

    node_view resolved = link.deref();
    REQUIRE( resolved.valid() );
    REQUIRE( resolved.as_int() == 42 );
}

TEST_CASE( "tag_opt: deref detects cycle", "[pjson_node][tag_opt]" )
{
    reset_pam();

    // Создаём ref, который ссылается на самого себя.
    fptr<node> fn;
    fn.New();
    node_set_ref( fn.addr(), "/self" );
    node_set_ref_target( fn.addr(), fn.addr() );

    node_view v{ fn.addr() };
    node_view d = v.deref();
    // Цикл: deref возвращает null.
    REQUIRE_FALSE( d.valid() );

    fn.Delete();
}

// ---------------------------------------------------------------------------
// Интеграция: erase корректно работает с оптимизированной проверкой тегов
// ---------------------------------------------------------------------------
TEST_CASE( "tag_opt: erase works for object and array after tag optimization", "[pjson_db][tag_opt]" )
{
    reset_pam();
    pjson_db_pmm db;

    // Объект: erase ключа.
    db.put( "/obj/a", 1 );
    db.put( "/obj/b", 2 );
    REQUIRE( db.exists( "/obj/a" ) );
    REQUIRE( db.erase( "/obj/a" ) );
    REQUIRE_FALSE( db.exists( "/obj/a" ) );
    REQUIRE( db.exists( "/obj/b" ) );

    // Массив: erase элемента.
    db.put( "/arr/0", 10 );
    db.put( "/arr/1", 20 );
    REQUIRE( db.erase( "/arr/0" ) );
    // После удаления элемента 0, элемент 1 сдвигается.
    REQUIRE( db.get( "/arr/0" ).as_int() == 20 );
}

// ---------------------------------------------------------------------------
// Интеграция: walk_path корректно работает с оптимизированной проверкой тегов
// ---------------------------------------------------------------------------
TEST_CASE( "tag_opt: walk_path navigates objects and arrays correctly", "[pjson_db][tag_opt]" )
{
    reset_pam();
    pjson_db_pmm db;

    // Глубокий путь через объект и массив.
    db.put( "/a/b/0/c", "deep" );

    node_view v = db.get( "/a/b/0/c" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_string() );
    REQUIRE( v.as_string() == "deep" );

    // Проверяем промежуточные типы.
    REQUIRE( db.get( "/a" ).is_object() );
    REQUIRE( db.get( "/a/b" ).is_array() );
    REQUIRE( db.get( "/a/b/0" ).is_object() );
}

// ---------------------------------------------------------------------------
// Интеграция: traversal корректно обходит дерево с оптимизированными тегами
// ---------------------------------------------------------------------------
TEST_CASE( "tag_opt: traversal counts nodes correctly after tag optimization", "[pjson_db][tag_opt]" )
{
    reset_pam();
    pjson_db_pmm db;

    // Создаём дерево: объект с массивом и вложенными элементами.
    db.put( "/root/name", "test" );
    db.put( "/root/items/0", int64_t( 1 ) );
    db.put( "/root/items/1", int64_t( 2 ) );
    db.put( "/root/items/2", int64_t( 3 ) );

    // Проверяем, что node_count_total > 0 (узлы были подсчитаны через оптимизированный traversal).
    node_view nc = db.get( "/$metrics/node_count_total" );
    REQUIRE( nc.valid() );
    REQUIRE( nc.as_uint() > 0u );

    // Проверяем array_count (items — массив).
    node_view ac = db.get( "/$metrics/array_count" );
    REQUIRE( ac.valid() );
    REQUIRE( ac.as_uint() >= 1u );

    // Проверяем object_count (root — объект).
    node_view oc = db.get( "/$metrics/object_count" );
    REQUIRE( oc.valid() );
    REQUIRE( oc.as_uint() >= 1u );
}
