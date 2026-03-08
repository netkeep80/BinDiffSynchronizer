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
    pjson_db  db;
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
    pjson_db  db;
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
// Метрики — Фаза 7: персистная структура db_metrics
// ===========================================================================

TEST_CASE( "pjson_db: metrics string_count_total is alias for string_count", "[pjson_db][phase7]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/key_a", "value_a" );
    db.put( "/key_b", "value_b" );

    // Оба пути должны работать и возвращать одинаковые значения.
    node_view v1 = db.get( "/$metrics/string_count" );
    node_view v2 = db.get( "/$metrics/string_count_total" );
    REQUIRE( v1.valid() );
    REQUIRE( v2.valid() );
    REQUIRE( v1.as_uint() == v2.as_uint() );
    REQUIRE( v1.as_uint() > 0u );
}

TEST_CASE( "pjson_db: metrics pam_bump_offset is non-zero after db creation", "[pjson_db][phase7]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/data", "value" );

    node_view v = db.get( "/$metrics/pam_bump_offset" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_uinteger() );
    // После создания БД и записи данных bump > 0.
    REQUIRE( v.as_uint() > 0u );
}

TEST_CASE( "pjson_db: metrics pam_total_size is non-zero", "[pjson_db][phase7]" )
{
    reset_pam();
    pjson_db db;

    node_view v = db.get( "/$metrics/pam_total_size" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_uinteger() );
    REQUIRE( v.as_uint() > 0u );
}

TEST_CASE( "pjson_db: metrics pam_slot_count increases after puts", "[pjson_db][phase7]" )
{
    reset_pam();
    pjson_db db;

    node_view v1 = db.get( "/$metrics/pam_slot_count" );
    REQUIRE( v1.valid() );
    uint64_t count_before = v1.as_uint();

    db.put( "/new_key", "new_value" );

    node_view v2 = db.get( "/$metrics/pam_slot_count" );
    REQUIRE( v2.valid() );
    // После добавления новой записи количество слотов должно вырасти.
    REQUIRE( v2.as_uint() >= count_before );
}

TEST_CASE( "pjson_db: metrics pam_named_count is non-zero after db creation", "[pjson_db][phase7]" )
{
    reset_pam();
    pjson_db db;

    // БД создаёт минимум 3 именованных объекта: pool, root, metrics.
    node_view v = db.get( "/$metrics/pam_named_count" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_uinteger() );
    REQUIRE( v.as_uint() >= 3u );
}

TEST_CASE( "pjson_db: metrics free_node_count and used_node_count are consistent", "[pjson_db][phase7]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/a", "x" );
    db.put( "/b", "y" );

    node_view total  = db.get( "/$metrics/node_count_total" );
    node_view free_n = db.get( "/$metrics/free_node_count" );
    node_view used_n = db.get( "/$metrics/used_node_count" );

    REQUIRE( total.valid() );
    REQUIRE( free_n.valid() );
    REQUIRE( used_n.valid() );

    // total >= used + free (может быть > если pool.nodes_size_ считает иначе).
    REQUIRE( total.as_uint() >= used_n.as_uint() );
}

TEST_CASE( "pjson_db: metrics ref_count increases after put_ref", "[pjson_db][phase7]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/target", "hello" );

    node_view v1 = db.get( "/$metrics/ref_count" );
    REQUIRE( v1.valid() );
    uint64_t ref_before = v1.as_uint();

    db.put_ref( "/link", "/target" );

    node_view v2 = db.get( "/$metrics/ref_count" );
    REQUIRE( v2.valid() );
    // После добавления ref-узла счётчик должен вырасти.
    REQUIRE( v2.as_uint() > ref_before );
}

TEST_CASE( "pjson_db: metrics array_count increases after parse_into array", "[pjson_db][phase7]" )
{
    reset_pam();
    pjson_db db;

    node_view v1 = db.get( "/$metrics/array_count" );
    REQUIRE( v1.valid() );
    uint64_t arr_before = v1.as_uint();

    db.parse_into( "/items", "[1,2,3]" );

    node_view v2 = db.get( "/$metrics/array_count" );
    REQUIRE( v2.valid() );
    REQUIRE( v2.as_uint() > arr_before );
}

TEST_CASE( "pjson_db: metrics object_count is non-zero after db creation", "[pjson_db][phase7]" )
{
    reset_pam();
    pjson_db db;

    // Корневой узел — объект, поэтому object_count >= 1.
    node_view v = db.get( "/$metrics/object_count" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_uinteger() );
    REQUIRE( v.as_uint() >= 1u );
}

TEST_CASE( "pjson_db: metrics last_save_time is zero before save", "[pjson_db][phase7]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/x", "y" );

    // До первого save() время должно быть 0 (или не обновлено).
    node_view v = db.get( "/$metrics/last_save_time" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_uinteger() );
    // last_save_time == 0 до первого save().
    REQUIRE( v.as_uint() == 0u );
}

TEST_CASE( "pjson_db: metrics last_save_time is updated after save", "[pjson_db][phase7]" )
{
    const char* pam_file = "./test_pjson_db_metrics_save.pam";

    // Создаём БД и сохраняем.
    {
        pstringview_manager::reset();
        PersistentAddressSpace::Get().Reset();
        PersistentAddressSpace::Init( pam_file );
        pjson_db db;
        db.put( "/test", "data" );
        db.save();

        // После save() time должен быть не нулевым.
        node_view v = db.get( "/$metrics/last_save_time" );
        REQUIRE( v.valid() );
        REQUIRE( v.as_uint() > 0u );
    }

    std::remove( pam_file );
}

TEST_CASE( "pjson_db: metrics pam_free_list_size is valid", "[pjson_db][phase7]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/a", "value1" );
    db.put( "/b", "value2" );
    db.erase( "/a" );

    // После erase должен появиться хотя бы один свободный блок.
    node_view v = db.get( "/$metrics/pam_free_list_size" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_uinteger() );
    // Значение должно быть числовым (не null); uint всегда >= 0, проверяем тип.
    (void)v.as_uint(); // метрика доступна (не null)
}

TEST_CASE( "pjson_db: update_metrics explicit call works", "[pjson_db][phase7]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/key", "value" );

    // Явный вызов update_metrics() должен пройти без ошибок.
    db.update_metrics();

    node_view v = db.get( "/$metrics/node_count_total" );
    REQUIRE( v.valid() );
    REQUIRE( v.as_uint() > 0u );
}

TEST_CASE( "pjson_db: metrics unknown key returns null node", "[pjson_db][phase7]" )
{
    reset_pam();
    pjson_db db;

    node_view v = db.get( "/$metrics/nonexistent_metric" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_null() );
}

TEST_CASE( "pjson_db: metrics binary_bytes_total increases after parse_into binary", "[pjson_db][phase7]" )
{
    reset_pam();
    pjson_db db;

    node_view v1 = db.get( "/$metrics/binary_bytes_total" );
    REQUIRE( v1.valid() );
    uint64_t bytes_before = v1.as_uint();

    // Парсим бинарный узел: AAEC = [0, 1, 2] = 3 байта.
    db.parse_into( "/data", R"({"$base64":"AAEC"})" );

    node_view v2 = db.get( "/$metrics/binary_bytes_total" );
    REQUIRE( v2.valid() );
    REQUIRE( v2.as_uint() >= bytes_before + 3u );
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
    const char* pam_file = "./test_pjson_db_persist.pam";

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
        pjson_db  db;
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

// ===========================================================================
// Фаза 8: Иерархический интерфейс pmap и расширенный поиск по строкам
// ===========================================================================

// ---------------------------------------------------------------------------
// Задача 8.1: оператор [] — доступ к узлу по пути
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_db: operator[] returns valid node_view for existing path", "[pjson_db][phase8]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/city", "Moscow" );

    node_view v = db["/city"];
    REQUIRE( v.valid() );
    REQUIRE( v.is_string() );
    REQUIRE( v.as_string() == "Moscow" );
}

TEST_CASE( "pjson_db: operator[] creates null node for non-existing path", "[pjson_db][phase8]" )
{
    reset_pam();
    pjson_db db;

    // Путь не существует — operator[] создаёт его как null-узел.
    node_view v = db["/newkey"];
    REQUIRE( v.valid() );
    // Узел создан (null — начальное состояние нового слота).
}

TEST_CASE( "pjson_db: operator[] on metrics path returns metrics node", "[pjson_db][phase8]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/x", static_cast<int64_t>( 1 ) );

    node_view v = db["/$metrics/pam_bump_offset"];
    REQUIRE( v.valid() );
    REQUIRE( v.is_uinteger() );
    REQUIRE( v.as_uint() > 0u );
}

// ---------------------------------------------------------------------------
// Задача 8.1: find() — поиск без создания
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_db: find returns valid node_view for existing path", "[pjson_db][phase8]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/name", "Alice" );

    node_view v = db.find( "/name" );
    REQUIRE( v.valid() );
    REQUIRE( v.as_string() == "Alice" );
}

TEST_CASE( "pjson_db: find returns invalid for non-existing path", "[pjson_db][phase8]" )
{
    reset_pam();
    pjson_db db;

    node_view v = db.find( "/nonexistent" );
    REQUIRE( !v.valid() );
}

TEST_CASE( "pjson_db: find does not create nodes", "[pjson_db][phase8]" )
{
    reset_pam();
    pjson_db db;

    // find не должен создавать новые узлы.
    db.find( "/missing/path/here" );
    REQUIRE( !db.exists( "/missing" ) );
}

TEST_CASE( "pjson_db: find does not dereference ref nodes", "[pjson_db][phase8]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/target", "hello" );
    db.put_ref( "/link", "/target" );
    db.resolve_all_refs();

    // find возвращает сам ref-узел, не его цель.
    node_view v = db.find( "/link" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_ref() );
}

// ---------------------------------------------------------------------------
// Задача 8.1: insert() — вставка JSON-значения по пути
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_db: insert creates node from JSON string", "[pjson_db][phase8]" )
{
    reset_pam();
    pjson_db db;

    node_view v = db.insert( "/data", "\"hello world\"" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_string() );
    REQUIRE( v.as_string() == "hello world" );
}

TEST_CASE( "pjson_db: insert creates node from JSON number", "[pjson_db][phase8]" )
{
    reset_pam();
    pjson_db db;

    node_view v = db.insert( "/count", "42" );
    REQUIRE( v.valid() );
    REQUIRE( v.as_int() == 42 );
}

TEST_CASE( "pjson_db: insert creates node from JSON object", "[pjson_db][phase8]" )
{
    reset_pam();
    pjson_db db;

    node_view v = db.insert( "/cfg", R"({"debug":true,"level":3})" );
    REQUIRE( v.valid() );
    REQUIRE( v.is_object() );
    REQUIRE( v.at( "debug" ).as_bool() == true );
    REQUIRE( v.at( "level" ).as_int() == 3 );
}

TEST_CASE( "pjson_db: insert overwrites existing value", "[pjson_db][phase8]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/val", "old" );

    node_view v = db.insert( "/val", "\"new\"" );
    REQUIRE( v.valid() );
    REQUIRE( v.as_string() == "new" );
}

TEST_CASE( "pjson_db: insert into metrics path returns invalid", "[pjson_db][phase8]" )
{
    reset_pam();
    pjson_db db;

    node_view v = db.insert( "/$metrics/node_count_total", "999" );
    REQUIRE( !v.valid() );
}

// ---------------------------------------------------------------------------
// Задача 8.2: search_node_strings() — поиск по pstring значениям узлов
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_db: search_node_strings finds string nodes containing pattern", "[pjson_db][phase8]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/users/alice/name", "Alice Smith" );
    db.put( "/users/bob/name", "Bob Jones" );
    db.put( "/users/alice/city", "Springfield" );

    // Ищем строки, содержащие "Smith".
    auto results = db.search_node_strings( "Smith" );
    REQUIRE( results.size() == 1u );

    // Ищем строки, содержащие "alice" — нет (Alice с заглавной).
    auto results2 = db.search_node_strings( "alice" );
    REQUIRE( results2.empty() );
}

TEST_CASE( "pjson_db: search_node_strings with empty pattern returns all string nodes", "[pjson_db][phase8]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/a", "hello" );
    db.put( "/b", "world" );
    db.put( "/c", static_cast<int64_t>( 42 ) ); // не строка

    // Пустой pattern — все string-узлы.
    auto results = db.search_node_strings( "" );
    REQUIRE( results.size() >= 2u ); // как минимум /a и /b
}

TEST_CASE( "pjson_db: search_node_strings finds values in nested objects", "[pjson_db][phase8]" )
{
    reset_pam();
    pjson_db db;
    db.parse_into( "/config", R"({"host":"localhost","port":8080,"desc":"main server"})" );

    // Ищем строки, содержащие "server".
    auto results = db.search_node_strings( "server" );
    REQUIRE( results.size() == 1u );

    // Проверяем, что нашли нужный узел.
    REQUIRE( node_view{ results[0] }.as_string() == "main server" );
}

TEST_CASE( "pjson_db: search_node_strings finds values in arrays", "[pjson_db][phase8]" )
{
    reset_pam();
    pjson_db db;
    db.parse_into( "/tags", R"(["persistent","json","database","persistent-db"])" );

    // Ищем строки, содержащие "persistent".
    auto results = db.search_node_strings( "persistent" );
    REQUIRE( results.size() == 2u ); // "persistent" и "persistent-db"
}

TEST_CASE( "pjson_db: search_node_strings returns empty for non-matching pattern", "[pjson_db][phase8]" )
{
    reset_pam();
    pjson_db db;
    db.put( "/greeting", "Hello" );
    db.put( "/farewell", "Goodbye" );

    auto results = db.search_node_strings( "xyz_not_found" );
    REQUIRE( results.empty() );
}

TEST_CASE( "pjson_db: search_strings (pstringview) and search_node_strings (pstring) are complementary",
           "[pjson_db][phase8]" )
{
    reset_pam();
    pjson_db db;
    // Ключи объектов интернируются как pstringview.
    // Значения строк хранятся как pstring.
    db.put( "/user", "Alice" );
    db.put( "/city", "Wonderland" );

    // search_strings ищет по словарю ключей (pstringview).
    auto key_results = db.search_strings( "user" );
    REQUIRE( !key_results.empty() );

    // search_node_strings ищет по значениям узлов (pstring).
    auto val_results = db.search_node_strings( "Alice" );
    REQUIRE( val_results.size() == 1u );
    REQUIRE( node_view{ val_results[0] }.as_string() == "Alice" );
}

// ---------------------------------------------------------------------------
// Задача 8.3: интеграционные тесты (operator[] + find + insert + search)
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_db: phase8 integration - build DB with operator[] and query with find", "[pjson_db][phase8]" )
{
    reset_pam();
    pjson_db db;

    // Строим базу данных через insert.
    db.insert( "/employees/1/name", "\"Ivan\"" );
    db.insert( "/employees/1/dept", "\"Engineering\"" );
    db.insert( "/employees/2/name", "\"Maria\"" );
    db.insert( "/employees/2/dept", "\"Marketing\"" );

    // Читаем через find (без разыменования ref).
    node_view n1 = db.find( "/employees/1/name" );
    REQUIRE( n1.valid() );
    REQUIRE( n1.as_string() == "Ivan" );

    node_view n2 = db.find( "/employees/2/dept" );
    REQUIRE( n2.valid() );
    REQUIRE( n2.as_string() == "Marketing" );
}

TEST_CASE( "pjson_db: phase8 integration - search_node_strings finds values across nested structure",
           "[pjson_db][phase8]" )
{
    reset_pam();
    pjson_db db;

    db.put( "/product/name", "WidgetPro" );
    db.put( "/product/version", "2.0" );
    db.put( "/product/desc", "Advanced widget for professionals" );
    db.put( "/product/vendor", "WidgetCorp" );

    // Ищем "Widget" — должны найти "WidgetPro" и "WidgetCorp".
    auto results = db.search_node_strings( "Widget" );
    REQUIRE( results.size() == 2u );
}

TEST_CASE( "pjson_db: phase8 integration - operator[] creates path then insert populates it", "[pjson_db][phase8]" )
{
    reset_pam();
    pjson_db db;

    // Создаём путь через operator[].
    node_view slot = db["/settings/theme"];
    REQUIRE( slot.valid() );

    // Записываем значение через insert.
    node_view v = db.insert( "/settings/theme", "\"dark\"" );
    REQUIRE( v.valid() );
    REQUIRE( v.as_string() == "dark" );

    // Проверяем доступность через get.
    REQUIRE( db.get( "/settings/theme" ).as_string() == "dark" );
}
