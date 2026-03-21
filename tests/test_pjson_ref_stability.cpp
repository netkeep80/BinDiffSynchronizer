// test_pjson_ref_stability.cpp — Тесты стабильности node_id ссылок после resize (Issue #194).
//
// Цель: убедиться, что node_id (смещение узла в ПАП) остаётся корректным
// после добавления новых элементов в json array и json object, даже когда
// внутренний parray выполняет реаллокацию (resize).
//
// Сценарий из Issue #194:
//   1. Создать json array на 10 элементов.
//   2. Взять node_id 5-го элемента и сохранить.
//   3. Добавить ещё элементов (вызвать resize).
//   4. Проверить, что сохранённый node_id всё ещё корректно разрешается.
//
// Аналогичные тесты для json object.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "pjson_node.h"

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
// Стабильность node_id ссылок в json array после resize
// =============================================================================

TEST_CASE( "pjson ref stability: node_id remains valid after array resize", "[pjson][ref_stability][array]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_array( fn.addr() );

    // Создаём 10 элементов.
    std::vector<node_id> saved_ids;
    for ( int i = 0; i < 10; i++ )
    {
        node_id slot = node_array_push_back( fn.addr() );
        node_set_int( slot, i * 100 );
        saved_ids.push_back( slot );
    }

    // Сохраняем node_id 5-го элемента (как в сценарии из Issue #194).
    node_id saved_5th = saved_ids[4];

    // Проверяем, что 5-й элемент корректен до resize.
    REQUIRE( node_view{ saved_5th }.is_integer() );
    REQUIRE( node_view{ saved_5th }.as_int() == 400 );

    // Добавляем ещё 50 элементов — гарантированный многократный resize parray.
    for ( int i = 10; i < 60; i++ )
    {
        node_id slot = node_array_push_back( fn.addr() );
        node_set_int( slot, i * 100 );
    }

    // Проверяем, что сохранённый node_id 5-го элемента всё ещё корректен.
    REQUIRE( node_view{ saved_5th }.is_integer() );
    REQUIRE( node_view{ saved_5th }.as_int() == 400 );

    // Проверяем, что ВСЕ ранее сохранённые node_id корректны.
    for ( int i = 0; i < 10; i++ )
    {
        node_view v{ saved_ids[static_cast<size_t>( i )] };
        REQUIRE( v.is_integer() );
        REQUIRE( v.as_int() == i * 100 );
    }

    // Проверяем через индексацию массива — те же значения.
    node_view arr{ fn.addr() };
    REQUIRE( arr.size() == 60u );
    for ( uintptr_t i = 0; i < 60u; i++ )
    {
        REQUIRE( arr.at( i ).is_integer() );
        REQUIRE( arr.at( i ).as_int() == static_cast<int64_t>( i ) * 100 );
    }

    fn.Delete();
}

TEST_CASE( "pjson ref stability: mixed types in array preserve node_id after resize", "[pjson][ref_stability][array]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_array( fn.addr() );

    // Создаём элементы разных типов.
    node_id id_null = node_array_push_back( fn.addr() );
    // id_null остаётся null (по умолчанию).

    node_id id_bool = node_array_push_back( fn.addr() );
    node_set_bool( id_bool, true );

    node_id id_int = node_array_push_back( fn.addr() );
    node_set_int( id_int, -42 );

    node_id id_uint = node_array_push_back( fn.addr() );
    node_set_uint( id_uint, 999u );

    node_id id_real = node_array_push_back( fn.addr() );
    node_set_real( id_real, 3.14 );

    node_id id_str = node_array_push_back( fn.addr() );
    node_set_string( id_str, "hello" );

    // Добавляем ещё 100 элементов — вызываем многократный resize.
    for ( int i = 0; i < 100; i++ )
    {
        node_id slot = node_array_push_back( fn.addr() );
        node_set_int( slot, i );
    }

    // Проверяем, что все ранее сохранённые node_id корректны.
    REQUIRE( node_view{ id_null }.is_null() );

    REQUIRE( node_view{ id_bool }.is_boolean() );
    REQUIRE( node_view{ id_bool }.as_bool() == true );

    REQUIRE( node_view{ id_int }.is_integer() );
    REQUIRE( node_view{ id_int }.as_int() == -42 );

    REQUIRE( node_view{ id_uint }.is_uinteger() );
    REQUIRE( node_view{ id_uint }.as_uint() == 999u );

    REQUIRE( node_view{ id_real }.is_real() );
    REQUIRE( node_view{ id_real }.as_double() == 3.14 );

    REQUIRE( node_view{ id_str }.is_string() );
    REQUIRE( std::string( node_view{ id_str }.as_string() ) == "hello" );

    fn.Delete();
}

// =============================================================================
// Стабильность node_id ссылок в json object после resize
// =============================================================================

TEST_CASE( "pjson ref stability: node_id remains valid after object resize", "[pjson][ref_stability][object]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_object( fn.addr() );

    // Создаём 10 ключей.
    std::vector<node_id> saved_ids;
    for ( int i = 0; i < 10; i++ )
    {
        std::string key  = "key_" + std::to_string( i );
        node_id     slot = node_object_insert( fn.addr(), key.c_str() );
        node_set_int( slot, i * 100 );
        saved_ids.push_back( slot );
    }

    // Сохраняем node_id для key_4.
    node_id saved_key4 = saved_ids[4];

    // Проверяем, что key_4 корректен до resize.
    REQUIRE( node_view{ saved_key4 }.is_integer() );
    REQUIRE( node_view{ saved_key4 }.as_int() == 400 );

    // Добавляем ещё 50 ключей — вызываем многократный resize.
    for ( int i = 10; i < 60; i++ )
    {
        std::string key  = "key_" + std::to_string( i );
        node_id     slot = node_object_insert( fn.addr(), key.c_str() );
        node_set_int( slot, i * 100 );
    }

    // Проверяем, что сохранённый node_id для key_4 всё ещё корректен.
    REQUIRE( node_view{ saved_key4 }.is_integer() );
    REQUIRE( node_view{ saved_key4 }.as_int() == 400 );

    // Проверяем, что ВСЕ ранее сохранённые node_id корректны.
    for ( int i = 0; i < 10; i++ )
    {
        node_view v{ saved_ids[static_cast<size_t>( i )] };
        REQUIRE( v.is_integer() );
        REQUIRE( v.as_int() == i * 100 );
    }

    // Проверяем доступ через ключ — те же значения.
    node_view obj{ fn.addr() };
    REQUIRE( obj.size() == 60u );
    for ( int i = 0; i < 60; i++ )
    {
        std::string key = "key_" + std::to_string( i );
        node_view   v   = obj.at( key.c_str() );
        REQUIRE( v.is_integer() );
        REQUIRE( v.as_int() == i * 100 );
    }

    fn.Delete();
}

// =============================================================================
// Сценарий из Issue #194: сохранить указатель, добавить элементы, проверить
// =============================================================================

TEST_CASE( "pjson ref stability: Issue #194 scenario — save ref, grow array, verify",
           "[pjson][ref_stability][array][issue194]" )
{
    reset_pam();

    // 1. Создаём json array на 10 элементов.
    fptr<node> fn;
    fn.New();
    node_set_array( fn.addr() );

    std::vector<node_id> ids;
    for ( int i = 0; i < 10; i++ )
    {
        node_id slot = node_array_push_back( fn.addr() );
        node_set_int( slot, i );
        ids.push_back( slot );
    }

    // 2. Сохраняем node_id 5-го элемента (индекс 4) — «указатель в контексте jsonRVM».
    node_id context_ref = ids[4];

    // 3. Проверяем корректность перед resize.
    REQUIRE( node_view{ context_ref }.as_int() == 4 );

    // 4. Добавляем ещё 200 элементов — гарантированно вызовет многократный resize
    //    внутреннего parray<node_id> (начальная ёмкость = 4).
    for ( int i = 10; i < 210; i++ )
    {
        node_id slot = node_array_push_back( fn.addr() );
        node_set_int( slot, i );
    }

    // 5. Проверяем, что сохранённый «указатель» (node_id) всё ещё корректен.
    REQUIRE( node_view{ context_ref }.is_integer() );
    REQUIRE( node_view{ context_ref }.as_int() == 4 );

    // 6. Можем даже модифицировать значение через сохранённый node_id.
    node_set_int( context_ref, 999 );
    REQUIRE( node_view{ context_ref }.as_int() == 999 );

    // 7. И через индексацию массива видим обновлённое значение.
    node_view arr{ fn.addr() };
    REQUIRE( arr.at( (uintptr_t)4 ).as_int() == 999 );

    fn.Delete();
}

TEST_CASE( "pjson ref stability: Issue #194 scenario — save ref, grow object, verify",
           "[pjson][ref_stability][object][issue194]" )
{
    reset_pam();

    // 1. Создаём json object с 10 ключами.
    fptr<node> fn;
    fn.New();
    node_set_object( fn.addr() );

    std::vector<node_id> ids;
    for ( int i = 0; i < 10; i++ )
    {
        std::string key  = "field_" + std::to_string( i );
        node_id     slot = node_object_insert( fn.addr(), key.c_str() );
        node_set_int( slot, i );
        ids.push_back( slot );
    }

    // 2. Сохраняем node_id для field_4.
    node_id context_ref = ids[4];

    // 3. Проверяем корректность перед resize.
    REQUIRE( node_view{ context_ref }.as_int() == 4 );

    // 4. Добавляем ещё 200 ключей — гарантированно вызовет многократный resize.
    for ( int i = 10; i < 210; i++ )
    {
        std::string key  = "field_" + std::to_string( i );
        node_id     slot = node_object_insert( fn.addr(), key.c_str() );
        node_set_int( slot, i );
    }

    // 5. Проверяем, что сохранённый «указатель» (node_id) всё ещё корректен.
    REQUIRE( node_view{ context_ref }.is_integer() );
    REQUIRE( node_view{ context_ref }.as_int() == 4 );

    // 6. Модифицируем через сохранённый node_id.
    node_set_int( context_ref, 777 );
    REQUIRE( node_view{ context_ref }.as_int() == 777 );

    // 7. И через ключ объекта видим обновлённое значение.
    node_view obj{ fn.addr() };
    REQUIRE( obj.at( "field_4" ).as_int() == 777 );

    fn.Delete();
}

// =============================================================================
// Стабильность node_id при вложенных структурах и resize
// =============================================================================

TEST_CASE( "pjson ref stability: nested array — inner refs survive outer resize",
           "[pjson][ref_stability][array][nested]" )
{
    reset_pam();

    // Внешний массив.
    fptr<node> outer;
    outer.New();
    node_set_array( outer.addr() );

    // Создаём внутренний массив в слоте 0.
    node_id inner_id = node_array_push_back( outer.addr() );
    node_set_array( inner_id );

    // Добавляем элементы во внутренний массив.
    node_id inner_elem = node_array_push_back( inner_id );
    node_set_int( inner_elem, 42 );

    // Сохраняем node_id элемента внутреннего массива.
    node_id saved_inner_elem = inner_elem;

    // Добавляем 100 элементов во внешний массив — resize внешнего parray.
    for ( int i = 0; i < 100; i++ )
    {
        node_id slot = node_array_push_back( outer.addr() );
        node_set_int( slot, i );
    }

    // Проверяем, что node_id внутреннего элемента всё ещё корректен.
    REQUIRE( node_view{ saved_inner_elem }.is_integer() );
    REQUIRE( node_view{ saved_inner_elem }.as_int() == 42 );

    // Внутренний массив тоже доступен через внешний.
    node_view outer_v{ outer.addr() };
    node_view inner_v = outer_v.at( (uintptr_t)0 );
    REQUIRE( inner_v.is_array() );
    REQUIRE( inner_v.size() == 1u );
    REQUIRE( inner_v.at( (uintptr_t)0 ).as_int() == 42 );

    outer.Delete();
}

TEST_CASE( "pjson ref stability: object inside array — refs survive resize", "[pjson][ref_stability][nested]" )
{
    reset_pam();

    // Массив с объектом внутри.
    fptr<node> arr;
    arr.New();
    node_set_array( arr.addr() );

    node_id obj_id = node_array_push_back( arr.addr() );
    node_set_object( obj_id );

    node_id name_id = node_object_insert( obj_id, "name" );
    node_set_string( name_id, "test" );

    node_id age_id = node_object_insert( obj_id, "age" );
    node_set_int( age_id, 25 );

    // Сохраняем node_id полей объекта.
    node_id saved_name = name_id;
    node_id saved_age  = age_id;

    // Добавляем много элементов в массив — resize.
    for ( int i = 0; i < 100; i++ )
    {
        node_id slot = node_array_push_back( arr.addr() );
        node_set_int( slot, i );
    }

    // Добавляем много полей в объект — resize.
    for ( int i = 0; i < 50; i++ )
    {
        std::string key  = "extra_" + std::to_string( i );
        node_id     slot = node_object_insert( obj_id, key.c_str() );
        node_set_int( slot, i );
    }

    // Проверяем, что сохранённые node_id полей объекта корректны.
    REQUIRE( node_view{ saved_name }.is_string() );
    REQUIRE( std::string( node_view{ saved_name }.as_string() ) == "test" );

    REQUIRE( node_view{ saved_age }.is_integer() );
    REQUIRE( node_view{ saved_age }.as_int() == 25 );

    arr.Delete();
}

// =============================================================================
// Крупный тест: 1000 элементов — множественный resize
// =============================================================================

TEST_CASE( "pjson ref stability: 1000 array elements — all node_ids survive", "[pjson][ref_stability][array][large]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_array( fn.addr() );

    // Создаём 1000 элементов, сохраняя все node_id.
    std::vector<node_id> all_ids;
    all_ids.reserve( 1000 );
    for ( int i = 0; i < 1000; i++ )
    {
        node_id slot = node_array_push_back( fn.addr() );
        node_set_int( slot, i );
        all_ids.push_back( slot );
    }

    // Проверяем все 1000 node_id.
    for ( int i = 0; i < 1000; i++ )
    {
        node_view v{ all_ids[static_cast<size_t>( i )] };
        REQUIRE( v.is_integer() );
        REQUIRE( v.as_int() == i );
    }

    // Проверяем через индексацию массива.
    node_view arr{ fn.addr() };
    REQUIRE( arr.size() == 1000u );
    for ( uintptr_t i = 0; i < 1000u; i++ )
    {
        REQUIRE( arr.at( i ).as_int() == static_cast<int64_t>( i ) );
    }

    fn.Delete();
}

TEST_CASE( "pjson ref stability: 500 object keys — all node_ids survive", "[pjson][ref_stability][object][large]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_object( fn.addr() );

    // Создаём 500 ключей, сохраняя все node_id.
    std::vector<node_id>     all_ids;
    std::vector<std::string> all_keys;
    all_ids.reserve( 500 );
    all_keys.reserve( 500 );
    for ( int i = 0; i < 500; i++ )
    {
        std::string key  = "k" + std::to_string( i );
        node_id     slot = node_object_insert( fn.addr(), key.c_str() );
        node_set_int( slot, i );
        all_ids.push_back( slot );
        all_keys.push_back( key );
    }

    // Проверяем все 500 node_id по сохранённым идентификаторам.
    for ( int i = 0; i < 500; i++ )
    {
        node_view v{ all_ids[static_cast<size_t>( i )] };
        REQUIRE( v.is_integer() );
        REQUIRE( v.as_int() == i );
    }

    // Проверяем через ключи объекта.
    node_view obj{ fn.addr() };
    REQUIRE( obj.size() == 500u );
    for ( int i = 0; i < 500; i++ )
    {
        node_view v = obj.at( all_keys[static_cast<size_t>( i )].c_str() );
        REQUIRE( v.is_integer() );
        REQUIRE( v.as_int() == i );
    }

    fn.Delete();
}

// =============================================================================
// Тест: модификация через сохранённый node_id после resize
// =============================================================================

TEST_CASE( "pjson ref stability: modify value via saved node_id after array resize",
           "[pjson][ref_stability][array][modify]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_array( fn.addr() );

    // Создаём элементы.
    node_id target = node_array_push_back( fn.addr() );
    node_set_int( target, 0 );

    for ( int i = 1; i < 50; i++ )
    {
        node_id slot = node_array_push_back( fn.addr() );
        node_set_int( slot, i );
    }

    // Модифицируем через сохранённый node_id.
    node_set_string( target, "modified" );

    // Проверяем, что значение обновлено.
    REQUIRE( node_view{ target }.is_string() );
    REQUIRE( std::string( node_view{ target }.as_string() ) == "modified" );

    // Проверяем через индексацию массива.
    node_view arr{ fn.addr() };
    REQUIRE( arr.at( (uintptr_t)0 ).is_string() );
    REQUIRE( std::string( arr.at( (uintptr_t)0 ).as_string() ) == "modified" );

    fn.Delete();
}

TEST_CASE( "pjson ref stability: modify value via saved node_id after object resize",
           "[pjson][ref_stability][object][modify]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_object( fn.addr() );

    node_id target = node_object_insert( fn.addr(), "target" );
    node_set_int( target, 0 );

    for ( int i = 1; i < 50; i++ )
    {
        std::string key  = "extra_" + std::to_string( i );
        node_id     slot = node_object_insert( fn.addr(), key.c_str() );
        node_set_int( slot, i );
    }

    // Модифицируем через сохранённый node_id.
    node_set_string( target, "updated" );

    // Проверяем, что значение обновлено.
    REQUIRE( node_view{ target }.is_string() );
    REQUIRE( std::string( node_view{ target }.as_string() ) == "updated" );

    // Проверяем через ключ объекта.
    node_view obj{ fn.addr() };
    REQUIRE( obj.at( "target" ).is_string() );
    REQUIRE( std::string( obj.at( "target" ).as_string() ) == "updated" );

    fn.Delete();
}
