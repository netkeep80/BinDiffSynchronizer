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
// Вспомогательная функция: конвертировать pjson обратно в nlohmann::json.
// ---------------------------------------------------------------------------
static json pjson_to_nlohmann(const pjson& src)
{
    switch( src.type_tag() )
    {
    case pjson_type::null:
        return json(nullptr);
    case pjson_type::boolean:
        return json(src.get_bool());
    case pjson_type::integer:
        return json(src.get_int());
    case pjson_type::uinteger:
        return json(src.get_uint());
    case pjson_type::real:
        return json(src.get_real());
    case pjson_type::string:
        return json(std::string(src.get_string()));
    case pjson_type::array:
    {
        json arr = json::array();
        uintptr_t sz = src.size();
        for( uintptr_t i = 0; i < sz; i++ )
            arr.push_back(pjson_to_nlohmann(src[i]));
        return arr;
    }
    case pjson_type::object:
    {
        json obj = json::object();
        // Итерируемся по парам объекта через data.addr() (pvector-совместимая раскладка).
        uintptr_t sz = src.size();
        uintptr_t data_addr = src.payload.object_val.data.addr();
        for( uintptr_t i = 0; i < sz; i++ )
        {
            const pjson_kv_entry& pair =
                AddressManager<pjson_kv_entry>::GetArrayElement(data_addr, i);
            // Используем pstring::c_str() для получения ключа.
            const char* key = pair.key.c_str();
            obj[key] = pjson_to_nlohmann(pair.value);
        }
        return obj;
    }
    default:
        return json(nullptr);
    }
}

// ---------------------------------------------------------------------------
// Вспомогательная функция: конвертировать nlohmann::json в pjson полностью
// (без ограничений на глубину и число узлов).
// Принимает смещение dst в ПАМ вместо сырой ссылки,
// чтобы избежать использования устаревших указателей после realloc.
// ---------------------------------------------------------------------------
static void nlohmann_to_pjson_full(const json& src, uintptr_t dst_offset)
{
    auto& pam = PersistentAddressSpace::Get();
    // Повторно разрешаем dst перед каждым использованием.
    pjson* dst = pam.Resolve<pjson>(dst_offset);
    if( dst == nullptr ) return;

    switch( src.type() )
    {
    case json::value_t::null:
        dst->set_null();
        break;
    case json::value_t::boolean:
        dst->set_bool(src.get<bool>());
        break;
    case json::value_t::number_integer:
        dst->set_int(src.get<int64_t>());
        break;
    case json::value_t::number_unsigned:
        dst->set_uint(src.get<uint64_t>());
        break;
    case json::value_t::number_float:
        dst->set_real(src.get<double>());
        break;
    case json::value_t::string:
        dst->set_string(src.get<std::string>().c_str());
        break;
    case json::value_t::array:
    {
        pam.Resolve<pjson>(dst_offset)->set_array();
        for( const auto& elem : src )
        {
            // Повторно разрешаем dst перед вызовом push_back (предыдущий рекурсивный
            // вызов мог вызвать realloc, делая старый указатель недействительным).
            pjson* d = pam.Resolve<pjson>(dst_offset);
            pjson& new_elem = d->push_back();
            // Немедленно сохраняем смещение нового элемента до любых последующих аллокаций.
            uintptr_t new_elem_offset = pam.PtrToOffset(&new_elem);
            nlohmann_to_pjson_full(elem, new_elem_offset);
        }
        break;
    }
    case json::value_t::object:
    {
        pam.Resolve<pjson>(dst_offset)->set_object();
        for( const auto& [key, val] : src.items() )
        {
            // Повторно разрешаем dst перед вызовом obj_insert.
            pjson* d = pam.Resolve<pjson>(dst_offset);
            pjson& new_val = d->obj_insert(key.c_str());
            // Немедленно сохраняем смещение нового значения до любых последующих аллокаций.
            uintptr_t new_val_offset = pam.PtrToOffset(&new_val);
            nlohmann_to_pjson_full(val, new_val_offset);
        }
        break;
    }
    default:
        pam.Resolve<pjson>(dst_offset)->set_null();
        break;
    }
}

// ---------------------------------------------------------------------------
// Вспомогательная функция: конвертировать nlohmann::json в pjson
// с ограничением максимальной глубины и числа узлов.
// Принимает смещение dst в ПАМ вместо сырой ссылки.
// Возвращает число созданных узлов.
// ---------------------------------------------------------------------------
static uintptr_t nlohmann_to_pjson_limited(const json& src, uintptr_t dst_offset,
                                           int max_depth, uintptr_t& node_count,
                                           uintptr_t max_nodes)
{
    auto& pam = PersistentAddressSpace::Get();
    pjson* dst = pam.Resolve<pjson>(dst_offset);
    if( dst == nullptr ) return node_count;

    if( node_count >= max_nodes || max_depth <= 0 )
    {
        dst->set_null();
        return 0;
    }

    node_count++;

    switch( src.type() )
    {
    case json::value_t::null:
        pam.Resolve<pjson>(dst_offset)->set_null();
        break;

    case json::value_t::boolean:
        pam.Resolve<pjson>(dst_offset)->set_bool(src.get<bool>());
        break;

    case json::value_t::number_integer:
        pam.Resolve<pjson>(dst_offset)->set_int(src.get<int64_t>());
        break;

    case json::value_t::number_unsigned:
        pam.Resolve<pjson>(dst_offset)->set_uint(src.get<uint64_t>());
        break;

    case json::value_t::number_float:
        pam.Resolve<pjson>(dst_offset)->set_real(src.get<double>());
        break;

    case json::value_t::string:
        pam.Resolve<pjson>(dst_offset)->set_string(src.get<std::string>().c_str());
        break;

    case json::value_t::array:
    {
        pam.Resolve<pjson>(dst_offset)->set_array();
        for( const auto& elem : src )
        {
            if( node_count >= max_nodes ) break;
            pjson* d = pam.Resolve<pjson>(dst_offset);
            pjson& new_elem = d->push_back();
            uintptr_t new_elem_offset = pam.PtrToOffset(&new_elem);
            nlohmann_to_pjson_limited(elem, new_elem_offset, max_depth - 1, node_count, max_nodes);
        }
        break;
    }

    case json::value_t::object:
    {
        pam.Resolve<pjson>(dst_offset)->set_object();
        for( const auto& [key, val] : src.items() )
        {
            if( node_count >= max_nodes ) break;
            pjson* d = pam.Resolve<pjson>(dst_offset);
            pjson& new_val = d->obj_insert(key.c_str());
            uintptr_t new_val_offset = pam.PtrToOffset(&new_val);
            nlohmann_to_pjson_limited(val, new_val_offset, max_depth - 1, node_count, max_nodes);
        }
        break;
    }

    default:
        pam.Resolve<pjson>(dst_offset)->set_null();
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
    // (таблица слотов ПАМ динамическая, но ограничиваем для быстрого теста).
    const uintptr_t MAX_NODES = 500;
    uintptr_t node_count = 0;

    // Конвертируем только первые несколько ключей верхнего уровня.
    int keys_processed = 0;
    for( const auto& [key, val] : nlohmann_root.items() )
    {
        if( node_count >= MAX_NODES ) break;
        // Повторно разрешаем froot перед вызовом obj_insert (предыдущая итерация
        // могла вызвать realloc).
        pjson& new_val = froot->obj_insert(key.c_str());
        // Немедленно сохраняем смещение нового значения до любых последующих аллокаций.
        uintptr_t new_val_offset = PersistentAddressSpace::Get().PtrToOffset(&new_val);
        node_count++;
        // Ограничиваем глубину и число узлов для поддерева.
        nlohmann_to_pjson_limited(val, new_val_offset, 3, node_count, MAX_NODES);
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

// ---------------------------------------------------------------------------
// Тест: полная загрузка test.json в ПАМ и выгрузка обратно с сравнением.
// Задача #54, требование 8.
// ---------------------------------------------------------------------------
TEST_CASE("pjson large: full round-trip -- load test.json into PAM and export back",
          "[pjson][large][json][roundtrip]")
{
    // Загружаем исходный JSON через nlohmann.
    std::ifstream fin(TEST_JSON_PATH);
    REQUIRE(fin.is_open());
    json original;
    fin >> original;
    fin.close();

    REQUIRE(!original.is_null());

    // Конвертируем полностью в pjson (без ограничений — ПАМ динамический).
    fptr<pjson> froot;
    froot.New();
    nlohmann_to_pjson_full(original, froot.addr());

    // Проверяем верхний уровень.
    if( original.is_object() )
    {
        REQUIRE(froot->is_object());
        REQUIRE(froot->size() == static_cast<uintptr_t>(original.size()));
    }
    else if( original.is_array() )
    {
        REQUIRE(froot->is_array());
        REQUIRE(froot->size() == static_cast<uintptr_t>(original.size()));
    }

    // Конвертируем обратно из pjson в nlohmann.
    json restored = pjson_to_nlohmann(*froot);

    // Сравниваем оригинал и восстановленный JSON.
    // Для корректного сравнения чисел с плавающей точкой используем dump().
    REQUIRE(original.dump() == restored.dump());

    // Освобождаем ресурсы.
    froot->free();
    froot.Delete();
}
