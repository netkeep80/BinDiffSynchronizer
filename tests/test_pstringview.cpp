#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <filesystem>
#include <type_traits>

#include "pstringview.h"

// Вспомогательная функция: удалить временный файл.
namespace
{
void rm_pstringview_file( const char* path )
{
    std::error_code ec;
    std::filesystem::remove( path, ec );
}
} // anonymous namespace

// =============================================================================
// Tests for pstringview (persistent read-only interned string)
// =============================================================================

// ---------------------------------------------------------------------------
// pstringview — layout checks
// ---------------------------------------------------------------------------
TEST_CASE( "pstringview: struct size is 2 * sizeof(void*)", "[pstringview][layout]" )
{
    REQUIRE( sizeof( pstringview ) == 2 * sizeof( void* ) );
    REQUIRE( sizeof( pstringview::length ) == sizeof( void* ) );
    REQUIRE( sizeof( pstringview::chars_offset ) == sizeof( void* ) );
}

// ---------------------------------------------------------------------------
// pstringview_table — layout checks
// ---------------------------------------------------------------------------
TEST_CASE( "pstringview_table: struct size is 3 * sizeof(void*)", "[pstringview][layout]" )
{
    REQUIRE( sizeof( pstringview_table ) == 3 * sizeof( void* ) );
}

// ---------------------------------------------------------------------------
// pstringview — default allocation gives empty string
// ---------------------------------------------------------------------------
TEST_CASE( "pstringview: zero-initialised pstringview gives empty string", "[pstringview][construct]" )
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();

    fptr<pstringview> fps;
    fps.New();

    REQUIRE( fps->empty() );
    REQUIRE( fps->size() == 0u );
    REQUIRE( std::strcmp( fps->c_str(), "" ) == 0 );

    fps.Delete();
}

// ---------------------------------------------------------------------------
// pstringview — intern short string
// ---------------------------------------------------------------------------
TEST_CASE( "pstringview: intern stores correct content", "[pstringview][intern]" )
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();

    fptr<pstringview> fps;
    fps.New();

    fps->intern( "hello" );
    REQUIRE( !fps->empty() );
    REQUIRE( fps->size() == 5u );
    REQUIRE( std::strcmp( fps->c_str(), "hello" ) == 0 );

    fps.Delete();
}

// ---------------------------------------------------------------------------
// pstringview — intern returns same chars_offset for duplicate strings
// ---------------------------------------------------------------------------
TEST_CASE( "pstringview: same string always yields same chars_offset (interning)", "[pstringview][intern]" )
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();

    fptr<pstringview> fps1;
    fps1.New();
    fps1->intern( "key" );

    fptr<pstringview> fps2;
    fps2.New();
    fps2->intern( "key" );

    // Интернирование гарантирует одинаковый chars_offset для одинаковых строк.
    REQUIRE( fps1->chars_offset == fps2->chars_offset );
    REQUIRE( fps1->length == fps2->length );
    REQUIRE( std::strcmp( fps1->c_str(), fps2->c_str() ) == 0 );

    fps1.Delete();
    fps2.Delete();
}

// ---------------------------------------------------------------------------
// pstringview — different strings have different chars_offset
// ---------------------------------------------------------------------------
TEST_CASE( "pstringview: different strings have different chars_offset", "[pstringview][intern]" )
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();

    fptr<pstringview> fps1;
    fps1.New();
    fps1->intern( "foo" );

    fptr<pstringview> fps2;
    fps2.New();
    fps2->intern( "bar" );

    REQUIRE( fps1->chars_offset != fps2->chars_offset );

    fps1.Delete();
    fps2.Delete();
}

// ---------------------------------------------------------------------------
// pstringview — operator== uses chars_offset comparison (O(1))
// ---------------------------------------------------------------------------
TEST_CASE( "pstringview: operator== compares by chars_offset", "[pstringview][compare]" )
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();

    fptr<pstringview> fps1;
    fps1.New();
    fps1->intern( "hello" );

    fptr<pstringview> fps2;
    fps2.New();
    fps2->intern( "hello" );

    fptr<pstringview> fps3;
    fps3.New();
    fps3->intern( "world" );

    REQUIRE( *fps1 == *fps2 );
    REQUIRE( !( *fps1 == *fps3 ) );
    REQUIRE( *fps1 == "hello" );
    REQUIRE( !( *fps1 == "world" ) );

    fps1.Delete();
    fps2.Delete();
    fps3.Delete();
}

// ---------------------------------------------------------------------------
// pstringview — operator< gives lexicographic order
// ---------------------------------------------------------------------------
TEST_CASE( "pstringview: operator< gives lexicographic order", "[pstringview][compare]" )
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();

    fptr<pstringview> fps_a;
    fps_a.New();
    fps_a->intern( "apple" );

    fptr<pstringview> fps_b;
    fps_b.New();
    fps_b->intern( "banana" );

    REQUIRE( *fps_a < *fps_b );
    REQUIRE( !( *fps_b < *fps_a ) );

    fps_a.Delete();
    fps_b.Delete();
}

// ---------------------------------------------------------------------------
// pstringview — intern empty string
// ---------------------------------------------------------------------------
TEST_CASE( "pstringview: intern empty string gives empty result", "[pstringview][intern]" )
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();

    fptr<pstringview> fps;
    fps.New();

    fps->intern( "" );
    REQUIRE( fps->empty() );
    REQUIRE( fps->size() == 0u );
    REQUIRE( std::strcmp( fps->c_str(), "" ) == 0 );

    fps.Delete();
}

// ---------------------------------------------------------------------------
// pstringview — intern nullptr treated as empty
// ---------------------------------------------------------------------------
TEST_CASE( "pstringview: intern nullptr treated as empty string", "[pstringview][intern]" )
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();

    fptr<pstringview> fps;
    fps.New();

    fps->intern( nullptr );
    REQUIRE( fps->empty() );
    REQUIRE( fps->size() == 0u );

    fps.Delete();
}

// ---------------------------------------------------------------------------
// pstringview — chars_offset is non-zero after intern (non-empty)
// ---------------------------------------------------------------------------
TEST_CASE( "pstringview: chars_offset is non-zero after intern non-empty string", "[pstringview][intern]" )
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();

    fptr<pstringview> fps;
    fps.New();

    REQUIRE( fps->chars_offset == 0u );

    fps->intern( "test" );
    REQUIRE( fps->chars_offset != 0u );
    REQUIRE( fps->length == 4u );

    fps.Delete();
}

// ---------------------------------------------------------------------------
// pstringview — many distinct strings all interned correctly
// ---------------------------------------------------------------------------
TEST_CASE( "pstringview: many distinct strings are all interned correctly", "[pstringview][intern]" )
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();

    static const char* words[] = { "alpha", "beta", "gamma", "delta", "epsilon",
                                   "zeta",  "eta",  "theta", "iota",  "kappa" };
    constexpr int      N       = 10;

    fptr<pstringview> fps[N];
    for ( int i = 0; i < N; i++ )
    {
        fps[i].New();
        fps[i]->intern( words[i] );
        REQUIRE( std::strcmp( fps[i]->c_str(), words[i] ) == 0 );
    }

    // Повторное интернирование — должны вернуться те же смещения.
    for ( int i = 0; i < N; i++ )
    {
        fptr<pstringview> dup;
        dup.New();
        dup->intern( words[i] );
        REQUIRE( dup->chars_offset == fps[i]->chars_offset );
        dup.Delete();
    }

    for ( int i = 0; i < N; i++ )
        fps[i].Delete();
}

// ---------------------------------------------------------------------------
// pstringview — pstringview_table trivially copyable
// ---------------------------------------------------------------------------
TEST_CASE( "pstringview_entry: is trivially copyable", "[pstringview][layout]" )
{
    REQUIRE( std::is_trivially_copyable<pstringview_entry>::value );
}

// =============================================================================
// Тесты Phase 2: словарь строк в ПАП — персистность, InternString, поиск
// =============================================================================

// ---------------------------------------------------------------------------
// Задача 2.1: pstringview_table хранится в ПАП и восстанавливается при Load
// Критерий приёмки фазы 2: «pstringview_table хранится в ПАП и восстанавливается
//   при загрузке образа»
// ---------------------------------------------------------------------------
TEST_CASE( "pstringview_table: survives PAM Save and Load (persistence)", "[pstringview][phase2][persist]" )
{
    const char* fname = "./test_pstringview_persist.pam";
    rm_pstringview_file( fname );

    uintptr_t saved_offset = 0;

    // Создаём ПАМ, интернируем строки, сохраняем.
    {
        pstringview_manager::reset();
        PersistentAddressSpace::Init( fname );

        fptr<pstringview> fps;
        fps.New();
        fps->intern( "persistent_key" );
        REQUIRE( std::strcmp( fps->c_str(), "persistent_key" ) == 0 );
        saved_offset = fps->chars_offset;
        REQUIRE( saved_offset != 0u );

        // Смещение таблицы должно быть сохранено в заголовке ПАМ.
        REQUIRE( PersistentAddressSpace::Get().GetStringTableOffset() != 0u );

        fps.Delete();
        PersistentAddressSpace::Get().Save();
    }

    // Перезагружаем ПАМ из файла.
    pstringview_manager::reset();
    PersistentAddressSpace::Init( fname );

    {
        // Таблица должна восстановиться из заголовка ПАМ.
        REQUIRE( PersistentAddressSpace::Get().GetStringTableOffset() != 0u );

        // Повторное интернирование той же строки должно вернуть тот же chars_offset.
        fptr<pstringview> fps;
        fps.New();
        fps->intern( "persistent_key" );
        REQUIRE( fps->chars_offset == saved_offset );
        REQUIRE( std::strcmp( fps->c_str(), "persistent_key" ) == 0 );

        fps.Delete();
    }

    pstringview_manager::reset();
    rm_pstringview_file( fname );
}

// ---------------------------------------------------------------------------
// Задача 2.1: два одинаковых intern("hello") дают одинаковый chars_offset
// Критерий приёмки фазы 2: «Два одинаковых intern("hello") дают одинаковый chars_offset»
// ---------------------------------------------------------------------------
TEST_CASE( "pstringview_table: two intern(same) calls give identical chars_offset", "[pstringview][phase2]" )
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();

    fptr<pstringview> fps1;
    fps1.New();
    fps1->intern( "hello" );

    fptr<pstringview> fps2;
    fps2.New();
    fps2->intern( "hello" );

    // Критерий приёмки фазы 2: идентичные строки → одно смещение.
    REQUIRE( fps1->chars_offset == fps2->chars_offset );
    REQUIRE( fps1->length == fps2->length );

    fps1.Delete();
    fps2.Delete();
}

// ---------------------------------------------------------------------------
// Задача 2.2: pam_intern_string — интернирование строки через уровень ПАМ
// ---------------------------------------------------------------------------
TEST_CASE( "pam_intern_string: returns InternResult with correct chars_offset and length",
           "[pstringview][phase2][intern]" )
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();

    auto r1 = pam_intern_string( "world" );
    REQUIRE( r1.length == 5u );
    REQUIRE( r1.chars_offset != 0u );
    auto& pam    = PersistentAddressSpace::Get();
    auto* chars1 = pam.Resolve<char>( r1.chars_offset );
    REQUIRE( chars1 != nullptr );
    REQUIRE( std::strcmp( chars1, "world" ) == 0 );

    // Повторный вызов — тот же chars_offset.
    auto r2 = pam_intern_string( "world" );
    REQUIRE( r2.chars_offset == r1.chars_offset );
    REQUIRE( r2.length == r1.length );
}

// ---------------------------------------------------------------------------
// Задача 2.2: pam_intern_string — разные строки имеют разные chars_offset
// ---------------------------------------------------------------------------
TEST_CASE( "pam_intern_string: different strings have different chars_offset", "[pstringview][phase2][intern]" )
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();

    auto r_a = pam_intern_string( "alpha" );
    auto r_b = pam_intern_string( "beta" );

    REQUIRE( r_a.chars_offset != r_b.chars_offset );
}

// ---------------------------------------------------------------------------
// Задача 2.2: pam_intern_string — пустая строка
// ---------------------------------------------------------------------------
TEST_CASE( "pam_intern_string: empty string gives length zero and valid chars_offset", "[pstringview][phase2][intern]" )
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();

    auto r = pam_intern_string( "" );
    REQUIRE( r.length == 0u );
    // Пустая строка: chars_offset может быть ненулевым (хранится нулевой терминатор).
    // Важно, что строка доступна и соответствует "".
    if ( r.chars_offset != 0u )
    {
        const char* s = PersistentAddressSpace::Get().Resolve<char>( r.chars_offset );
        REQUIRE( s != nullptr );
        REQUIRE( s[0] == '\0' );
    }

    // Повторный вызов должен вернуть тот же chars_offset (дедупликация).
    auto r2 = pam_intern_string( "" );
    REQUIRE( r2.chars_offset == r.chars_offset );
    REQUIRE( r2.length == r.length );
}

// ---------------------------------------------------------------------------
// Задача 2.2: pam_intern_string — nullptr трактуется как пустая строка
// ---------------------------------------------------------------------------
TEST_CASE( "pam_intern_string: nullptr treated as empty string", "[pstringview][phase2][intern]" )
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();

    auto r_empty   = pam_intern_string( "" );
    auto r_nullptr = pam_intern_string( nullptr );
    REQUIRE( r_nullptr.length == 0u );
    // nullptr должен давать тот же результат, что и пустая строка.
    REQUIRE( r_nullptr.chars_offset == r_empty.chars_offset );
}

// ---------------------------------------------------------------------------
// Задача 2.5: pam_search_strings — поиск строк, содержащих подстроку
// ---------------------------------------------------------------------------
TEST_CASE( "pam_search_strings: finds strings containing pattern", "[pstringview][phase2][search]" )
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();

    // Интернируем несколько строк.
    pam_intern_string( "user_name" );
    pam_intern_string( "user_email" );
    pam_intern_string( "product_id" );
    pam_intern_string( "product_name" );
    pam_intern_string( "order_id" );

    // Поиск по «user» должен вернуть 2 строки.
    auto results = pam_search_strings( "user" );
    REQUIRE( results.size() == 2u );
    for ( const auto& r : results )
        REQUIRE( std::strstr( r.value.c_str(), "user" ) != nullptr );

    // Поиск по «product» должен вернуть 2 строки.
    auto results2 = pam_search_strings( "product" );
    REQUIRE( results2.size() == 2u );

    // Поиск по «order» должен вернуть 1 строку.
    auto results3 = pam_search_strings( "order" );
    REQUIRE( results3.size() == 1u );
    REQUIRE( results3[0].value == "order_id" );

    // Поиск по несуществующей строке.
    auto results4 = pam_search_strings( "xyz_not_found" );
    REQUIRE( results4.empty() );
}

// ---------------------------------------------------------------------------
// Задача 2.5: pam_all_strings — возвращает все интернированные строки
// ---------------------------------------------------------------------------
TEST_CASE( "pam_all_strings: returns all interned strings", "[pstringview][phase2][search]" )
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();

    const char* words[] = { "alpha", "beta", "gamma", "delta" };
    for ( const char* w : words )
        pam_intern_string( w );

    auto all = pam_all_strings();
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
}

// ---------------------------------------------------------------------------
// Задача 2.5: pam_search_strings — пустой паттерн эквивалентен pam_all_strings
// ---------------------------------------------------------------------------
TEST_CASE( "pam_search_strings: empty pattern returns all strings", "[pstringview][phase2][search]" )
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();

    pam_intern_string( "foo" );
    pam_intern_string( "bar" );
    pam_intern_string( "baz" );

    auto all    = pam_all_strings();
    auto search = pam_search_strings( "" );
    REQUIRE( all.size() == search.size() );
    REQUIRE( all.size() == 3u );
}

// ---------------------------------------------------------------------------
// Задача 2.5: pam_search_results содержат корректные chars_offset и length
// ---------------------------------------------------------------------------
TEST_CASE( "pam_search_strings: results contain correct chars_offset and length", "[pstringview][phase2][search]" )
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();

    auto r = pam_intern_string( "check_me" );

    auto results = pam_search_strings( "check" );
    REQUIRE( results.size() == 1u );
    REQUIRE( results[0].chars_offset == r.chars_offset );
    REQUIRE( results[0].length == r.length );
    REQUIRE( results[0].value == "check_me" );
}

// ---------------------------------------------------------------------------
// Критерий приёмки фазы 2: GetStringTableOffset() обновляется после первого интернирования
// ---------------------------------------------------------------------------
TEST_CASE( "pstringview_table: GetStringTableOffset non-zero after intern", "[pstringview][phase2]" )
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();

    // До первого интернирования таблица может ещё не быть создана.
    // (После Reset() _string_table_offset = 0 в ПАМ.)

    pam_intern_string( "trigger_creation" );

    // После первого интернирования таблица должна быть зарегистрирована в ПАМ.
    REQUIRE( PersistentAddressSpace::Get().GetStringTableOffset() != 0u );
}
