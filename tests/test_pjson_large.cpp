// test_pjson_large.cpp — Тест загрузки большого JSON-файла через nlohmann::json
// и сохранения его структуры в персистные структуры pjson.
//
// Использует: tests/test.json (большой файл ~11.7 МБ)
// Загрузка JSON: third_party/nlohmann/json.hpp
// Хранение: fptr<pjson>

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <string>
#include <cstring>

// nlohmann/json.hpp подключается через include directories (third_party/)
#include "nlohmann/json.hpp"
#include "pjson.h"

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Вспомогательная функция: конвертировать nlohmann::json в pjson
// с ограничением максимальной глубины и числа узлов.
// Возвращает число созданных узлов.
// ---------------------------------------------------------------------------
static uintptr_t nlohmann_to_pjson_limited(const json& src, pjson& dst,
                                           int max_depth, uintptr_t& node_count,
                                           uintptr_t max_nodes)
{
    if( node_count >= max_nodes || max_depth <= 0 )
    {
        dst.set_null();
        return 0;
    }

    node_count++;

    switch( src.type() )
    {
    case json::value_t::null:
        dst.set_null();
        break;

    case json::value_t::boolean:
        dst.set_bool(src.get<bool>());
        break;

    case json::value_t::number_integer:
        dst.set_int(src.get<int64_t>());
        break;

    case json::value_t::number_unsigned:
        dst.set_uint(src.get<uint64_t>());
        break;

    case json::value_t::number_float:
        dst.set_real(src.get<double>());
        break;

    case json::value_t::string:
        dst.set_string(src.get<std::string>().c_str());
        break;

    case json::value_t::array:
    {
        dst.set_array();
        for( const auto& elem : src )
        {
            if( node_count >= max_nodes ) break;
            pjson& new_elem = dst.push_back();
            nlohmann_to_pjson_limited(elem, new_elem, max_depth - 1, node_count, max_nodes);
        }
        break;
    }

    case json::value_t::object:
    {
        dst.set_object();
        for( const auto& [key, val] : src.items() )
        {
            if( node_count >= max_nodes ) break;
            pjson& new_val = dst.obj_insert(key.c_str());
            nlohmann_to_pjson_limited(val, new_val, max_depth - 1, node_count, max_nodes);
        }
        break;
    }

    default:
        dst.set_null();
        break;
    }

    return node_count;
}

// ---------------------------------------------------------------------------
// Путь к тестовому JSON-файлу (задаётся через CMake compile definition)
// ---------------------------------------------------------------------------
#ifndef TEST_JSON_PATH
#define TEST_JSON_PATH "tests/test.json"
#endif

// ---------------------------------------------------------------------------
// Тест: загрузка test.json через nlohmann — проверка структуры верхнего уровня
// ---------------------------------------------------------------------------
TEST_CASE("pjson large: test.json loads correctly via nlohmann::json",
          "[pjson][large][json]")
{
    std::ifstream f(TEST_JSON_PATH);
    REQUIRE(f.is_open());

    json nlohmann_root;
    f >> nlohmann_root;
    f.close();

    // test.json должен быть непустым объектом.
    REQUIRE(!nlohmann_root.is_null());
    REQUIRE(nlohmann_root.is_object());
    REQUIRE(nlohmann_root.size() > 0u);

    // Проверяем, что файл содержит данные (хотя бы несколько ключей).
    REQUIRE(nlohmann_root.size() >= 1u);
}

// ---------------------------------------------------------------------------
// Тест: конвертация первых N узлов test.json в pjson
// ---------------------------------------------------------------------------
TEST_CASE("pjson large: first keys from test.json are stored in pjson",
          "[pjson][large][json]")
{
    std::ifstream f(TEST_JSON_PATH);
    REQUIRE(f.is_open());

    json nlohmann_root;
    f >> nlohmann_root;
    f.close();

    REQUIRE(nlohmann_root.is_object());

    // Создаём pjson-объект в ПАП.
    fptr<pjson> froot;
    froot.New();
    froot->set_object();

    // Ограничиваем число узлов для хранения в ПАП
    // (PAP_MAX_SLOTS = 4096, каждый узел/строка/массив занимает слот).
    const uintptr_t MAX_NODES = 500;
    uintptr_t node_count = 0;

    // Конвертируем только первые несколько ключей верхнего уровня.
    int keys_processed = 0;
    for( const auto& [key, val] : nlohmann_root.items() )
    {
        if( node_count >= MAX_NODES ) break;
        pjson& new_val = froot->obj_insert(key.c_str());
        node_count++;
        // Ограничиваем глубину и число узлов для поддерева.
        nlohmann_to_pjson_limited(val, new_val, 3, node_count, MAX_NODES);
        keys_processed++;
    }

    // Должны успешно сохранить хотя бы несколько ключей.
    REQUIRE(keys_processed > 0);
    REQUIRE(froot->size() > 0u);

    // Проверяем, что ключи из nlohmann присутствуют в pjson.
    int verified = 0;
    for( const auto& [key, val] : nlohmann_root.items() )
    {
        pjson* found = froot->obj_find(key.c_str());
        if( found == nullptr ) break;  // дошли до конца конвертированной части
        verified++;
        if( verified >= keys_processed ) break;
    }
    REQUIRE(verified > 0);

    // Освобождаем ресурсы.
    froot->free();
    froot.Delete();
}

// ---------------------------------------------------------------------------
// Тест: строковые значения корректно копируются из nlohmann в pjson
// ---------------------------------------------------------------------------
TEST_CASE("pjson large: string values from test.json are stored correctly in pjson",
          "[pjson][large][json]")
{
    std::ifstream f(TEST_JSON_PATH);
    REQUIRE(f.is_open());

    json nlohmann_root;
    f >> nlohmann_root;
    f.close();

    REQUIRE(nlohmann_root.is_object());

    // Ищем первое строковое значение на верхнем уровне.
    for( const auto& [key, val] : nlohmann_root.items() )
    {
        if( !val.is_string() ) continue;

        std::string expected_str = val.get<std::string>();

        fptr<pjson> fv;
        fv.New();
        fv->set_string(expected_str.c_str());

        REQUIRE(fv->is_string());
        REQUIRE(std::strcmp(fv->get_string(), expected_str.c_str()) == 0);
        REQUIRE(fv->size() == expected_str.size());

        fv->free();
        fv.Delete();

        // Достаточно проверить одно строковое значение.
        break;
    }
}

// ---------------------------------------------------------------------------
// Тест: объектные ключи из test.json корректно хранятся в pjson
// ---------------------------------------------------------------------------
TEST_CASE("pjson large: keys from test.json are stored correctly in pjson object",
          "[pjson][large][json]")
{
    std::ifstream f(TEST_JSON_PATH);
    REQUIRE(f.is_open());

    json nlohmann_root;
    f >> nlohmann_root;
    f.close();

    REQUIRE(nlohmann_root.is_object());

    fptr<pjson> froot;
    froot.New();
    froot->set_object();

    // Вставляем только верхний уровень ключей без рекурсии (значения = null).
    for( const auto& [key, val] : nlohmann_root.items() )
    {
        froot->obj_insert(key.c_str());
    }

    // Проверяем, что размер совпадает.
    REQUIRE(froot->size() == static_cast<uintptr_t>(nlohmann_root.size()));

    // Проверяем наличие каждого ключа.
    for( const auto& [key, val] : nlohmann_root.items() )
    {
        REQUIRE(froot->obj_find(key.c_str()) != nullptr);
    }

    froot->free();
    froot.Delete();
}
