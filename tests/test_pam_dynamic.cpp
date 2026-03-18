#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <filesystem>

#include "pam_pmm.h"
#include "pstring_pmm.h"

using namespace pjson;

// =============================================================================
// Тесты на динамичность менеджера ПАП (issue #56)
// Обновлено для PMM — Задача 14.11.
//
// Проверяет способность PMM динамически расширяться при создании большого
// числа именованных объектов, а также корректность хранения и поиска.
// =============================================================================

namespace
{

/// Количество записей для быстрого теста на динамичность ПАМ.
constexpr unsigned PAM_DYNAMIC_SMALL_COUNT = 10'000;

/// Количество записей для нагрузочного теста (100000, опционально).
constexpr unsigned PAM_DYNAMIC_LARGE_COUNT = 100'000;

/// Вспомогательная функция: удалить временный файл.
void rm_pam_dyn_file( const char* path )
{
    std::error_code ec;
    std::filesystem::remove( path, ec );
}

/**
 * Сформировать уникальное PAM-имя для N-го объекта.
 * Формат: "s%06u" — 's' + 6 цифр, итого не более 7 байт + null = 8 байт.
 */
inline void make_pam_name( char* buf, unsigned idx )
{
    std::snprintf( buf, 8, "s%06u", idx );
}

/**
 * Создать N именованных pstring_pmm в ПАП.
 * Каждая pstring_pmm получает имя "sNNNNNN" и содержимое, равное этому же имени.
 * Возвращает вектор смещений (offsets) созданных pstring_pmm в том же порядке.
 */
std::vector<uintptr_t> create_pstrings( unsigned count )
{
    std::vector<uintptr_t> offsets;
    offsets.reserve( count );

    char name_buf[8];
    for ( unsigned i = 0; i < count; i++ )
    {
        make_pam_name( name_buf, i );
        // Создаём pstring_pmm в ПАП под именем "sNNNNNN".
        uintptr_t offset = pam_pmm_create<pstring_pmm>( name_buf );
        REQUIRE( offset != 0u );

        // Инициализируем содержимое pstring_pmm её же именем.
        pstring_pmm* ps = pam_pmm_resolve<pstring_pmm>( offset );
        REQUIRE( ps != nullptr );
        ps->assign( name_buf, offset );

        offsets.push_back( offset );
    }
    return offsets;
}

/**
 * Проверить N именованных pstring_pmm:
 *   1. Найти каждую в ПАМ по имени (pam_pmm_find).
 *   2. Убедиться, что смещение совпадает с записанным при создании.
 *   3. Убедиться, что содержимое pstring_pmm совпадает с её именем.
 */
void verify_pstrings( const std::vector<uintptr_t>& offsets )
{
    char name_buf[8];
    for ( unsigned i = 0; i < static_cast<unsigned>( offsets.size() ); i++ )
    {
        make_pam_name( name_buf, i );

        // Поиск по имени в ПАМ.
        uintptr_t found = pam_pmm_find( name_buf );
        REQUIRE( found == offsets[i] );

        // Проверка содержимого pstring_pmm.
        const pstring_pmm* ps = pam_pmm_resolve<pstring_pmm>( found );
        REQUIRE( ps != nullptr );
        REQUIRE( std::strcmp( ps->c_str(), name_buf ) == 0 );
    }
}

/**
 * Удалить все pstring_pmm по сохранённым смещениям.
 * Вызывает pstring_pmm::clear() для освобождения символьных данных в ПАП,
 * затем pam_pmm_delete() для освобождения слота самой pstring_pmm.
 */
void delete_pstrings( const std::vector<uintptr_t>& offsets )
{
    for ( uintptr_t offset : offsets )
    {
        pstring_pmm* ps = pam_pmm_resolve<pstring_pmm>( offset );
        if ( ps != nullptr )
            ps->clear( offset );
        pam_pmm_delete( offset );
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Быстрый тест динамичности ПАМ: 10 000 pstring_pmm
// ---------------------------------------------------------------------------
TEST_CASE( "PAM dynamic: create and verify 10000 named pstrings", "[pam][dynamic][pstring]" )
{
    // Подготовка: инициализируем PMM in-memory.
    pam_pmm_init( nullptr );

    // Шаг 1: создаём 10 000 именованных pstring_pmm.
    std::vector<uintptr_t> offsets = create_pstrings( PAM_DYNAMIC_SMALL_COUNT );
    REQUIRE( offsets.size() == PAM_DYNAMIC_SMALL_COUNT );

    // Шаг 2: находим и проверяем все записи.
    verify_pstrings( offsets );

    // Шаг 3: убеждаемся в уникальности содержимого.
    {
        char name_first[8], name_last[8];
        make_pam_name( name_first, 0 );
        make_pam_name( name_last, PAM_DYNAMIC_SMALL_COUNT - 1 );

        uintptr_t off_first = pam_pmm_find( name_first );
        uintptr_t off_last  = pam_pmm_find( name_last );
        REQUIRE( off_first != off_last );

        const pstring_pmm* ps_first = pam_pmm_resolve<pstring_pmm>( off_first );
        const pstring_pmm* ps_last  = pam_pmm_resolve<pstring_pmm>( off_last );
        REQUIRE( ps_first != nullptr );
        REQUIRE( ps_last != nullptr );
        REQUIRE( *ps_first != *ps_last );
    }

    // Шаг 4: чистим за собой.
    delete_pstrings( offsets );

    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// Тест: содержимое pstring_pmm уникально для каждой записи
// ---------------------------------------------------------------------------
TEST_CASE( "PAM dynamic: pstring content is unique for each named entry", "[pam][dynamic][pstring][unique]" )
{
    pam_pmm_init( nullptr );

    constexpr unsigned     COUNT   = 1000u;
    std::vector<uintptr_t> offsets = create_pstrings( COUNT );
    REQUIRE( offsets.size() == COUNT );

    // Проверяем 100 случайных пар (i, i+COUNT/2) на неравенство содержимого.
    for ( unsigned i = 0; i < 100u; i++ )
    {
        unsigned           j    = i + COUNT / 2;
        const pstring_pmm* ps_i = pam_pmm_resolve<pstring_pmm>( offsets[i] );
        const pstring_pmm* ps_j = pam_pmm_resolve<pstring_pmm>( offsets[j] );
        REQUIRE( ps_i != nullptr );
        REQUIRE( ps_j != nullptr );
        REQUIRE( *ps_i != *ps_j );
    }

    delete_pstrings( offsets );
    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// Тест: таблица слотов ПАМ динамически расширяется
// ---------------------------------------------------------------------------
TEST_CASE( "PAM dynamic: slot table grows beyond initial capacity", "[pam][dynamic][slots]" )
{
    pam_pmm_init( nullptr );

    // Создаём 1000 объектов.
    constexpr unsigned     COUNT   = 1000u;
    std::vector<uintptr_t> offsets = create_pstrings( COUNT );
    REQUIRE( offsets.size() == COUNT );

    // Все объекты должны быть найдены.
    char name_buf[8];
    for ( unsigned i = 0; i < COUNT; i++ )
    {
        make_pam_name( name_buf, i );
        uintptr_t found = pam_pmm_find( name_buf );
        REQUIRE( found != 0u );
        REQUIRE( found == offsets[i] );
    }

    delete_pstrings( offsets );
    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// Нагрузочный тест ПАМ: 100 000 именованных pstring_pmm
// ---------------------------------------------------------------------------
TEST_CASE( "PAM dynamic stress: create and verify 100k named pstrings", "[pam][dynamic][pstring][stress]" )
{
    pam_pmm_init( nullptr );

    // Шаг 1: создаём 100 000 именованных pstring_pmm.
    std::vector<uintptr_t> offsets;
    offsets.reserve( PAM_DYNAMIC_LARGE_COUNT );

    char name_buf[8];
    for ( unsigned i = 0; i < PAM_DYNAMIC_LARGE_COUNT; i++ )
    {
        make_pam_name( name_buf, i );
        uintptr_t offset = pam_pmm_create<pstring_pmm>( name_buf );
        REQUIRE( offset != 0u );
        pstring_pmm* ps = pam_pmm_resolve<pstring_pmm>( offset );
        REQUIRE( ps != nullptr );
        ps->assign( name_buf, offset );
        offsets.push_back( offset );
    }
    REQUIRE( offsets.size() == PAM_DYNAMIC_LARGE_COUNT );

    // Шаг 2: находим каждую pstring_pmm по сохранённому смещению и проверяем.
    for ( unsigned i = 0; i < PAM_DYNAMIC_LARGE_COUNT; i++ )
    {
        make_pam_name( name_buf, i );
        const pstring_pmm* ps = pam_pmm_resolve<pstring_pmm>( offsets[i] );
        REQUIRE( ps != nullptr );
        REQUIRE( std::strcmp( ps->c_str(), name_buf ) == 0 );
    }

    // Шаг 3: выборочно находим через pam_pmm_find() первые и последние записи.
    {
        char name_first[8], name_last[8];
        make_pam_name( name_first, 0 );
        make_pam_name( name_last, PAM_DYNAMIC_LARGE_COUNT - 1 );

        uintptr_t off_first = pam_pmm_find( name_first );
        uintptr_t off_last  = pam_pmm_find( name_last );
        REQUIRE( off_first == offsets[0] );
        REQUIRE( off_last == offsets[PAM_DYNAMIC_LARGE_COUNT - 1] );

        const pstring_pmm* ps_first = pam_pmm_resolve<pstring_pmm>( off_first );
        const pstring_pmm* ps_last  = pam_pmm_resolve<pstring_pmm>( off_last );
        REQUIRE( ps_first != nullptr );
        REQUIRE( ps_last != nullptr );
        REQUIRE( *ps_first != *ps_last );
        REQUIRE( std::strcmp( ps_first->c_str(), name_first ) == 0 );
        REQUIRE( std::strcmp( ps_last->c_str(), name_last ) == 0 );
    }

    // Шаг 4: чистим за собой.
    for ( uintptr_t offset : offsets )
    {
        pstring_pmm* ps = pam_pmm_resolve<pstring_pmm>( offset );
        if ( ps != nullptr )
            ps->clear( offset );
        pam_pmm_delete( offset );
    }

    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// Тест: PMM повторно использует память после удаления объектов.
// ---------------------------------------------------------------------------
TEST_CASE( "PAM reuse: memory is reused after deletion cycles", "[pam][reuse][dynamic]" )
{
    pam_pmm_init( nullptr );

    constexpr unsigned COUNT  = 100u;
    constexpr unsigned CYCLES = 10u;

    uintptr_t data_size_after_first_cycle = 0;

    char name_buf[8];
    for ( unsigned cycle = 0; cycle < CYCLES; cycle++ )
    {
        // Создаём 100 именованных pstring_pmm.
        std::vector<uintptr_t> offsets;
        offsets.reserve( COUNT );
        for ( unsigned i = 0; i < COUNT; i++ )
        {
            make_pam_name( name_buf, i );
            uintptr_t offset = pam_pmm_create<pstring_pmm>( name_buf );
            REQUIRE( offset != 0u );
            pstring_pmm* ps = pam_pmm_resolve<pstring_pmm>( offset );
            REQUIRE( ps != nullptr );
            ps->assign( name_buf, offset );
            offsets.push_back( offset );
        }

        // Удаляем все pstring_pmm.
        for ( uintptr_t offset : offsets )
        {
            pstring_pmm* ps = pam_pmm_resolve<pstring_pmm>( offset );
            if ( ps != nullptr )
                ps->clear( offset );
            pam_pmm_delete( offset );
        }

        uintptr_t data_size = pam_pmm_get_data_size();

        if ( cycle == 0 )
        {
            // Запоминаем размер после первого цикла.
            data_size_after_first_cycle = data_size;
        }
        else
        {
            // Начиная со второго цикла размер данных не должен расти.
            REQUIRE( data_size <= data_size_after_first_cycle );
        }
    }

    pam_pmm_destroy();
}
