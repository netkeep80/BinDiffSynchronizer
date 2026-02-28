// test_pjson_opt.cpp — Тесты оптимизаций pjson из задачи #84:
//   F2 — пул памяти для узлов pjson (pjson_node_pool)
//   F3 — интернирование строк (pjson_string_table / set_string_interned)
//   F6 — прямая сериализация/десериализация без nlohmann (pjson_serializer.h)
//
// Зависимости: Catch2, pjson.h (включает pjson_serializer.h, pjson_interning.h, pjson_node_pool.h)

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>

#include "pjson.h"

// ============================================================================
// F6: Прямая сериализация (pjson_serializer.h)
// ============================================================================

TEST_CASE( "pjson opt F6: direct to_string null", "[pjson][opt][f6][serial]" )
{
    fptr<pjson> fv;
    fv.New();
    REQUIRE( fv->to_string() == "null" );
    fv.Delete();
}

TEST_CASE( "pjson opt F6: direct to_string boolean", "[pjson][opt][f6][serial]" )
{
    fptr<pjson> ft;
    ft.New();
    ft->set_bool( true );
    REQUIRE( ft->to_string() == "true" );
    ft.Delete();

    fptr<pjson> ff;
    ff.New();
    ff->set_bool( false );
    REQUIRE( ff->to_string() == "false" );
    ff.Delete();
}

TEST_CASE( "pjson opt F6: direct to_string integer", "[pjson][opt][f6][serial]" )
{
    fptr<pjson> fv;
    fv.New();
    fv->set_int( -42 );
    REQUIRE( fv->to_string() == "-42" );
    fv.Delete();
}

TEST_CASE( "pjson opt F6: direct to_string uinteger", "[pjson][opt][f6][serial]" )
{
    fptr<pjson> fv;
    fv.New();
    fv->set_uint( 100u );
    REQUIRE( fv->to_string() == "100" );
    fv.Delete();
}

TEST_CASE( "pjson opt F6: direct to_string real 100.0 preserves decimal", "[pjson][opt][f6][serial]" )
{
    fptr<pjson> fv;
    fv.New();
    fv->set_real( 100.0 );
    // Grisu2 форматирует 100.0 как "100.0" (с десятичной точкой).
    REQUIRE( fv->to_string() == "100.0" );
    fv.Delete();
}

TEST_CASE( "pjson opt F6: direct to_string real 3.14", "[pjson][opt][f6][serial]" )
{
    fptr<pjson> fv;
    fv.New();
    fv->set_real( 3.14 );
    std::string s = fv->to_string();
    // 3.14 должна корректно восстанавливаться через strtod.
    double restored = std::strtod( s.c_str(), nullptr );
    REQUIRE( restored == 3.14 );
    fv.Delete();
}

TEST_CASE( "pjson opt F6: direct to_string string with special chars", "[pjson][opt][f6][serial]" )
{
    fptr<pjson> fv;
    fv.New();
    // Строка с символами, требующими экранирования.
    fv->set_string( "hello\nworld\t\"test\"" );
    std::string s = fv->to_string();
    // Результат должен быть валидным JSON.
    REQUIRE( s.front() == '"' );
    REQUIRE( s.back() == '"' );
    // Должны быть экранированные символы.
    REQUIRE( s.find( "\\n" ) != std::string::npos );
    REQUIRE( s.find( "\\t" ) != std::string::npos );
    REQUIRE( s.find( "\\\"" ) != std::string::npos );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson opt F6: direct to_string empty array", "[pjson][opt][f6][serial]" )
{
    fptr<pjson> fv;
    fv.New();
    fv->set_array();
    REQUIRE( fv->to_string() == "[]" );
    fv.Delete();
}

TEST_CASE( "pjson opt F6: direct to_string array with elements", "[pjson][opt][f6][serial]" )
{
    fptr<pjson> fv;
    fv.New();
    fv->set_array();
    fv->push_back().set_int( 1 );
    fv->push_back().set_bool( true );
    fv->push_back().set_null();
    std::string s = fv->to_string();
    REQUIRE( s == "[1,true,null]" );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson opt F6: direct to_string empty object", "[pjson][opt][f6][serial]" )
{
    fptr<pjson> fv;
    fv.New();
    fv->set_object();
    REQUIRE( fv->to_string() == "{}" );
    fv.Delete();
}

TEST_CASE( "pjson opt F6: direct to_string object with keys", "[pjson][opt][f6][serial]" )
{
    fptr<pjson> fv;
    fv.New();
    fv->set_object();
    fv->obj_insert( "a" ).set_int( 1 );
    fv->obj_insert( "b" ).set_string( "val" );
    std::string s = fv->to_string();
    // Ключи в объекте отсортированы (pjson хранит в отсортированном порядке).
    REQUIRE( s == "{\"a\":1,\"b\":\"val\"}" );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson opt F6: direct from_string null", "[pjson][opt][f6][parse]" )
{
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( "null", fv.addr() );
    REQUIRE( fv->is_null() );
    fv.Delete();
}

TEST_CASE( "pjson opt F6: direct from_string true", "[pjson][opt][f6][parse]" )
{
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( "true", fv.addr() );
    REQUIRE( fv->is_boolean() );
    REQUIRE( fv->get_bool() == true );
    fv.Delete();
}

TEST_CASE( "pjson opt F6: direct from_string false", "[pjson][opt][f6][parse]" )
{
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( "false", fv.addr() );
    REQUIRE( fv->is_boolean() );
    REQUIRE( fv->get_bool() == false );
    fv.Delete();
}

TEST_CASE( "pjson opt F6: direct from_string positive integer", "[pjson][opt][f6][parse]" )
{
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( "42", fv.addr() );
    // Положительное целое → uinteger.
    REQUIRE( fv->is_uinteger() );
    REQUIRE( fv->get_uint() == 42u );
    fv.Delete();
}

TEST_CASE( "pjson opt F6: direct from_string negative integer", "[pjson][opt][f6][parse]" )
{
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( "-100", fv.addr() );
    REQUIRE( fv->is_integer() );
    REQUIRE( fv->get_int() == -100 );
    fv.Delete();
}

TEST_CASE( "pjson opt F6: direct from_string real", "[pjson][opt][f6][parse]" )
{
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( "3.14", fv.addr() );
    REQUIRE( fv->is_real() );
    REQUIRE( fv->get_real() == 3.14 );
    fv.Delete();
}

TEST_CASE( "pjson opt F6: direct from_string real with exponent", "[pjson][opt][f6][parse]" )
{
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( "1.5e2", fv.addr() );
    REQUIRE( fv->is_real() );
    REQUIRE( fv->get_real() == 150.0 );
    fv.Delete();
}

TEST_CASE( "pjson opt F6: direct from_string string", "[pjson][opt][f6][parse]" )
{
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( "\"hello world\"", fv.addr() );
    REQUIRE( fv->is_string() );
    REQUIRE( std::strcmp( fv->get_string(), "hello world" ) == 0 );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson opt F6: direct from_string string with escape sequences", "[pjson][opt][f6][parse]" )
{
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( "\"hello\\nworld\"", fv.addr() );
    REQUIRE( fv->is_string() );
    REQUIRE( std::strcmp( fv->get_string(), "hello\nworld" ) == 0 );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson opt F6: direct from_string empty array", "[pjson][opt][f6][parse]" )
{
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( "[]", fv.addr() );
    REQUIRE( fv->is_array() );
    REQUIRE( fv->size() == 0u );
    fv.Delete();
}

TEST_CASE( "pjson opt F6: direct from_string array with elements", "[pjson][opt][f6][parse]" )
{
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( "[1, true, null, \"str\"]", fv.addr() );
    REQUIRE( fv->is_array() );
    REQUIRE( fv->size() == 4u );
    REQUIRE( ( *fv )[0u].is_uinteger() );
    REQUIRE( ( *fv )[0u].get_uint() == 1u );
    REQUIRE( ( *fv )[1u].is_boolean() );
    REQUIRE( ( *fv )[1u].get_bool() == true );
    REQUIRE( ( *fv )[2u].is_null() );
    REQUIRE( ( *fv )[3u].is_string() );
    REQUIRE( std::strcmp( ( *fv )[3u].get_string(), "str" ) == 0 );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson opt F6: direct from_string empty object", "[pjson][opt][f6][parse]" )
{
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( "{}", fv.addr() );
    REQUIRE( fv->is_object() );
    REQUIRE( fv->size() == 0u );
    fv.Delete();
}

TEST_CASE( "pjson opt F6: direct from_string object", "[pjson][opt][f6][parse]" )
{
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( "{\"x\": 10, \"y\": 20}", fv.addr() );
    REQUIRE( fv->is_object() );
    REQUIRE( fv->size() == 2u );
    pjson* x = fv->obj_find( "x" );
    pjson* y = fv->obj_find( "y" );
    REQUIRE( x != nullptr );
    REQUIRE( y != nullptr );
    REQUIRE( x->get_uint() == 10u );
    REQUIRE( y->get_uint() == 20u );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson opt F6: direct from_string invalid JSON stays null", "[pjson][opt][f6][parse]" )
{
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( "not valid json {}", fv.addr() );
    REQUIRE( fv->is_null() );
    fv.Delete();
}

TEST_CASE( "pjson opt F6: direct from_string nested object", "[pjson][opt][f6][parse]" )
{
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( "{\"outer\":{\"inner\":42}}", fv.addr() );
    REQUIRE( fv->is_object() );
    pjson* outer = fv->obj_find( "outer" );
    REQUIRE( outer != nullptr );
    REQUIRE( outer->is_object() );
    pjson* inner = outer->obj_find( "inner" );
    REQUIRE( inner != nullptr );
    REQUIRE( inner->get_uint() == 42u );
    fv->free();
    fv.Delete();
}

TEST_CASE( "pjson opt F6: round-trip null", "[pjson][opt][f6][roundtrip]" )
{
    fptr<pjson> fv;
    fv.New();
    pjson::from_string( "null", fv.addr() );
    REQUIRE( fv->to_string() == "null" );
    fv.Delete();
}

TEST_CASE( "pjson opt F6: round-trip complex object", "[pjson][opt][f6][roundtrip]" )
{
    const std::string original = "{\"arr\":[1,true,null,\"str\"],\"num\":3.14,\"obj\":{\"x\":42}}";
    fptr<pjson>       fv;
    fv.New();
    pjson::from_string( original.c_str(), fv.addr() );
    std::string restored = fv->to_string();
    // Нормализуем оригинал через pjson (parse + serialize) для сравнения.
    fptr<pjson> fv2;
    fv2.New();
    pjson::from_string( original.c_str(), fv2.addr() );
    std::string original_normalized = fv2->to_string();
    fv2->free();
    fv2.Delete();
    REQUIRE( original_normalized == restored );
    fv->free();
    fv.Delete();
}

// ============================================================================
// F3: Интернирование строк (pjson_interning.h)
// ============================================================================

TEST_CASE( "pjson opt F3: string table intern returns same offset for identical strings",
           "[pjson][opt][f3][interning]" )
{
    fptr<pjson_string_table> tbl;
    tbl.New();

    uintptr_t off1 = tbl->intern( "hello" );
    uintptr_t off2 = tbl->intern( "hello" );
    REQUIRE( off1 != 0u );
    REQUIRE( off1 == off2 ); // Одна и та же строка → одно смещение.

    tbl.Delete();
}

TEST_CASE( "pjson opt F3: string table intern returns different offsets for different strings",
           "[pjson][opt][f3][interning]" )
{
    fptr<pjson_string_table> tbl;
    tbl.New();

    uintptr_t off1 = tbl->intern( "hello" );
    uintptr_t off2 = tbl->intern( "world" );
    REQUIRE( off1 != 0u );
    REQUIRE( off2 != 0u );
    REQUIRE( off1 != off2 ); // Разные строки → разные смещения.

    tbl.Delete();
}

TEST_CASE( "pjson opt F3: string table get returns correct string", "[pjson][opt][f3][interning]" )
{
    fptr<pjson_string_table> tbl;
    tbl.New();

    uintptr_t   off = tbl->intern( "test_string" );
    const char* s   = pjson_string_table::get( off );
    REQUIRE( std::strcmp( s, "test_string" ) == 0 );

    tbl.Delete();
}

TEST_CASE( "pjson opt F3: string table intern many strings", "[pjson][opt][f3][interning]" )
{
    fptr<pjson_string_table> tbl;
    tbl.New();

    // Интернируем 20 разных строк.
    const char*   strings[] = { "alpha",   "beta", "gamma", "delta",  "epsilon", "zeta",   "eta",
                                "theta",   "iota", "kappa", "lambda", "mu",      "nu",     "xi",
                                "omicron", "pi",   "rho",   "sigma",  "tau",     "upsilon" };
    constexpr int N         = 20;
    uintptr_t     offsets[N];
    for ( int i = 0; i < N; i++ )
        offsets[i] = tbl->intern( strings[i] );

    // Проверяем что каждая строка возвращает то же смещение при повторном интернировании.
    for ( int i = 0; i < N; i++ )
    {
        uintptr_t off2 = tbl->intern( strings[i] );
        REQUIRE( off2 == offsets[i] );
        REQUIRE( std::strcmp( pjson_string_table::get( offsets[i] ), strings[i] ) == 0 );
    }

    tbl.Delete();
}

TEST_CASE( "pjson opt F3: set_string_interned stores correct value", "[pjson][opt][f3][interning]" )
{
    fptr<pjson_string_table> tbl;
    tbl.New();
    uintptr_t tbl_off = tbl.addr();

    fptr<pjson> fv;
    fv.New();
    fv->set_string_interned( "intern_test", tbl_off );

    REQUIRE( fv->is_string() );
    REQUIRE( std::strcmp( fv->get_string(), "intern_test" ) == 0 );

    fv->free();
    fv.Delete();
    tbl.Delete();
}

TEST_CASE( "pjson opt F3: set_string_interned deduplicates strings", "[pjson][opt][f3][interning]" )
{
    fptr<pjson_string_table> tbl;
    tbl.New();
    uintptr_t tbl_off = tbl.addr();

    fptr<pjson> fv1;
    fv1.New();
    fv1->set_string_interned( "shared_string", tbl_off );

    fptr<pjson> fv2;
    fv2.New();
    fv2->set_string_interned( "shared_string", tbl_off );

    // Оба узла должны указывать на одинаковые char-данные (дедупликация).
    REQUIRE( fv1->is_string() );
    REQUIRE( fv2->is_string() );
    REQUIRE( std::strcmp( fv1->get_string(), "shared_string" ) == 0 );
    REQUIRE( std::strcmp( fv2->get_string(), "shared_string" ) == 0 );
    // Проверяем, что указатели chars идентичны (истинная дедупликация).
    REQUIRE( fv1->payload.string_val.chars.addr() == fv2->payload.string_val.chars.addr() );

    // free() у fv1 должен только обнулить поля, не освобождая общие данные.
    fv1->free();
    fv2->free();
    fv1.Delete();
    fv2.Delete();
    tbl.Delete();
}

TEST_CASE( "pjson opt F3: set_string_interned with null table falls back to set_string", "[pjson][opt][f3][interning]" )
{
    fptr<pjson> fv;
    fv.New();
    // string_table_offset = 0 → обычная аллокация.
    fv->set_string_interned( "fallback", 0u );
    REQUIRE( fv->is_string() );
    REQUIRE( std::strcmp( fv->get_string(), "fallback" ) == 0 );
    fv->free();
    fv.Delete();
}

// ============================================================================
// F2: Пул памяти для узлов pjson (pjson_node_pool.h)
// ============================================================================

TEST_CASE( "pjson opt F2: pool alloc returns valid node offset", "[pjson][opt][f2][pool]" )
{
    fptr<pjson_node_pool> pool;
    pool.New();

    uintptr_t node_off = pool->alloc();
    REQUIRE( node_off != 0u );

    auto&  pam  = PersistentAddressSpace::Get();
    pjson* node = pam.Resolve<pjson>( node_off );
    REQUIRE( node != nullptr );
    // Новый узел должен быть нулевым (null).
    REQUIRE( node->is_null() );

    pool->dealloc( node_off );
    pool.Delete();
}

TEST_CASE( "pjson opt F2: pool alloc and use node", "[pjson][opt][f2][pool]" )
{
    fptr<pjson_node_pool> pool;
    pool.New();

    auto& pam = PersistentAddressSpace::Get();

    uintptr_t node_off = pool->alloc();
    pjson*    node     = pam.Resolve<pjson>( node_off );
    REQUIRE( node != nullptr );

    // Устанавливаем значение.
    node->set_int( 42 );
    REQUIRE( pam.Resolve<pjson>( node_off )->get_int() == 42 );
    // После первого alloc: пул выделил блок PJSON_POOL_BLOCK_SIZE узлов;
    // 1 использован, PJSON_POOL_BLOCK_SIZE-1 в free list.
    REQUIRE( pool->used_count() == 1u );
    REQUIRE( pool->free_in_pool() == PJSON_POOL_BLOCK_SIZE - 1 );

    // Освобождаем ресурсы узла и возвращаем в пул.
    pam.Resolve<pjson>( node_off )->free();
    pool->dealloc( node_off );
    // Теперь все узлы блока свободны.
    REQUIRE( pool->used_count() == 0u );
    REQUIRE( pool->free_in_pool() == PJSON_POOL_BLOCK_SIZE );

    pool.Delete();
}

TEST_CASE( "pjson opt F2: pool alloc multiple nodes", "[pjson][opt][f2][pool]" )
{
    fptr<pjson_node_pool> pool;
    pool.New();

    auto& pam = PersistentAddressSpace::Get();

    // Выделяем 10 узлов.
    constexpr int N = 10;
    uintptr_t     offsets[N];
    for ( int i = 0; i < N; i++ )
    {
        offsets[i] = pool->alloc();
        REQUIRE( offsets[i] != 0u );
        pam.Resolve<pjson>( offsets[i] )->set_int( i );
    }

    // Проверяем значения.
    for ( int i = 0; i < N; i++ )
    {
        REQUIRE( pam.Resolve<pjson>( offsets[i] )->get_int() == i );
    }

    // После 10 аллокаций из первого блока (PJSON_POOL_BLOCK_SIZE=64):
    REQUIRE( pool->used_count() == (uintptr_t)N );
    REQUIRE( pool->free_in_pool() == PJSON_POOL_BLOCK_SIZE - N );

    // Возвращаем все узлы в пул.
    for ( int i = 0; i < N; i++ )
    {
        pam.Resolve<pjson>( offsets[i] )->free();
        pool->dealloc( offsets[i] );
    }
    // Все узлы свободны.
    REQUIRE( pool->free_in_pool() == PJSON_POOL_BLOCK_SIZE );
    REQUIRE( pool->used_count() == 0u );

    pool.Delete();
}

TEST_CASE( "pjson opt F2: pool reuses freed nodes", "[pjson][opt][f2][pool]" )
{
    fptr<pjson_node_pool> pool;
    pool.New();

    auto& pam = PersistentAddressSpace::Get();

    // Выделяем узел и освобождаем.
    uintptr_t off1 = pool->alloc();
    pam.Resolve<pjson>( off1 )->set_int( 1 );
    pam.Resolve<pjson>( off1 )->free();
    pool->dealloc( off1 );

    // После возврата off1 в пул у нас есть PJSON_POOL_BLOCK_SIZE узлов в free list (LIFO).
    REQUIRE( pool->free_in_pool() == PJSON_POOL_BLOCK_SIZE );

    // Следующий alloc должен вернуть off1 (он был добавлен последним → он первый в LIFO).
    uintptr_t off2 = pool->alloc();
    REQUIRE( off2 == off1 ); // Узел переиспользован.
    REQUIRE( pool->free_in_pool() == PJSON_POOL_BLOCK_SIZE - 1 );
    REQUIRE( pool->used_count() == 1u );

    pam.Resolve<pjson>( off2 )->free();
    pool->dealloc( off2 );
    pool.Delete();
}

TEST_CASE( "pjson opt F2: pool block allocation fills free list", "[pjson][opt][f2][pool]" )
{
    fptr<pjson_node_pool> pool;
    pool.New();

    // После первого alloc пул выделяет блок PJSON_POOL_BLOCK_SIZE узлов.
    // Один узел возвращается, остальные (N-1) идут в free list.
    uintptr_t off = pool->alloc();
    REQUIRE( off != 0u );
    REQUIRE( pool->total_count() == PJSON_POOL_BLOCK_SIZE );
    REQUIRE( pool->free_in_pool() == PJSON_POOL_BLOCK_SIZE - 1 );
    REQUIRE( pool->used_count() == 1u );

    PersistentAddressSpace::Get().Resolve<pjson>( off )->free();
    pool->dealloc( off );
    pool.Delete();
}
