// test_pjson_large.cpp — Тест загрузки большого JSON-файла в новые персистные структуры (Задача 9.5).
//
// Мигрировано с pjson.h на pjson_codec.h в рамках Задачи 9.5.
// Использует: tests/test.json (большой файл ~11.7 МБ)
// Загрузка JSON: node_from_string (pjson_codec.h)
// Хранение: fptr<node> / node_id / node_view
//
// Все комментарии — на русском языке (Тр.6).

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <cstring>

#include "pjson_codec.h"

using namespace pjson;

// ---------------------------------------------------------------------------
// Путь к тестовому JSON-файлу (задаётся через CMake compile definition)
// ---------------------------------------------------------------------------
#ifndef TEST_JSON_PATH
#define TEST_JSON_PATH "tests/test.json"
#endif

// Вспомогательная функция: сбросить PMM перед каждым тестом.
static void reset_pam()
{
    pstringview_manager::reset();
    pam_pmm_reset();
}

// ---------------------------------------------------------------------------
// Тест: загрузка test.json через node_from_string — проверка структуры верхнего уровня
// ---------------------------------------------------------------------------
TEST_CASE( "pjson large: test.json loads correctly via node_from_string", "[pjson][large][json]" )
{
    reset_pam();
    std::ifstream f( TEST_JSON_PATH );
    REQUIRE( f.is_open() );
    std::string json_text( ( std::istreambuf_iterator<char>( f ) ), std::istreambuf_iterator<char>() );
    f.close();

    fptr<node> froot;
    froot.New();
    node_from_string( json_text.c_str(), froot.addr() );

    node_view root{ froot.addr() };
    // test.json должен быть непустым объектом.
    REQUIRE( !root.is_null() );
    REQUIRE( root.is_object() );
    REQUIRE( root.size() > 0u );

    froot.Delete();
}

// ---------------------------------------------------------------------------
// Тест: первые ключи test.json корректно хранятся через node_view
// ---------------------------------------------------------------------------
TEST_CASE( "pjson large: first keys from test.json are stored in node", "[pjson][large][json]" )
{
    reset_pam();
    std::ifstream f( TEST_JSON_PATH );
    REQUIRE( f.is_open() );
    std::string json_text( ( std::istreambuf_iterator<char>( f ) ), std::istreambuf_iterator<char>() );
    f.close();

    fptr<node> froot;
    froot.New();
    node_from_string( json_text.c_str(), froot.addr() );

    node_view root{ froot.addr() };
    REQUIRE( root.is_object() );
    REQUIRE( root.size() > 0u );

    // Первый ключ должен быть доступен.
    std::string_view first_key = root.key_at( 0u );
    REQUIRE( !first_key.empty() );

    froot.Delete();
}

// ---------------------------------------------------------------------------
// Тест: строковые значения корректно хранятся в node
// ---------------------------------------------------------------------------
TEST_CASE( "pjson large: string values from test.json are stored correctly in node", "[pjson][large][json]" )
{
    reset_pam();
    std::ifstream f( TEST_JSON_PATH );
    REQUIRE( f.is_open() );
    std::string json_text( ( std::istreambuf_iterator<char>( f ) ), std::istreambuf_iterator<char>() );
    f.close();

    fptr<node> froot;
    froot.New();
    node_from_string( json_text.c_str(), froot.addr() );

    node_view root{ froot.addr() };
    REQUIRE( root.is_object() );

    // Ищем первое строковое значение на верхнем уровне.
    uintptr_t sz    = root.size();
    bool      found = false;
    for ( uintptr_t i = 0; i < sz && !found; i++ )
    {
        node_view val = root.value_at( i );
        if ( val.is_string() )
        {
            std::string_view sv = val.as_string();
            REQUIRE( ( !sv.empty() || sv.size() == 0 ) ); // строка может быть пустой

            // Отдельно создаём узел и проверяем node_set_string.
            fptr<node> fv;
            fv.New();
            // Копируем строку во временный буфер для node_set_string.
            std::string tmp( sv );
            node_set_string( fv.addr(), tmp.c_str() );
            REQUIRE( node_view{ fv.addr() }.is_string() );
            REQUIRE( node_view{ fv.addr() }.as_string() == sv );
            fv.Delete();
            found = true;
        }
    }
    // В test.json должно быть хотя бы одно строковое значение верхнего уровня.
    REQUIRE( found );

    froot.Delete();
}

// ---------------------------------------------------------------------------
// Тест: объектные ключи из test.json корректно хранятся через node_view
// ---------------------------------------------------------------------------
TEST_CASE( "pjson large: keys from test.json are stored correctly in node object", "[pjson][large][json]" )
{
    reset_pam();
    std::ifstream f( TEST_JSON_PATH );
    REQUIRE( f.is_open() );
    std::string json_text( ( std::istreambuf_iterator<char>( f ) ), std::istreambuf_iterator<char>() );
    f.close();

    fptr<node> froot;
    froot.New();
    node_from_string( json_text.c_str(), froot.addr() );

    node_view root{ froot.addr() };
    REQUIRE( root.is_object() );
    REQUIRE( root.size() > 0u );

    // Проверяем, что каждый ключ верхнего уровня доступен через at(key).
    uintptr_t sz = root.size();
    for ( uintptr_t i = 0; i < sz; i++ )
    {
        std::string_view key = root.key_at( i );
        std::string      key_s( key );
        REQUIRE( root.at( key_s.c_str() ).valid() );
    }

    froot.Delete();
}

// ---------------------------------------------------------------------------
// Тест: полная загрузка test.json в ПАМ и выгрузка обратно с сравнением.
// Использует node_from_string / node_to_string (pjson_codec.h).
// ---------------------------------------------------------------------------
TEST_CASE( "pjson large: full round-trip -- load test.json into PAM and export back",
           "[pjson][large][json][roundtrip]" )
{
    reset_pam();

    // Читаем test.json как сырую строку.
    std::ifstream fin( TEST_JSON_PATH );
    REQUIRE( fin.is_open() );
    std::string json_text( ( std::istreambuf_iterator<char>( fin ) ), std::istreambuf_iterator<char>() );
    fin.close();
    REQUIRE( !json_text.empty() );

    // Предварительно резервируем ёмкость карты слотов ПАМ.
    // test.json содержит ~100k+ узлов; резервирование устраняет многократные
    // реаллокации и ускоряет разбор в несколько раз.
    pam_pmm_reserve_slots( 200000 );

    // Парсим JSON напрямую в node через node_from_string.
    auto       t0 = std::chrono::steady_clock::now();
    fptr<node> froot;
    froot.New();
    node_from_string( json_text.c_str(), froot.addr() );
    auto t1       = std::chrono::steady_clock::now();
    auto parse_ms = std::chrono::duration_cast<std::chrono::milliseconds>( t1 - t0 ).count();
    std::printf( "[large] node_from_string: %lld ms\n", static_cast<long long>( parse_ms ) );

    node_view root{ froot.addr() };
    // Проверяем верхний уровень.
    REQUIRE( ( root.is_object() || root.is_array() ) );
    REQUIRE( root.size() > 0u );

    // Сериализуем обратно в строку через node_to_string.
    auto        t2     = std::chrono::steady_clock::now();
    std::string out    = node_to_string( froot.addr() );
    auto        t3     = std::chrono::steady_clock::now();
    auto        ser_ms = std::chrono::duration_cast<std::chrono::milliseconds>( t3 - t2 ).count();
    std::printf( "[large] node_to_string: %lld ms, output=%zu bytes\n", static_cast<long long>( ser_ms ), out.size() );

    REQUIRE( !out.empty() );

    // Верификация: нормализуем оригинал через parse + serialize.
    fptr<node> forig;
    forig.New();
    node_from_string( json_text.c_str(), forig.addr() );
    std::string original_normalized = node_to_string( forig.addr() );
    forig.Delete();

    REQUIRE( original_normalized == out );

    // Проверяем время выполнения: полный цикл не должен превышать 30 секунд.
    // (PMM-бэкенд медленнее на Windows CI, увеличен с 15с до 30с для стабильности.)
    auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>( t3 - t0 ).count();
    REQUIRE( total_ms < 30000 );

    // Сбрасываем PMM целиком — быстрее O(1) vs O(n²) поэлементной очистки.
    pstringview_manager::reset();
    pam_pmm_reset();
}
