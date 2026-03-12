/**
 * @file test_pstringview_pmm.cpp
 * @brief Тесты для pstringview_pmm (Задача 14.4.1).
 *
 * Тесты проверяют корректность реализации персистной интернированной read-only строки
 * на базе PersistMemoryManager.
 */

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <type_traits>

#include "pstringview_pmm.h"

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ LAYOUT (Задача 14.4.1)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pstringview_pmm: struct size is 2 * sizeof(void*)", "[pstringview_pmm][layout][task14.4]" )
{
    REQUIRE( sizeof( pjson::pstringview_pmm ) == 2 * sizeof( void* ) );
    REQUIRE( sizeof( pjson::pstringview_pmm::length ) == sizeof( void* ) );
    REQUIRE( sizeof( pjson::pstringview_pmm::chars_offset ) == sizeof( void* ) );
}

TEST_CASE( "pstringview_pmm: is trivially copyable", "[pstringview_pmm][layout][task14.4]" )
{
    REQUIRE( std::is_trivially_copyable<pjson::pstringview_pmm>::value );
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ БАЗОВЫХ ОПЕРАЦИЙ
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pstringview_pmm: zero-initialized gives empty string", "[pstringview_pmm][construct][task14.4]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    pjson::pstringview_pmm sv{};
    sv.length       = 0;
    sv.chars_offset = 0;

    REQUIRE( sv.empty() );
    REQUIRE( sv.size() == 0u );
    REQUIRE( std::strcmp( sv.c_str(), "" ) == 0 );

    pjson::PamManager::destroy();
}

TEST_CASE( "pstringview_pmm: intern stores correct content", "[pstringview_pmm][intern][task14.4]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    pjson::pstringview_pmm sv{};
    sv.intern( "hello" );

    REQUIRE( !sv.empty() );
    REQUIRE( sv.size() == 5u );
    REQUIRE( std::strcmp( sv.c_str(), "hello" ) == 0 );

    pjson::PamManager::destroy();
}

TEST_CASE( "pstringview_pmm: same string always yields same chars_offset (interning)",
           "[pstringview_pmm][intern][task14.4]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    pjson::pstringview_pmm sv1{};
    sv1.intern( "key" );

    pjson::pstringview_pmm sv2{};
    sv2.intern( "key" );

    // Интернирование гарантирует одинаковый chars_offset для одинаковых строк
    REQUIRE( sv1.chars_offset == sv2.chars_offset );
    REQUIRE( sv1.length == sv2.length );
    REQUIRE( std::strcmp( sv1.c_str(), sv2.c_str() ) == 0 );

    pjson::PamManager::destroy();
}

TEST_CASE( "pstringview_pmm: different strings have different chars_offset", "[pstringview_pmm][intern][task14.4]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    pjson::pstringview_pmm sv1{};
    sv1.intern( "foo" );

    pjson::pstringview_pmm sv2{};
    sv2.intern( "bar" );

    REQUIRE( sv1.chars_offset != sv2.chars_offset );

    pjson::PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ СРАВНЕНИЯ
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pstringview_pmm: operator== compares by chars_offset", "[pstringview_pmm][compare][task14.4]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    pjson::pstringview_pmm sv1{};
    sv1.intern( "hello" );

    pjson::pstringview_pmm sv2{};
    sv2.intern( "hello" );

    pjson::pstringview_pmm sv3{};
    sv3.intern( "world" );

    REQUIRE( sv1 == sv2 );
    REQUIRE( !( sv1 == sv3 ) );
    REQUIRE( sv1 == "hello" );
    REQUIRE( !( sv1 == "world" ) );

    pjson::PamManager::destroy();
}

TEST_CASE( "pstringview_pmm: operator< gives lexicographic order", "[pstringview_pmm][compare][task14.4]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    pjson::pstringview_pmm sv_a{};
    sv_a.intern( "apple" );

    pjson::pstringview_pmm sv_b{};
    sv_b.intern( "banana" );

    REQUIRE( sv_a < sv_b );
    REQUIRE( !( sv_b < sv_a ) );

    pjson::PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ ПУСТЫХ СТРОК
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pstringview_pmm: intern empty string gives empty result", "[pstringview_pmm][intern][task14.4]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    pjson::pstringview_pmm sv{};
    sv.intern( "" );

    REQUIRE( sv.empty() );
    REQUIRE( sv.size() == 0u );
    REQUIRE( std::strcmp( sv.c_str(), "" ) == 0 );

    pjson::PamManager::destroy();
}

TEST_CASE( "pstringview_pmm: intern nullptr treated as empty string", "[pstringview_pmm][intern][task14.4]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    pjson::pstringview_pmm sv{};
    sv.intern( nullptr );

    REQUIRE( sv.empty() );
    REQUIRE( sv.size() == 0u );

    pjson::PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ СОСТОЯНИЯ ДАННЫХ
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pstringview_pmm: chars_offset is non-zero after intern non-empty string",
           "[pstringview_pmm][intern][task14.4]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    pjson::pstringview_pmm sv{};
    sv.chars_offset = 0;
    sv.length       = 0;

    REQUIRE( sv.chars_offset == 0u );

    sv.intern( "test" );
    REQUIRE( sv.chars_offset != 0u );
    REQUIRE( sv.length == 4u );

    pjson::PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ МНОЖЕСТВЕННЫХ СТРОК
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pstringview_pmm: many distinct strings are all interned correctly", "[pstringview_pmm][intern][task14.4]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    static const char* words[] = { "alpha", "beta", "gamma", "delta", "epsilon",
                                   "zeta",  "eta",  "theta", "iota",  "kappa" };
    constexpr int      N       = 10;

    pjson::pstringview_pmm svs[N];
    for ( int i = 0; i < N; i++ )
    {
        svs[i].intern( words[i] );
        REQUIRE( std::strcmp( svs[i].c_str(), words[i] ) == 0 );
    }

    // Повторное интернирование — должны вернуться те же смещения
    for ( int i = 0; i < N; i++ )
    {
        pjson::pstringview_pmm dup{};
        dup.intern( words[i] );
        REQUIRE( dup.chars_offset == svs[i].chars_offset );
    }

    pjson::PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ ХЕЛПЕР-ФУНКЦИЙ
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pstringview_pmm_intern: returns InternResult with correct values", "[pstringview_pmm][helper][task14.4]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    auto r1 = pjson::pstringview_pmm_intern( "world" );
    REQUIRE( r1.length == 5u );
    REQUIRE( r1.chars_offset != 0u );

    // Проверяем, что можно получить строку по смещению
    auto  p  = pjson::offset_to_pptr<pjson::pmm_pstringview>( r1.chars_offset );
    auto* sv = p.resolve();
    REQUIRE( sv != nullptr );
    REQUIRE( std::strcmp( sv->c_str(), "world" ) == 0 );

    // Повторный вызов — тот же chars_offset
    auto r2 = pjson::pstringview_pmm_intern( "world" );
    REQUIRE( r2.chars_offset == r1.chars_offset );
    REQUIRE( r2.length == r1.length );

    pjson::PamManager::destroy();
}

TEST_CASE( "pstringview_pmm_intern: different strings have different chars_offset",
           "[pstringview_pmm][helper][task14.4]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    auto r_a = pjson::pstringview_pmm_intern( "alpha" );
    auto r_b = pjson::pstringview_pmm_intern( "beta" );

    REQUIRE( r_a.chars_offset != r_b.chars_offset );

    pjson::PamManager::destroy();
}

TEST_CASE( "pstringview_pmm_intern: empty string gives length zero", "[pstringview_pmm][helper][task14.4]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    auto r = pjson::pstringview_pmm_intern( "" );
    REQUIRE( r.length == 0u );

    // Повторный вызов должен вернуть тот же chars_offset (дедупликация)
    auto r2 = pjson::pstringview_pmm_intern( "" );
    REQUIRE( r2.chars_offset == r.chars_offset );
    REQUIRE( r2.length == r.length );

    pjson::PamManager::destroy();
}

TEST_CASE( "pstringview_pmm_intern: nullptr treated as empty string", "[pstringview_pmm][helper][task14.4]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    auto r_empty   = pjson::pstringview_pmm_intern( "" );
    auto r_nullptr = pjson::pstringview_pmm_intern( nullptr );
    REQUIRE( r_nullptr.length == 0u );
    // nullptr должен давать тот же результат, что и пустая строка
    REQUIRE( r_nullptr.chars_offset == r_empty.chars_offset );

    pjson::PamManager::destroy();
}

TEST_CASE( "pstringview_pmm_reset: resets the dictionary for tests", "[pstringview_pmm][helper][task14.4]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    // Интернируем строку
    auto r1 = pjson::pstringview_pmm_intern( "unique_string_123" );
    REQUIRE( r1.chars_offset != 0u );

    // Сбрасываем словарь и пересоздаём менеджер
    pjson::pstringview_pmm_reset();
    pjson::PamManager::destroy();

    // После пересоздания словарь должен быть пустым
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    // Новое интернирование — новый chars_offset (но на том же месте, если менеджер пустой)
    auto r2 = pjson::pstringview_pmm_intern( "unique_string_123" );
    REQUIRE( r2.chars_offset != 0u );
    // После reset и recreate смещения могут совпадать (память переиспользуется),
    // но это нормально для тестов

    pjson::PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ ИСПОЛЬЗОВАНИЯ В КАЧЕСТВЕ КЛЮЧЕЙ
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pstringview_pmm: can be used as map keys (comparison)", "[pstringview_pmm][key][task14.4]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    // Создаём несколько ключей
    pjson::pstringview_pmm key1{};
    key1.intern( "name" );

    pjson::pstringview_pmm key2{};
    key2.intern( "age" );

    pjson::pstringview_pmm key3{};
    key3.intern( "name" ); // Дубликат key1

    // Проверяем уникальность и сравнение
    REQUIRE( key1 == key3 );                       // Одинаковые строки
    REQUIRE( key1 != key2 );                       // Разные строки
    REQUIRE( ( key1 < key2 ) != ( key2 < key1 ) ); // Строгий порядок

    pjson::PamManager::destroy();
}

TEST_CASE( "pstringview_pmm: offset aligned to granule size", "[pstringview_pmm][alignment][task14.4]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    pjson::pstringview_pmm sv{};
    sv.intern( "aligned_test" );

    // chars_offset должен быть выровнен по размеру гранулы PMM
    REQUIRE( pjson::is_aligned_offset( sv.chars_offset ) );

    pjson::PamManager::destroy();
}
