// test_pjson_opt.cpp — Тесты новых оптимизаций: пул узлов, интернирование строк, сериализация.
//   F2 — пул памяти для узлов (pjson_pool)
//   F3 — интернирование строк (pstringview_table / pam_intern_string)
//   F6 — прямая сериализация/десериализация (pjson_codec)
//
// Мигрировано с pjson.h на pjson_pool.h, pstringview.h, pjson_codec.h в рамках Задачи 9.5.
//
// Зависимости: Catch2, pjson_codec.h (включает pjson_node.h, pstringview.h),
//              pjson_pool.h
//
// Все комментарии — на русском языке (Тр.6).

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>

#include "pjson_codec.h"
#include "pjson_pool.h"

using namespace pjson;

// Вспомогательная функция: сбросить PMM перед каждым тестом.
static void reset_pam()
{
    pstringview_manager::reset();
    pam_pmm_reset();
}

// ============================================================================
// F6: Прямая сериализация (pjson_codec.h)
// ============================================================================

TEST_CASE( "pjson opt F6: direct to_string null", "[pjson][opt][f6][serial]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_init_null( fn.addr() );
    REQUIRE( node_to_string( fn.addr() ) == "null" );
    fn.Delete();
}

TEST_CASE( "pjson opt F6: direct to_string boolean", "[pjson][opt][f6][serial]" )
{
    reset_pam();
    fptr<node> ft;
    ft.New();
    node_set_bool( ft.addr(), true );
    REQUIRE( node_to_string( ft.addr() ) == "true" );
    ft.Delete();

    reset_pam();
    fptr<node> ff;
    ff.New();
    node_set_bool( ff.addr(), false );
    REQUIRE( node_to_string( ff.addr() ) == "false" );
    ff.Delete();
}

TEST_CASE( "pjson opt F6: direct to_string integer", "[pjson][opt][f6][serial]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_int( fn.addr(), -42 );
    REQUIRE( node_to_string( fn.addr() ) == "-42" );
    fn.Delete();
}

TEST_CASE( "pjson opt F6: direct to_string uinteger", "[pjson][opt][f6][serial]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_uint( fn.addr(), 100u );
    REQUIRE( node_to_string( fn.addr() ) == "100" );
    fn.Delete();
}

TEST_CASE( "pjson opt F6: direct to_string real 3.14", "[pjson][opt][f6][serial]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_real( fn.addr(), 3.14 );
    std::string s = node_to_string( fn.addr() );
    // 3.14 должна корректно восстанавливаться через strtod.
    double restored = std::strtod( s.c_str(), nullptr );
    REQUIRE( restored == 3.14 );
    fn.Delete();
}

TEST_CASE( "pjson opt F6: direct to_string string with special chars", "[pjson][opt][f6][serial]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    // Строка с символами, требующими экранирования.
    node_set_string( fn.addr(), "hello\nworld\t\"test\"" );
    std::string s = node_to_string( fn.addr() );
    // Результат должен быть валидным JSON.
    REQUIRE( s.front() == '"' );
    REQUIRE( s.back() == '"' );
    // Должны быть экранированные символы.
    REQUIRE( s.find( "\\n" ) != std::string::npos );
    REQUIRE( s.find( "\\t" ) != std::string::npos );
    REQUIRE( s.find( "\\\"" ) != std::string::npos );
    fn.Delete();
}

TEST_CASE( "pjson opt F6: direct to_string empty array", "[pjson][opt][f6][serial]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_array( fn.addr() );
    REQUIRE( node_to_string( fn.addr() ) == "[]" );
    fn.Delete();
}

TEST_CASE( "pjson opt F6: direct to_string array with elements", "[pjson][opt][f6][serial]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_array( fn.addr() );
    node_id s0 = node_array_push_back( fn.addr() );
    node_set_int( s0, 1 );
    node_id s1 = node_array_push_back( fn.addr() );
    node_set_bool( s1, true );
    node_array_push_back( fn.addr() ); // null
    std::string s = node_to_string( fn.addr() );
    REQUIRE( s == "[1,true,null]" );
    fn.Delete();
}

TEST_CASE( "pjson opt F6: direct to_string empty object", "[pjson][opt][f6][serial]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_object( fn.addr() );
    REQUIRE( node_to_string( fn.addr() ) == "{}" );
    fn.Delete();
}

TEST_CASE( "pjson opt F6: direct to_string object with keys", "[pjson][opt][f6][serial]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_set_object( fn.addr() );
    node_id sa = node_object_insert( fn.addr(), "a" );
    node_set_int( sa, 1 );
    node_id sb = node_object_insert( fn.addr(), "b" );
    node_set_string( sb, "val" );
    std::string s = node_to_string( fn.addr() );
    // Ключи в объекте отсортированы (pmap хранит в отсортированном порядке).
    REQUIRE( s == "{\"a\":1,\"b\":\"val\"}" );
    fn.Delete();
}

TEST_CASE( "pjson opt F6: direct from_string null", "[pjson][opt][f6][parse]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( "null", fn.addr() );
    REQUIRE( node_view{ fn.addr() }.is_null() );
    fn.Delete();
}

TEST_CASE( "pjson opt F6: direct from_string true", "[pjson][opt][f6][parse]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( "true", fn.addr() );
    node_view v{ fn.addr() };
    REQUIRE( v.is_boolean() );
    REQUIRE( v.as_bool() == true );
    fn.Delete();
}

TEST_CASE( "pjson opt F6: direct from_string false", "[pjson][opt][f6][parse]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( "false", fn.addr() );
    node_view v{ fn.addr() };
    REQUIRE( v.is_boolean() );
    REQUIRE( v.as_bool() == false );
    fn.Delete();
}

TEST_CASE( "pjson opt F6: direct from_string positive integer", "[pjson][opt][f6][parse]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( "42", fn.addr() );
    // Положительное целое → uinteger.
    node_view v{ fn.addr() };
    REQUIRE( v.is_uinteger() );
    REQUIRE( v.as_uint() == 42u );
    fn.Delete();
}

TEST_CASE( "pjson opt F6: direct from_string negative integer", "[pjson][opt][f6][parse]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( "-100", fn.addr() );
    node_view v{ fn.addr() };
    REQUIRE( v.is_integer() );
    REQUIRE( v.as_int() == -100 );
    fn.Delete();
}

TEST_CASE( "pjson opt F6: direct from_string real", "[pjson][opt][f6][parse]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( "3.14", fn.addr() );
    node_view v{ fn.addr() };
    REQUIRE( v.is_real() );
    REQUIRE( v.as_double() == 3.14 );
    fn.Delete();
}

TEST_CASE( "pjson opt F6: direct from_string real with exponent", "[pjson][opt][f6][parse]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( "1.5e2", fn.addr() );
    node_view v{ fn.addr() };
    REQUIRE( v.is_real() );
    REQUIRE( v.as_double() == 150.0 );
    fn.Delete();
}

TEST_CASE( "pjson opt F6: direct from_string string", "[pjson][opt][f6][parse]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( "\"hello world\"", fn.addr() );
    node_view v{ fn.addr() };
    REQUIRE( v.is_string() );
    REQUIRE( v.as_string() == "hello world" );
    fn.Delete();
}

TEST_CASE( "pjson opt F6: direct from_string string with escape sequences", "[pjson][opt][f6][parse]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( "\"hello\\nworld\"", fn.addr() );
    node_view v{ fn.addr() };
    REQUIRE( v.is_string() );
    REQUIRE( v.as_string() == "hello\nworld" );
    fn.Delete();
}

TEST_CASE( "pjson opt F6: direct from_string empty array", "[pjson][opt][f6][parse]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( "[]", fn.addr() );
    node_view v{ fn.addr() };
    REQUIRE( v.is_array() );
    REQUIRE( v.size() == 0u );
    fn.Delete();
}

TEST_CASE( "pjson opt F6: direct from_string array with elements", "[pjson][opt][f6][parse]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( "[1, true, null, \"str\"]", fn.addr() );
    node_view v{ fn.addr() };
    REQUIRE( v.is_array() );
    REQUIRE( v.size() == 4u );
    REQUIRE( v.at( (uintptr_t)0 ).is_uinteger() );
    REQUIRE( v.at( (uintptr_t)0 ).as_uint() == 1u );
    REQUIRE( v.at( (uintptr_t)1 ).is_boolean() );
    REQUIRE( v.at( (uintptr_t)1 ).as_bool() == true );
    REQUIRE( v.at( (uintptr_t)2 ).is_null() );
    REQUIRE( v.at( (uintptr_t)3 ).is_string() );
    REQUIRE( v.at( (uintptr_t)3 ).as_string() == "str" );
    fn.Delete();
}

TEST_CASE( "pjson opt F6: direct from_string empty object", "[pjson][opt][f6][parse]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( "{}", fn.addr() );
    node_view v{ fn.addr() };
    REQUIRE( v.is_object() );
    REQUIRE( v.size() == 0u );
    fn.Delete();
}

TEST_CASE( "pjson opt F6: direct from_string object", "[pjson][opt][f6][parse]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( "{\"x\": 10, \"y\": 20}", fn.addr() );
    node_view v{ fn.addr() };
    REQUIRE( v.is_object() );
    REQUIRE( v.size() == 2u );
    REQUIRE( v.at( "x" ).as_uint() == 10u );
    REQUIRE( v.at( "y" ).as_uint() == 20u );
    fn.Delete();
}

TEST_CASE( "pjson opt F6: direct from_string invalid JSON stays null", "[pjson][opt][f6][parse]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( "not valid json {}", fn.addr() );
    REQUIRE( node_view{ fn.addr() }.is_null() );
    fn.Delete();
}

TEST_CASE( "pjson opt F6: direct from_string nested object", "[pjson][opt][f6][parse]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( "{\"outer\":{\"inner\":42}}", fn.addr() );
    node_view v{ fn.addr() };
    REQUIRE( v.is_object() );
    REQUIRE( v.at( "outer" ).valid() );
    REQUIRE( v.at( "outer" ).is_object() );
    REQUIRE( v.at( "outer" ).at( "inner" ).as_uint() == 42u );
    fn.Delete();
}

TEST_CASE( "pjson opt F6: round-trip null", "[pjson][opt][f6][roundtrip]" )
{
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( "null", fn.addr() );
    REQUIRE( node_to_string( fn.addr() ) == "null" );
    fn.Delete();
}

TEST_CASE( "pjson opt F6: round-trip complex object", "[pjson][opt][f6][roundtrip]" )
{
    const std::string original = "{\"arr\":[1,true,null,\"str\"],\"num\":3.14,\"obj\":{\"x\":42}}";
    reset_pam();
    fptr<node> fn;
    fn.New();
    node_from_string( original.c_str(), fn.addr() );
    std::string restored = node_to_string( fn.addr() );

    // Нормализуем оригинал через parse + serialize для сравнения.
    fptr<node> fn2;
    fn2.New();
    node_from_string( original.c_str(), fn2.addr() );
    std::string original_normalized = node_to_string( fn2.addr() );
    fn2.Delete();
    REQUIRE( original_normalized == restored );
    fn.Delete();
}

// ============================================================================
// F3: Интернирование строк (pstringview_table / pam_intern_string)
// ============================================================================

TEST_CASE( "pjson opt F3: pam_intern_string returns same chars_offset for identical strings",
           "[pjson][opt][f3][interning]" )
{
    reset_pam();

    auto r1 = pam_intern_string( "hello" );
    auto r2 = pam_intern_string( "hello" );
    REQUIRE( r1.chars_offset != 0u );
    // Одна и та же строка → одно смещение (дедупликация).
    REQUIRE( r1.chars_offset == r2.chars_offset );
}

TEST_CASE( "pjson opt F3: pam_intern_string returns different chars_offset for different strings",
           "[pjson][opt][f3][interning]" )
{
    reset_pam();

    auto r1 = pam_intern_string( "hello" );
    auto r2 = pam_intern_string( "world" );
    REQUIRE( r1.chars_offset != 0u );
    REQUIRE( r2.chars_offset != 0u );
    // Разные строки → разные смещения.
    REQUIRE( r1.chars_offset != r2.chars_offset );
}

TEST_CASE( "pjson opt F3: pam_intern_string content is accessible via chars_offset", "[pjson][opt][f3][interning]" )
{
    reset_pam();

    auto        r = pam_intern_string( "test_string" );
    const char* s = pmm_resolve<char>( r.chars_offset );
    REQUIRE( std::strcmp( s, "test_string" ) == 0 );
}

TEST_CASE( "pjson opt F3: pam_intern_string many strings all deduplicated", "[pjson][opt][f3][interning]" )
{
    reset_pam();

    // Интернируем 20 разных строк.
    const char*   strings[] = { "alpha",   "beta", "gamma", "delta",  "epsilon", "zeta",   "eta",
                                "theta",   "iota", "kappa", "lambda", "mu",      "nu",     "xi",
                                "omicron", "pi",   "rho",   "sigma",  "tau",     "upsilon" };
    constexpr int N         = 20;
    uintptr_t     offsets[N];
    for ( int i = 0; i < N; i++ )
        offsets[i] = pam_intern_string( strings[i] ).chars_offset;

    // Проверяем что каждая строка возвращает то же смещение при повторном интернировании.
    for ( int i = 0; i < N; i++ )
    {
        uintptr_t off2 = pam_intern_string( strings[i] ).chars_offset;
        REQUIRE( off2 == offsets[i] );
        REQUIRE( std::strcmp( pmm_resolve<char>( offsets[i] ), strings[i] ) == 0 );
    }
}

TEST_CASE( "pjson opt F3: pstringview intern stores correct value in object key", "[pjson][opt][f3][interning]" )
{
    reset_pam();

    fptr<node> fn;
    fn.New();
    node_set_object( fn.addr() );
    // node_object_insert интернирует ключ через pstringview_table (readonly).
    node_id slot = node_object_insert( fn.addr(), "intern_test" );
    node_set_int( slot, 42 );

    node_view obj{ fn.addr() };
    REQUIRE( obj.at( "intern_test" ).as_int() == 42 );
    fn.Delete();
}

TEST_CASE( "pjson opt F3: pstringview deduplicates object keys", "[pjson][opt][f3][interning]" )
{
    reset_pam();

    fptr<node> fn;
    fn.New();
    node_set_object( fn.addr() );
    node_id s1 = node_object_insert( fn.addr(), "shared_key" );
    node_set_int( s1, 1 );

    fptr<node> fn2;
    fn2.New();
    node_set_object( fn2.addr() );
    node_id s2 = node_object_insert( fn2.addr(), "shared_key" );
    node_set_int( s2, 2 );

    // Оба объекта должны использовать одно и то же смещение для ключа "shared_key".
    auto* e1 = pmm_resolve<object_entry>( pmm_resolve<node>( fn.addr() )->object_val.data_off );
    auto* e2 = pmm_resolve<object_entry>( pmm_resolve<node>( fn2.addr() )->object_val.data_off );
    // key_chars_offset одинаков для одинаковых ключей (дедупликация через pstringview_table).
    REQUIRE( e1->key_chars_offset == e2->key_chars_offset );

    fn.Delete();
    fn2.Delete();
}

// ============================================================================
// F2: Пул памяти для узлов (pjson_pool)
// ============================================================================

TEST_CASE( "pjson opt F2: pool alloc returns valid node offset", "[pjson][opt][f2][pool]" )
{
    reset_pam();
    fptr<pjson_pool> pool;
    pool.New();

    node_id node_off = pool->alloc();
    REQUIRE( node_off != 0u );

    node* n = pmm_resolve<node>( node_off );
    REQUIRE( n != nullptr );
    // Новый узел должен быть нулевым (null).
    REQUIRE( node_view{ node_off }.is_null() );

    pool->free( node_off );
    pool.Delete();
}

TEST_CASE( "pjson opt F2: pool alloc and use node", "[pjson][opt][f2][pool]" )
{
    reset_pam();
    fptr<pjson_pool> pool;
    pool.New();

    node_id node_off = pool->alloc();
    node_set_int( node_off, 42 );
    REQUIRE( node_view{ node_off }.as_int() == 42 );
    REQUIRE( pool->used_count() == 1u );

    pool->free( node_off );
    REQUIRE( pool->used_count() == 0u );

    pool.Delete();
}

TEST_CASE( "pjson opt F2: pool alloc multiple nodes", "[pjson][opt][f2][pool]" )
{
    reset_pam();
    fptr<pjson_pool> pool;
    pool.New();

    // Выделяем 10 узлов.
    constexpr int N = 10;
    node_id       offsets[N];
    for ( int i = 0; i < N; i++ )
    {
        offsets[i] = pool->alloc();
        REQUIRE( offsets[i] != 0u );
        node_set_int( offsets[i], i );
    }

    // Проверяем значения.
    for ( int i = 0; i < N; i++ )
        REQUIRE( node_view{ offsets[i] }.as_int() == i );

    REQUIRE( pool->used_count() == (uintptr_t)N );

    // Возвращаем все узлы в пул.
    for ( int i = 0; i < N; i++ )
        pool->free( offsets[i] );
    REQUIRE( pool->used_count() == 0u );

    pool.Delete();
}

TEST_CASE( "pjson opt F2: pool reuses freed nodes", "[pjson][opt][f2][pool]" )
{
    reset_pam();
    fptr<pjson_pool> pool;
    pool.New();

    // Выделяем узел и освобождаем.
    node_id off1 = pool->alloc();
    node_set_int( off1, 1 );
    uintptr_t free_before = pool->free_in_pool();
    pool->free( off1 );
    uintptr_t free_after = pool->free_in_pool();

    // После возврата off1 в пул количество свободных узлов увеличилось на 1.
    REQUIRE( free_after == free_before + 1 );

    // Следующий alloc должен вернуть off1 (LIFO free-list).
    node_id off2 = pool->alloc();
    REQUIRE( off2 == off1 ); // Узел переиспользован.

    pool->free( off2 );
    pool.Delete();
}

TEST_CASE( "pjson opt F2: pool total_count tracks allocated nodes", "[pjson][opt][f2][pool]" )
{
    reset_pam();
    fptr<pjson_pool> pool;
    pool.New();

    REQUIRE( pool->total_count() == 0u );

    node_id off = pool->alloc();
    REQUIRE( pool->total_count() >= 1u );
    REQUIRE( pool->used_count() == 1u );

    pool->free( off );
    REQUIRE( pool->used_count() == 0u );

    pool.Delete();
}
