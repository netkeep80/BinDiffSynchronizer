#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <filesystem>
#include <type_traits>

#include "pjson_pool_pmm.h"
#include "fptr_pmm.h"

using namespace pjson;

// =============================================================================
// Tests for Phase 4 — pjson_pool_pmm: пул памяти для узлов node
// =============================================================================

// Вспомогательные функции.
namespace
{
void reset_pam()
{
    pstringview_manager::reset();
    pam_pmm_reset();
}

void rm_file( const char* path )
{
    std::error_code ec;
    std::filesystem::remove( path, ec );
}
} // anonymous namespace

// ---------------------------------------------------------------------------
// pjson_pool_pmm — layout checks (Task 4.1)
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_pool_pmm: is trivially copyable POD struct", "[pjson_pool][layout]" )
{
    REQUIRE( std::is_trivially_copyable<pjson_pool_pmm>::value );
}

TEST_CASE( "pjson_pool_pmm: fields are correctly sized uintptr_t", "[pjson_pool][layout]" )
{
    // Пул хранит 3 * uintptr_t для pvector-совместимого заголовка +
    // node_id (uintptr_t) + uintptr_t для free_count_.
    REQUIRE( sizeof( pjson_pool_pmm ) == 5 * sizeof( uintptr_t ) );
}

// ---------------------------------------------------------------------------
// pjson_pool_pmm — базовая аллокация (Task 4.2)
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_pool_pmm: initial state after New() is empty", "[pjson_pool][alloc]" )
{
    reset_pam();

    fptr<pjson_pool_pmm> pool;
    pool.New();

    REQUIRE( pjson_pool_pmm_total_count( pool.addr() ) == 0u );
    REQUIRE( pjson_pool_pmm_free_in_pool( pool.addr() ) == 0u );
    REQUIRE( pjson_pool_pmm_used_count( pool.addr() ) == 0u );

    pool.Delete();
}

TEST_CASE( "pjson_pool_pmm: alloc returns nonzero node_id", "[pjson_pool][alloc]" )
{
    reset_pam();

    fptr<pjson_pool_pmm> pool;
    pool.New();

    node_id id = pjson_pool_pmm_alloc( pool.addr() );
    REQUIRE( id != 0u );

    pjson_pool_pmm_free( pool.addr(), id );
    pool.Delete();
}

TEST_CASE( "pjson_pool_pmm: alloc increments used_count and total_count", "[pjson_pool][alloc]" )
{
    reset_pam();

    fptr<pjson_pool_pmm> pool;
    pool.New();

    node_id id1 = pjson_pool_pmm_alloc( pool.addr() );
    REQUIRE( pjson_pool_pmm_total_count( pool.addr() ) == 1u );
    REQUIRE( pjson_pool_pmm_used_count( pool.addr() ) == 1u );
    REQUIRE( pjson_pool_pmm_free_in_pool( pool.addr() ) == 0u );

    node_id id2 = pjson_pool_pmm_alloc( pool.addr() );
    REQUIRE( pjson_pool_pmm_total_count( pool.addr() ) == 2u );
    REQUIRE( pjson_pool_pmm_used_count( pool.addr() ) == 2u );
    REQUIRE( pjson_pool_pmm_free_in_pool( pool.addr() ) == 0u );

    REQUIRE( id1 != id2 );

    pjson_pool_pmm_free( pool.addr(), id1 );
    pjson_pool_pmm_free( pool.addr(), id2 );
    pool.Delete();
}

TEST_CASE( "pjson_pool_pmm: allocated node is initialized as null", "[pjson_pool][alloc]" )
{
    reset_pam();

    fptr<pjson_pool_pmm> pool;
    pool.New();

    node_id id = pjson_pool_pmm_alloc( pool.addr() );
    REQUIRE( id != 0u );

    const node* n = pjson_pool_pmm_get_const( id );
    REQUIRE( n != nullptr );
    REQUIRE( n->tag == node_tag::null );

    pjson_pool_pmm_free( pool.addr(), id );
    pool.Delete();
}

// ---------------------------------------------------------------------------
// pjson_pool_pmm — free (Task 4.2)
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_pool_pmm: free adds node to free-list (free_count increases)", "[pjson_pool][free]" )
{
    reset_pam();

    fptr<pjson_pool_pmm> pool;
    pool.New();

    node_id id = pjson_pool_pmm_alloc( pool.addr() );
    REQUIRE( pjson_pool_pmm_free_in_pool( pool.addr() ) == 0u );

    pjson_pool_pmm_free( pool.addr(), id );
    REQUIRE( pjson_pool_pmm_free_in_pool( pool.addr() ) == 1u );
    REQUIRE( pjson_pool_pmm_used_count( pool.addr() ) == 0u );

    pool.Delete();
}

TEST_CASE( "pjson_pool_pmm: freed node is tagged as _free", "[pjson_pool][free]" )
{
    reset_pam();

    fptr<pjson_pool_pmm> pool;
    pool.New();

    node_id id = pjson_pool_pmm_alloc( pool.addr() );
    pjson_pool_pmm_free( pool.addr(), id );

    // Проверяем тег через PAM (не через get, т.к. слот помечен _free).
    const node* n = pmm_resolve<node>( id );
    REQUIRE( n != nullptr );
    REQUIRE( n->tag == node_tag::_free );

    pool.Delete();
}

TEST_CASE( "pjson_pool_pmm: free of null id is a no-op", "[pjson_pool][free]" )
{
    reset_pam();

    fptr<pjson_pool_pmm> pool;
    pool.New();

    // Не должно вызывать ошибок.
    REQUIRE_NOTHROW( pjson_pool_pmm_free( pool.addr(), 0 ) );
    REQUIRE( pjson_pool_pmm_free_in_pool( pool.addr() ) == 0u );

    pool.Delete();
}

// ---------------------------------------------------------------------------
// pjson_pool_pmm — повторное использование из free-list (Task 4.2)
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_pool_pmm: realloc from free-list reuses freed slot", "[pjson_pool][reuse]" )
{
    reset_pam();

    fptr<pjson_pool_pmm> pool;
    pool.New();

    node_id id1 = pjson_pool_pmm_alloc( pool.addr() );
    pjson_pool_pmm_free( pool.addr(), id1 );

    REQUIRE( pjson_pool_pmm_free_in_pool( pool.addr() ) == 1u );

    node_id id2 = pjson_pool_pmm_alloc( pool.addr() );

    // Повторно выделенный слот совпадает с освобождённым.
    REQUIRE( id2 == id1 );
    REQUIRE( pjson_pool_pmm_free_in_pool( pool.addr() ) == 0u );
    REQUIRE( pjson_pool_pmm_used_count( pool.addr() ) == 1u );

    pjson_pool_pmm_free( pool.addr(), id2 );
    pool.Delete();
}

TEST_CASE( "pjson_pool_pmm: reallocated node is reinitialized as null", "[pjson_pool][reuse]" )
{
    reset_pam();

    fptr<pjson_pool_pmm> pool;
    pool.New();

    node_id id1 = pjson_pool_pmm_alloc( pool.addr() );
    node_set_int( id1, 42 );

    pjson_pool_pmm_free( pool.addr(), id1 );
    node_id id2 = pjson_pool_pmm_alloc( pool.addr() );

    REQUIRE( id2 == id1 );
    const node* n = pjson_pool_pmm_get_const( id2 );
    REQUIRE( n != nullptr );
    REQUIRE( n->tag == node_tag::null );

    pjson_pool_pmm_free( pool.addr(), id2 );
    pool.Delete();
}

// ---------------------------------------------------------------------------
// pjson_pool_pmm — get()/const get() (Task 4.2)
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_pool_pmm: get returns pointer to writable node", "[pjson_pool][get]" )
{
    reset_pam();

    fptr<pjson_pool_pmm> pool;
    pool.New();

    node_id id = pjson_pool_pmm_alloc( pool.addr() );
    node_set_int( id, 100 );

    node* n = pjson_pool_pmm_get( id );
    REQUIRE( n != nullptr );
    REQUIRE( n->tag == node_tag::integer );
    REQUIRE( n->int_val == 100 );

    pjson_pool_pmm_free( pool.addr(), id );
    pool.Delete();
}

TEST_CASE( "pjson_pool_pmm: const get returns const pointer", "[pjson_pool][get]" )
{
    reset_pam();

    fptr<pjson_pool_pmm> pool;
    pool.New();

    node_id id = pjson_pool_pmm_alloc( pool.addr() );
    node_set_real( id, 3.14 );

    const node* cn = pjson_pool_pmm_get_const( id );
    REQUIRE( cn != nullptr );
    REQUIRE( cn->tag == node_tag::real );

    pjson_pool_pmm_free( pool.addr(), id );
    pool.Delete();
}

// ---------------------------------------------------------------------------
// pjson_pool_pmm — работа с различными типами узлов (Task 4.2)
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_pool_pmm: alloc and set boolean node", "[pjson_pool][node_types]" )
{
    reset_pam();

    fptr<pjson_pool_pmm> pool;
    pool.New();

    node_id id = pjson_pool_pmm_alloc( pool.addr() );
    node_set_bool( id, true );

    REQUIRE( node_view{ id }.is_boolean() );
    REQUIRE( node_view{ id }.as_bool() == true );

    pjson_pool_pmm_free( pool.addr(), id );
    pool.Delete();
}

TEST_CASE( "pjson_pool_pmm: alloc and set integer node", "[pjson_pool][node_types]" )
{
    reset_pam();

    fptr<pjson_pool_pmm> pool;
    pool.New();

    node_id id = pjson_pool_pmm_alloc( pool.addr() );
    node_set_int( id, -77 );

    REQUIRE( node_view{ id }.is_integer() );
    REQUIRE( node_view{ id }.as_int() == -77 );

    pjson_pool_pmm_free( pool.addr(), id );
    pool.Delete();
}

TEST_CASE( "pjson_pool_pmm: alloc and set string node", "[pjson_pool][node_types]" )
{
    reset_pam();

    fptr<pjson_pool_pmm> pool;
    pool.New();

    node_id id = pjson_pool_pmm_alloc( pool.addr() );
    node_set_string( id, "hello" );

    REQUIRE( node_view{ id }.is_string() );
    REQUIRE( node_view{ id }.as_string() == "hello" );

    pjson_pool_pmm_free( pool.addr(), id );
    pool.Delete();
}

// ---------------------------------------------------------------------------
// pjson_pool_pmm — аллокация 10000 узлов (Task 4.3)
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_pool_pmm: allocate 10000 nodes", "[pjson_pool][stress]" )
{
    reset_pam();

    fptr<pjson_pool_pmm> pool;
    pool.New();

    constexpr uintptr_t  N = 10000u;
    std::vector<node_id> ids;
    ids.reserve( N );

    for ( uintptr_t i = 0; i < N; i++ )
    {
        node_id id = pjson_pool_pmm_alloc( pool.addr() );
        REQUIRE( id != 0u );
        node_set_int( id, static_cast<int64_t>( i ) );
        ids.push_back( id );
    }

    REQUIRE( pjson_pool_pmm_total_count( pool.addr() ) == N );
    REQUIRE( pjson_pool_pmm_used_count( pool.addr() ) == N );
    REQUIRE( pjson_pool_pmm_free_in_pool( pool.addr() ) == 0u );

    // Проверяем, что значения сохранились.
    for ( uintptr_t i = 0; i < N; i++ )
    {
        REQUIRE( node_view{ ids[i] }.is_integer() );
        REQUIRE( node_view{ ids[i] }.as_int() == static_cast<int64_t>( i ) );
    }

    for ( auto id : ids )
        pjson_pool_pmm_free( pool.addr(), id );

    pool.Delete();
}

// ---------------------------------------------------------------------------
// pjson_pool_pmm — освобождение каждого второго, повторная аллокация без роста ПАП (Task 4.3)
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_pool_pmm: free every second node and reallocate without PAM growth", "[pjson_pool][stress][reuse]" )
{
    reset_pam();

    fptr<pjson_pool_pmm> pool;
    pool.New();

    constexpr uintptr_t  N = 10000u;
    std::vector<node_id> ids;
    ids.reserve( N );

    for ( uintptr_t i = 0; i < N; i++ )
    {
        node_id id = pjson_pool_pmm_alloc( pool.addr() );
        REQUIRE( id != 0u );
        ids.push_back( id );
    }

    uintptr_t total_before = pjson_pool_pmm_total_count( pool.addr() );
    REQUIRE( total_before == N );

    // Освобождаем каждый второй узел.
    for ( uintptr_t i = 0; i < N; i += 2 )
        pjson_pool_pmm_free( pool.addr(), ids[i] );

    REQUIRE( pjson_pool_pmm_free_in_pool( pool.addr() ) == N / 2 );

    // Повторно аллоцируем N/2 узлов — должны браться из free-list.
    std::vector<node_id> new_ids;
    new_ids.reserve( N / 2 );
    for ( uintptr_t i = 0; i < N / 2; i++ )
    {
        node_id id = pjson_pool_pmm_alloc( pool.addr() );
        REQUIRE( id != 0u );
        new_ids.push_back( id );
    }

    // total_count не должен увеличиться — повторное использование из free-list.
    REQUIRE( pjson_pool_pmm_total_count( pool.addr() ) == total_before );
    REQUIRE( pjson_pool_pmm_free_in_pool( pool.addr() ) == 0u );
    REQUIRE( pjson_pool_pmm_used_count( pool.addr() ) == N );

    // Освобождаем все оставшиеся узлы.
    for ( uintptr_t i = 1; i < N; i += 2 )
        pjson_pool_pmm_free( pool.addr(), ids[i] );
    for ( auto id : new_ids )
        pjson_pool_pmm_free( pool.addr(), id );

    pool.Delete();
}

// ---------------------------------------------------------------------------
// pjson_pool_pmm — сохранение/загрузка образа с пулом (Task 4.3)
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_pool_pmm: save and load PAM image with pool", "[pjson_pool][persistence]" )
{
    const char* fname = "./test_pjson_pool_save.pam";
    rm_file( fname );

    uintptr_t pool_offset = 0;
    node_id   saved_id    = 0;

    // Сохраняем пул с одним узлом.
    {
        pam_pmm_init( fname );
        pstringview_manager::reset();

        fptr<pjson_pool_pmm> pool;
        pool.New( "test_pool" );
        pool_offset = pool.addr();

        node_id id = pjson_pool_pmm_alloc( pool.addr() );
        node_set_int( id, 9999 );
        saved_id = id;

        pam_pmm_save();
    }

    // Загружаем образ и проверяем данные.
    {
        pam_pmm_init( fname );

        // Ищем пул по имени.
        uintptr_t off = pam_pmm_find( "test_pool" );
        REQUIRE( off != 0u );
        REQUIRE( off == pool_offset );

        REQUIRE( pjson_pool_pmm_total_count( off ) >= 1u );

        // Узел должен быть восстановлен с правильным значением.
        REQUIRE( node_view{ saved_id }.is_integer() );
        REQUIRE( node_view{ saved_id }.as_int() == 9999 );

        pjson_pool_pmm_free_pool( off );
        pam_pmm_delete( off );
    }

    rm_file( fname );
}

// ---------------------------------------------------------------------------
// pjson_pool_pmm — free_pool освобождает всю память (Task 4.1)
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_pool_pmm: free_pool releases all nodes", "[pjson_pool][free_pool]" )
{
    reset_pam();

    fptr<pjson_pool_pmm> pool;
    pool.New();

    node_id id1 = pjson_pool_pmm_alloc( pool.addr() );
    node_id id2 = pjson_pool_pmm_alloc( pool.addr() );
    node_id id3 = pjson_pool_pmm_alloc( pool.addr() );
    (void)id1;
    (void)id2;
    (void)id3;

    REQUIRE( pjson_pool_pmm_total_count( pool.addr() ) == 3u );

    pjson_pool_pmm_free_pool( pool.addr() );

    REQUIRE( pjson_pool_pmm_total_count( pool.addr() ) == 0u );
    REQUIRE( pjson_pool_pmm_free_in_pool( pool.addr() ) == 0u );
    REQUIRE( pjson_pool_pmm_used_count( pool.addr() ) == 0u );

    pool.Delete();
}

// ---------------------------------------------------------------------------
// pjson_pool_pmm — node_view через pool (интеграция с node helpers)
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_pool_pmm: node_view works correctly for pool-allocated nodes", "[pjson_pool][node_view]" )
{
    reset_pam();

    fptr<pjson_pool_pmm> pool;
    pool.New();

    // null
    node_id nid_null = pjson_pool_pmm_alloc( pool.addr() );
    REQUIRE( node_view{ nid_null }.is_null() );

    // boolean
    node_id nid_bool = pjson_pool_pmm_alloc( pool.addr() );
    node_set_bool( nid_bool, false );
    REQUIRE( node_view{ nid_bool }.is_boolean() );
    REQUIRE( node_view{ nid_bool }.as_bool() == false );

    // integer
    node_id nid_int = pjson_pool_pmm_alloc( pool.addr() );
    node_set_int( nid_int, INT64_MIN );
    REQUIRE( node_view{ nid_int }.is_integer() );
    REQUIRE( node_view{ nid_int }.as_int() == INT64_MIN );

    // uinteger
    node_id nid_uint = pjson_pool_pmm_alloc( pool.addr() );
    node_set_uint( nid_uint, UINT64_MAX );
    REQUIRE( node_view{ nid_uint }.is_uinteger() );
    REQUIRE( node_view{ nid_uint }.as_uint() == UINT64_MAX );

    // real
    node_id nid_real = pjson_pool_pmm_alloc( pool.addr() );
    node_set_real( nid_real, 2.718281828 );
    REQUIRE( node_view{ nid_real }.is_real() );

    pjson_pool_pmm_free( pool.addr(), nid_null );
    pjson_pool_pmm_free( pool.addr(), nid_bool );
    pjson_pool_pmm_free( pool.addr(), nid_int );
    pjson_pool_pmm_free( pool.addr(), nid_uint );
    pjson_pool_pmm_free( pool.addr(), nid_real );
    pool.Delete();
}

TEST_CASE( "pjson_pool_pmm: free-list chain is correct after multiple free/alloc cycles", "[pjson_pool][reuse]" )
{
    reset_pam();

    fptr<pjson_pool_pmm> pool;
    pool.New();

    // Аллоцируем 5 узлов.
    node_id ids[5];
    for ( int i = 0; i < 5; i++ )
    {
        ids[i] = pjson_pool_pmm_alloc( pool.addr() );
        node_set_int( ids[i], i );
    }
    REQUIRE( pjson_pool_pmm_total_count( pool.addr() ) == 5u );
    REQUIRE( pjson_pool_pmm_used_count( pool.addr() ) == 5u );

    // Освобождаем все 5 узлов.
    for ( int i = 0; i < 5; i++ )
        pjson_pool_pmm_free( pool.addr(), ids[i] );

    REQUIRE( pjson_pool_pmm_free_in_pool( pool.addr() ) == 5u );
    REQUIRE( pjson_pool_pmm_used_count( pool.addr() ) == 0u );

    // Аллоцируем снова 5 узлов — все должны браться из free-list.
    node_id ids2[5];
    for ( int i = 0; i < 5; i++ )
        ids2[i] = pjson_pool_pmm_alloc( pool.addr() );

    REQUIRE( pjson_pool_pmm_total_count( pool.addr() ) == 5u ); // без роста
    REQUIRE( pjson_pool_pmm_free_in_pool( pool.addr() ) == 0u );
    REQUIRE( pjson_pool_pmm_used_count( pool.addr() ) == 5u );

    // Узлы переинициализированы как null.
    for ( int i = 0; i < 5; i++ )
        REQUIRE( node_view{ ids2[i] }.is_null() );

    for ( int i = 0; i < 5; i++ )
        pjson_pool_pmm_free( pool.addr(), ids2[i] );

    pool.Delete();
}
