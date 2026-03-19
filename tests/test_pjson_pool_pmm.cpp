/**
 * @file test_pjson_pool_pmm.cpp
 * @brief Тесты для pjson_pool_pmm.
 *
 * Проверяют корректность работы пула узлов node на базе PMM.
 */

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <filesystem>
#include <type_traits>
#include <vector>

#include "pjson_pool_pmm.h"
#include "pam_pmm.h"

using namespace pjson;

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ pjson_pool_pmm — Layout
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pjson_pool_pmm: is trivially copyable POD struct", "[pjson_pool_pmm][layout]" )
{
    REQUIRE( std::is_trivially_copyable<pjson_pool_pmm>::value );
}

TEST_CASE( "pjson_pool_pmm: struct size is 5 * sizeof(uintptr_t)", "[pjson_pool_pmm][layout]" )
{
    REQUIRE( sizeof( pjson_pool_pmm ) == 5 * sizeof( uintptr_t ) );
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ pjson_pool_pmm_init
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pjson_pool_pmm_init: initialises pool to empty state", "[pjson_pool_pmm][init]" )
{
    PamManager::create( 64 * 1024 );

    uintptr_t pool_off = pjson_pool_pmm_create();
    REQUIRE( pool_off != 0u );

    REQUIRE( pjson_pool_pmm_total_count( pool_off ) == 0u );
    REQUIRE( pjson_pool_pmm_free_in_pool( pool_off ) == 0u );
    REQUIRE( pjson_pool_pmm_used_count( pool_off ) == 0u );

    pjson_pool_pmm_destroy( pool_off );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ pjson_pool_pmm_alloc — базовая аллокация
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pjson_pool_pmm_alloc: returns nonzero node_id", "[pjson_pool_pmm][alloc]" )
{
    PamManager::create( 64 * 1024 );

    uintptr_t pool_off = pjson_pool_pmm_create();
    REQUIRE( pool_off != 0u );

    node_id id = pjson_pool_pmm_alloc( pool_off );
    REQUIRE( id != 0u );

    pjson_pool_pmm_free( pool_off, id );
    pjson_pool_pmm_destroy( pool_off );
    PamManager::destroy();
}

TEST_CASE( "pjson_pool_pmm_alloc: increments used_count and total_count", "[pjson_pool_pmm][alloc]" )
{
    PamManager::create( 64 * 1024 );

    uintptr_t pool_off = pjson_pool_pmm_create();

    node_id id1 = pjson_pool_pmm_alloc( pool_off );
    REQUIRE( pjson_pool_pmm_total_count( pool_off ) == 1u );
    REQUIRE( pjson_pool_pmm_used_count( pool_off ) == 1u );
    REQUIRE( pjson_pool_pmm_free_in_pool( pool_off ) == 0u );

    node_id id2 = pjson_pool_pmm_alloc( pool_off );
    REQUIRE( pjson_pool_pmm_total_count( pool_off ) == 2u );
    REQUIRE( pjson_pool_pmm_used_count( pool_off ) == 2u );
    REQUIRE( pjson_pool_pmm_free_in_pool( pool_off ) == 0u );

    REQUIRE( id1 != id2 );

    pjson_pool_pmm_free( pool_off, id1 );
    pjson_pool_pmm_free( pool_off, id2 );
    pjson_pool_pmm_destroy( pool_off );
    PamManager::destroy();
}

TEST_CASE( "pjson_pool_pmm_alloc: allocated node is initialized as null", "[pjson_pool_pmm][alloc]" )
{
    PamManager::create( 64 * 1024 );

    uintptr_t pool_off = pjson_pool_pmm_create();

    node_id id = pjson_pool_pmm_alloc( pool_off );
    REQUIRE( id != 0u );

    const ::node* n = pjson_pool_pmm_get_const( id );
    REQUIRE( n != nullptr );
    REQUIRE( n->tag == node_tag::null );

    pjson_pool_pmm_free( pool_off, id );
    pjson_pool_pmm_destroy( pool_off );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ pjson_pool_pmm_free
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pjson_pool_pmm_free: adds node to free-list (free_count increases)", "[pjson_pool_pmm][free]" )
{
    PamManager::create( 64 * 1024 );

    uintptr_t pool_off = pjson_pool_pmm_create();

    node_id id = pjson_pool_pmm_alloc( pool_off );
    REQUIRE( pjson_pool_pmm_free_in_pool( pool_off ) == 0u );

    pjson_pool_pmm_free( pool_off, id );
    REQUIRE( pjson_pool_pmm_free_in_pool( pool_off ) == 1u );
    REQUIRE( pjson_pool_pmm_used_count( pool_off ) == 0u );

    pjson_pool_pmm_destroy( pool_off );
    PamManager::destroy();
}

TEST_CASE( "pjson_pool_pmm_free: freed node is tagged as _free", "[pjson_pool_pmm][free]" )
{
    PamManager::create( 64 * 1024 );

    uintptr_t pool_off = pjson_pool_pmm_create();

    node_id id = pjson_pool_pmm_alloc( pool_off );
    pjson_pool_pmm_free( pool_off, id );

    const ::node* n = pjson_pool_pmm_get_const( id );
    REQUIRE( n != nullptr );
    REQUIRE( n->tag == node_tag::_free );

    pjson_pool_pmm_destroy( pool_off );
    PamManager::destroy();
}

TEST_CASE( "pjson_pool_pmm_free: free of null id is a no-op", "[pjson_pool_pmm][free]" )
{
    PamManager::create( 64 * 1024 );

    uintptr_t pool_off = pjson_pool_pmm_create();

    // Не должно вызывать ошибок.
    REQUIRE_NOTHROW( pjson_pool_pmm_free( pool_off, 0 ) );
    REQUIRE( pjson_pool_pmm_free_in_pool( pool_off ) == 0u );

    pjson_pool_pmm_destroy( pool_off );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ повторного использования из free-list
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pjson_pool_pmm: realloc from free-list reuses freed slot", "[pjson_pool_pmm][reuse]" )
{
    PamManager::create( 64 * 1024 );

    uintptr_t pool_off = pjson_pool_pmm_create();

    node_id id1 = pjson_pool_pmm_alloc( pool_off );
    pjson_pool_pmm_free( pool_off, id1 );

    REQUIRE( pjson_pool_pmm_free_in_pool( pool_off ) == 1u );

    node_id id2 = pjson_pool_pmm_alloc( pool_off );

    // Повторно выделенный слот совпадает с освобождённым.
    REQUIRE( id2 == id1 );
    REQUIRE( pjson_pool_pmm_free_in_pool( pool_off ) == 0u );
    REQUIRE( pjson_pool_pmm_used_count( pool_off ) == 1u );

    pjson_pool_pmm_free( pool_off, id2 );
    pjson_pool_pmm_destroy( pool_off );
    PamManager::destroy();
}

TEST_CASE( "pjson_pool_pmm: reallocated node is reinitialized as null", "[pjson_pool_pmm][reuse]" )
{
    PamManager::create( 64 * 1024 );

    uintptr_t pool_off = pjson_pool_pmm_create();

    node_id id1 = pjson_pool_pmm_alloc( pool_off );

    // Устанавливаем значение integer.
    ::node* n1  = pjson_pool_pmm_get( id1 );
    n1->tag     = node_tag::integer;
    n1->int_val = 42;

    pjson_pool_pmm_free( pool_off, id1 );
    node_id id2 = pjson_pool_pmm_alloc( pool_off );

    REQUIRE( id2 == id1 );
    const ::node* n2 = pjson_pool_pmm_get_const( id2 );
    REQUIRE( n2->tag == node_tag::null );

    pjson_pool_pmm_free( pool_off, id2 );
    pjson_pool_pmm_destroy( pool_off );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ pjson_pool_pmm_get
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pjson_pool_pmm_get: returns pointer to writable node", "[pjson_pool_pmm][get]" )
{
    PamManager::create( 64 * 1024 );

    uintptr_t pool_off = pjson_pool_pmm_create();

    node_id id = pjson_pool_pmm_alloc( pool_off );

    ::node* n  = pjson_pool_pmm_get( id );
    n->tag     = node_tag::integer;
    n->int_val = 100;

    REQUIRE( n->tag == node_tag::integer );
    REQUIRE( n->int_val == 100 );

    pjson_pool_pmm_free( pool_off, id );
    pjson_pool_pmm_destroy( pool_off );
    PamManager::destroy();
}

TEST_CASE( "pjson_pool_pmm_get_const: returns const pointer", "[pjson_pool_pmm][get]" )
{
    PamManager::create( 64 * 1024 );

    uintptr_t pool_off = pjson_pool_pmm_create();

    node_id id = pjson_pool_pmm_alloc( pool_off );

    ::node* n   = pjson_pool_pmm_get( id );
    n->tag      = node_tag::real;
    n->real_val = 3.14;

    const ::node* cn = pjson_pool_pmm_get_const( id );
    REQUIRE( cn->tag == node_tag::real );

    pjson_pool_pmm_free( pool_off, id );
    pjson_pool_pmm_destroy( pool_off );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ работы с различными типами узлов
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pjson_pool_pmm: alloc and set boolean node", "[pjson_pool_pmm][node_types]" )
{
    PamManager::create( 64 * 1024 );

    uintptr_t pool_off = pjson_pool_pmm_create();

    node_id id     = pjson_pool_pmm_alloc( pool_off );
    ::node* n      = pjson_pool_pmm_get( id );
    n->tag         = node_tag::boolean;
    n->boolean_val = 1;

    REQUIRE( n->tag == node_tag::boolean );
    REQUIRE( n->boolean_val != 0 );

    pjson_pool_pmm_free( pool_off, id );
    pjson_pool_pmm_destroy( pool_off );
    PamManager::destroy();
}

TEST_CASE( "pjson_pool_pmm: alloc and set integer node", "[pjson_pool_pmm][node_types]" )
{
    PamManager::create( 64 * 1024 );

    uintptr_t pool_off = pjson_pool_pmm_create();

    node_id id = pjson_pool_pmm_alloc( pool_off );
    ::node* n  = pjson_pool_pmm_get( id );
    n->tag     = node_tag::integer;
    n->int_val = -77;

    REQUIRE( n->tag == node_tag::integer );
    REQUIRE( n->int_val == -77 );

    pjson_pool_pmm_free( pool_off, id );
    pjson_pool_pmm_destroy( pool_off );
    PamManager::destroy();
}

TEST_CASE( "pjson_pool_pmm: alloc and set uinteger node", "[pjson_pool_pmm][node_types]" )
{
    PamManager::create( 64 * 1024 );

    uintptr_t pool_off = pjson_pool_pmm_create();

    node_id id  = pjson_pool_pmm_alloc( pool_off );
    ::node* n   = pjson_pool_pmm_get( id );
    n->tag      = node_tag::uinteger;
    n->uint_val = UINT64_MAX;

    REQUIRE( n->tag == node_tag::uinteger );
    REQUIRE( n->uint_val == UINT64_MAX );

    pjson_pool_pmm_free( pool_off, id );
    pjson_pool_pmm_destroy( pool_off );
    PamManager::destroy();
}

TEST_CASE( "pjson_pool_pmm: alloc and set real node", "[pjson_pool_pmm][node_types]" )
{
    PamManager::create( 64 * 1024 );

    uintptr_t pool_off = pjson_pool_pmm_create();

    node_id id  = pjson_pool_pmm_alloc( pool_off );
    ::node* n   = pjson_pool_pmm_get( id );
    n->tag      = node_tag::real;
    n->real_val = 2.718281828;

    REQUIRE( n->tag == node_tag::real );
    REQUIRE( n->real_val == 2.718281828 );

    pjson_pool_pmm_free( pool_off, id );
    pjson_pool_pmm_destroy( pool_off );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ стресс-аллокации (Task 4.3 эквивалент)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pjson_pool_pmm: allocate 1000 nodes", "[pjson_pool_pmm][stress]" )
{
    PamManager::create( 256 * 1024 );

    uintptr_t pool_off = pjson_pool_pmm_create();

    constexpr uintptr_t  N = 1000u;
    std::vector<node_id> ids;
    ids.reserve( N );

    for ( uintptr_t i = 0; i < N; i++ )
    {
        node_id id = pjson_pool_pmm_alloc( pool_off );
        REQUIRE( id != 0u );

        ::node* n  = pjson_pool_pmm_get( id );
        n->tag     = node_tag::integer;
        n->int_val = static_cast<int64_t>( i );

        ids.push_back( id );
    }

    REQUIRE( pjson_pool_pmm_total_count( pool_off ) == N );
    REQUIRE( pjson_pool_pmm_used_count( pool_off ) == N );
    REQUIRE( pjson_pool_pmm_free_in_pool( pool_off ) == 0u );

    // Проверяем, что значения сохранились.
    for ( uintptr_t i = 0; i < N; i++ )
    {
        const ::node* n = pjson_pool_pmm_get_const( ids[i] );
        REQUIRE( n->tag == node_tag::integer );
        REQUIRE( n->int_val == static_cast<int64_t>( i ) );
    }

    for ( auto id : ids )
        pjson_pool_pmm_free( pool_off, id );

    pjson_pool_pmm_destroy( pool_off );
    PamManager::destroy();
}

TEST_CASE( "pjson_pool_pmm: free every second node and reallocate without growth", "[pjson_pool_pmm][stress][reuse]" )
{
    PamManager::create( 256 * 1024 );

    uintptr_t pool_off = pjson_pool_pmm_create();

    constexpr uintptr_t  N = 1000u;
    std::vector<node_id> ids;
    ids.reserve( N );

    for ( uintptr_t i = 0; i < N; i++ )
    {
        node_id id = pjson_pool_pmm_alloc( pool_off );
        REQUIRE( id != 0u );
        ids.push_back( id );
    }

    uintptr_t total_before = pjson_pool_pmm_total_count( pool_off );
    REQUIRE( total_before == N );

    // Освобождаем каждый второй узел.
    for ( uintptr_t i = 0; i < N; i += 2 )
        pjson_pool_pmm_free( pool_off, ids[i] );

    REQUIRE( pjson_pool_pmm_free_in_pool( pool_off ) == N / 2 );

    // Повторно аллоцируем N/2 узлов — должны браться из free-list.
    std::vector<node_id> new_ids;
    new_ids.reserve( N / 2 );
    for ( uintptr_t i = 0; i < N / 2; i++ )
    {
        node_id id = pjson_pool_pmm_alloc( pool_off );
        REQUIRE( id != 0u );
        new_ids.push_back( id );
    }

    // total_count не должен увеличиться — повторное использование из free-list.
    REQUIRE( pjson_pool_pmm_total_count( pool_off ) == total_before );
    REQUIRE( pjson_pool_pmm_free_in_pool( pool_off ) == 0u );
    REQUIRE( pjson_pool_pmm_used_count( pool_off ) == N );

    // Освобождаем все оставшиеся узлы.
    for ( uintptr_t i = 1; i < N; i += 2 )
        pjson_pool_pmm_free( pool_off, ids[i] );
    for ( auto id : new_ids )
        pjson_pool_pmm_free( pool_off, id );

    pjson_pool_pmm_destroy( pool_off );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ pjson_pool_pmm_free_pool
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pjson_pool_pmm_free_pool: releases all nodes", "[pjson_pool_pmm][free_pool]" )
{
    PamManager::create( 64 * 1024 );

    uintptr_t pool_off = pjson_pool_pmm_create();

    node_id id1 = pjson_pool_pmm_alloc( pool_off );
    node_id id2 = pjson_pool_pmm_alloc( pool_off );
    node_id id3 = pjson_pool_pmm_alloc( pool_off );
    (void)id1;
    (void)id2;
    (void)id3;

    REQUIRE( pjson_pool_pmm_total_count( pool_off ) == 3u );

    pjson_pool_pmm_free_pool( pool_off );

    REQUIRE( pjson_pool_pmm_total_count( pool_off ) == 0u );
    REQUIRE( pjson_pool_pmm_free_in_pool( pool_off ) == 0u );
    REQUIRE( pjson_pool_pmm_used_count( pool_off ) == 0u );

    pjson_pool_pmm_destroy( pool_off );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ free-list chain корректности
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pjson_pool_pmm: free-list chain is correct after multiple free/alloc cycles", "[pjson_pool_pmm][reuse]" )
{
    PamManager::create( 64 * 1024 );

    uintptr_t pool_off = pjson_pool_pmm_create();

    // Аллоцируем 5 узлов.
    node_id ids[5];
    for ( int i = 0; i < 5; i++ )
    {
        ids[i]     = pjson_pool_pmm_alloc( pool_off );
        ::node* n  = pjson_pool_pmm_get( ids[i] );
        n->tag     = node_tag::integer;
        n->int_val = i;
    }
    REQUIRE( pjson_pool_pmm_total_count( pool_off ) == 5u );
    REQUIRE( pjson_pool_pmm_used_count( pool_off ) == 5u );

    // Освобождаем все 5 узлов.
    for ( int i = 0; i < 5; i++ )
        pjson_pool_pmm_free( pool_off, ids[i] );

    REQUIRE( pjson_pool_pmm_free_in_pool( pool_off ) == 5u );
    REQUIRE( pjson_pool_pmm_used_count( pool_off ) == 0u );

    // Аллоцируем снова 5 узлов — все должны браться из free-list.
    node_id ids2[5];
    for ( int i = 0; i < 5; i++ )
        ids2[i] = pjson_pool_pmm_alloc( pool_off );

    REQUIRE( pjson_pool_pmm_total_count( pool_off ) == 5u ); // без роста
    REQUIRE( pjson_pool_pmm_free_in_pool( pool_off ) == 0u );
    REQUIRE( pjson_pool_pmm_used_count( pool_off ) == 5u );

    // Узлы переинициализированы как null.
    for ( int i = 0; i < 5; i++ )
    {
        const ::node* n = pjson_pool_pmm_get_const( ids2[i] );
        REQUIRE( n->tag == node_tag::null );
    }

    for ( int i = 0; i < 5; i++ )
        pjson_pool_pmm_free( pool_off, ids2[i] );

    pjson_pool_pmm_destroy( pool_off );
    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ создания/удаления пула
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pjson_pool_pmm_create: returns nonzero offset", "[pjson_pool_pmm][create]" )
{
    PamManager::create( 64 * 1024 );

    uintptr_t pool_off = pjson_pool_pmm_create();
    REQUIRE( pool_off != 0u );

    pjson_pool_pmm_destroy( pool_off );
    PamManager::destroy();
}

TEST_CASE( "pjson_pool_pmm_destroy: cleans up properly", "[pjson_pool_pmm][destroy]" )
{
    PamManager::create( 64 * 1024 );

    uintptr_t pool_off = pjson_pool_pmm_create();

    // Аллоцируем несколько узлов
    for ( int i = 0; i < 10; i++ )
        pjson_pool_pmm_alloc( pool_off );

    // Destroy не должен вызывать утечек памяти или ошибок
    REQUIRE_NOTHROW( pjson_pool_pmm_destroy( pool_off ) );

    PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ персистности (PAM save/load)
// ═══════════════════════════════════════════════════════════════════════════

namespace
{
void rm_pool_file( const char* path )
{
    std::error_code ec;
    std::filesystem::remove( path, ec );
}
} // anonymous namespace

TEST_CASE( "pjson_pool_pmm: save and load PAM image with pool", "[pjson_pool_pmm][persistence]" )
{
    const char* fname = "./test_pjson_pool_pmm_save.pam";
    rm_pool_file( fname );

    uintptr_t pool_offset = 0;
    node_id   saved_id    = 0;

    // Сохраняем пул с одним узлом.
    {
        pstringview_pmm_reset();
        pam_pmm_init( fname );

        // Создаём именованный пул через pam_pmm_create для возможности поиска по имени.
        uintptr_t pool_off = pam_pmm_create<pjson_pool_pmm>( "test_pool_pmm" );
        REQUIRE( pool_off != 0u );
        pjson_pool_pmm_init( pool_off );
        pool_offset = pool_off;

        node_id id = pjson_pool_pmm_alloc( pool_off );
        ::node* n  = pjson_pool_pmm_get( id );
        n->tag     = node_tag::integer;
        n->int_val = 9999;
        saved_id   = id;

        pam_pmm_save();
    }

    // Загружаем образ и проверяем данные.
    {
        pstringview_pmm_reset();
        pam_pmm_init( fname );

        // Ищем пул по имени.
        uintptr_t off = pam_pmm_find( "test_pool_pmm" );
        REQUIRE( off != 0u );
        REQUIRE( off == pool_offset );

        REQUIRE( pjson_pool_pmm_total_count( off ) >= 1u );

        // Узел должен быть восстановлен с правильным значением.
        const ::node* n = pjson_pool_pmm_get_const( saved_id );
        REQUIRE( n != nullptr );
        REQUIRE( n->tag == node_tag::integer );
        REQUIRE( n->int_val == 9999 );

        pjson_pool_pmm_free_pool( off );
    }

    rm_pool_file( fname );
}
