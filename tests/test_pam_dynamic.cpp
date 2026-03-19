#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <filesystem>

#include "pam_pmm.h"

using namespace pjson;

/// Тип PMM pstring для удобства.
using pmm_pstring = PamManager::pstring;

// =============================================================================
// Тесты на динамичность менеджера ПАП (issue #56)
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
 * Присвоить значение pmm_pstring, находящемуся в ПАП.
 * Работает через стековую копию для обхода realloc-безопасности:
 * assign() может расширить хранилище, инвалидируя указатель на объект.
 */
inline void pstring_assign_safe( uintptr_t offset, const char* s )
{
    pmm_pstring* ps = pam_pmm_resolve<pmm_pstring>( offset );
    if ( ps == nullptr )
        return;

    // Копируем pstring на стек, выполняем assign, записываем обратно.
    pmm_pstring tmp = *ps;
    tmp.assign( s );

    // Переразрешаем после возможного realloc.
    ps = pam_pmm_resolve<pmm_pstring>( offset );
    if ( ps != nullptr )
        *ps = tmp;
}

/**
 * Создать N именованных pmm_pstring в ПАП.
 * Каждая pmm_pstring получает имя "sNNNNNN" и содержимое, равное этому же имени.
 * Возвращает вектор смещений (offsets) созданных pmm_pstring в том же порядке.
 */
std::vector<uintptr_t> create_pstrings( unsigned count )
{
    std::vector<uintptr_t> offsets;
    offsets.reserve( count );

    char name_buf[8];
    for ( unsigned i = 0; i < count; i++ )
    {
        make_pam_name( name_buf, i );
        // Создаём pmm_pstring в ПАП под именем "sNNNNNN".
        uintptr_t offset = pam_pmm_create<pmm_pstring>( name_buf );
        REQUIRE( offset != 0u );

        // Инициализируем содержимое pmm_pstring её же именем.
        pstring_assign_safe( offset, name_buf );

        offsets.push_back( offset );
    }
    return offsets;
}

/**
 * Проверить N именованных pmm_pstring:
 *   1. Найти каждую в ПАМ по имени (pam_pmm_find).
 *   2. Убедиться, что смещение совпадает с записанным при создании.
 *   3. Убедиться, что содержимое pmm_pstring совпадает с её именем.
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

        // Проверка содержимого pmm_pstring.
        const pmm_pstring* ps = pam_pmm_resolve<pmm_pstring>( found );
        REQUIRE( ps != nullptr );
        REQUIRE( std::strcmp( ps->c_str(), name_buf ) == 0 );
    }
}

/**
 * Удалить все pmm_pstring по сохранённым смещениям.
 * Вызывает pmm_pstring::free_data() для освобождения символьных данных в ПАП,
 * затем pam_pmm_delete() для освобождения слота самой pmm_pstring.
 */
void delete_pstrings( const std::vector<uintptr_t>& offsets )
{
    for ( uintptr_t offset : offsets )
    {
        pmm_pstring* ps = pam_pmm_resolve<pmm_pstring>( offset );
        if ( ps != nullptr )
            ps->free_data();
        pam_pmm_delete( offset );
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Быстрый тест динамичности ПАМ: 10 000 pmm_pstring
// ---------------------------------------------------------------------------
TEST_CASE( "PAM dynamic: create and verify 10000 named pstrings", "[pam][dynamic][pstring]" )
{
    // Подготовка: инициализируем PMM in-memory.
    pam_pmm_init( nullptr );

    // Шаг 1: создаём 10 000 именованных pmm_pstring.
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

        const pmm_pstring* ps_first = pam_pmm_resolve<pmm_pstring>( off_first );
        const pmm_pstring* ps_last  = pam_pmm_resolve<pmm_pstring>( off_last );
        REQUIRE( ps_first != nullptr );
        REQUIRE( ps_last != nullptr );
        REQUIRE( *ps_first != *ps_last );
    }

    // Шаг 4: чистим за собой.
    delete_pstrings( offsets );

    pam_pmm_destroy();
}

// ---------------------------------------------------------------------------
// Тест: содержимое pmm_pstring уникально для каждой записи
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
        const pmm_pstring* ps_i = pam_pmm_resolve<pmm_pstring>( offsets[i] );
        const pmm_pstring* ps_j = pam_pmm_resolve<pmm_pstring>( offsets[j] );
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
// Нагрузочный тест ПАМ: 100 000 именованных pmm_pstring
// ---------------------------------------------------------------------------
TEST_CASE( "PAM dynamic stress: create and verify 100k named pstrings", "[pam][dynamic][pstring][stress]" )
{
    pam_pmm_init( nullptr );

    // Шаг 1: создаём 100 000 именованных pmm_pstring.
    std::vector<uintptr_t> offsets;
    offsets.reserve( PAM_DYNAMIC_LARGE_COUNT );

    char name_buf[8];
    for ( unsigned i = 0; i < PAM_DYNAMIC_LARGE_COUNT; i++ )
    {
        make_pam_name( name_buf, i );
        uintptr_t offset = pam_pmm_create<pmm_pstring>( name_buf );
        REQUIRE( offset != 0u );
        pstring_assign_safe( offset, name_buf );
        offsets.push_back( offset );
    }
    REQUIRE( offsets.size() == PAM_DYNAMIC_LARGE_COUNT );

    // Шаг 2: находим каждую pmm_pstring по сохранённому смещению и проверяем.
    for ( unsigned i = 0; i < PAM_DYNAMIC_LARGE_COUNT; i++ )
    {
        make_pam_name( name_buf, i );
        const pmm_pstring* ps = pam_pmm_resolve<pmm_pstring>( offsets[i] );
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

        const pmm_pstring* ps_first = pam_pmm_resolve<pmm_pstring>( off_first );
        const pmm_pstring* ps_last  = pam_pmm_resolve<pmm_pstring>( off_last );
        REQUIRE( ps_first != nullptr );
        REQUIRE( ps_last != nullptr );
        REQUIRE( *ps_first != *ps_last );
        REQUIRE( std::strcmp( ps_first->c_str(), name_first ) == 0 );
        REQUIRE( std::strcmp( ps_last->c_str(), name_last ) == 0 );
    }

    // Шаг 4: чистим за собой.
    for ( uintptr_t offset : offsets )
    {
        pmm_pstring* ps = pam_pmm_resolve<pmm_pstring>( offset );
        if ( ps != nullptr )
            ps->free_data();
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
        // Создаём 100 именованных pmm_pstring.
        std::vector<uintptr_t> offsets;
        offsets.reserve( COUNT );
        for ( unsigned i = 0; i < COUNT; i++ )
        {
            make_pam_name( name_buf, i );
            uintptr_t offset = pam_pmm_create<pmm_pstring>( name_buf );
            REQUIRE( offset != 0u );
            pstring_assign_safe( offset, name_buf );
            offsets.push_back( offset );
        }

        // Удаляем все pmm_pstring.
        for ( uintptr_t offset : offsets )
        {
            pmm_pstring* ps = pam_pmm_resolve<pmm_pstring>( offset );
            if ( ps != nullptr )
                ps->free_data();
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
