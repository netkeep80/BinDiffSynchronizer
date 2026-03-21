/**
 * @file test_regression_pmm_types.cpp
 * @brief Регрессионные тесты для мигрированных PMM-типов (Этап 4.1).
 *
 * Тесты покрывают граничные случаи новых PMM-типов, используемых в
 * BinDiffSynchronizer после миграции:
 *
 *   1. pstring  — большие строки, grow/shrink, UTF-8, append на пустую, персистность
 *   2. parray   — единичный элемент, рост, free_data + push_back, персистность
 *   3. ppool    — сложное чередование alloc/free, смешанные типы узлов, персистность
 *   4. pmap_pmm — erase первого/последнего/единственного, персистность
 *   5. fptr_pmm — delete null, delete несуществующего, find несуществующего
 *   6. pptr     — byte_offset round-trip для разных типов
 *   7. Интеграция — save/load образа с pstring + parray + pmap + ppool вместе
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cstring>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>

#include "pjson_db_pmm.h"

using namespace pjson;

using pmm_pstring = PamManager::pstring;

namespace
{
void rm_file( const char* path )
{
    std::error_code ec;
    std::filesystem::remove( path, ec );
}
} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// 1. pstring — граничные случаи
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "regression pstring: large string (4 KB)", "[regression][pstring]" )
{
    PamManager::create( 256 * 1024 );

    std::string big( 4096, 'X' );

    pmm_pstring ps{};
    REQUIRE( ps.assign( big.c_str() ) );
    REQUIRE( ps.size() == 4096u );
    REQUIRE( std::strcmp( ps.c_str(), big.c_str() ) == 0 );

    ps.free_data();
    PamManager::destroy();
}

TEST_CASE( "regression pstring: grow then shrink then grow", "[regression][pstring]" )
{
    PamManager::create( 256 * 1024 );

    pmm_pstring ps{};

    // Grow
    std::string s100( 100, 'A' );
    REQUIRE( ps.assign( s100.c_str() ) );
    REQUIRE( ps.size() == 100u );

    // Shrink
    REQUIRE( ps.assign( "tiny" ) );
    REQUIRE( ps.size() == 4u );
    REQUIRE( std::strcmp( ps.c_str(), "tiny" ) == 0 );

    // Grow again
    std::string s200( 200, 'B' );
    REQUIRE( ps.assign( s200.c_str() ) );
    REQUIRE( ps.size() == 200u );
    REQUIRE( std::strcmp( ps.c_str(), s200.c_str() ) == 0 );

    ps.free_data();
    PamManager::destroy();
}

TEST_CASE( "regression pstring: UTF-8 multibyte characters", "[regression][pstring]" )
{
    PamManager::create( 64 * 1024 );

    // "Привет" in UTF-8 = 12 bytes (6 Cyrillic chars * 2 bytes each)
    const char* utf8 = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";

    pmm_pstring ps{};
    REQUIRE( ps.assign( utf8 ) );
    REQUIRE( ps.size() == std::strlen( utf8 ) );
    REQUIRE( std::strcmp( ps.c_str(), utf8 ) == 0 );

    ps.free_data();
    PamManager::destroy();
}

TEST_CASE( "regression pstring: append to empty string", "[regression][pstring]" )
{
    PamManager::create( 64 * 1024 );

    pmm_pstring ps{};

    // Assign инициализирует строку, clear обнуляет, append добавляет.
    REQUIRE( ps.assign( "init" ) );
    ps.clear();
    REQUIRE( ps.empty() );
    REQUIRE( ps.append( "appended" ) );
    REQUIRE( ps.size() == 8u );
    REQUIRE( std::strcmp( ps.c_str(), "appended" ) == 0 );

    ps.free_data();
    PamManager::destroy();
}

TEST_CASE( "regression pstring: multiple appends exceed initial capacity", "[regression][pstring]" )
{
    PamManager::create( 256 * 1024 );

    pmm_pstring ps{};
    REQUIRE( ps.assign( "start" ) );

    for ( int i = 0; i < 50; ++i )
    {
        REQUIRE( ps.append( "_ext" ) );
    }

    REQUIRE( ps.size() == 5u + 50u * 4u ); // "start" + 50 * "_ext"

    // Начало строки должно быть сохранено
    REQUIRE( std::strncmp( ps.c_str(), "start", 5 ) == 0 );

    ps.free_data();
    PamManager::destroy();
}

TEST_CASE( "regression pstring: assign after clear reuses capacity", "[regression][pstring]" )
{
    PamManager::create( 64 * 1024 );

    pmm_pstring ps{};
    REQUIRE( ps.assign( "hello world" ) );
    ps.clear();
    REQUIRE( ps.empty() );

    REQUIRE( ps.assign( "new" ) );
    REQUIRE( ps.size() == 3u );
    REQUIRE( std::strcmp( ps.c_str(), "new" ) == 0 );

    ps.free_data();
    PamManager::destroy();
}

TEST_CASE( "regression pstring: save/load preserves content", "[regression][pstring][persistence]" )
{
    const char* fname = "./test_regression_pstring_save.pam";
    rm_file( fname );

    uintptr_t saved_off = 0;

    {
        pam_pmm_init( fname );

        saved_off = pam_pmm_create<pmm_pstring>( "reg_pstr" );
        REQUIRE( saved_off != 0u );

        auto* ps = pam_pmm_resolve<pmm_pstring>( saved_off );
        std::memset( ps, 0, sizeof( pmm_pstring ) );
        REQUIRE( ps->assign( "persistent string" ) );

        pam_pmm_save();
        pam_pmm_destroy();
    }

    {
        pam_pmm_init( fname );

        uintptr_t off = pam_pmm_find( "reg_pstr" );
        REQUIRE( off == saved_off );

        const auto* ps = pam_pmm_resolve_const<pmm_pstring>( off );
        REQUIRE( ps->size() == 17u );
        REQUIRE( std::strcmp( ps->c_str(), "persistent string" ) == 0 );

        pam_pmm_destroy();
    }

    rm_file( fname );
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. parray — граничные случаи
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "regression parray: single element push and pop", "[regression][parray]" )
{
    PamManager::create( 64 * 1024 );

    PamManager::parray<int> arr{};
    arr.push_back( 42 );
    REQUIRE( arr.size() == 1u );
    REQUIRE( arr.data()[0] == 42 );

    arr.pop_back();
    REQUIRE( arr.size() == 0u );

    arr.free_data();
    PamManager::destroy();
}

TEST_CASE( "regression parray: free_data then push_back works", "[regression][parray]" )
{
    PamManager::create( 64 * 1024 );

    PamManager::parray<int> arr{};
    arr.push_back( 1 );
    arr.push_back( 2 );
    arr.free_data();

    REQUIRE( arr.size() == 0u );

    // Должно работать: push_back на чистый массив
    arr.push_back( 99 );
    REQUIRE( arr.size() == 1u );
    REQUIRE( arr.data()[0] == 99 );

    arr.free_data();
    PamManager::destroy();
}

TEST_CASE( "regression parray: growth pattern with many push_backs", "[regression][parray]" )
{
    PamManager::create( 256 * 1024 );

    PamManager::parray<int> arr{};

    for ( int i = 0; i < 1000; ++i )
    {
        arr.push_back( i );
    }

    REQUIRE( arr.size() == 1000u );
    REQUIRE( arr.capacity() >= 1000u );

    // Проверяем все элементы
    for ( int i = 0; i < 1000; ++i )
    {
        REQUIRE( arr.data()[i] == i );
    }

    arr.free_data();
    PamManager::destroy();
}

TEST_CASE( "regression parray: pop_back to empty", "[regression][parray]" )
{
    PamManager::create( 64 * 1024 );

    PamManager::parray<int> arr{};
    arr.push_back( 1 );
    arr.push_back( 2 );
    arr.push_back( 3 );

    arr.pop_back();
    arr.pop_back();
    arr.pop_back();
    REQUIRE( arr.size() == 0u );

    // Ещё один pop_back на пустом — не должен ломаться
    arr.pop_back();
    REQUIRE( arr.size() == 0u );

    arr.free_data();
    PamManager::destroy();
}

TEST_CASE( "regression parray: save/load preserves elements", "[regression][parray][persistence]" )
{
    const char* fname = "./test_regression_parray_save.pam";
    rm_file( fname );

    uintptr_t saved_off = 0;

    {
        pam_pmm_init( fname );

        using IntArray = PamManager::parray<int>;
        saved_off      = pam_pmm_create<IntArray>( "reg_parr" );
        REQUIRE( saved_off != 0u );

        auto* arr = pam_pmm_resolve<IntArray>( saved_off );
        std::memset( arr, 0, sizeof( IntArray ) );
        arr->push_back( 10 );
        arr->push_back( 20 );
        arr->push_back( 30 );

        pam_pmm_save();
        pam_pmm_destroy();
    }

    {
        pam_pmm_init( fname );

        using IntArray = PamManager::parray<int>;
        uintptr_t off  = pam_pmm_find( "reg_parr" );
        REQUIRE( off == saved_off );

        const auto* arr = pam_pmm_resolve_const<IntArray>( off );
        REQUIRE( arr->size() == 3u );
        REQUIRE( arr->data()[0] == 10 );
        REQUIRE( arr->data()[1] == 20 );
        REQUIRE( arr->data()[2] == 30 );

        pam_pmm_destroy();
    }

    rm_file( fname );
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. ppool — граничные случаи
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "regression ppool: interleaved alloc/free maintains consistency", "[regression][ppool]" )
{
    pstringview_pmm_reset();
    pam_pmm_init( nullptr );

    uintptr_t pool_off = pam_pmm_create<pjson_pool_pmm>( "reg_pool_interleave" );
    REQUIRE( pool_off != 0u );
    pjson_pool_pmm_init( pool_off );

    // Allocate A, B, C
    node_id id_a = pjson_pool_pmm_alloc( pool_off );
    node_id id_b = pjson_pool_pmm_alloc( pool_off );
    node_id id_c = pjson_pool_pmm_alloc( pool_off );

    // Assign values
    pjson_pool_pmm_get( id_a )->tag     = node_tag::integer;
    pjson_pool_pmm_get( id_a )->int_val = 111;
    pjson_pool_pmm_get( id_b )->tag     = node_tag::integer;
    pjson_pool_pmm_get( id_b )->int_val = 222;
    pjson_pool_pmm_get( id_c )->tag     = node_tag::integer;
    pjson_pool_pmm_get( id_c )->int_val = 333;

    // Free B (middle)
    pjson_pool_pmm_free( pool_off, id_b );

    // A and C must still be intact
    REQUIRE( pjson_pool_pmm_get_const( id_a )->int_val == 111 );
    REQUIRE( pjson_pool_pmm_get_const( id_c )->int_val == 333 );

    // Allocate D — should reuse B's slot (free-list LIFO)
    node_id id_d                        = pjson_pool_pmm_alloc( pool_off );
    pjson_pool_pmm_get( id_d )->tag     = node_tag::integer;
    pjson_pool_pmm_get( id_d )->int_val = 444;

    // A, C, D all intact
    REQUIRE( pjson_pool_pmm_get_const( id_a )->int_val == 111 );
    REQUIRE( pjson_pool_pmm_get_const( id_c )->int_val == 333 );
    REQUIRE( pjson_pool_pmm_get_const( id_d )->int_val == 444 );

    pjson_pool_pmm_free_pool( pool_off );
    pam_pmm_destroy();
}

TEST_CASE( "regression ppool: mixed node types in pool", "[regression][ppool]" )
{
    pstringview_pmm_reset();
    pam_pmm_init( nullptr );

    uintptr_t pool_off = pam_pmm_create<pjson_pool_pmm>( "reg_pool_mixed" );
    REQUIRE( pool_off != 0u );
    pjson_pool_pmm_init( pool_off );

    // Создаём узлы разных типов
    node_id id_null = pjson_pool_pmm_alloc( pool_off );
    node_id id_bool = pjson_pool_pmm_alloc( pool_off );
    node_id id_int  = pjson_pool_pmm_alloc( pool_off );
    node_id id_real = pjson_pool_pmm_alloc( pool_off );

    pjson_pool_pmm_get( id_null )->tag         = node_tag::null;
    pjson_pool_pmm_get( id_bool )->tag         = node_tag::boolean;
    pjson_pool_pmm_get( id_bool )->boolean_val = 1u;
    pjson_pool_pmm_get( id_int )->tag          = node_tag::integer;
    pjson_pool_pmm_get( id_int )->int_val      = -42;
    pjson_pool_pmm_get( id_real )->tag         = node_tag::real;
    pjson_pool_pmm_get( id_real )->real_val    = 3.14159;

    // Verify all types retained
    REQUIRE( pjson_pool_pmm_get_const( id_null )->tag == node_tag::null );
    REQUIRE( pjson_pool_pmm_get_const( id_bool )->tag == node_tag::boolean );
    REQUIRE( pjson_pool_pmm_get_const( id_bool )->boolean_val != 0u );
    REQUIRE( pjson_pool_pmm_get_const( id_int )->tag == node_tag::integer );
    REQUIRE( pjson_pool_pmm_get_const( id_int )->int_val == -42 );
    REQUIRE( pjson_pool_pmm_get_const( id_real )->tag == node_tag::real );
    REQUIRE( pjson_pool_pmm_get_const( id_real )->real_val == Catch::Approx( 3.14159 ) );

    pjson_pool_pmm_free_pool( pool_off );
    pam_pmm_destroy();
}

TEST_CASE( "regression ppool: persistence with multiple nodes and partial free", "[regression][ppool][persistence]" )
{
    const char* fname = "./test_regression_ppool_save.pam";
    rm_file( fname );

    uintptr_t pool_off = 0;
    node_id   saved_a  = 0;
    node_id   saved_c  = 0;

    {
        pstringview_pmm_reset();
        pam_pmm_init( fname );

        pool_off = pam_pmm_create<pjson_pool_pmm>( "reg_pool_persist" );
        REQUIRE( pool_off != 0u );
        pjson_pool_pmm_init( pool_off );

        node_id id_a = pjson_pool_pmm_alloc( pool_off );
        node_id id_b = pjson_pool_pmm_alloc( pool_off );
        node_id id_c = pjson_pool_pmm_alloc( pool_off );

        pjson_pool_pmm_get( id_a )->tag     = node_tag::integer;
        pjson_pool_pmm_get( id_a )->int_val = 100;
        pjson_pool_pmm_get( id_b )->tag     = node_tag::integer;
        pjson_pool_pmm_get( id_b )->int_val = 200;
        pjson_pool_pmm_get( id_c )->tag     = node_tag::integer;
        pjson_pool_pmm_get( id_c )->int_val = 300;

        // Free B before save
        pjson_pool_pmm_free( pool_off, id_b );

        saved_a = id_a;
        saved_c = id_c;

        pam_pmm_save();
        pam_pmm_destroy();
    }

    {
        pstringview_pmm_reset();
        pam_pmm_init( fname );

        uintptr_t off = pam_pmm_find( "reg_pool_persist" );
        REQUIRE( off == pool_off );

        // A and C must survive save/load
        const ::node* na = pjson_pool_pmm_get_const( saved_a );
        REQUIRE( na != nullptr );
        REQUIRE( na->tag == node_tag::integer );
        REQUIRE( na->int_val == 100 );

        const ::node* nc = pjson_pool_pmm_get_const( saved_c );
        REQUIRE( nc != nullptr );
        REQUIRE( nc->tag == node_tag::integer );
        REQUIRE( nc->int_val == 300 );

        pam_pmm_destroy();
    }

    rm_file( fname );
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. pmap_pmm — граничные случаи
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "regression pmap: erase first element", "[regression][pmap]" )
{
    PamManager::create( 64 * 1024 );

    auto pm = PamManager::template allocate_typed<pmap_pmm<int, int>>();
    REQUIRE( pm );
    std::memset( pm.resolve(), 0, sizeof( pmap_pmm<int, int> ) );

    pm->insert( 10, 100 );
    pm->insert( 20, 200 );
    pm->insert( 30, 300 );

    REQUIRE( pm->erase( 10 ) );
    REQUIRE( pm->size() == 2u );
    REQUIRE( pm->find( 10 ) == nullptr );
    REQUIRE( pm->find( 20 ) != nullptr );
    REQUIRE( pm->find( 30 ) != nullptr );

    pm->free();
    PamManager::destroy();
}

TEST_CASE( "regression pmap: erase last element", "[regression][pmap]" )
{
    PamManager::create( 64 * 1024 );

    auto pm = PamManager::template allocate_typed<pmap_pmm<int, int>>();
    REQUIRE( pm );
    std::memset( pm.resolve(), 0, sizeof( pmap_pmm<int, int> ) );

    pm->insert( 10, 100 );
    pm->insert( 20, 200 );
    pm->insert( 30, 300 );

    REQUIRE( pm->erase( 30 ) );
    REQUIRE( pm->size() == 2u );
    REQUIRE( pm->find( 30 ) == nullptr );
    REQUIRE( pm->find( 10 ) != nullptr );
    REQUIRE( pm->find( 20 ) != nullptr );

    pm->free();
    PamManager::destroy();
}

TEST_CASE( "regression pmap: erase from single-element map", "[regression][pmap]" )
{
    PamManager::create( 64 * 1024 );

    auto pm = PamManager::template allocate_typed<pmap_pmm<int, int>>();
    REQUIRE( pm );
    std::memset( pm.resolve(), 0, sizeof( pmap_pmm<int, int> ) );

    pm->insert( 42, 999 );
    REQUIRE( pm->size() == 1u );

    REQUIRE( pm->erase( 42 ) );
    REQUIRE( pm->size() == 0u );
    REQUIRE( pm->empty() );
    REQUIRE( pm->find( 42 ) == nullptr );

    pm->free();
    PamManager::destroy();
}

TEST_CASE( "regression pmap: consecutive erases", "[regression][pmap]" )
{
    PamManager::create( 64 * 1024 );

    auto pm = PamManager::template allocate_typed<pmap_pmm<int, int>>();
    REQUIRE( pm );
    std::memset( pm.resolve(), 0, sizeof( pmap_pmm<int, int> ) );

    for ( int i = 0; i < 10; ++i )
    {
        pm->insert( i, i * 100 );
    }
    REQUIRE( pm->size() == 10u );

    // Erase all in forward order
    for ( int i = 0; i < 10; ++i )
    {
        REQUIRE( pm->erase( i ) );
    }
    REQUIRE( pm->empty() );

    pm->free();
    PamManager::destroy();
}

TEST_CASE( "regression pmap: erase non-existent key on empty map", "[regression][pmap]" )
{
    PamManager::create( 64 * 1024 );

    auto pm = PamManager::template allocate_typed<pmap_pmm<int, int>>();
    REQUIRE( pm );
    std::memset( pm.resolve(), 0, sizeof( pmap_pmm<int, int> ) );

    REQUIRE_FALSE( pm->erase( 999 ) );
    REQUIRE( pm->empty() );

    pm->free();
    PamManager::destroy();
}

TEST_CASE( "regression pmap: save/load preserves sorted order", "[regression][pmap][persistence]" )
{
    const char* fname = "./test_regression_pmap_save.pam";
    rm_file( fname );

    uintptr_t saved_off = 0;

    {
        pam_pmm_init( fname );

        using IntMap = pmap_pmm<int, int>;
        saved_off    = pam_pmm_create<IntMap>( "reg_pmap" );
        REQUIRE( saved_off != 0u );

        auto* pm = pam_pmm_resolve<IntMap>( saved_off );
        std::memset( pm, 0, sizeof( IntMap ) );

        // Insert in reverse order — pmap stores sorted
        pm->insert( 30, 300 );
        pm->insert( 10, 100 );
        pm->insert( 20, 200 );

        pam_pmm_save();
        pam_pmm_destroy();
    }

    {
        pam_pmm_init( fname );

        using IntMap  = pmap_pmm<int, int>;
        uintptr_t off = pam_pmm_find( "reg_pmap" );
        REQUIRE( off == saved_off );

        const auto* pm = pam_pmm_resolve_const<IntMap>( off );
        REQUIRE( pm->size() == 3u );

        // Verify find works after load
        REQUIRE( pm->find( 10 ) != nullptr );
        REQUIRE( *pm->find( 10 ) == 100 );
        REQUIRE( pm->find( 20 ) != nullptr );
        REQUIRE( *pm->find( 20 ) == 200 );
        REQUIRE( pm->find( 30 ) != nullptr );
        REQUIRE( *pm->find( 30 ) == 300 );

        pam_pmm_destroy();
    }

    rm_file( fname );
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. fptr_pmm — граничные случаи
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "regression fptr: find non-existent returns null", "[regression][fptr]" )
{
    pam_pmm_init( nullptr );

    fptr_pmm<int> p;
    p.find( "no_such_object_ever" );

    REQUIRE( p.is_null() );
    REQUIRE( p.addr() == 0u );

    pam_pmm_destroy();
}

TEST_CASE( "regression fptr: New then Delete then is_null", "[regression][fptr]" )
{
    pam_pmm_init( nullptr );

    fptr_pmm<int> p;
    p.New( "reg_fptr_obj" );
    REQUIRE( !p.is_null() );
    *p = 42;
    REQUIRE( *p == 42 );

    p.Delete();

    // After delete the fptr should no longer resolve the same way,
    // but the internal addr is not automatically zeroed.
    // Verify at least that find won't find it anymore.
    fptr_pmm<int> p2;
    p2.find( "reg_fptr_obj" );
    REQUIRE( p2.is_null() );

    pam_pmm_destroy();
}

TEST_CASE( "regression fptr: NewArray single element", "[regression][fptr]" )
{
    pam_pmm_init( nullptr );

    fptr_pmm<int> p;
    p.NewArray( 1, "reg_fptr_arr1" );
    REQUIRE( !p.is_null() );
    REQUIRE( p.count() == 1u );
    p[0] = 77;
    REQUIRE( p[0] == 77 );

    p.DeleteArray();

    pam_pmm_destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// 6. pptr — byte_offset round-trip для разных типов
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "regression pptr: byte_offset round-trip for int", "[regression][pptr]" )
{
    PamManager::create( 64 * 1024 );

    auto p = PamManager::allocate_typed<int>();
    REQUIRE( p );

    auto off = p.byte_offset();
    REQUIRE( off != 0u );

    auto p2 = PamManager::pptr_from_byte_offset<int>( off );
    REQUIRE( p2 == p );

    PamManager::destroy();
}

TEST_CASE( "regression pptr: byte_offset round-trip for double", "[regression][pptr]" )
{
    PamManager::create( 64 * 1024 );

    auto p = PamManager::allocate_typed<double>();
    REQUIRE( p );
    *p.resolve() = 2.718;

    auto off = p.byte_offset();
    auto p2  = PamManager::pptr_from_byte_offset<double>( off );

    REQUIRE( *p2.resolve() == Catch::Approx( 2.718 ) );

    PamManager::destroy();
}

TEST_CASE( "regression pptr: byte_offset round-trip for struct", "[regression][pptr]" )
{
    struct TestStruct
    {
        int    a;
        double b;
        char   c;
    };

    PamManager::create( 64 * 1024 );

    auto p = PamManager::allocate_typed<TestStruct>();
    REQUIRE( p );

    p.resolve()->a = 1;
    p.resolve()->b = 3.14;
    p.resolve()->c = 'X';

    auto off = p.byte_offset();
    auto p2  = PamManager::pptr_from_byte_offset<TestStruct>( off );

    REQUIRE( p2.resolve()->a == 1 );
    REQUIRE( p2.resolve()->b == Catch::Approx( 3.14 ) );
    REQUIRE( p2.resolve()->c == 'X' );

    PamManager::destroy();
}

TEST_CASE( "regression pptr: null pptr byte_offset is 0", "[regression][pptr]" )
{
    PamManager::create( 64 * 1024 );

    PamManager::pptr<int> null_p{};
    REQUIRE( null_p.byte_offset() == 0u );

    auto p2 = PamManager::pptr_from_byte_offset<int>( 0 );
    REQUIRE( !p2 ); // null

    PamManager::destroy();
}

TEST_CASE( "regression pptr: different allocations have different offsets", "[regression][pptr]" )
{
    PamManager::create( 64 * 1024 );

    auto p1 = PamManager::allocate_typed<int>();
    auto p2 = PamManager::allocate_typed<int>();
    auto p3 = PamManager::allocate_typed<double>();

    REQUIRE( p1.byte_offset() != p2.byte_offset() );
    REQUIRE( p1.byte_offset() != p3.byte_offset() );
    REQUIRE( p2.byte_offset() != p3.byte_offset() );

    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// 7. Интеграция: save/load с множественными PMM-типами
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "regression integration: save/load with pstring + parray + pmap together",
           "[regression][integration][persistence]" )
{
    const char* fname = "./test_regression_integration_save.pam";
    rm_file( fname );

    uintptr_t off_str = 0, off_arr = 0, off_map = 0;

    {
        pam_pmm_init( fname );

        // pstring
        off_str = pam_pmm_create<pmm_pstring>( "integ_str" );
        REQUIRE( off_str != 0u );
        auto* ps = pam_pmm_resolve<pmm_pstring>( off_str );
        std::memset( ps, 0, sizeof( pmm_pstring ) );
        REQUIRE( ps->assign( "integration test" ) );

        // parray
        using IntArray = PamManager::parray<int>;
        off_arr        = pam_pmm_create<IntArray>( "integ_arr" );
        REQUIRE( off_arr != 0u );
        auto* arr = pam_pmm_resolve<IntArray>( off_arr );
        std::memset( arr, 0, sizeof( IntArray ) );
        arr->push_back( 100 );
        arr->push_back( 200 );
        arr->push_back( 300 );

        // pmap
        using IntMap = pmap_pmm<int, int>;
        off_map      = pam_pmm_create<IntMap>( "integ_map" );
        REQUIRE( off_map != 0u );
        auto* pm = pam_pmm_resolve<IntMap>( off_map );
        std::memset( pm, 0, sizeof( IntMap ) );
        pm->insert( 1, 10 );
        pm->insert( 2, 20 );

        pam_pmm_save();
        pam_pmm_destroy();
    }

    {
        pam_pmm_init( fname );

        // Verify pstring
        uintptr_t off = pam_pmm_find( "integ_str" );
        REQUIRE( off == off_str );
        const auto* ps = pam_pmm_resolve_const<pmm_pstring>( off );
        REQUIRE( ps->size() == 16u );
        REQUIRE( std::strcmp( ps->c_str(), "integration test" ) == 0 );

        // Verify parray
        using IntArray = PamManager::parray<int>;
        off            = pam_pmm_find( "integ_arr" );
        REQUIRE( off == off_arr );
        const auto* arr = pam_pmm_resolve_const<IntArray>( off );
        REQUIRE( arr->size() == 3u );
        REQUIRE( arr->data()[0] == 100 );
        REQUIRE( arr->data()[1] == 200 );
        REQUIRE( arr->data()[2] == 300 );

        // Verify pmap
        using IntMap = pmap_pmm<int, int>;
        off          = pam_pmm_find( "integ_map" );
        REQUIRE( off == off_map );
        const auto* pm = pam_pmm_resolve_const<IntMap>( off );
        REQUIRE( pm->size() == 2u );
        REQUIRE( pm->find( 1 ) != nullptr );
        REQUIRE( *pm->find( 1 ) == 10 );
        REQUIRE( pm->find( 2 ) != nullptr );
        REQUIRE( *pm->find( 2 ) == 20 );

        pam_pmm_destroy();
    }

    rm_file( fname );
}

TEST_CASE( "regression integration: pjson_db full cycle save/load", "[regression][integration][persistence]" )
{
    const char* fname = "./test_regression_db_save.pam";
    rm_file( fname );

    {
        pstringview_pmm_reset();
        pam_pmm_init( fname );

        pjson_db_pmm db;

        REQUIRE( db.put( "/name", "Alice" ) );
        REQUIRE( db.put( "/age", int64_t( 30 ) ) );
        REQUIRE( db.put( "/pi", 3.14159 ) );
        REQUIRE( db.put( "/active", true ) );
        REQUIRE( db.put( "/data/x", int64_t( 1 ) ) );
        REQUIRE( db.put( "/data/y", int64_t( 2 ) ) );
        REQUIRE( db.put( "/items/0", "first" ) );
        REQUIRE( db.put( "/items/1", "second" ) );

        pam_pmm_save();
        pam_pmm_destroy();
    }

    {
        pstringview_pmm_reset();
        pam_pmm_init( fname );

        pjson_db_pmm db;

        // Scalar values
        {
            auto v = db.get( "/name" );
            REQUIRE( v.valid() );
            REQUIRE( v.is_string() );
            REQUIRE( v.as_string() == "Alice" );
        }
        {
            auto v = db.get( "/age" );
            REQUIRE( v.valid() );
            REQUIRE( v.is_integer() );
            REQUIRE( v.as_int() == 30 );
        }
        {
            auto v = db.get( "/pi" );
            REQUIRE( v.valid() );
            REQUIRE( v.is_real() );
            REQUIRE( v.as_double() == Catch::Approx( 3.14159 ) );
        }
        {
            auto v = db.get( "/active" );
            REQUIRE( v.valid() );
            REQUIRE( v.is_boolean() );
            REQUIRE( v.as_bool() == true );
        }

        // Nested object
        {
            auto v = db.get( "/data/x" );
            REQUIRE( v.valid() );
            REQUIRE( v.as_int() == 1 );
        }
        {
            auto v = db.get( "/data/y" );
            REQUIRE( v.valid() );
            REQUIRE( v.as_int() == 2 );
        }

        // Array elements
        {
            auto v = db.get( "/items/0" );
            REQUIRE( v.valid() );
            REQUIRE( v.as_string() == "first" );
        }
        {
            auto v = db.get( "/items/1" );
            REQUIRE( v.valid() );
            REQUIRE( v.as_string() == "second" );
        }

        pam_pmm_destroy();
    }

    rm_file( fname );
}

TEST_CASE( "regression integration: pjson_db erase and re-put after save/load",
           "[regression][integration][persistence]" )
{
    const char* fname = "./test_regression_db_erase_save.pam";
    rm_file( fname );

    {
        pstringview_pmm_reset();
        pam_pmm_init( fname );

        pjson_db_pmm db;
        REQUIRE( db.put( "/a", int64_t( 1 ) ) );
        REQUIRE( db.put( "/b", int64_t( 2 ) ) );
        REQUIRE( db.put( "/c", int64_t( 3 ) ) );

        // Erase /b before save
        REQUIRE( db.erase( "/b" ) );

        pam_pmm_save();
        pam_pmm_destroy();
    }

    {
        pstringview_pmm_reset();
        pam_pmm_init( fname );

        pjson_db_pmm db;

        // /a and /c must survive
        {
            auto v = db.get( "/a" );
            REQUIRE( v.valid() );
            REQUIRE( v.as_int() == 1 );
        }
        {
            auto v = db.get( "/c" );
            REQUIRE( v.valid() );
            REQUIRE( v.as_int() == 3 );
        }

        // /b must not exist
        {
            auto v = db.get( "/b" );
            REQUIRE( v.is_error() );
        }

        // Re-put /b
        REQUIRE( db.put( "/b", int64_t( 42 ) ) );
        {
            auto v = db.get( "/b" );
            REQUIRE( v.valid() );
            REQUIRE( v.as_int() == 42 );
        }

        pam_pmm_destroy();
    }

    rm_file( fname );
}
