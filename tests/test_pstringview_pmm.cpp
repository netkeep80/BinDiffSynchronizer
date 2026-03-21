/**
 * @file test_pstringview_pmm.cpp
 * @brief Тесты для pstringview_pmm и API словаря строк.
 *
 * Тесты проверяют корректность интернирования, поиска и персистентности
 * read-only строк через pam_intern_string / pam_search_strings / pam_all_strings.
 *
 * Структура pstringview_pmm удалена (Issue #167, План 2.5).
 * Тесты переведены на pam_intern_string() / pam_search_strings() из pam_pmm.h.
 */

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <filesystem>
#include <type_traits>

#include "pstringview_pmm.h"
#include "pam_pmm.h"

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ БАЗОВЫХ ОПЕРАЦИЙ
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pam_intern_string: intern stores correct content", "[pstringview_pmm][intern]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    auto r = pjson::pam_intern_string( "hello" );

    REQUIRE( r.length == 5u );
    REQUIRE( r.chars_offset != 0u );

    const char* s = pjson::pmm_resolve<char>( r.chars_offset );
    REQUIRE( s != nullptr );
    REQUIRE( std::strcmp( s, "hello" ) == 0 );

    pjson::PamManager::destroy();
}

TEST_CASE( "pam_intern_string: same string always yields same chars_offset (interning)", "[pstringview_pmm][intern]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    auto r1 = pjson::pam_intern_string( "key" );
    auto r2 = pjson::pam_intern_string( "key" );

    // Интернирование гарантирует одинаковый chars_offset для одинаковых строк
    REQUIRE( r1.chars_offset == r2.chars_offset );
    REQUIRE( r1.length == r2.length );

    pjson::PamManager::destroy();
}

TEST_CASE( "pam_intern_string: different strings have different chars_offset", "[pstringview_pmm][intern]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    auto r1 = pjson::pam_intern_string( "foo" );
    auto r2 = pjson::pam_intern_string( "bar" );

    REQUIRE( r1.chars_offset != r2.chars_offset );

    pjson::PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ СРАВНЕНИЯ ЧЕРЕЗ chars_offset
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pam_intern_string: O(1) equality via chars_offset", "[pstringview_pmm][compare]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    auto r1 = pjson::pam_intern_string( "hello" );
    auto r2 = pjson::pam_intern_string( "hello" );
    auto r3 = pjson::pam_intern_string( "world" );

    // Одинаковые строки → одинаковый chars_offset (O(1) сравнение)
    REQUIRE( r1.chars_offset == r2.chars_offset );
    REQUIRE( r1.chars_offset != r3.chars_offset );

    pjson::PamManager::destroy();
}

TEST_CASE( "pam_intern_string: lexicographic order via strcmp", "[pstringview_pmm][compare]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    auto r_a = pjson::pam_intern_string( "apple" );
    auto r_b = pjson::pam_intern_string( "banana" );

    const char* s_a = pjson::pmm_resolve<char>( r_a.chars_offset );
    const char* s_b = pjson::pmm_resolve<char>( r_b.chars_offset );

    REQUIRE( std::strcmp( s_a, s_b ) < 0 );
    REQUIRE( std::strcmp( s_b, s_a ) > 0 );

    pjson::PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ ПУСТЫХ СТРОК
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pam_intern_string: empty string gives length zero", "[pstringview_pmm][intern]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    auto r = pjson::pam_intern_string( "" );
    REQUIRE( r.length == 0u );

    // Повторный вызов должен вернуть тот же chars_offset (дедупликация)
    auto r2 = pjson::pam_intern_string( "" );
    REQUIRE( r2.chars_offset == r.chars_offset );
    REQUIRE( r2.length == r.length );

    pjson::PamManager::destroy();
}

TEST_CASE( "pam_intern_string: nullptr treated as empty string", "[pstringview_pmm][intern]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    auto r_empty   = pjson::pam_intern_string( "" );
    auto r_nullptr = pjson::pam_intern_string( nullptr );
    REQUIRE( r_nullptr.length == 0u );
    // nullptr должен давать тот же результат, что и пустая строка
    REQUIRE( r_nullptr.chars_offset == r_empty.chars_offset );

    pjson::PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ СОСТОЯНИЯ ДАННЫХ
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pam_intern_string: chars_offset is non-zero after intern non-empty string", "[pstringview_pmm][intern]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    auto r = pjson::pam_intern_string( "test" );
    REQUIRE( r.chars_offset != 0u );
    REQUIRE( r.length == 4u );

    pjson::PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ МНОЖЕСТВЕННЫХ СТРОК
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pam_intern_string: many distinct strings are all interned correctly", "[pstringview_pmm][intern]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    static const char* words[] = { "alpha", "beta", "gamma", "delta", "epsilon",
                                   "zeta",  "eta",  "theta", "iota",  "kappa" };
    constexpr int      N       = 10;

    pjson::pam_intern_result results[N];
    for ( int i = 0; i < N; i++ )
    {
        results[i] = pjson::pam_intern_string( words[i] );
        const char* s = pjson::pmm_resolve<char>( results[i].chars_offset );
        REQUIRE( std::strcmp( s, words[i] ) == 0 );
    }

    // Повторное интернирование — должны вернуться те же смещения
    for ( int i = 0; i < N; i++ )
    {
        auto dup = pjson::pam_intern_string( words[i] );
        REQUIRE( dup.chars_offset == results[i].chars_offset );
    }

    pjson::PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ ХЕЛПЕР-ФУНКЦИЙ (pam_intern_string)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pam_intern_string: returns correct values", "[pstringview_pmm][helper]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    auto r1 = pjson::pam_intern_string( "world" );
    REQUIRE( r1.length == 5u );
    REQUIRE( r1.chars_offset != 0u );

    // Проверяем, что можно получить строку по смещению
    const char* s = pjson::pmm_resolve<char>( r1.chars_offset );
    REQUIRE( s != nullptr );
    REQUIRE( std::strcmp( s, "world" ) == 0 );

    // Повторный вызов — тот же chars_offset
    auto r2 = pjson::pam_intern_string( "world" );
    REQUIRE( r2.chars_offset == r1.chars_offset );
    REQUIRE( r2.length == r1.length );

    pjson::PamManager::destroy();
}

TEST_CASE( "pstringview_pmm_reset: resets the dictionary for tests", "[pstringview_pmm][helper]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    // Интернируем строку
    auto r1 = pjson::pam_intern_string( "unique_string_123" );
    REQUIRE( r1.chars_offset != 0u );

    // Сбрасываем словарь и пересоздаём менеджер
    pjson::pstringview_pmm_reset();
    pjson::PamManager::destroy();

    // После пересоздания словарь должен быть пустым
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    // Новое интернирование — новый chars_offset (но на том же месте, если менеджер пустой)
    auto r2 = pjson::pam_intern_string( "unique_string_123" );
    REQUIRE( r2.chars_offset != 0u );
    // После reset и recreate смещения могут совпадать (память переиспользуется),
    // но это нормально для тестов

    pjson::PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ ИСПОЛЬЗОВАНИЯ В КАЧЕСТВЕ КЛЮЧЕЙ
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pam_intern_string: can be used as map keys (O(1) comparison via chars_offset)", "[pstringview_pmm][key]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    // Создаём несколько ключей
    auto key1 = pjson::pam_intern_string( "name" );
    auto key2 = pjson::pam_intern_string( "age" );
    auto key3 = pjson::pam_intern_string( "name" ); // Дубликат key1

    // Проверяем уникальность и сравнение
    REQUIRE( key1.chars_offset == key3.chars_offset ); // Одинаковые строки
    REQUIRE( key1.chars_offset != key2.chars_offset ); // Разные строки

    // Лексикографический порядок
    const char* s1 = pjson::pmm_resolve<char>( key1.chars_offset );
    const char* s2 = pjson::pmm_resolve<char>( key2.chars_offset );
    bool        lt = std::strcmp( s1, s2 ) < 0;
    bool        gt = std::strcmp( s2, s1 ) < 0;
    REQUIRE( lt != gt ); // Строгий порядок

    pjson::PamManager::destroy();
}

TEST_CASE( "pam_intern_string: chars_offset resolves to string data", "[pstringview_pmm][alignment]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    auto r = pjson::pam_intern_string( "aligned_test" );

    // chars_offset указывает на str[] внутри блока pmm_pstringview.
    // Это НЕ гранульно выровненное смещение — оно указывает на символьные данные,
    // которые можно разрешить через pmm_resolve<char>(chars_offset).
    REQUIRE( r.chars_offset != 0 );
    const char* resolved = pjson::pmm_resolve<char>( r.chars_offset );
    REQUIRE( resolved != nullptr );
    REQUIRE( std::strcmp( resolved, "aligned_test" ) == 0 );

    pjson::PamManager::destroy();
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ ПЕРСИСТНОСТИ (PAM save/load)
// ═══════════════════════════════════════════════════════════════════════════

namespace
{
void rm_pstringview_file( const char* path )
{
    std::error_code ec;
    std::filesystem::remove( path, ec );
}
} // anonymous namespace

TEST_CASE( "pam_intern_string: survives PAM Save and Load (persistence)", "[pstringview_pmm][persist]" )
{
    const char* fname = "./test_pstringview_pmm_persist.pam";
    rm_pstringview_file( fname );

    uintptr_t saved_offset = 0;

    // Создаём ПАМ, интернируем строки, сохраняем.
    {
        pjson::pstringview_pmm_reset();
        pjson::pam_pmm_init( fname );

        auto r = pjson::pam_intern_string( "persistent_key" );
        const char* s = pjson::pmm_resolve<char>( r.chars_offset );
        REQUIRE( std::strcmp( s, "persistent_key" ) == 0 );
        saved_offset = r.chars_offset;
        REQUIRE( saved_offset != 0u );

        pjson::pam_pmm_save();
    }

    // Перезагружаем ПАМ из файла.
    pjson::pstringview_pmm_reset();
    pjson::pam_pmm_init( fname );

    {
        // Повторное интернирование той же строки должно вернуть тот же chars_offset.
        auto r = pjson::pam_intern_string( "persistent_key" );
        REQUIRE( r.chars_offset == saved_offset );
        const char* s = pjson::pmm_resolve<char>( r.chars_offset );
        REQUIRE( std::strcmp( s, "persistent_key" ) == 0 );
    }

    pjson::pstringview_pmm_reset();
    pjson::PamManager::destroy();
    rm_pstringview_file( fname );
}

// ═══════════════════════════════════════════════════════════════════════════
// ТЕСТЫ pam_search_strings / pam_all_strings
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE( "pam_search_strings: finds strings containing pattern", "[pstringview_pmm][search]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    // Интернируем несколько строк.
    pjson::pam_intern_string( "user_name" );
    pjson::pam_intern_string( "user_email" );
    pjson::pam_intern_string( "product_id" );
    pjson::pam_intern_string( "product_name" );
    pjson::pam_intern_string( "order_id" );

    // Поиск по «user» должен вернуть 2 строки.
    auto results = pjson::pam_search_strings( "user" );
    REQUIRE( results.size() == 2u );
    for ( const auto& r : results )
        REQUIRE( std::strstr( r.value.c_str(), "user" ) != nullptr );

    // Поиск по «product» должен вернуть 2 строки.
    auto results2 = pjson::pam_search_strings( "product" );
    REQUIRE( results2.size() == 2u );

    // Поиск по «order» должен вернуть 1 строку.
    auto results3 = pjson::pam_search_strings( "order" );
    REQUIRE( results3.size() == 1u );
    REQUIRE( results3[0].value == "order_id" );

    // Поиск по несуществующей строке.
    auto results4 = pjson::pam_search_strings( "xyz_not_found" );
    REQUIRE( results4.empty() );

    pjson::PamManager::destroy();
}

TEST_CASE( "pam_all_strings: returns all interned strings", "[pstringview_pmm][search]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    const char* words[] = { "alpha", "beta", "gamma", "delta" };
    for ( const char* w : words )
        pjson::pam_intern_string( w );

    auto all = pjson::pam_all_strings();
    REQUIRE( all.size() == 4u );

    // Все 4 слова должны присутствовать в результатах.
    for ( const char* w : words )
    {
        bool found = false;
        for ( const auto& r : all )
            if ( r.value == w )
            {
                found = true;
                break;
            }
        REQUIRE( found );
    }

    pjson::PamManager::destroy();
}

TEST_CASE( "pam_search_strings: empty pattern returns all strings", "[pstringview_pmm][search]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    pjson::pam_intern_string( "foo" );
    pjson::pam_intern_string( "bar" );
    pjson::pam_intern_string( "baz" );

    auto all    = pjson::pam_all_strings();
    auto search = pjson::pam_search_strings( "" );
    REQUIRE( all.size() == search.size() );
    REQUIRE( all.size() == 3u );

    pjson::PamManager::destroy();
}

TEST_CASE( "pam_search_strings: results contain correct chars_offset and length", "[pstringview_pmm][search]" )
{
    pjson::PamManager::create( 64 * 1024 );
    pjson::pstringview_pmm_reset();

    auto r = pjson::pam_intern_string( "check_me" );

    auto results = pjson::pam_search_strings( "check" );
    REQUIRE( results.size() == 1u );
    REQUIRE( results[0].chars_offset == r.chars_offset );
    REQUIRE( results[0].length == r.length );
    REQUIRE( results[0].value == "check_me" );

    pjson::PamManager::destroy();
}
