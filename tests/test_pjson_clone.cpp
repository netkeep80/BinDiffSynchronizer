// test_pjson_clone.cpp — Тесты для глубокого копирования узлов.
//
// Покрытие:
//
// node_clone — низкоуровневая функция клонирования
//   - Клонирование null-узла
//   - Клонирование boolean-узла
//   - Клонирование integer-узла
//   - Клонирование uinteger-узла
//   - Клонирование real-узла
//   - Клонирование string-узла (pstring, readwrite)
//   - Клонирование binary-узла
//   - Клонирование array-узла (рекурсивно)
//   - Клонирование object-узла (рекурсивно)
//   - Клонирование ref-узла (path копируется, target = 0)
//
// pjson_db_pmm::clone — высокоуровневый API
//   - Клонирование по пути
//   - Изменение копии не влияет на оригинал
//   - Клонирование вложенных структур
//

#include <catch2/catch_test_macros.hpp>

#include "pjson_db_pmm.h"

using namespace pjson;

// ===========================================================================
// Вспомогательные функции
// ===========================================================================

/// Сбросить ПАП в начальное состояние перед каждым тестом.
static void reset_pam()
{
    pstringview_manager::reset();
    pam_pmm_reset();
}

// ===========================================================================
// node_clone — низкоуровневая функция клонирования
// ===========================================================================

TEST_CASE( "node_clone: cloning null node", "[clone]" )
{
    reset_pam();

    fptr<node> src;
    src.New();
    node_init_null( src.addr() );

    node_id cloned = node_clone( src.addr() );
    REQUIRE( cloned != 0 );
    REQUIRE( cloned != src.addr() );

    node_view cv{ cloned };
    REQUIRE( cv.is_null() == true );
}

TEST_CASE( "node_clone: cloning boolean node", "[clone]" )
{
    reset_pam();

    fptr<node> src;
    src.New();
    node_set_bool( src.addr(), true );

    node_id cloned = node_clone( src.addr() );
    REQUIRE( cloned != 0 );

    node_view cv{ cloned };
    REQUIRE( cv.is_boolean() == true );
    REQUIRE( cv.as_bool() == true );

    // Изменение оригинала не влияет на копию.
    node_set_bool( src.addr(), false );
    REQUIRE( cv.as_bool() == true );
}

TEST_CASE( "node_clone: cloning integer node", "[clone]" )
{
    reset_pam();

    fptr<node> src;
    src.New();
    node_set_int( src.addr(), -42 );

    node_id cloned = node_clone( src.addr() );
    REQUIRE( cloned != 0 );

    node_view cv{ cloned };
    REQUIRE( cv.is_integer() == true );
    REQUIRE( cv.as_int() == -42 );
}

TEST_CASE( "node_clone: cloning uinteger node", "[clone]" )
{
    reset_pam();

    fptr<node> src;
    src.New();
    node_set_uint( src.addr(), 12345u );

    node_id cloned = node_clone( src.addr() );
    REQUIRE( cloned != 0 );

    node_view cv{ cloned };
    REQUIRE( cv.is_uinteger() == true );
    REQUIRE( cv.as_uint() == 12345u );
}

TEST_CASE( "node_clone: cloning real node", "[clone]" )
{
    reset_pam();

    fptr<node> src;
    src.New();
    node_set_real( src.addr(), 3.14159 );

    node_id cloned = node_clone( src.addr() );
    REQUIRE( cloned != 0 );

    node_view cv{ cloned };
    REQUIRE( cv.is_real() == true );
    REQUIRE( cv.as_double() == 3.14159 );
}

TEST_CASE( "node_clone: cloning string node", "[clone]" )
{
    reset_pam();

    fptr<node> src;
    src.New();
    node_set_string( src.addr(), "hello world" );

    node_id cloned = node_clone( src.addr() );
    REQUIRE( cloned != 0 );

    node_view cv{ cloned };
    REQUIRE( cv.is_string() == true );
    REQUIRE( cv.as_string() == "hello world" );

    // Изменение оригинала не влияет на копию (независимые аллокации).
    node_assign_string( src.addr(), "changed" );
    REQUIRE( cv.as_string() == "hello world" );
    REQUIRE( node_view{ src.addr() }.as_string() == "changed" );
}

TEST_CASE( "node_clone: cloning binary node", "[clone]" )
{
    reset_pam();

    fptr<node> src;
    src.New();
    node_set_binary( src.addr() );
    node_binary_push_back( src.addr(), 0xAA );
    node_binary_push_back( src.addr(), 0xBB );
    node_binary_push_back( src.addr(), 0xCC );

    node_id cloned = node_clone( src.addr() );
    REQUIRE( cloned != 0 );

    node_view cv{ cloned };
    REQUIRE( cv.is_binary() == true );
    REQUIRE( cv.size() == 3 );
}

TEST_CASE( "node_clone: cloning array node recursively", "[clone]" )
{
    reset_pam();

    fptr<node> src;
    src.New();
    node_set_array( src.addr() );

    // Добавляем элементы [1, 2, 3].
    node_id slot1 = node_array_push_back( src.addr() );
    node_set_int( slot1, 1 );
    node_id slot2 = node_array_push_back( src.addr() );
    node_set_int( slot2, 2 );
    node_id slot3 = node_array_push_back( src.addr() );
    node_set_int( slot3, 3 );

    node_id cloned = node_clone( src.addr() );
    REQUIRE( cloned != 0 );

    node_view cv{ cloned };
    REQUIRE( cv.is_array() == true );
    REQUIRE( cv.size() == 3 );
    REQUIRE( cv[uintptr_t( 0 )].as_int() == 1 );
    REQUIRE( cv[uintptr_t( 1 )].as_int() == 2 );
    REQUIRE( cv[uintptr_t( 2 )].as_int() == 3 );

    // Изменение оригинала не влияет на копию.
    node_set_int( slot1, 100 );
    REQUIRE( cv[uintptr_t( 0 )].as_int() == 1 );
}

TEST_CASE( "node_clone: cloning object node recursively", "[clone]" )
{
    reset_pam();

    fptr<node> src;
    src.New();
    node_set_object( src.addr() );

    node_id name_slot = node_object_insert( src.addr(), "name" );
    node_set_string( name_slot, "Alice" );
    node_id age_slot = node_object_insert( src.addr(), "age" );
    node_set_int( age_slot, 30 );

    node_id cloned = node_clone( src.addr() );
    REQUIRE( cloned != 0 );

    node_view cv{ cloned };
    REQUIRE( cv.is_object() == true );
    REQUIRE( cv.size() == 2 );
    REQUIRE( cv.at( "name" ).as_string() == "Alice" );
    REQUIRE( cv.at( "age" ).as_int() == 30 );

    // Изменение оригинала не влияет на копию.
    node_assign_string( name_slot, "Bob" );
    REQUIRE( cv.at( "name" ).as_string() == "Alice" );
}

TEST_CASE( "node_clone: cloning ref node copies path, target is 0", "[clone]" )
{
    reset_pam();

    fptr<node> src;
    src.New();
    node_set_ref( src.addr(), "/users/alice" );
    node_set_ref_target( src.addr(), 12345 ); // симулируем разрешённую ссылку

    node_id cloned = node_clone( src.addr() );
    REQUIRE( cloned != 0 );

    node_view cv{ cloned };
    REQUIRE( cv.is_ref() == true );
    REQUIRE( cv.ref_path() == "/users/alice" );
    REQUIRE( cv.ref_target() == 0 ); // target не копируется
}

TEST_CASE( "node_clone: cloning nested structure", "[clone]" )
{
    reset_pam();

    // Создаём вложенную структуру: {"users": [{"name": "Alice"}]}
    fptr<node> root;
    root.New();
    node_set_object( root.addr() );

    node_id users_slot = node_object_insert( root.addr(), "users" );
    node_set_array( users_slot );

    node_id user_slot = node_array_push_back( users_slot );
    node_set_object( user_slot );

    node_id name_slot = node_object_insert( user_slot, "name" );
    node_set_string( name_slot, "Alice" );

    // Клонируем всю структуру.
    node_id cloned = node_clone( root.addr() );
    REQUIRE( cloned != 0 );

    node_view cv{ cloned };
    REQUIRE( cv.is_object() == true );
    REQUIRE( cv.at( "users" ).is_array() == true );
    REQUIRE( cv.at( "users" ).size() == 1 );
    REQUIRE( cv.at( "users" )[uintptr_t( 0 )].at( "name" ).as_string() == "Alice" );
}

// ===========================================================================
// pjson_db_pmm::clone — высокоуровневый API
// ===========================================================================

TEST_CASE( "pjson_db_pmm::clone: clone path to another path", "[clone][db]" )
{
    reset_pam();
    pjson_db_pmm db;

    db.put( "/original/name", "Alice" );
    db.put( "/original/age", 30 );

    bool ok = db.clone( "/original", "/copy" );
    REQUIRE( ok == true );

    REQUIRE( db.get( "/copy/name" ).as_string() == "Alice" );
    REQUIRE( db.get( "/copy/age" ).as_int() == 30 );
}

TEST_CASE( "pjson_db_pmm::clone: modifying copy does not affect original", "[clone][db]" )
{
    reset_pam();
    pjson_db_pmm db;

    db.put( "/original/value", "original_value" );

    bool ok = db.clone( "/original", "/copy" );
    REQUIRE( ok == true );

    // Изменяем копию.
    db.put( "/copy/value", "modified_value" );

    // Оригинал не изменился.
    REQUIRE( db.get( "/original/value" ).as_string() == "original_value" );
    REQUIRE( db.get( "/copy/value" ).as_string() == "modified_value" );
}

TEST_CASE( "pjson_db_pmm::clone: clone nested structure", "[clone][db]" )
{
    reset_pam();
    pjson_db_pmm db;

    db.parse_into( "/data", R"({"users": [{"name": "Alice"}, {"name": "Bob"}], "count": 2})" );

    bool ok = db.clone( "/data", "/backup" );
    REQUIRE( ok == true );

    REQUIRE( db.get( "/backup/count" ).as_int() == 2 );
    REQUIRE( db.get( "/backup/users/0/name" ).as_string() == "Alice" );
    REQUIRE( db.get( "/backup/users/1/name" ).as_string() == "Bob" );
}

TEST_CASE( "pjson_db_pmm::clone: clone nonexistent path returns false", "[clone][db]" )
{
    reset_pam();
    pjson_db_pmm db;

    bool ok = db.clone( "/nonexistent", "/copy" );
    REQUIRE( ok == false );
}

TEST_CASE( "pjson_db_pmm::clone: clone to metrics path returns false", "[clone][db]" )
{
    reset_pam();
    pjson_db_pmm db;

    db.put( "/data", 42 );

    bool ok = db.clone( "/data", "/$metrics/hacked" );
    REQUIRE( ok == false );
}

TEST_CASE( "pjson_db_pmm::clone: clone from metrics path returns false", "[clone][db]" )
{
    reset_pam();
    pjson_db_pmm db;

    bool ok = db.clone( "/$metrics/node_count_total", "/copy" );
    REQUIRE( ok == false );
}

TEST_CASE( "pjson_db_pmm::clone(node_id): low-level clone by node_id", "[clone][db]" )
{
    reset_pam();
    pjson_db_pmm db;

    db.put( "/value", 42 );
    node_id src_id = db.get( "/value" ).id;

    node_id cloned = db.clone( src_id );
    REQUIRE( cloned != 0 );
    REQUIRE( cloned != src_id );

    node_view cv{ cloned };
    REQUIRE( cv.as_int() == 42 );
}
