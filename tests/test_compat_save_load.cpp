// test_compat_save_load.cpp — Тесты совместимости save/load (Этап 4.2).
//
// Проверяют, что данные, записанные через pjson_db_pmm,
// корректно восстанавливаются после save/load цикла:
//
//   - Все типы узлов: null, bool, int64, uint64, double, string, binary, array, object, ref
//   - Вложенные структуры (глубина 5+)
//   - Большие строки (4 КБ+)
//   - Массивы с 100+ элементами
//   - Объекты с 100+ ключами
//   - Метрики: корректность после reload
//   - Множественные save/load циклы (3 итерации)
//   - Мутации после reload (put/erase/put)
//   - $ref-узлы: путь и разрешение после reload
//   - $base64 (binary) узлы: данные после reload
//   - Пустая БД: save/load пустого файла
//   - Batch-мутации после reload
//
// Все тесты используют файл на диске и проверяют совместимость
// через полный цикл: create → populate → save → destroy → load → verify.
//

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "pjson_db_pmm.h"

using namespace pjson;

// ===========================================================================
// Вспомогательные функции
// ===========================================================================

namespace
{
/// Сбросить PMM в начальное состояние.
static void reset_pam_compat()
{
    pstringview_manager::reset();
    pam_pmm_reset();
}

/// Удалить файл БД (cleanup).
static void remove_db_file( const char* filename )
{
    std::remove( filename );
}

} // anonymous namespace

// ===========================================================================
// 1. Все типы узлов: save/load round-trip
// ===========================================================================

TEST_CASE( "compat: all node types survive save/load", "[compat][save_load]" )
{
    const char* filename = "compat_all_types.pam";
    remove_db_file( filename );

    // --- SAVE PHASE ---
    {
        reset_pam_compat();
        pam_pmm_init( filename );
        pjson_db_pmm db;

        db.put_null( "/v_null" );
        db.put( "/v_bool_true", true );
        db.put( "/v_bool_false", false );
        db.put( "/v_int", static_cast<int64_t>( -42 ) );
        db.put( "/v_uint", static_cast<uint64_t>( 18446744073709551615ULL ) );
        db.put( "/v_real", 3.141592653589793 );
        db.put( "/v_string", "Hello, World!" );

        // binary ($base64)
        db.parse_into( "/v_binary", R"({"$base64":"AQID/w=="})" );

        // array
        db.parse_into( "/v_array", "[1,2,3,\"four\",true,null]" );

        // object
        db.parse_into( "/v_object", R"({"a":1,"b":"two","c":false})" );

        // ref
        db.put_ref( "/v_ref", "/v_string" );
        db.resolve_all_refs();

        db.save();
        pam_pmm_destroy();
    }

    // --- LOAD PHASE ---
    {
        reset_pam_compat();
        pam_pmm_init( filename );
        pjson_db_pmm db;

        // null
        node_view v_null = db.get( "/v_null" );
        REQUIRE( v_null.valid() );
        REQUIRE( v_null.is_null() );

        // bool true
        node_view v_bt = db.get( "/v_bool_true" );
        REQUIRE( v_bt.valid() );
        REQUIRE( v_bt.is_boolean() );
        REQUIRE( v_bt.as_bool() == true );

        // bool false
        node_view v_bf = db.get( "/v_bool_false" );
        REQUIRE( v_bf.valid() );
        REQUIRE( v_bf.is_boolean() );
        REQUIRE( v_bf.as_bool() == false );

        // int64
        node_view v_int = db.get( "/v_int" );
        REQUIRE( v_int.valid() );
        REQUIRE( v_int.is_integer() );
        REQUIRE( v_int.as_int() == -42 );

        // uint64
        node_view v_uint = db.get( "/v_uint" );
        REQUIRE( v_uint.valid() );
        REQUIRE( v_uint.is_uinteger() );
        REQUIRE( v_uint.as_uint() == 18446744073709551615ULL );

        // double
        node_view v_real = db.get( "/v_real" );
        REQUIRE( v_real.valid() );
        REQUIRE( v_real.is_real() );
        REQUIRE( v_real.as_double() == 3.141592653589793 );

        // string
        node_view v_str = db.get( "/v_string" );
        REQUIRE( v_str.valid() );
        REQUIRE( v_str.is_string() );
        REQUIRE( v_str.as_string() == std::string_view( "Hello, World!" ) );

        // binary ($base64): bytes [1, 2, 3, 255]
        node_view v_bin = db.get( "/v_binary" );
        REQUIRE( v_bin.valid() );
        REQUIRE( v_bin.is_binary() );
        REQUIRE( v_bin.size() == 4 );

        // array
        node_view v_arr = db.get( "/v_array" );
        REQUIRE( v_arr.valid() );
        REQUIRE( v_arr.is_array() );
        REQUIRE( v_arr.size() == 6 );
        REQUIRE( v_arr.at( uintptr_t( 0 ) ).as_int() == 1 );
        REQUIRE( v_arr.at( uintptr_t( 3 ) ).as_string() == std::string_view( "four" ) );
        REQUIRE( v_arr.at( uintptr_t( 4 ) ).as_bool() == true );
        REQUIRE( v_arr.at( uintptr_t( 5 ) ).is_null() );

        // object
        node_view v_obj = db.get( "/v_object" );
        REQUIRE( v_obj.valid() );
        REQUIRE( v_obj.is_object() );
        REQUIRE( v_obj.size() == 3 );

        node_view oa = db.get( "/v_object/a" );
        REQUIRE( oa.valid() );
        REQUIRE( oa.as_int() == 1 );

        node_view ob = db.get( "/v_object/b" );
        REQUIRE( ob.valid() );
        REQUIRE( ob.as_string() == std::string_view( "two" ) );

        node_view oc = db.get( "/v_object/c" );
        REQUIRE( oc.valid() );
        REQUIRE( oc.as_bool() == false );

        // ref — разыменовывается к строке
        node_view v_ref = db.get( "/v_ref" );
        REQUIRE( v_ref.valid() );
        REQUIRE( v_ref.is_string() );
        REQUIRE( v_ref.as_string() == std::string_view( "Hello, World!" ) );

        // ref — без разыменования
        node_view v_ref_raw = db.get( "/v_ref", false );
        REQUIRE( v_ref_raw.valid() );
        REQUIRE( v_ref_raw.is_ref() );

        pam_pmm_destroy();
    }

    remove_db_file( filename );
}

// ===========================================================================
// 2. Вложенные структуры (глубина 5+)
// ===========================================================================

TEST_CASE( "compat: nested structures survive save/load", "[compat][save_load]" )
{
    const char* filename = "compat_nested.pam";
    remove_db_file( filename );

    // --- SAVE ---
    {
        reset_pam_compat();
        pam_pmm_init( filename );
        pjson_db_pmm db;

        db.put( "/a/b/c/d/e/value", static_cast<int64_t>( 42 ) );
        db.put( "/a/b/c/d/e/name", "deep" );
        db.put( "/a/b/x", 3.14 );

        // Вложенный JSON через parse_into
        db.parse_into( "/nested", R"({"level1":{"level2":{"level3":{"level4":{"level5":"bottom"}}}}})" );

        db.save();
        pam_pmm_destroy();
    }

    // --- LOAD ---
    {
        reset_pam_compat();
        pam_pmm_init( filename );
        pjson_db_pmm db;

        node_view v1 = db.get( "/a/b/c/d/e/value" );
        REQUIRE( v1.valid() );
        REQUIRE( v1.as_int() == 42 );

        node_view v2 = db.get( "/a/b/c/d/e/name" );
        REQUIRE( v2.valid() );
        REQUIRE( v2.as_string() == std::string_view( "deep" ) );

        node_view v3 = db.get( "/a/b/x" );
        REQUIRE( v3.valid() );
        REQUIRE( v3.as_double() == 3.14 );

        node_view v4 = db.get( "/nested/level1/level2/level3/level4/level5" );
        REQUIRE( v4.valid() );
        REQUIRE( v4.as_string() == std::string_view( "bottom" ) );

        pam_pmm_destroy();
    }

    remove_db_file( filename );
}

// ===========================================================================
// 3. Большие строки (4 КБ+)
// ===========================================================================

TEST_CASE( "compat: large strings survive save/load", "[compat][save_load]" )
{
    const char* filename = "compat_large_str.pam";
    remove_db_file( filename );

    std::string big_str( 4096, 'A' );
    // Добавим UTF-8 символы в конец.
    big_str += "\xC3\xA9\xC3\xA0\xC3\xBC"; // éàü

    // --- SAVE ---
    {
        reset_pam_compat();
        pam_pmm_init( filename );
        pjson_db_pmm db;

        db.put( "/big_string", big_str.c_str() );
        db.put( "/small_string", "tiny" );

        db.save();
        pam_pmm_destroy();
    }

    // --- LOAD ---
    {
        reset_pam_compat();
        pam_pmm_init( filename );
        pjson_db_pmm db;

        node_view v1 = db.get( "/big_string" );
        REQUIRE( v1.valid() );
        REQUIRE( v1.is_string() );
        REQUIRE( v1.as_string() == std::string_view( big_str ) );

        node_view v2 = db.get( "/small_string" );
        REQUIRE( v2.valid() );
        REQUIRE( v2.as_string() == std::string_view( "tiny" ) );

        pam_pmm_destroy();
    }

    remove_db_file( filename );
}

// ===========================================================================
// 4. Массивы с 100+ элементами
// ===========================================================================

TEST_CASE( "compat: large arrays survive save/load", "[compat][save_load]" )
{
    const char* filename = "compat_large_arr.pam";
    remove_db_file( filename );

    constexpr unsigned ARR_N = 200;

    // --- SAVE ---
    {
        reset_pam_compat();
        pam_pmm_init( filename );
        pjson_db_pmm db;

        for ( unsigned i = 0; i < ARR_N; ++i )
        {
            char path[64];
            std::snprintf( path, sizeof( path ), "/arr/%u", i );
            db.put( path, static_cast<int64_t>( i * 7 ) );
        }

        db.save();
        pam_pmm_destroy();
    }

    // --- LOAD ---
    {
        reset_pam_compat();
        pam_pmm_init( filename );
        pjson_db_pmm db;

        node_view arr = db.get( "/arr" );
        REQUIRE( arr.valid() );
        REQUIRE( arr.is_array() );
        REQUIRE( arr.size() == ARR_N );

        for ( unsigned i = 0; i < ARR_N; ++i )
        {
            node_view elem = arr.at( i );
            REQUIRE( elem.valid() );
            REQUIRE( elem.as_int() == static_cast<int64_t>( i * 7 ) );
        }

        pam_pmm_destroy();
    }

    remove_db_file( filename );
}

// ===========================================================================
// 5. Объекты с 100+ ключами
// ===========================================================================

TEST_CASE( "compat: large objects survive save/load", "[compat][save_load]" )
{
    const char* filename = "compat_large_obj.pam";
    remove_db_file( filename );

    constexpr unsigned OBJ_N = 150;

    // --- SAVE ---
    {
        reset_pam_compat();
        pam_pmm_init( filename );
        pjson_db_pmm db;

        for ( unsigned i = 0; i < OBJ_N; ++i )
        {
            char path[64];
            std::snprintf( path, sizeof( path ), "/obj/key_%03u", i );
            db.put( path, static_cast<int64_t>( i ) );
        }

        db.save();
        pam_pmm_destroy();
    }

    // --- LOAD ---
    {
        reset_pam_compat();
        pam_pmm_init( filename );
        pjson_db_pmm db;

        node_view obj = db.get( "/obj" );
        REQUIRE( obj.valid() );
        REQUIRE( obj.is_object() );
        REQUIRE( obj.size() == OBJ_N );

        // Проверяем выборочно.
        for ( unsigned i = 0; i < OBJ_N; i += 10 )
        {
            char path[64];
            std::snprintf( path, sizeof( path ), "/obj/key_%03u", i );
            node_view v = db.get( path );
            REQUIRE( v.valid() );
            REQUIRE( v.as_int() == static_cast<int64_t>( i ) );
        }

        pam_pmm_destroy();
    }

    remove_db_file( filename );
}

// ===========================================================================
// 6. Метрики корректны после reload
// ===========================================================================

TEST_CASE( "compat: metrics correct after save/load", "[compat][save_load]" )
{
    const char* filename = "compat_metrics.pam";
    remove_db_file( filename );

    uint64_t saved_node_count  = 0;
    uint64_t saved_array_count = 0;

    // --- SAVE ---
    {
        reset_pam_compat();
        pam_pmm_init( filename );
        pjson_db_pmm db;

        db.put( "/a", static_cast<int64_t>( 1 ) );
        db.put( "/b", "hello" );
        db.parse_into( "/c", "[1,2,3]" );
        db.parse_into( "/d", R"({"x":1})" );

        db.update_metrics();

        node_view nc = db.get( "/$metrics/node_count_total" );
        REQUIRE( nc.valid() );
        saved_node_count = nc.as_uint();

        node_view ac = db.get( "/$metrics/array_count" );
        REQUIRE( ac.valid() );
        saved_array_count = ac.as_uint();

        REQUIRE( saved_node_count > 0 );
        REQUIRE( saved_array_count > 0 );

        db.save();
        pam_pmm_destroy();
    }

    // --- LOAD ---
    {
        reset_pam_compat();
        pam_pmm_init( filename );
        pjson_db_pmm db;

        // Метрики обновляются при save(), поэтому после load должны быть корректны.
        node_view nc = db.get( "/$metrics/node_count_total" );
        REQUIRE( nc.valid() );
        REQUIRE( nc.as_uint() == saved_node_count );

        node_view ac = db.get( "/$metrics/array_count" );
        REQUIRE( ac.valid() );
        REQUIRE( ac.as_uint() == saved_array_count );

        node_view st = db.get( "/$metrics/last_save_time" );
        REQUIRE( st.valid() );
        REQUIRE( st.as_uint() > 0 );

        pam_pmm_destroy();
    }

    remove_db_file( filename );
}

// ===========================================================================
// 7. Множественные save/load циклы (3 итерации)
// ===========================================================================

TEST_CASE( "compat: multiple save/load cycles", "[compat][save_load]" )
{
    const char* filename = "compat_multi_cycle.pam";
    remove_db_file( filename );

    // Цикл 1: создаём начальные данные.
    {
        reset_pam_compat();
        pam_pmm_init( filename );
        pjson_db_pmm db;

        db.put( "/counter", static_cast<int64_t>( 1 ) );
        db.put( "/name", "cycle_1" );

        db.save();
        pam_pmm_destroy();
    }

    // Цикл 2: читаем, модифицируем, сохраняем.
    {
        reset_pam_compat();
        pam_pmm_init( filename );
        pjson_db_pmm db;

        node_view c = db.get( "/counter" );
        REQUIRE( c.valid() );
        REQUIRE( c.as_int() == 1 );

        db.put( "/counter", static_cast<int64_t>( 2 ) );
        db.put( "/name", "cycle_2" );
        db.put( "/extra", "added_in_cycle_2" );

        db.save();
        pam_pmm_destroy();
    }

    // Цикл 3: читаем, модифицируем, сохраняем.
    {
        reset_pam_compat();
        pam_pmm_init( filename );
        pjson_db_pmm db;

        node_view c = db.get( "/counter" );
        REQUIRE( c.valid() );
        REQUIRE( c.as_int() == 2 );

        node_view n = db.get( "/name" );
        REQUIRE( n.valid() );
        REQUIRE( n.as_string() == std::string_view( "cycle_2" ) );

        node_view e = db.get( "/extra" );
        REQUIRE( e.valid() );
        REQUIRE( e.as_string() == std::string_view( "added_in_cycle_2" ) );

        db.put( "/counter", static_cast<int64_t>( 3 ) );
        db.erase( "/extra" );
        db.put( "/final", true );

        db.save();
        pam_pmm_destroy();
    }

    // Финальная проверка.
    {
        reset_pam_compat();
        pam_pmm_init( filename );
        pjson_db_pmm db;

        REQUIRE( db.get( "/counter" ).as_int() == 3 );
        REQUIRE( db.get( "/name" ).as_string() == std::string_view( "cycle_2" ) );
        REQUIRE( !db.exists( "/extra" ) );
        REQUIRE( db.get( "/final" ).as_bool() == true );

        pam_pmm_destroy();
    }

    remove_db_file( filename );
}

// ===========================================================================
// 8. Мутации после reload (put/erase/put)
// ===========================================================================

TEST_CASE( "compat: mutations after reload work correctly", "[compat][save_load]" )
{
    const char* filename = "compat_mutations.pam";
    remove_db_file( filename );

    // --- SAVE ---
    {
        reset_pam_compat();
        pam_pmm_init( filename );
        pjson_db_pmm db;

        for ( int i = 0; i < 20; ++i )
        {
            char path[64];
            std::snprintf( path, sizeof( path ), "/items/k_%02d", i );
            db.put( path, static_cast<int64_t>( i ) );
        }

        db.save();
        pam_pmm_destroy();
    }

    // --- LOAD + MUTATE ---
    {
        reset_pam_compat();
        pam_pmm_init( filename );
        pjson_db_pmm db;

        // Удаляем нечётные.
        for ( int i = 1; i < 20; i += 2 )
        {
            char path[64];
            std::snprintf( path, sizeof( path ), "/items/k_%02d", i );
            db.erase( path );
        }

        // Добавляем новые.
        for ( int i = 20; i < 30; ++i )
        {
            char path[64];
            std::snprintf( path, sizeof( path ), "/items/k_%02d", i );
            db.put( path, static_cast<int64_t>( i * 10 ) );
        }

        db.save();
        pam_pmm_destroy();
    }

    // --- VERIFY ---
    {
        reset_pam_compat();
        pam_pmm_init( filename );
        pjson_db_pmm db;

        // Чётные 0..18 должны быть.
        for ( int i = 0; i < 20; i += 2 )
        {
            char path[64];
            std::snprintf( path, sizeof( path ), "/items/k_%02d", i );
            node_view v = db.get( path );
            REQUIRE( v.valid() );
            REQUIRE( v.as_int() == static_cast<int64_t>( i ) );
        }

        // Нечётные 1..19 должны отсутствовать.
        for ( int i = 1; i < 20; i += 2 )
        {
            char path[64];
            std::snprintf( path, sizeof( path ), "/items/k_%02d", i );
            REQUIRE( !db.exists( path ) );
        }

        // Новые 20..29.
        for ( int i = 20; i < 30; ++i )
        {
            char path[64];
            std::snprintf( path, sizeof( path ), "/items/k_%02d", i );
            node_view v = db.get( path );
            REQUIRE( v.valid() );
            REQUIRE( v.as_int() == static_cast<int64_t>( i * 10 ) );
        }

        pam_pmm_destroy();
    }

    remove_db_file( filename );
}

// ===========================================================================
// 9. $ref-узлы: путь сохраняется, разрешение работает после reload
// ===========================================================================

TEST_CASE( "compat: ref nodes survive save/load and resolve", "[compat][save_load]" )
{
    const char* filename = "compat_refs.pam";
    remove_db_file( filename );

    // --- SAVE ---
    {
        reset_pam_compat();
        pam_pmm_init( filename );
        pjson_db_pmm db;

        db.put( "/targets/alpha", "Alpha" );
        db.put( "/targets/beta", static_cast<int64_t>( 99 ) );

        db.put_ref( "/links/link_alpha", "/targets/alpha" );
        db.put_ref( "/links/link_beta", "/targets/beta" );

        db.resolve_all_refs();

        db.save();
        pam_pmm_destroy();
    }

    // --- LOAD ---
    {
        reset_pam_compat();
        pam_pmm_init( filename );
        pjson_db_pmm db;

        // ref-узлы сохраняют путь, но target=0 после reload (нужен resolve).
        db.resolve_all_refs();

        // Через deref.
        node_view la = db.get( "/links/link_alpha" );
        REQUIRE( la.valid() );
        REQUIRE( la.is_string() );
        REQUIRE( la.as_string() == std::string_view( "Alpha" ) );

        node_view lb = db.get( "/links/link_beta" );
        REQUIRE( lb.valid() );
        REQUIRE( lb.is_integer() );
        REQUIRE( lb.as_int() == 99 );

        pam_pmm_destroy();
    }

    remove_db_file( filename );
}

// ===========================================================================
// 10. Пустая БД: save/load пустого файла
// ===========================================================================

TEST_CASE( "compat: empty db save/load", "[compat][save_load]" )
{
    const char* filename = "compat_empty.pam";
    remove_db_file( filename );

    // --- SAVE (empty) ---
    {
        reset_pam_compat();
        pam_pmm_init( filename );
        pjson_db_pmm db;
        db.save();
        pam_pmm_destroy();
    }

    // --- LOAD (should be empty) ---
    {
        reset_pam_compat();
        pam_pmm_init( filename );
        pjson_db_pmm db;

        node_view r = db.root();
        REQUIRE( r.valid() );
        // Пустой корень — null или пустой объект.
        // Просто проверяем, что не упало и что нет данных.
        REQUIRE( !db.exists( "/anything" ) );

        // Можно добавлять данные после загрузки пустой БД.
        db.put( "/new_key", static_cast<int64_t>( 123 ) );
        node_view v = db.get( "/new_key" );
        REQUIRE( v.valid() );
        REQUIRE( v.as_int() == 123 );

        pam_pmm_destroy();
    }

    remove_db_file( filename );
}

// ===========================================================================
// 11. Batch-мутации после reload
// ===========================================================================

TEST_CASE( "compat: batch mutations after reload", "[compat][save_load]" )
{
    const char* filename = "compat_batch.pam";
    remove_db_file( filename );

    // --- SAVE ---
    {
        reset_pam_compat();
        pam_pmm_init( filename );
        pjson_db_pmm db;

        db.put( "/init", static_cast<int64_t>( 1 ) );
        db.save();
        pam_pmm_destroy();
    }

    // --- LOAD + BATCH MUTATE ---
    {
        reset_pam_compat();
        pam_pmm_init( filename );
        pjson_db_pmm db;

        REQUIRE( db.get( "/init" ).as_int() == 1 );

        {
            auto guard = db.batch();
            for ( int i = 0; i < 50; ++i )
            {
                char path[64];
                std::snprintf( path, sizeof( path ), "/batch/k_%02d", i );
                db.put( path, static_cast<int64_t>( i ) );
            }
        } // batch_end — метрики пересчитываются.

        db.save();
        pam_pmm_destroy();
    }

    // --- VERIFY ---
    {
        reset_pam_compat();
        pam_pmm_init( filename );
        pjson_db_pmm db;

        REQUIRE( db.get( "/init" ).as_int() == 1 );

        for ( int i = 0; i < 50; ++i )
        {
            char path[64];
            std::snprintf( path, sizeof( path ), "/batch/k_%02d", i );
            node_view v = db.get( path );
            REQUIRE( v.valid() );
            REQUIRE( v.as_int() == static_cast<int64_t>( i ) );
        }

        pam_pmm_destroy();
    }

    remove_db_file( filename );
}

// ===========================================================================
// 12. Полный JSON документ: parse → save → load → dump → compare
// ===========================================================================

TEST_CASE( "compat: full JSON document parse/dump round-trip", "[compat][save_load]" )
{
    const char* filename = "compat_json_roundtrip.pam";
    remove_db_file( filename );

    const char* json_input = R"({
        "name": "Alice",
        "age": 30,
        "active": true,
        "scores": [95, 87, 92],
        "address": {
            "city": "Moscow",
            "zip": "123456"
        }
    })";

    std::string dump_before;

    // --- SAVE ---
    {
        reset_pam_compat();
        pam_pmm_init( filename );
        pjson_db_pmm db;

        db.parse( json_input );
        dump_before = db.dump();

        db.save();
        pam_pmm_destroy();
    }

    // --- LOAD ---
    {
        reset_pam_compat();
        pam_pmm_init( filename );
        pjson_db_pmm db;

        std::string dump_after = db.dump();

        // Точное совпадение дампов.
        REQUIRE( dump_after == dump_before );

        // Проверяем отдельные поля.
        REQUIRE( db.get( "/name" ).as_string() == std::string_view( "Alice" ) );
        REQUIRE( db.get( "/age" ).as_int() == 30 );
        REQUIRE( db.get( "/active" ).as_bool() == true );
        REQUIRE( db.get( "/scores/0" ).as_int() == 95 );
        REQUIRE( db.get( "/scores/1" ).as_int() == 87 );
        REQUIRE( db.get( "/scores/2" ).as_int() == 92 );
        REQUIRE( db.get( "/address/city" ).as_string() == std::string_view( "Moscow" ) );
        REQUIRE( db.get( "/address/zip" ).as_string() == std::string_view( "123456" ) );

        pam_pmm_destroy();
    }

    remove_db_file( filename );
}

// ===========================================================================
// 13. Бенчмарк save/load (информационный, Этап 4.3)
// ===========================================================================

TEST_CASE( "compat: save/load benchmark with 1k nodes", "[compat][save_load][bench]" )
{
    const char* filename = "compat_bench_save_load.pam";
    remove_db_file( filename );

    using bench_clk = std::chrono::steady_clock;

    constexpr unsigned BENCH_N = 1'000u;

    // --- CREATE + SAVE ---
    {
        reset_pam_compat();
        pam_pmm_init( filename );
        pjson_db_pmm db;
        pam_pmm_reserve_slots( BENCH_N * 4 + 64u );

        char path[64];
        char value[64];
        for ( unsigned i = 0; i < BENCH_N; ++i )
        {
            std::snprintf( path, sizeof( path ), "/bench/k_%06u", i );
            switch ( i % 4 )
            {
            case 0:
                db.put( path, static_cast<int64_t>( i ) );
                break;
            case 1:
                std::snprintf( value, sizeof( value ), "val_%u", i );
                db.put( path, value );
                break;
            case 2:
                db.put( path, static_cast<double>( i ) * 0.1 );
                break;
            case 3:
                db.put( path, ( i % 2 ) == 0 );
                break;
            }
        }

        auto t0 = bench_clk::now();
        db.save();
        long long save_ms = std::chrono::duration_cast<std::chrono::milliseconds>( bench_clk::now() - t0 ).count();

        std::printf( "[compat_bench] save %u mixed nodes: %lld ms\n", BENCH_N, save_ms );

        pam_pmm_destroy();
    }

    // --- LOAD + VERIFY ---
    {
        reset_pam_compat();

        auto t0 = bench_clk::now();
        pam_pmm_init( filename );
        long long load_ms = std::chrono::duration_cast<std::chrono::milliseconds>( bench_clk::now() - t0 ).count();

        pjson_db_pmm db;

        std::printf( "[compat_bench] load %u mixed nodes: %lld ms\n", BENCH_N, load_ms );

        // Выборочная проверка.
        node_view v0 = db.get( "/bench/k_000000" );
        REQUIRE( v0.valid() );
        REQUIRE( v0.as_int() == 0 );

        node_view v1 = db.get( "/bench/k_000501" );
        REQUIRE( v1.valid() );
        REQUIRE( v1.is_string() );

        pam_pmm_destroy();
    }

    remove_db_file( filename );
}
