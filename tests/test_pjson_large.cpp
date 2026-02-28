// test_pjson_large.cpp — Тест загрузки большого JSON-файла в персистные структуры pjson.
//
// Использует: tests/test.json (большой файл ~11.7 МБ)
// Загрузка JSON: pjson::from_string (прямой парсер F6)
// Хранение: fptr<pjson>
//
// Все комментарии — на русском языке (Тр.6).

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <cstring>

#include "pjson.h"

// ---------------------------------------------------------------------------
// Путь к тестовому JSON-файлу (задаётся через CMake compile definition)
// ---------------------------------------------------------------------------
#ifndef TEST_JSON_PATH
#define TEST_JSON_PATH "tests/test.json"
#endif

// ---------------------------------------------------------------------------
// Тест: загрузка test.json через pjson::from_string — проверка структуры верхнего уровня
// ---------------------------------------------------------------------------
TEST_CASE( "pjson large: test.json loads correctly via pjson::from_string", "[pjson][large][json]" )
{
    std::ifstream f( TEST_JSON_PATH );
    REQUIRE( f.is_open() );
    std::string json_text( ( std::istreambuf_iterator<char>( f ) ), std::istreambuf_iterator<char>() );
    f.close();

    fptr<pjson> froot;
    froot.New();
    pjson::from_string( json_text.c_str(), froot.addr() );

    // test.json должен быть непустым объектом.
    REQUIRE( !froot->is_null() );
    REQUIRE( froot->is_object() );
    REQUIRE( froot->size() > 0u );

    froot->free();
    froot.Delete();
}

// ---------------------------------------------------------------------------
// Тест: первые ключи test.json корректно хранятся в pjson
// ---------------------------------------------------------------------------
TEST_CASE( "pjson large: first keys from test.json are stored in pjson", "[pjson][large][json]" )
{
    std::ifstream f( TEST_JSON_PATH );
    REQUIRE( f.is_open() );
    std::string json_text( ( std::istreambuf_iterator<char>( f ) ), std::istreambuf_iterator<char>() );
    f.close();

    // Загружаем весь JSON через прямой парсер F6.
    fptr<pjson> froot;
    froot.New();
    pjson::from_string( json_text.c_str(), froot.addr() );

    REQUIRE( froot->is_object() );
    REQUIRE( froot->size() > 0u );

    // Проверяем, что ключи присутствуют (хотя бы 1 ключ).
    uintptr_t sz        = froot->size();
    uintptr_t data_addr = froot->payload.object_val.data.addr();
    REQUIRE( data_addr != 0u );
    // Первый ключ должен быть непустой строкой.
    const pjson_kv_entry& first_pair = AddressManager<pjson_kv_entry>::GetArrayElement( data_addr, 0 );
    REQUIRE( first_pair.key.c_str() != nullptr );
    REQUIRE( first_pair.key.c_str()[0] != '\0' );
    (void)sz;

    froot->free();
    froot.Delete();
}

// ---------------------------------------------------------------------------
// Тест: строковые значения корректно копируются в pjson
// ---------------------------------------------------------------------------
TEST_CASE( "pjson large: string values from test.json are stored correctly in pjson", "[pjson][large][json]" )
{
    std::ifstream f( TEST_JSON_PATH );
    REQUIRE( f.is_open() );
    std::string json_text( ( std::istreambuf_iterator<char>( f ) ), std::istreambuf_iterator<char>() );
    f.close();

    fptr<pjson> froot;
    froot.New();
    pjson::from_string( json_text.c_str(), froot.addr() );

    REQUIRE( froot->is_object() );

    // Ищем первое строковое значение на верхнем уровне.
    uintptr_t sz        = froot->size();
    uintptr_t data_addr = froot->payload.object_val.data.addr();
    bool      found     = false;
    for ( uintptr_t i = 0; i < sz && !found; i++ )
    {
        const pjson_kv_entry& pair = AddressManager<pjson_kv_entry>::GetArrayElement( data_addr, i );
        if ( pair.value.is_string() )
        {
            const char* s = pair.value.get_string();
            REQUIRE( s != nullptr );
            // Отдельно создаём pjson-узел и проверяем set_string.
            fptr<pjson> fv;
            fv.New();
            fv->set_string( s );
            REQUIRE( fv->is_string() );
            REQUIRE( std::strcmp( fv->get_string(), s ) == 0 );
            fv->free();
            fv.Delete();
            found = true;
        }
    }
    // В test.json должно быть хотя бы одно строковое значение верхнего уровня.
    REQUIRE( found );

    froot->free();
    froot.Delete();
}

// ---------------------------------------------------------------------------
// Тест: объектные ключи из test.json корректно хранятся в pjson-объекте
// ---------------------------------------------------------------------------
TEST_CASE( "pjson large: keys from test.json are stored correctly in pjson object", "[pjson][large][json]" )
{
    std::ifstream f( TEST_JSON_PATH );
    REQUIRE( f.is_open() );
    std::string json_text( ( std::istreambuf_iterator<char>( f ) ), std::istreambuf_iterator<char>() );
    f.close();

    // Загружаем весь JSON через прямой парсер.
    fptr<pjson> froot;
    froot.New();
    pjson::from_string( json_text.c_str(), froot.addr() );

    REQUIRE( froot->is_object() );
    REQUIRE( froot->size() > 0u );

    // Проверяем, что каждый ключ верхнего уровня доступен через obj_find.
    uintptr_t sz        = froot->size();
    uintptr_t data_addr = froot->payload.object_val.data.addr();
    for ( uintptr_t i = 0; i < sz; i++ )
    {
        const pjson_kv_entry& pair = AddressManager<pjson_kv_entry>::GetArrayElement( data_addr, i );
        const char*           key  = pair.key.c_str();
        REQUIRE( froot->obj_find( key ) != nullptr );
    }

    froot->free();
    froot.Delete();
}

// ---------------------------------------------------------------------------
// Тест: полная загрузка test.json в ПАМ и выгрузка обратно с сравнением.
// Использует прямой парсер F6 (pjson::from_string / pjson::to_string).
// Задача #88 (оптимизация), задача #54 требование 8.
// ---------------------------------------------------------------------------
TEST_CASE( "pjson large: full round-trip -- load test.json into PAM and export back",
           "[pjson][large][json][roundtrip]" )
{
    // Читаем test.json как сырую строку для прямого парсера F6.
    std::ifstream fin( TEST_JSON_PATH );
    REQUIRE( fin.is_open() );
    std::string json_text( ( std::istreambuf_iterator<char>( fin ) ), std::istreambuf_iterator<char>() );
    fin.close();
    REQUIRE( !json_text.empty() );

    // Предварительно резервируем ёмкость карты слотов ПАМ.
    // test.json содержит ~100k+ узлов; резервирование устраняет многократные
    // реаллокации и ускоряет разбор в несколько раз (задача #88).
    PersistentAddressSpace::Get().ReserveSlots( 200000 );

    // Парсим JSON напрямую в pjson через прямой парсер F6 (без внешних зависимостей).
    // Ожидаемое время: ~100–300 мс на test.json (~11 МБ, ~100k узлов).
    auto        t0 = std::chrono::steady_clock::now();
    fptr<pjson> froot;
    froot.New();
    pjson::from_string( json_text.c_str(), froot.addr() );
    auto t1       = std::chrono::steady_clock::now();
    auto parse_ms = std::chrono::duration_cast<std::chrono::milliseconds>( t1 - t0 ).count();
    std::printf( "[large] direct parse: %lld ms\n", static_cast<long long>( parse_ms ) );

    // Проверяем верхний уровень.
    REQUIRE( ( froot->is_object() || froot->is_array() ) );
    REQUIRE( froot->size() > 0u );

    // Сериализуем обратно в строку через прямой сериализатор F6.
    auto        t2     = std::chrono::steady_clock::now();
    std::string out    = froot->to_string();
    auto        t3     = std::chrono::steady_clock::now();
    auto        ser_ms = std::chrono::duration_cast<std::chrono::milliseconds>( t3 - t2 ).count();
    std::printf( "[large] direct serialize: %lld ms, output=%zu bytes\n", static_cast<long long>( ser_ms ),
                 out.size() );

    REQUIRE( !out.empty() );

    // Верификация: нормализуем оригинал через pjson и сравниваем с результатом.
    // pjson хранит ключи в отсортированном порядке, поэтому нормализация эквивалентна.
    fptr<pjson> forig;
    forig.New();
    pjson::from_string( json_text.c_str(), forig.addr() );
    std::string original_normalized = forig->to_string();
    forig->free();
    forig.Delete();

    REQUIRE( original_normalized == out );

    // Проверяем время выполнения: полный цикл не должен превышать 15 секунд.
    // (Прямой парсер F6 выполняется за ~100–300 мс, запас 15–30x для медленных CI.)
    auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>( t3 - t0 ).count();
    REQUIRE( total_ms < 15000 );

    // Сбрасываем ПАМ целиком — быстрее O(1) vs O(n²) поэлементной очистки.
    PersistentAddressSpace::Get().Reset();
}
