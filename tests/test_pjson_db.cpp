// test_pjson_db.cpp — Тесты для менеджера персистной JSON-БД (Фаза 6).
//
// Покрытие:
//   - Открытие и инициализация БД
//   - put/get для всех типов значений
//   - Вложенные объекты и массивы через path-адресацию
//   - $ref разыменование
//   - Обнаружение цикличных ссылок
//   - resolve_all_refs()
//   - erase()
//   - exists()
//   - Запрет записи в /$metrics
//   - Метрики
//   - dump() / parse()
//   - search_strings()
//
// Все комментарии — на русском языке (Тр.6).

#include <catch2/catch_test_macros.hpp>

#include "pjson_db.h"

// ===========================================================================
// Вспомогательные функции
// ===========================================================================

/// Сбросить ПАП в начальное состояние перед каждым тестом.
/// Используем Reset() для O(1) очистки.
static void reset_pam()
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();
}

// ===========================================================================
// Открытие и инициализация БД
// ===========================================================================

TEST_CASE( "pjson_db: default constructor creates root object", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    node_view root = db.root();
    REQUIRE( root.valid() );
    REQUIRE( root.is_object() );
}

TEST_CASE( "pjson_db: root_id returns valid node_id", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    REQUIRE( db.root_id() != 0u );
}

TEST_CASE( "pjson_db: second pjson_db reuses existing root", "[pjson_db]" )
{
    reset_pam();
    pjson_db db1;
    node_id  root1 = db1.root_id();

    pjson_db db2;
    node_id  root2 = db2.root_id();

    // Оба должны указывать на один корневой узел.
    REQUIRE( root1 == root2 );
}

// ===========================================================================
// put / get — базовые типы
// ===========================================================================

TEST_CASE( "pjson_db: put and get boolean true", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    REQUIRE( db.put( "/flag", true ) );
    node_view v = db.get( "/flag" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_boolean() );
    REQUIRE( v.as_bool() == true );
}

TEST_CASE( "pjson_db: put and get boolean false", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    REQUIRE( db.put( "/flag", false ) );
    REQUIRE( db.get( "/flag" ).as_bool() == false );
}

TEST_CASE( "pjson_db: put and get integer", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    REQUIRE( db.put( "/count", static_cast<int64_t>( -42 ) ) );
    node_view v = db.get( "/count" );
    REQUIRE( v.is_integer() );
    REQUIRE( v.as_int() == -42 );
}

TEST_CASE( "pjson_db: put and get uinteger", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    REQUIRE( db.put( "/uid", static_cast<uint64_t>( 9999999999ULL ) ) );
    node_view v = db.get( "/uid" );
    REQUIRE( v.is_uinteger() );
    REQUIRE( v.as_uint() == 9999999999ULL );
}

TEST_CASE( "pjson_db: put and get real", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    REQUIRE( db.put( "/pi", 3.14159 ) );
    node_view v = db.get( "/pi" );
    REQUIRE( v.is_real() );
    REQUIRE( v.as_double() > 3.14 );
    REQUIRE( v.as_double() < 3.15 );
}

TEST_CASE( "pjson_db: put and get string", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    REQUIRE( db.put( "/name", "Alice" ) );
    node_view v = db.get( "/name" );
    REQUIRE( v.is_string() );
    REQUIRE( v.as_string() == "Alice" );
}

TEST_CASE( "pjson_db: put and get empty string", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    REQUIRE( db.put( "/empty", "" ) );
    node_view v = db.get( "/empty" );
    REQUIRE( v.is_string() );
    REQUIRE( v.as_string() == "" );
}

TEST_CASE( "pjson_db: put int convenience overload", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    REQUIRE( db.put( "/x", 10 ) );
    node_view v = db.get( "/x" );
    REQUIRE( v.as_int() == 10 );
}

// ===========================================================================
// put / get — вложенные пути
// ===========================================================================

TEST_CASE( "pjson_db: put creates intermediate objects", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    REQUIRE( db.put( "/users/alice/name", "Alice" ) );
    node_view v = db.get( "/users/alice/name" );
    REQUIRE( v.is_string() );
    REQUIRE( v.as_string() == "Alice" );
}

TEST_CASE( "pjson_db: put multiple nested keys", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    REQUIRE( db.put( "/users/alice/name", "Alice" ) );
    REQUIRE( db.put( "/users/alice/age", static_cast<int64_t>( 30 ) ) );
    REQUIRE( db.put( "/users/bob/name", "Bob" ) );

    REQUIRE( db.get( "/users/alice/name" ).as_string() == "Alice" );
    REQUIRE( db.get( "/users/alice/age" ).as_int() == 30 );
    REQUIRE( db.get( "/users/bob/name" ).as_string() == "Bob" );
}

TEST_CASE( "pjson_db: get returns invalid view for missing path", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    node_view v = db.get( "/nonexistent/path" );
    REQUIRE( !v.valid() );
}

TEST_CASE( "pjson_db: get returns invalid view for path beyond scalar", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/x", static_cast<int64_t>( 5 ) );
    node_view v = db.get( "/x/y" );
    REQUIRE( !v.valid() );
}

// ===========================================================================
// exists()
// ===========================================================================

TEST_CASE( "pjson_db: exists returns true for existing path", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/a/b", "hello" );
    REQUIRE( db.exists( "/a/b" ) );
}

TEST_CASE( "pjson_db: exists returns false for missing path", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    REQUIRE( !db.exists( "/missing" ) );
}

TEST_CASE( "pjson_db: exists returns true for metrics path", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    REQUIRE( db.exists( "/$metrics/node_count_total" ) );
}

// ===========================================================================
// erase()
// ===========================================================================

TEST_CASE( "pjson_db: erase removes existing key", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/key", "value" );
    REQUIRE( db.exists( "/key" ) );
    REQUIRE( db.erase( "/key" ) );
    REQUIRE( !db.exists( "/key" ) );
}

TEST_CASE( "pjson_db: erase returns false for missing key", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    REQUIRE( !db.erase( "/nonexistent" ) );
}

TEST_CASE( "pjson_db: erase cannot remove metrics", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    REQUIRE( !db.erase( "/$metrics/node_count_total" ) );
}

TEST_CASE( "pjson_db: erase removes nested key", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/a/b/c", "deep" );
    REQUIRE( db.exists( "/a/b/c" ) );
    REQUIRE( db.erase( "/a/b/c" ) );
    REQUIRE( !db.exists( "/a/b/c" ) );
    // Родительский объект /a/b должен остаться.
    REQUIRE( db.exists( "/a/b" ) );
}

// ===========================================================================
// $ref — разыменование (Задача 6.4)
// ===========================================================================

TEST_CASE( "pjson_db: put_ref creates ref node", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/target", "hello" );
    REQUIRE( db.put_ref( "/link", "/target" ) );

    // Без разыменования — возвращается ref-узел.
    node_view ref_v = db.get( "/link", /*deref_refs=*/false );
    REQUIRE( ref_v.valid() );
    REQUIRE( ref_v.is_ref() );
    REQUIRE( ref_v.ref_path() == "/target" );
}

TEST_CASE( "pjson_db: get with deref follows ref", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/target", "hello" );
    db.put_ref( "/link", "/target" );

    // После resolve_all_refs() target должен быть разрешён.
    db.resolve_all_refs();

    node_view v = db.get( "/link", /*deref_refs=*/true );
    REQUIRE( v.valid() );
    REQUIRE( v.is_string() );
    REQUIRE( v.as_string() == "hello" );
}

TEST_CASE( "pjson_db: resolve_all_refs sets targets", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
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

TEST_CASE( "pjson_db: cyclic ref returns invalid node_view", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;

    // Создаём два ref-узла, ссылающихся друг на друга.
    db.put_ref( "/a", "/b" );
    db.put_ref( "/b", "/a" );

    // Разрешаем ссылки — получаем цикл.
    db.resolve_all_refs();

    node_view a_ref = db.get( "/a", /*deref_refs=*/false );
    REQUIRE( a_ref.is_ref() );

    // deref() должен вернуть invalid при цикле.
    node_view resolved = a_ref.deref( true, 32 );
    REQUIRE( !resolved.valid() );
}

TEST_CASE( "pjson_db: resolve_ref returns invalid for unresolved ref", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;

    // ref с несуществующим путём.
    db.put_ref( "/ghost", "/no/such/path" );

    node_view ref_v = db.get( "/ghost", /*deref_refs=*/false );
    REQUIRE( ref_v.is_ref() );
    REQUIRE( ref_v.ref_target() == 0u );

    node_view resolved = db.resolve_ref( ref_v.id );
    REQUIRE( !resolved.valid() );
}

// ===========================================================================
// $ref через parse()
// ===========================================================================

TEST_CASE( "pjson_db: parse JSON with $ref creates ref node", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;

    db.put( "/source", "value" );
    REQUIRE( db.parse_into( "/link", R"({"$ref":"/source"})" ) );

    node_view link = db.get( "/link", /*deref_refs=*/false );
    REQUIRE( link.is_ref() );
    REQUIRE( link.ref_path() == "/source" );
}

TEST_CASE( "pjson_db: parse JSON with $base64 creates binary node", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;

    REQUIRE( db.parse_into( "/data", R"({"$base64":"AAEC"})" ) );

    node_view v = db.get( "/data" );
    REQUIRE( v.is_binary() );
    REQUIRE( v.size() == 3u );
}

// ===========================================================================
// Метрики (Задача 6.1, /$metrics)
// ===========================================================================

TEST_CASE( "pjson_db: metrics path is read-only for put", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    REQUIRE( !db.put( "/$metrics/node_count_total", static_cast<int64_t>( 0 ) ) );
}

TEST_CASE( "pjson_db: metrics path is read-only for erase", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    REQUIRE( !db.erase( "/$metrics/node_count_total" ) );
}

TEST_CASE( "pjson_db: get metrics returns uinteger", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/a", "hello" );
    db.put( "/b", "world" );

    node_view v = db.get( "/$metrics/node_count_total" );
    // Метрика должна быть числовой.
    REQUIRE( v.valid() );
    REQUIRE( ( v.is_uinteger() || v.is_integer() ) );
}

TEST_CASE( "pjson_db: metrics string_count returns non-zero after puts", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/hello", "world" );

    node_view v = db.get( "/$metrics/string_count" );
    REQUIRE( v.valid() );
    REQUIRE( v.as_uint() > 0u );
}

// ===========================================================================
// dump() / parse()
// ===========================================================================

TEST_CASE( "pjson_db: dump returns JSON string", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/x", static_cast<int64_t>( 1 ) );
    std::string json = db.dump();
    REQUIRE( json.find( "\"x\"" ) != std::string::npos );
    REQUIRE( json.find( "1" ) != std::string::npos );
}

TEST_CASE( "pjson_db: dump node_id returns JSON for specific node", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/val", "test_val" );
    node_view v = db.get( "/val" );
    REQUIRE( v.valid() );
    std::string json = db.dump( v.id );
    REQUIRE( json == "\"test_val\"" );
}

TEST_CASE( "pjson_db: parse replaces root with parsed JSON", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    REQUIRE( db.parse( R"({"greeting":"hello","count":42})" ) );

    node_view g = db.get( "/greeting" );
    REQUIRE( g.is_string() );
    REQUIRE( g.as_string() == "hello" );

    node_view c = db.get( "/count" );
    REQUIRE( c.as_uint() == 42u );
}

// ===========================================================================
// search_strings()
// ===========================================================================

TEST_CASE( "pjson_db: search_strings finds interned keys", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/users/alice/name", "Alice" );
    db.put( "/users/bob/name", "Bob" );

    auto results = db.search_strings( "alice" );
    bool found   = false;
    for ( const auto& r : results )
        if ( r.value.find( "alice" ) != std::string::npos )
            found = true;
    REQUIRE( found );
}

TEST_CASE( "pjson_db: all_strings returns non-empty list after puts", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/key1", "val1" );
    db.put( "/key2", "val2" );

    auto all = db.all_strings();
    REQUIRE( all.size() > 0u );
}

// ===========================================================================
// Персистентность: save и reload
// ===========================================================================

TEST_CASE( "pjson_db: save and reload preserves data", "[pjson_db]" )
{
    const char* pam_file = "/tmp/test_pjson_db_persist.pam";

    // Создаём и заполняем БД.
    {
        pstringview_manager::reset();
        PersistentAddressSpace::Get().Reset();
        PersistentAddressSpace::Init( pam_file );
        pjson_db db;
        db.put( "/greeting", "hello" );
        db.put( "/count", static_cast<int64_t>( 99 ) );
        db.save();
    }

    // Загружаем БД и проверяем данные.
    {
        pstringview_manager::reset();
        PersistentAddressSpace::Get().Reset();
        PersistentAddressSpace::Init( pam_file );
        pjson_db db;
        node_view greeting = db.get( "/greeting" );
        REQUIRE( greeting.is_string() );
        REQUIRE( greeting.as_string() == "hello" );

        node_view count = db.get( "/count" );
        REQUIRE( count.as_int() == 99 );
    }

    // Удаляем тестовый файл.
    std::remove( pam_file );
}

// ===========================================================================
// Массивы через path-адресацию
// ===========================================================================

TEST_CASE( "pjson_db: parse_into with array JSON", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    REQUIRE( db.parse_into( "/items", "[1,2,3]" ) );

    node_view items = db.get( "/items" );
    REQUIRE( items.is_array() );
    REQUIRE( items.size() == 3u );
    REQUIRE( items.at( static_cast<uintptr_t>( 0 ) ).as_uint() == 1u );
    REQUIRE( items.at( static_cast<uintptr_t>( 1 ) ).as_uint() == 2u );
    REQUIRE( items.at( static_cast<uintptr_t>( 2 ) ).as_uint() == 3u );
}

TEST_CASE( "pjson_db: get array element by index path", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    db.parse_into( "/arr", "[10,20,30]" );

    node_view v = db.get( "/arr/1" );
    REQUIRE( v.valid() );
    REQUIRE( v.as_uint() == 20u );
}

TEST_CASE( "pjson_db: get returns invalid for out-of-bounds array index", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    db.parse_into( "/arr", "[1,2]" );

    node_view v = db.get( "/arr/5" );
    REQUIRE( !v.valid() );
}

// ===========================================================================
// Перезапись значений
// ===========================================================================

TEST_CASE( "pjson_db: overwrite string value at existing path", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/name", "Alice" );
    REQUIRE( db.get( "/name" ).as_string() == "Alice" );

    db.put( "/name", "Bob" );
    REQUIRE( db.get( "/name" ).as_string() == "Bob" );
}

TEST_CASE( "pjson_db: overwrite with different type", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/val", "text" );
    db.put( "/val", static_cast<int64_t>( 42 ) );
    node_view v = db.get( "/val" );
    REQUIRE( v.is_integer() );
    REQUIRE( v.as_int() == 42 );
}

// ===========================================================================
// put_null()
// ===========================================================================

TEST_CASE( "pjson_db: put_null creates null node", "[pjson_db]" )
{
    reset_pam();
    pjson_db db;
    REQUIRE( db.put_null( "/nothing" ) );
    node_view v = db.get( "/nothing" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_null() );
}
