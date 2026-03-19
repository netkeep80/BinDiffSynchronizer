/**
 * @file test_pstring_pmm.cpp
 * @brief Тесты для pstring_pmm.
 *
 * Тесты проверяют корректность реализации персистной изменяемой строки
 * на базе PersistMemoryManager.
 */

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <type_traits>

#include "pstring_pmm.h"

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ LAYOUT
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pstring_pmm: struct size is 2 * sizeof(void*)", "[pstring_pmm][layout]" )
{
    REQUIRE( sizeof( pjson::pstring_pmm ) == 2 * sizeof( void* ) );
    REQUIRE( sizeof( pjson::pstring_pmm::length ) == sizeof( void* ) );
    REQUIRE( sizeof( pjson::pstring_pmm::chars_off ) == sizeof( void* ) );
}

TEST_CASE( "pstring_pmm: is trivially copyable", "[pstring_pmm][layout]" )
{
    REQUIRE( std::is_trivially_copyable<pjson::pstring_pmm>::value );
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ БАЗОВЫХ ОПЕРАЦИЙ
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pstring_pmm: zero-initialized gives empty string", "[pstring_pmm][construct]" )
{
    pjson::PamManager::create( 64 * 1024 );

    pjson::pstring_pmm ps{};
    ps.length    = 0;
    ps.chars_off = 0;

    REQUIRE( ps.empty() );
    REQUIRE( ps.size() == 0u );
    REQUIRE( std::strcmp( ps.c_str(), "" ) == 0 );

    pjson::PamManager::destroy();
}

TEST_CASE( "pstring_pmm: assign short string stores correct content", "[pstring_pmm][assign]" )
{
    pjson::PamManager::create( 64 * 1024 );

    // Создаём pstring_pmm в ПАП
    uintptr_t off = pjson::pstring_pmm_create( nullptr );
    REQUIRE( off != 0 );

    pjson::pstring_pmm* ps = pjson::pstring_pmm_get( off );
    REQUIRE( ps != nullptr );

    // Присваиваем строку
    ps->assign( "hello", off );

    // Переразрешаем после assign
    ps = pjson::pstring_pmm_get( off );
    REQUIRE( !ps->empty() );
    REQUIRE( ps->size() == 5u );
    REQUIRE( std::strcmp( ps->c_str(), "hello" ) == 0 );

    pjson::pstring_pmm_destroy( off );
    pjson::PamManager::destroy();
}

TEST_CASE( "pstring_pmm: assign longer string stores correct content", "[pstring_pmm][assign]" )
{
    pjson::PamManager::create( 64 * 1024 );

    uintptr_t off = pjson::pstring_pmm_create( nullptr );
    REQUIRE( off != 0 );

    const char* long_str = "The quick brown fox jumps over the lazy dog";

    pjson::pstring_pmm* ps = pjson::pstring_pmm_get( off );
    ps->assign( long_str, off );

    ps = pjson::pstring_pmm_get( off );
    REQUIRE( ps->size() == std::strlen( long_str ) );
    REQUIRE( std::strcmp( ps->c_str(), long_str ) == 0 );

    pjson::pstring_pmm_destroy( off );
    pjson::PamManager::destroy();
}

TEST_CASE( "pstring_pmm: reassigning frees old allocation and stores new content", "[pstring_pmm][reassign]" )
{
    pjson::PamManager::create( 64 * 1024 );

    uintptr_t off = pjson::pstring_pmm_create( "first" );
    REQUIRE( off != 0 );

    pjson::pstring_pmm* ps = pjson::pstring_pmm_get( off );
    REQUIRE( ps->size() == 5u );

    ps->assign( "second value", off );

    ps = pjson::pstring_pmm_get( off );
    REQUIRE( ps->size() == 12u );
    REQUIRE( std::strcmp( ps->c_str(), "second value" ) == 0 );

    pjson::pstring_pmm_destroy( off );
    pjson::PamManager::destroy();
}

TEST_CASE( "pstring_pmm: assign empty string clears content", "[pstring_pmm][assign]" )
{
    pjson::PamManager::create( 64 * 1024 );

    uintptr_t off = pjson::pstring_pmm_create( "nonempty" );
    REQUIRE( off != 0 );

    pjson::pstring_pmm* ps = pjson::pstring_pmm_get( off );
    REQUIRE( !ps->empty() );

    ps->assign( "", off );

    ps = pjson::pstring_pmm_get( off );
    REQUIRE( ps->empty() );
    REQUIRE( ps->size() == 0u );

    pjson::pstring_pmm_destroy( off );
    pjson::PamManager::destroy();
}

TEST_CASE( "pstring_pmm: assign nullptr gives empty string", "[pstring_pmm][assign]" )
{
    pjson::PamManager::create( 64 * 1024 );

    uintptr_t off = pjson::pstring_pmm_create( "test" );
    REQUIRE( off != 0 );

    pjson::pstring_pmm* ps = pjson::pstring_pmm_get( off );
    ps->assign( nullptr, off );

    ps = pjson::pstring_pmm_get( off );
    REQUIRE( ps->empty() );
    REQUIRE( ps->size() == 0u );
    REQUIRE( std::strcmp( ps->c_str(), "" ) == 0 );

    pjson::pstring_pmm_destroy( off );
    pjson::PamManager::destroy();
}

TEST_CASE( "pstring_pmm: clear resets to empty", "[pstring_pmm][clear]" )
{
    pjson::PamManager::create( 64 * 1024 );

    uintptr_t off = pjson::pstring_pmm_create( "hello world" );
    REQUIRE( off != 0 );

    pjson::pstring_pmm* ps = pjson::pstring_pmm_get( off );
    REQUIRE( !ps->empty() );

    ps->clear( off );

    ps = pjson::pstring_pmm_get( off );
    REQUIRE( ps->empty() );
    REQUIRE( ps->size() == 0u );
    REQUIRE( ps->chars_off == 0u );

    pjson::pstring_pmm_destroy( off );
    pjson::PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ СРАВНЕНИЯ
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pstring_pmm: operator== compares correctly", "[pstring_pmm][compare]" )
{
    pjson::PamManager::create( 64 * 1024 );

    uintptr_t off1 = pjson::pstring_pmm_create( "hello" );
    uintptr_t off2 = pjson::pstring_pmm_create( "hello" );
    uintptr_t off3 = pjson::pstring_pmm_create( "world" );

    pjson::pstring_pmm* ps1 = pjson::pstring_pmm_get( off1 );
    pjson::pstring_pmm* ps2 = pjson::pstring_pmm_get( off2 );
    pjson::pstring_pmm* ps3 = pjson::pstring_pmm_get( off3 );

    REQUIRE( *ps1 == *ps2 );
    REQUIRE( !( *ps1 == *ps3 ) );
    REQUIRE( *ps1 == "hello" );
    REQUIRE( !( *ps1 == "world" ) );

    pjson::pstring_pmm_destroy( off1 );
    pjson::pstring_pmm_destroy( off2 );
    pjson::pstring_pmm_destroy( off3 );
    pjson::PamManager::destroy();
}

TEST_CASE( "pstring_pmm: operator< gives lexicographic order", "[pstring_pmm][compare]" )
{
    pjson::PamManager::create( 64 * 1024 );

    uintptr_t off_a = pjson::pstring_pmm_create( "apple" );
    uintptr_t off_b = pjson::pstring_pmm_create( "banana" );

    pjson::pstring_pmm* ps_a = pjson::pstring_pmm_get( off_a );
    pjson::pstring_pmm* ps_b = pjson::pstring_pmm_get( off_b );

    REQUIRE( *ps_a < *ps_b );
    REQUIRE( !( *ps_b < *ps_a ) );

    pjson::pstring_pmm_destroy( off_a );
    pjson::pstring_pmm_destroy( off_b );
    pjson::PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ ДОСТУПА К СИМВОЛАМ
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pstring_pmm: operator[] accesses individual characters", "[pstring_pmm][index]" )
{
    pjson::PamManager::create( 64 * 1024 );

    uintptr_t off = pjson::pstring_pmm_create( "abc" );
    REQUIRE( off != 0 );

    pjson::pstring_pmm* ps = pjson::pstring_pmm_get( off );

    REQUIRE( ( *ps )[0] == 'a' );
    REQUIRE( ( *ps )[1] == 'b' );
    REQUIRE( ( *ps )[2] == 'c' );

    pjson::pstring_pmm_destroy( off );
    pjson::PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ СОСТОЯНИЯ ДАННЫХ
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pstring_pmm: chars_off is non-zero after assign, zero after clear", "[pstring_pmm][data]" )
{
    pjson::PamManager::create( 64 * 1024 );

    uintptr_t off = pjson::pstring_pmm_create( nullptr );
    REQUIRE( off != 0 );

    pjson::pstring_pmm* ps = pjson::pstring_pmm_get( off );
    REQUIRE( ps->chars_off == 0u );

    ps->assign( "test", off );
    ps = pjson::pstring_pmm_get( off );
    REQUIRE( ps->chars_off != 0u );
    REQUIRE( ps->length == 4u );

    ps->clear( off );
    ps = pjson::pstring_pmm_get( off );
    REQUIRE( ps->chars_off == 0u );
    REQUIRE( ps->length == 0u );

    pjson::pstring_pmm_destroy( off );
    pjson::PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ ХЕЛПЕР-ФУНКЦИЙ
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pstring_pmm_create: creates string in PAP", "[pstring_pmm][helper]" )
{
    pjson::PamManager::create( 64 * 1024 );

    uintptr_t off = pjson::pstring_pmm_create( "test string" );
    REQUIRE( off != 0 );
    REQUIRE( pjson::is_aligned_offset( off ) );

    pjson::pstring_pmm* ps = pjson::pstring_pmm_get( off );
    REQUIRE( ps != nullptr );
    REQUIRE( ps->size() == 11u );
    REQUIRE( std::strcmp( ps->c_str(), "test string" ) == 0 );

    pjson::pstring_pmm_destroy( off );
    pjson::PamManager::destroy();
}

TEST_CASE( "pstring_pmm_create: nullptr creates empty string", "[pstring_pmm][helper]" )
{
    pjson::PamManager::create( 64 * 1024 );

    uintptr_t off = pjson::pstring_pmm_create( nullptr );
    REQUIRE( off != 0 );

    pjson::pstring_pmm* ps = pjson::pstring_pmm_get( off );
    REQUIRE( ps != nullptr );
    REQUIRE( ps->empty() );
    REQUIRE( ps->size() == 0u );

    pjson::pstring_pmm_destroy( off );
    pjson::PamManager::destroy();
}

TEST_CASE( "pstring_pmm_destroy: safely destroys string", "[pstring_pmm][helper]" )
{
    pjson::PamManager::create( 64 * 1024 );

    uintptr_t off = pjson::pstring_pmm_create( "to be destroyed" );
    REQUIRE( off != 0 );

    // Уничтожаем строку
    pjson::pstring_pmm_destroy( off );

    // Повторное уничтожение с 0 не должно вызвать проблем
    pjson::pstring_pmm_destroy( 0 );

    pjson::PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ МНОЖЕСТВЕННЫХ СТРОК
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pstring_pmm: multiple strings are independent", "[pstring_pmm][multiple]" )
{
    pjson::PamManager::create( 64 * 1024 );

    uintptr_t off1 = pjson::pstring_pmm_create( "first" );
    uintptr_t off2 = pjson::pstring_pmm_create( "second" );
    uintptr_t off3 = pjson::pstring_pmm_create( "third" );

    // Все смещения разные
    REQUIRE( off1 != off2 );
    REQUIRE( off2 != off3 );
    REQUIRE( off1 != off3 );

    // Данные независимы
    pjson::pstring_pmm* ps1 = pjson::pstring_pmm_get( off1 );
    pjson::pstring_pmm* ps2 = pjson::pstring_pmm_get( off2 );
    pjson::pstring_pmm* ps3 = pjson::pstring_pmm_get( off3 );

    REQUIRE( std::strcmp( ps1->c_str(), "first" ) == 0 );
    REQUIRE( std::strcmp( ps2->c_str(), "second" ) == 0 );
    REQUIRE( std::strcmp( ps3->c_str(), "third" ) == 0 );

    // Изменение одной не влияет на другие
    ps2->assign( "modified", off2 );

    ps1 = pjson::pstring_pmm_get( off1 );
    ps2 = pjson::pstring_pmm_get( off2 );
    ps3 = pjson::pstring_pmm_get( off3 );

    REQUIRE( std::strcmp( ps1->c_str(), "first" ) == 0 );
    REQUIRE( std::strcmp( ps2->c_str(), "modified" ) == 0 );
    REQUIRE( std::strcmp( ps3->c_str(), "third" ) == 0 );

    pjson::pstring_pmm_destroy( off1 );
    pjson::pstring_pmm_destroy( off2 );
    pjson::pstring_pmm_destroy( off3 );
    pjson::PamManager::destroy();
}
