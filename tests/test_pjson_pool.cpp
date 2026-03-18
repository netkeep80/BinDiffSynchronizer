#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <filesystem>
#include <type_traits>

#include "pjson_pool.h"

using namespace pjson;

// =============================================================================
// Tests for Phase 4 — pjson_pool: пул памяти для узлов node
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
// pjson_pool — layout checks (Task 4.1)
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_pool: is trivially copyable POD struct", "[pjson_pool][layout]" )
{
    REQUIRE( std::is_trivially_copyable<pjson_pool>::value );
}

TEST_CASE( "pjson_pool: fields are correctly sized uintptr_t", "[pjson_pool][layout]" )
{
    // Пул хранит 3 * uintptr_t для pvector-совместимого заголовка +
    // node_id (uintptr_t) + uintptr_t для free_count_.
    REQUIRE( sizeof( pjson_pool ) == 5 * sizeof( uintptr_t ) );
}

// ---------------------------------------------------------------------------
// pjson_pool — базовая аллокация (Task 4.2)
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_pool: initial state after New() is empty", "[pjson_pool][alloc]" )
{
    reset_pam();

    fptr<pjson_pool> pool;
    pool.New();

    REQUIRE( pool->total_count() == 0u );
    REQUIRE( pool->free_in_pool() == 0u );
    REQUIRE( pool->used_count() == 0u );

    pool.Delete();
}

TEST_CASE( "pjson_pool: alloc returns nonzero node_id", "[pjson_pool][alloc]" )
{
    reset_pam();

    fptr<pjson_pool> pool;
    pool.New();

    node_id id = pool->alloc();
    REQUIRE( id != 0u );

    pool->free( id );
    pool.Delete();
}

TEST_CASE( "pjson_pool: alloc increments used_count and total_count", "[pjson_pool][alloc]" )
{
    reset_pam();

    fptr<pjson_pool> pool;
    pool.New();

    node_id id1 = pool->alloc();
    REQUIRE( pool->total_count() == 1u );
    REQUIRE( pool->used_count() == 1u );
    REQUIRE( pool->free_in_pool() == 0u );

    node_id id2 = pool->alloc();
    REQUIRE( pool->total_count() == 2u );
    REQUIRE( pool->used_count() == 2u );
    REQUIRE( pool->free_in_pool() == 0u );

    REQUIRE( id1 != id2 );

    pool->free( id1 );
    pool->free( id2 );
    pool.Delete();
}

TEST_CASE( "pjson_pool: allocated node is initialized as null", "[pjson_pool][alloc]" )
{
    reset_pam();

    fptr<pjson_pool> pool;
    pool.New();

    node_id id = pool->alloc();
    REQUIRE( id != 0u );

    const node& n = pool->get( id );
    REQUIRE( n.tag == node_tag::null );

    pool->free( id );
    pool.Delete();
}

// ---------------------------------------------------------------------------
// pjson_pool — free (Task 4.2)
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_pool: free adds node to free-list (free_count increases)", "[pjson_pool][free]" )
{
    reset_pam();

    fptr<pjson_pool> pool;
    pool.New();

    node_id id = pool->alloc();
    REQUIRE( pool->free_in_pool() == 0u );

    pool->free( id );
    REQUIRE( pool->free_in_pool() == 1u );
    REQUIRE( pool->used_count() == 0u );

    pool.Delete();
}

TEST_CASE( "pjson_pool: freed node is tagged as _free", "[pjson_pool][free]" )
{
    reset_pam();

    fptr<pjson_pool> pool;
    pool.New();

    node_id id = pool->alloc();
    pool->free( id );

    // Проверяем тег через PAM (не через get, т.к. слот помечен _free).
    const node* n = pmm_resolve<node>( id );
    REQUIRE( n != nullptr );
    REQUIRE( n->tag == node_tag::_free );

    pool.Delete();
}

TEST_CASE( "pjson_pool: free of null id is a no-op", "[pjson_pool][free]" )
{
    reset_pam();

    fptr<pjson_pool> pool;
    pool.New();

    // Не должно вызывать ошибок.
    REQUIRE_NOTHROW( pool->free( 0 ) );
    REQUIRE( pool->free_in_pool() == 0u );

    pool.Delete();
}

// ---------------------------------------------------------------------------
// pjson_pool — повторное использование из free-list (Task 4.2)
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_pool: realloc from free-list reuses freed slot", "[pjson_pool][reuse]" )
{
    reset_pam();

    fptr<pjson_pool> pool;
    pool.New();

    node_id id1 = pool->alloc();
    pool->free( id1 );

    REQUIRE( pool->free_in_pool() == 1u );

    node_id id2 = pool->alloc();

    // Повторно выделенный слот совпадает с освобождённым.
    REQUIRE( id2 == id1 );
    REQUIRE( pool->free_in_pool() == 0u );
    REQUIRE( pool->used_count() == 1u );

    pool->free( id2 );
    pool.Delete();
}

TEST_CASE( "pjson_pool: reallocated node is reinitialized as null", "[pjson_pool][reuse]" )
{
    reset_pam();

    fptr<pjson_pool> pool;
    pool.New();

    node_id id1 = pool->alloc();
    node_set_int( id1, 42 );

    pool->free( id1 );
    node_id id2 = pool->alloc();

    REQUIRE( id2 == id1 );
    const node& n = pool->get( id2 );
    REQUIRE( n.tag == node_tag::null );

    pool->free( id2 );
    pool.Delete();
}

// ---------------------------------------------------------------------------
// pjson_pool — get()/const get() (Task 4.2)
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_pool: get returns reference to writable node", "[pjson_pool][get]" )
{
    reset_pam();

    fptr<pjson_pool> pool;
    pool.New();

    node_id id = pool->alloc();
    node_set_int( id, 100 );

    node& n = pool->get( id );
    REQUIRE( n.tag == node_tag::integer );
    REQUIRE( n.int_val == 100 );

    pool->free( id );
    pool.Delete();
}

TEST_CASE( "pjson_pool: const get returns const reference", "[pjson_pool][get]" )
{
    reset_pam();

    fptr<pjson_pool> pool;
    pool.New();

    node_id id = pool->alloc();
    node_set_real( id, 3.14 );

    const pjson_pool* cpool = pool.operator->();
    const node&       cn    = cpool->get( id );
    REQUIRE( cn.tag == node_tag::real );

    pool->free( id );
    pool.Delete();
}

// ---------------------------------------------------------------------------
// pjson_pool — работа с различными типами узлов (Task 4.2)
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_pool: alloc and set boolean node", "[pjson_pool][node_types]" )
{
    reset_pam();

    fptr<pjson_pool> pool;
    pool.New();

    node_id id = pool->alloc();
    node_set_bool( id, true );

    REQUIRE( node_view{ id }.is_boolean() );
    REQUIRE( node_view{ id }.as_bool() == true );

    pool->free( id );
    pool.Delete();
}

TEST_CASE( "pjson_pool: alloc and set integer node", "[pjson_pool][node_types]" )
{
    reset_pam();

    fptr<pjson_pool> pool;
    pool.New();

    node_id id = pool->alloc();
    node_set_int( id, -77 );

    REQUIRE( node_view{ id }.is_integer() );
    REQUIRE( node_view{ id }.as_int() == -77 );

    pool->free( id );
    pool.Delete();
}

TEST_CASE( "pjson_pool: alloc and set string node", "[pjson_pool][node_types]" )
{
    reset_pam();

    fptr<pjson_pool> pool;
    pool.New();

    node_id id = pool->alloc();
    node_set_string( id, "hello" );

    REQUIRE( node_view{ id }.is_string() );
    REQUIRE( node_view{ id }.as_string() == "hello" );

    pool->free( id );
    pool.Delete();
}

// ---------------------------------------------------------------------------
// pjson_pool — аллокация 10000 узлов (Task 4.3)
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_pool: allocate 10000 nodes", "[pjson_pool][stress]" )
{
    reset_pam();

    fptr<pjson_pool> pool;
    pool.New();

    constexpr uintptr_t  N = 10000u;
    std::vector<node_id> ids;
    ids.reserve( N );

    for ( uintptr_t i = 0; i < N; i++ )
    {
        node_id id = pool->alloc();
        REQUIRE( id != 0u );
        node_set_int( id, static_cast<int64_t>( i ) );
        ids.push_back( id );
    }

    REQUIRE( pool->total_count() == N );
    REQUIRE( pool->used_count() == N );
    REQUIRE( pool->free_in_pool() == 0u );

    // Проверяем, что значения сохранились.
    for ( uintptr_t i = 0; i < N; i++ )
    {
        REQUIRE( node_view{ ids[i] }.is_integer() );
        REQUIRE( node_view{ ids[i] }.as_int() == static_cast<int64_t>( i ) );
    }

    for ( auto id : ids )
        pool->free( id );

    pool.Delete();
}

// ---------------------------------------------------------------------------
// pjson_pool — освобождение каждого второго, повторная аллокация без роста ПАП (Task 4.3)
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_pool: free every second node and reallocate without PAM growth", "[pjson_pool][stress][reuse]" )
{
    reset_pam();

    fptr<pjson_pool> pool;
    pool.New();

    constexpr uintptr_t  N = 10000u;
    std::vector<node_id> ids;
    ids.reserve( N );

    for ( uintptr_t i = 0; i < N; i++ )
    {
        node_id id = pool->alloc();
        REQUIRE( id != 0u );
        ids.push_back( id );
    }

    uintptr_t total_before = pool->total_count();
    REQUIRE( total_before == N );

    // Освобождаем каждый второй узел.
    for ( uintptr_t i = 0; i < N; i += 2 )
        pool->free( ids[i] );

    REQUIRE( pool->free_in_pool() == N / 2 );

    // Повторно аллоцируем N/2 узлов — должны браться из free-list.
    std::vector<node_id> new_ids;
    new_ids.reserve( N / 2 );
    for ( uintptr_t i = 0; i < N / 2; i++ )
    {
        node_id id = pool->alloc();
        REQUIRE( id != 0u );
        new_ids.push_back( id );
    }

    // total_count не должен увеличиться — повторное использование из free-list.
    REQUIRE( pool->total_count() == total_before );
    REQUIRE( pool->free_in_pool() == 0u );
    REQUIRE( pool->used_count() == N );

    // Освобождаем все оставшиеся узлы.
    for ( uintptr_t i = 1; i < N; i += 2 )
        pool->free( ids[i] );
    for ( auto id : new_ids )
        pool->free( id );

    pool.Delete();
}

// ---------------------------------------------------------------------------
// pjson_pool — сохранение/загрузка образа с пулом (Task 4.3)
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_pool: save and load PAM image with pool", "[pjson_pool][persistence]" )
{
    const char* fname = "./test_pjson_pool_save.pam";
    rm_file( fname );

    uintptr_t pool_offset = 0;
    node_id   saved_id    = 0;

    // Сохраняем пул с одним узлом.
    {
        pam_pmm_init( fname );
        pstringview_manager::reset();

        fptr<pjson_pool> pool;
        pool.New( "test_pool" );
        pool_offset = pool.addr();

        node_id id = pool->alloc();
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

        pjson_pool* pool = pmm_resolve<pjson_pool>( off );
        REQUIRE( pool != nullptr );
        REQUIRE( pool->total_count() >= 1u );

        // Узел должен быть восстановлен с правильным значением.
        REQUIRE( node_view{ saved_id }.is_integer() );
        REQUIRE( node_view{ saved_id }.as_int() == 9999 );

        pool->free_pool();
        pam_pmm_delete( off );
    }

    rm_file( fname );
}

// ---------------------------------------------------------------------------
// pjson_pool — free_pool освобождает всю память (Task 4.1)
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_pool: free_pool releases all nodes", "[pjson_pool][free_pool]" )
{
    reset_pam();

    fptr<pjson_pool> pool;
    pool.New();

    node_id id1 = pool->alloc();
    node_id id2 = pool->alloc();
    node_id id3 = pool->alloc();
    (void)id1;
    (void)id2;
    (void)id3;

    REQUIRE( pool->total_count() == 3u );

    pool->free_pool();

    REQUIRE( pool->total_count() == 0u );
    REQUIRE( pool->free_in_pool() == 0u );
    REQUIRE( pool->used_count() == 0u );

    pool.Delete();
}

// ---------------------------------------------------------------------------
// pjson_pool — node_view через pool (интеграция с node helpers)
// ---------------------------------------------------------------------------

TEST_CASE( "pjson_pool: node_view works correctly for pool-allocated nodes", "[pjson_pool][node_view]" )
{
    reset_pam();

    fptr<pjson_pool> pool;
    pool.New();

    // null
    node_id nid_null = pool->alloc();
    REQUIRE( node_view{ nid_null }.is_null() );

    // boolean
    node_id nid_bool = pool->alloc();
    node_set_bool( nid_bool, false );
    REQUIRE( node_view{ nid_bool }.is_boolean() );
    REQUIRE( node_view{ nid_bool }.as_bool() == false );

    // integer
    node_id nid_int = pool->alloc();
    node_set_int( nid_int, INT64_MIN );
    REQUIRE( node_view{ nid_int }.is_integer() );
    REQUIRE( node_view{ nid_int }.as_int() == INT64_MIN );

    // uinteger
    node_id nid_uint = pool->alloc();
    node_set_uint( nid_uint, UINT64_MAX );
    REQUIRE( node_view{ nid_uint }.is_uinteger() );
    REQUIRE( node_view{ nid_uint }.as_uint() == UINT64_MAX );

    // real
    node_id nid_real = pool->alloc();
    node_set_real( nid_real, 2.718281828 );
    REQUIRE( node_view{ nid_real }.is_real() );

    pool->free( nid_null );
    pool->free( nid_bool );
    pool->free( nid_int );
    pool->free( nid_uint );
    pool->free( nid_real );
    pool.Delete();
}

TEST_CASE( "pjson_pool: free-list chain is correct after multiple free/alloc cycles", "[pjson_pool][reuse]" )
{
    reset_pam();

    fptr<pjson_pool> pool;
    pool.New();

    // Аллоцируем 5 узлов.
    node_id ids[5];
    for ( int i = 0; i < 5; i++ )
    {
        ids[i] = pool->alloc();
        node_set_int( ids[i], i );
    }
    REQUIRE( pool->total_count() == 5u );
    REQUIRE( pool->used_count() == 5u );

    // Освобождаем все 5 узлов.
    for ( int i = 0; i < 5; i++ )
        pool->free( ids[i] );

    REQUIRE( pool->free_in_pool() == 5u );
    REQUIRE( pool->used_count() == 0u );

    // Аллоцируем снова 5 узлов — все должны браться из free-list.
    node_id ids2[5];
    for ( int i = 0; i < 5; i++ )
        ids2[i] = pool->alloc();

    REQUIRE( pool->total_count() == 5u ); // без роста
    REQUIRE( pool->free_in_pool() == 0u );
    REQUIRE( pool->used_count() == 5u );

    // Узлы переинициализированы как null.
    for ( int i = 0; i < 5; i++ )
        REQUIRE( node_view{ ids2[i] }.is_null() );

    for ( int i = 0; i < 5; i++ )
        pool->free( ids2[i] );

    pool.Delete();
}
