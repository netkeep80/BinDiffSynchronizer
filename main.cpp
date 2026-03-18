/*
 * main.cpp — Демонстрационный пример использования pjson_db_pmm.
 *
 * Показывает возможности высокоуровневого API pjson_db_pmm (PMM-бэкенд, Задача 14.11):
 *   - Открытие/создание базы данных через pjson_db_pmm::open()
 *   - Запись данных: put() (bool, int64, double, string), put_ref(), parse_into()
 *   - Чтение данных: get(), exists()
 *   - Удаление данных: erase()
 *   - Поддержка $ref (настоящие указатели на узлы)
 *   - Поддержка $base64 (бинарные данные)
 *   - Персистные метрики через /$metrics
 *   - Иерархический доступ: operator[], find(), insert() (Фаза 8)
 *   - Поиск по строкам: search_strings(), search_node_strings() (Фаза 8)
 *   - Сохранение: save()
 *
 * При каждом запуске программа:
 *   1. Открывает БД из файла demo.pam (или создаёт новый).
 *   2. Демонстрирует все возможности API.
 *   3. Сохраняет БД обратно в файл.
 *
 * Запустите программу несколько раз, чтобы увидеть накопленные данные.
 */

#include <cstdio>
#include "pjson_db_pmm.h"

using namespace pjson;

// ---------------------------------------------------------------------------
// Вспомогательные функции для вывода
// ---------------------------------------------------------------------------

static void print_separator( const char* title )
{
    std::printf( "\n--- %s ---\n", title );
}

// ---------------------------------------------------------------------------
// Демо 1: открытие БД и базовые put/get операции
// ---------------------------------------------------------------------------

static void demo_open_and_put_get( pjson_db_pmm& db )
{
    print_separator( "Демо 1: открытие БД и put/get" );

    // Запись различных типов данных по путям.
    db.put( "/app/name", "BinDiffSynchronizer" );
    db.put( "/app/version", static_cast<int64_t>( 1 ) );
    db.put( "/app/debug", false );
    db.put( "/app/threshold", 0.75 );

    // Чтение записанных данных.
    std::string_view name = db.get( "/app/name" ).as_string();
    int64_t          ver  = db.get( "/app/version" ).as_int();
    bool             dbg  = db.get( "/app/debug" ).as_bool();
    double           thr  = db.get( "/app/threshold" ).as_double();

    std::printf( "app.name      = \"%.*s\"\n", static_cast<int>( name.size() ), name.data() );
    std::printf( "app.version   = %lld\n", static_cast<long long>( ver ) );
    std::printf( "app.debug     = %s\n", dbg ? "true" : "false" );
    std::printf( "app.threshold = %.2f\n", thr );

    // Проверка существования пути.
    std::printf( "exists(\"/app/name\")   = %s\n", db.exists( "/app/name" ) ? "true" : "false" );
    std::printf( "exists(\"/nonexistent\") = %s\n", db.exists( "/nonexistent" ) ? "true" : "false" );
}

// ---------------------------------------------------------------------------
// Демо 2: вложенные объекты и массивы через parse_into()
// ---------------------------------------------------------------------------

static void demo_nested_objects( pjson_db_pmm& db )
{
    print_separator( "Демо 2: вложенные объекты и массивы (parse_into)" );

    // Создаём вложенную структуру через parse_into().
    db.parse_into( "/users/alice", R"({"age": 30, "active": true})" );
    db.parse_into( "/users/bob", R"({"age": 25, "active": false})" );
    db.parse_into( "/tags", R"(["persistent", "header-only", "c++20"])" );

    // Чтение вложенных полей.
    int64_t alice_age  = db.get( "/users/alice/age" ).as_int();
    bool    bob_active = db.get( "/users/bob/active" ).as_bool();

    std::printf( "users.alice.age   = %lld\n", static_cast<long long>( alice_age ) );
    std::printf( "users.bob.active  = %s\n", bob_active ? "true" : "false" );

    // Чтение элемента массива по индексу.
    std::string_view tag0 = db.get( "/tags/0" ).as_string();
    std::printf( "tags[0]           = \"%.*s\"\n", static_cast<int>( tag0.size() ), tag0.data() );

    // Сериализация поддерева в JSON-строку.
    std::string alice_json = db.dump( db.get( "/users/alice" ).id );
    std::printf( "dump(/users/alice) = %s\n", alice_json.c_str() );
}

// ---------------------------------------------------------------------------
// Демо 3: $ref — настоящие указатели на узлы
// ---------------------------------------------------------------------------

static void demo_ref( pjson_db_pmm& db )
{
    print_separator( "Демо 3: $ref — настоящие указатели" );

    // Создаём данные, на которые будем ссылаться.
    db.put( "/defaults/timeout", static_cast<int64_t>( 30 ) );
    db.put( "/defaults/retries", static_cast<int64_t>( 3 ) );

    // Создаём $ref-узел, указывающий на /defaults/timeout.
    db.put_ref( "/config/timeout", "/defaults/timeout" );

    // Можно создать $ref и через parse_into.
    db.parse_into( "/config/retries", R"({"$ref": "/defaults/retries"})" );

    // resolve_all_refs() разрешает все $ref-узлы в дереве.
    db.resolve_all_refs();

    // При чтении get() автоматически разыменовывает ref.
    int64_t timeout = db.get( "/config/timeout" ).as_int();
    std::printf( "config.timeout (via $ref) = %lld\n", static_cast<long long>( timeout ) );

    int64_t retries = db.get( "/config/retries" ).as_int();
    std::printf( "config.retries (via $ref) = %lld\n", static_cast<long long>( retries ) );

    // Чтение без разыменования: get(..., deref_ref=false).
    node_view ref_node = db.get( "/config/timeout", /*deref_refs=*/false );
    std::printf( "config.timeout (raw ref) is_ref = %s\n", ref_node.is_ref() ? "true" : "false" );
    std::printf( "ref path = \"%.*s\"\n", static_cast<int>( ref_node.ref_path().size() ), ref_node.ref_path().data() );

    // Явное разыменование через resolve_ref().
    node_view resolved = db.resolve_ref( ref_node.id );
    std::printf( "resolved value = %lld\n", static_cast<long long>( resolved.as_int() ) );
}

// ---------------------------------------------------------------------------
// Демо 4: $base64 — бинарные данные
// ---------------------------------------------------------------------------

static void demo_base64( pjson_db_pmm& db )
{
    print_separator( "Демо 4: $base64 — бинарные данные" );

    // Парсинг $base64: "AAEC" = байты [0x00, 0x01, 0x02].
    db.parse_into( "/data/thumbnail", R"({"$base64": "AAEC"})" );

    node_view bin = db.get( "/data/thumbnail" );
    std::printf( "thumbnail is binary = %s\n", bin.is_binary() ? "true" : "false" );
    std::printf( "thumbnail size (bytes) = %llu\n", static_cast<unsigned long long>( bin.size() ) );

    // Сериализация обратно в JSON с $base64.
    std::string json = db.dump( bin.id );
    std::printf( "dump(/data/thumbnail) = %s\n", json.c_str() );
}

// ---------------------------------------------------------------------------
// Демо 5: метрики (Фаза 7)
// ---------------------------------------------------------------------------

static void demo_metrics( pjson_db_pmm& db )
{
    print_separator( "Демо 5: персистные метрики (/$metrics)" );

    uint64_t node_count = db.get( "/$metrics/node_count_total" ).as_uint();
    uint64_t str_count  = db.get( "/$metrics/string_count_total" ).as_uint();
    uint64_t ref_cnt    = db.get( "/$metrics/ref_count" ).as_uint();
    uint64_t arr_cnt    = db.get( "/$metrics/array_count" ).as_uint();
    uint64_t obj_cnt    = db.get( "/$metrics/object_count" ).as_uint();
    uint64_t bin_bytes  = db.get( "/$metrics/binary_bytes_total" ).as_uint();
    uint64_t bump       = db.get( "/$metrics/pam_bump_offset" ).as_uint();
    uint64_t total_size = db.get( "/$metrics/pam_total_size" ).as_uint();
    uint64_t slot_cnt   = db.get( "/$metrics/pam_slot_count" ).as_uint();
    uint64_t named_cnt  = db.get( "/$metrics/pam_named_count" ).as_uint();

    std::printf( "node_count_total   = %llu\n", static_cast<unsigned long long>( node_count ) );
    std::printf( "string_count_total = %llu\n", static_cast<unsigned long long>( str_count ) );
    std::printf( "ref_count          = %llu\n", static_cast<unsigned long long>( ref_cnt ) );
    std::printf( "array_count        = %llu\n", static_cast<unsigned long long>( arr_cnt ) );
    std::printf( "object_count       = %llu\n", static_cast<unsigned long long>( obj_cnt ) );
    std::printf( "binary_bytes_total = %llu\n", static_cast<unsigned long long>( bin_bytes ) );
    std::printf( "pam_bump_offset    = %llu\n", static_cast<unsigned long long>( bump ) );
    std::printf( "pam_total_size     = %llu\n", static_cast<unsigned long long>( total_size ) );
    std::printf( "pam_slot_count     = %llu\n", static_cast<unsigned long long>( slot_cnt ) );
    std::printf( "pam_named_count    = %llu\n", static_cast<unsigned long long>( named_cnt ) );

    // Попытка записи в метрики — запрещена.
    bool ok = db.put( "/$metrics/node_count_total", static_cast<int64_t>( 0 ) );
    std::printf( "put(/$metrics/...) allowed = %s (должно быть false)\n", ok ? "true" : "false" );
}

// ---------------------------------------------------------------------------
// Демо 6: иерархический интерфейс operator[], find(), insert() (Фаза 8)
// ---------------------------------------------------------------------------

static void demo_hierarchical( pjson_db_pmm& db )
{
    print_separator( "Демо 6: иерархический интерфейс (Фаза 8)" );

    // insert() — вставка/перезапись JSON-значения по пути.
    db.insert( "/employees/1/name", "\"Иван Петров\"" );
    db.insert( "/employees/1/dept", "\"Разработка\"" );
    db.insert( "/employees/1/salary", "120000" );
    db.insert( "/employees/2/name", "\"Мария Сидорова\"" );
    db.insert( "/employees/2/dept", "\"Маркетинг\"" );
    db.insert( "/employees/2/salary", "95000" );

    // find() — поиск без создания нового узла, без разыменования ref.
    node_view found = db.find( "/employees/1/name" );
    std::printf( "find(\"/employees/1/name\") = \"%.*s\"\n", static_cast<int>( found.as_string().size() ),
                 found.as_string().data() );
    std::printf( "find(\"/nonexistent\").valid() = %s\n", db.find( "/nonexistent" ).valid() ? "true" : "false" );

    // operator[] — доступ по пути, создаёт null-узел если не существует.
    node_view existing = db["/employees/1/dept"];
    std::printf( "operator[](\"/employees/1/dept\") = \"%.*s\"\n", static_cast<int>( existing.as_string().size() ),
                 existing.as_string().data() );
}

// ---------------------------------------------------------------------------
// Демо 7: поиск по строкам (Фаза 8)
// ---------------------------------------------------------------------------

static void demo_search( pjson_db_pmm& db )
{
    print_separator( "Демо 7: поиск по строкам (Фаза 8)" );

    // search_node_strings() — поиск по pstring-значениям.
    auto name_results = db.search_node_strings( "Петров" );
    std::printf( "search_node_strings(\"Петров\"): %zu узла(ов)\n", name_results.size() );
    for ( node_id id : name_results )
    {
        std::string_view sv = node_view{ id }.as_string();
        std::printf( "  -> \"%.*s\"\n", static_cast<int>( sv.size() ), sv.data() );
    }

    // Пустой pattern — все string-значения в дереве.
    auto all_vals = db.search_node_strings( "" );
    std::printf( "search_node_strings(\"\") — всего string-узлов: %zu\n", all_vals.size() );

    // search_strings() — поиск по словарю интернированных ключей.
    auto key_results = db.search_strings( "name" );
    std::printf( "search_strings(\"name\") — ключей в словаре: %zu\n", key_results.size() );
}

// ---------------------------------------------------------------------------
// Демо 8: удаление и resolve_all_refs()
// ---------------------------------------------------------------------------

static void demo_erase_and_resolve( pjson_db_pmm& db )
{
    print_separator( "Демо 8: удаление и resolve_all_refs()" );

    // Создаём узел и удаляем его.
    db.put( "/temp/value", static_cast<int64_t>( 42 ) );
    std::printf( "exists(\"/temp/value\") before erase = %s\n", db.exists( "/temp/value" ) ? "true" : "false" );

    db.erase( "/temp/value" );
    std::printf( "exists(\"/temp/value\") after erase  = %s\n", db.exists( "/temp/value" ) ? "true" : "false" );

    // resolve_all_refs() — разрешает все $ref-узлы в дереве.
    db.resolve_all_refs();
    std::printf( "resolve_all_refs() — выполнено\n" );
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    std::printf( "=== BinDiffSynchronizer — pjson_db_pmm демонстрация (PMM-бэкенд) ===\n" );

    // Открываем (или создаём) базу данных через PMM-бэкенд.
    pjson_db_pmm db = pjson_db_pmm::open( "demo.pam" );

    demo_open_and_put_get( db );
    demo_nested_objects( db );
    demo_ref( db );
    demo_base64( db );
    demo_metrics( db );
    demo_hierarchical( db );
    demo_search( db );
    demo_erase_and_resolve( db );

    // Сохраняем БД в файл для следующего запуска.
    db.save();

    std::printf( "\n=== Данные сохранены в demo.pam ===\n" );
    return 0;
}
