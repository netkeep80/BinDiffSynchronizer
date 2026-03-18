# BinDiffSynchronizer — pjson_db: персистная JSON-база данных

C++17 header-only библиотека для работы с JSON в персистном адресном пространстве (ПАП).

---

## Концепция

**pjson_db** позволяет работать с JSON-данными так же, как с `nlohmann::json`, но с одним принципиальным отличием: все объекты хранятся в **персистном адресном пространстве** — двоичном образе файла, отображённом в память. Это превращает JSON в полноценную базу данных: данные переживают перезапуск программы без сериализации и десериализации.

Помимо стандартного JSON, библиотека поддерживает расширенные типы узлов:

- **`$ref`** — настоящий указатель на другой узел (не текстовый путь, а прямая ссылка в ПАП)
- **`$base64`** — бинарные данные (хранятся как байтовый массив, сериализуются в base64)

---

## Ключевые характеристики

| Характеристика | Описание |
|---|---|
| **Header-only** | Вся реализация — только `.h` файлы, без `.cpp` |
| **C++20** | Требует C++20 (для PMM); без внешних зависимостей |
| **Персистность** | Данные в ПАП переживают перезапуск без явной сериализации |
| **Два типа строк** | readonly (`pstringview`): ключи объектов, пути `$ref`, интернированы, сравнение O(1); readwrite (`pstring`): строковые значения JSON, изменяемые на лету |
| **Нет SSO** | Ни `pstringview`, ни `pstring` не используют SSO — все строки хранятся в ПАП (необходимо для сквозного поиска) |
| **jsonRVM-совместимость** | `pstring`-узлы могут модифицироваться непосредственно в БД библиотекой [jsonRVM](https://github.com/netkeep80/jsonRVM) |
| **Path-адресация** | Доступ к узлам через строковые пути вида `/a/b/0/c` |
| **$ref как указатели** | `{ "$ref": "/path" }` при разборе становится прямым указателем в ПАП |
| **Метрики** | Персистная структура `db_metrics` в ПАМ; обновляется при каждой мутации; доступ через `/$metrics/...` (Фаза 7) |
| **pmap-интерфейс** | `operator[]`, `find`, `insert` для доступа по пути без явного указания типа (Фаза 8) |
| **Поиск по строкам** | `search_strings` — по словарю ключей (pstringview); `search_node_strings` — по значениям узлов (pstring) (Фаза 8) |
| **Итераторы** | `node_view` поддерживает range-based for: `begin()`/`end()` для массивов, `items()` для объектов (Фаза 10) |
| **Коды ошибок** | `node_error` enum + `is_error()` / `error()` в `node_view`; `get()` возвращает типизированные ошибки (`not_found`, `wrong_type`, `index_out_of_range`, `ref_cycle`) вместо `node_view(0)` (Фаза 11) |
| **Сообщения об ошибках** | `node_error_message()` + `node_view::error_message()` — человекочитаемые описания ошибок (Фаза 12) |
| **Глубокое копирование** | `node_clone()` + `pjson_db::clone()` — создание полных копий поддеревьев JSON в ПАП (Фаза 13) |
| **PMM интеграция** | Подключена библиотека [PersistMemoryManager](https://github.com/netkeep80/PersistMemoryManager) — единственный бэкенд ПАП (Фаза 14); утилита миграции `pam_migrate` (Задача 14.9); устаревший код ПАМ удалён (Задача 14.10); все тесты и демо адаптированы для PMM (Задача 14.11) |
| **Консолидация** | Устранение дублирования между оригинальными `.h` и `_pmm.h` файлами (Фаза 15); `_pmm.h` — канонические реализации, оригинальные файлы — тонкие обёртки-алиасы; `pmem_array.h` → `pmem_array_pmm.h` (Задача 15.1); `pvector.h` → `pvector_pmm.h` (Задача 15.2); `pmap.h` → `pmap_pmm.h` (Задача 15.3) |

---

## Расширения JSON

### `$ref` — ссылки (указатели)

```json
{
  "config": { "$ref": "/defaults/config" }
}
```

При разборе объект `{ "$ref": "path" }` (ровно один ключ) преобразуется в **ref-узел** в ПАП:

- `ref_val.path` — интернированный путь (для сериализации и диагностики)
- `ref_val.target` — прямой `node_id` целевого узла (разрешается при загрузке)

При чтении через `get()` ref-узлы разыменовываются автоматически. Обнаруживаются циклические ссылки (ошибка `ref_cycle`).

### `$base64` — бинарные данные

```json
{
  "thumbnail": { "$base64": "iVBORw0KGgoAAAANSUhEUgAA..." }
}
```

При разборе объект `{ "$base64": "..." }` преобразуется в **binary-узел** с байтовым массивом в ПАП. При сериализации байтовый массив кодируется обратно в base64.

---

## Архитектура

### Слои системы

```
┌─────────────────────────────────────────────┐
│   Слой D: pjson_db (Фазы 6–9) ✅            │
│   (path-адресация, $ref, метрики, API)      │
│   db_metrics: персистные метрики в ПАП      │
│   operator[], find, insert, search_node_strings │
├─────────────────────────────────────────────┤
│   Слой D: Итераторы (Фаза 10) ✅            │
│   node_view_iterator: range-based for array │
│   object_iterator: items() для объектов     │
├─────────────────────────────────────────────┤
│   Слой D: Коды ошибок (Фаза 11) ✅          │
│   node_error: not_found, wrong_type,        │
│   index_out_of_range, ref_cycle, ...        │
│   node_view::is_error() / error()           │
├─────────────────────────────────────────────┤
│   Слой D: Сообщения об ошибках (Фаза 12) ✅ │
│   node_error_message(): текст ошибки        │
│   node_view::error_message()                │
├─────────────────────────────────────────────┤
│   Слой D: Глубокое копирование (Фаза 13) ✅ │
│   node_clone(): глубокая копия узла         │
│   pjson_db::clone(): копирование по пути    │
├─────────────────────────────────────────────┤
│   Слой C: pjson_node + pjson_pool           │
│   (модель узлов, пул аллокации)             │
├─────────────────────────────────────────────┤
│   Слой C: pjson_codec                       │
│   (парсинг, сериализация, base64)           │
├─────────────────────────────────────────────┤
│   Слой B: pstringview + pstring + pmem_array │
│   (readonly/readwrite строки, массивы)       │
│   pmem_array.h → обёртка (Фаза 15) ✅       │
│   pmap.h → обёртка (Фаза 15) ✅             │
├─────────────────────────────────────────────┤
│   Слой A: PMM (Фаза 14) ✅                   │
│   PersistMemoryManager: новый бэкенд ПАП    │
│   pam_pmm_config.h: конфигурация PamManager │
│   pam_adapter.h: pptr<T> ↔ uintptr_t        │
│   pmem_array_pmm.h: массивы (каноник) ✅    │
│   pvector.h → обёртка (Фаза 15) ✅          │
│   pmap_pmm.h: карта (каноник) ✅            │
│   pjson_pool_pmm.h: пул узлов на PMM ✅     │
│   pam_pmm.h: фасад ПАМ на PMM ✅            │
│   fptr_pmm.h: персистный указатель PMM ✅   │
│   persist_pmm.h: обёртка persist PMM ✅     │
│   pallocator_pmm.h: STL аллокатор PMM ✅    │
│   pjson_db_pmm.h: JSON-БД на PMM ✅         │
└─────────────────────────────────────────────┘
```

### Файлы проекта

| Файл | Слой | Описание |
|------|------|----------|
| ~~`pam_core.h`~~ | ~~A~~ | Удалён (Задача 14.10): старое ядро ПАМ заменено на PMM |
| ~~`pam.h`~~ | ~~A~~ | Удалён (Задача 14.10): заменён на `pam_pmm.h` |
| `pam_pmm_config.h` | A' | Конфигурация менеджера PMM: определяет `PamManager` для будущей миграции (Фаза 14) |
| `pam_adapter.h` | A' | Адаптер pptr<T> ↔ uintptr_t: слой совместимости для плавного перехода на PMM (Задача 14.1); `pptr_to_offset()`, `offset_to_pptr()`, `pmm_resolve<T>()` |
| `pmem_array_pmm.h` | A' | Каноническая реализация персистного массива (Задача 14.2, консолидация 15.1): `pmem_array_hdr_pmm`, шаблонные функции `pmem_array_pmm_*` |
| `pvector_pmm.h` | A' | PMM-реализация динамического массива (Задача 14.2): `pvector_pmm<T>`, совместим по API с `pvector<T>` |
| `pmap_pmm.h` | A' | PMM-реализация персистной карты (Задача 14.3): `pmap_pmm<K,V>` — sorted array, совместим по API с `pmap<K,V>` |
| `pstring_pmm.h` | A' | PMM-реализация персистной изменяемой строки (Задача 14.4.2): `pstring_pmm` — readwrite строка с `assign()`, `clear()` |
| `pstringview_pmm.h` | A' | PMM-реализация интернированной строки (Задача 14.4.1): `pstringview_pmm` — адаптер для `pmm::pstringview`, O(1) сравнение |
| `pjson_pool_pmm.h` | A' | PMM-реализация пула узлов (Задача 14.5): `pjson_pool_pmm` — быстрая аллокация O(1) через PMM + free-list |
| `pam_pmm.h` | A' | Фасад ПАМ на PMM (Задача 14.6): `pam_pmm_init()`, `pam_pmm_create<T>()`, `pam_pmm_find()`, `pam_pmm_save()`, реестр именованных объектов |
| `fptr_pmm.h` | A' | PMM-реализация персистного указателя (Задача 14.7): `fptr_pmm<T>` — обёртка над `uintptr_t`, `New()`, `NewArray()`, `Delete()`, `find()`, алиас `pjson::fptr<T>` |
| `pjson_db_pmm.h` | D' | PMM-реализация JSON-БД (Задача 14.8): `pjson_db_pmm` — API совместим с `pjson_db`, использует PMM для хранения |
| `persist_pmm.h` | A' | PMM-реализация обёртки для POD-типов (Задача 14.7): `persist_pmm<T>` — для обратной совместимости, `sizeof(persist_pmm<T>) == sizeof(T)`, алиас `pjson::persist<T>` |
| `pallocator_pmm.h` | A' | PMM-реализация STL-аллокатора (Задача 14.7): `pallocator_pmm<T>` — совместим с `std::vector<T, pallocator_pmm<T>>`, алиас `pjson::pallocator<T>` |
| `deps/pmm/pmm.h` | A' | [PersistMemoryManager](https://github.com/netkeep80/PersistMemoryManager) — новый бэкенд ПАП (Фаза 14) |
| ~~`persist.h`~~ | ~~A~~ | Удалён (Задача 14.10): заменён на `persist_pmm.h`, `fptr_pmm.h` |
| `pmem_array.h` | B | Обёртка-алиас для `pmem_array_pmm.h` (Задача 15.1): `pmem_array_hdr` = `pmem_array_hdr_pmm`, функции `pmem_array_*` делегируют в `pmem_array_pmm_*` |
| `pvector.h` | B | Обёртка-алиас для `pvector_pmm.h` (Задача 15.2): `pvector<T>` = `pvector_pmm<T>` |
| `pmap.h` | B | Обёртка-алиас для `pmap_pmm.h` (Задача 15.3): `pmap<K,V>` = `pmap_pmm<K,V>`, `pmap_entry<K,V>` = `pmap_entry_pmm<K,V>` |
| `pstring.h` | B | Персистная readwrite строка для JSON string-value узлов; нет SSO; `assign()` изменяет значение на месте |
| `pstringview.h` | B | Интернированная read-only строка + персистный словарь (`pstringview_table`); смещение таблицы хранится в `pam_header.string_table_offset`; содержит `pam_intern_string()`, `pam_search_strings()`, `pam_all_strings()` |
| `pallocator.h` | B | STL-совместимый аллокатор поверх ПАМ |
| `pjson_node.h` | C | Новая модель узлов JSON (Фаза 3): `node_tag`, `node_id`, `node`, `node_view`, `object_entry`; вспомогательные функции init/set/assign/push_back/insert; итераторы: `node_view_iterator`, `object_iterator`, `object_items_range`, `array_range` (Фаза 10); коды ошибок: `node_error`, `NODE_ERROR_BASE`, `node_view_error()`, `node_view::is_error()`, `node_view::error()` (Фаза 11); сообщения об ошибках: `node_error_message()`, `node_view::error_message()` (Фаза 12); глубокое копирование: `node_clone()` (Фаза 13) |
| `pjson_pool.h` | C | Пул узлов JSON (Фаза 4): `pjson_pool` — быстрая аллокация O(1) через `pvector<node>` + free-list на основе `node_tag::_free`; API: `alloc()`, `free()`, `get()` |
| `pjson_codec.h` | C | Новая сериализация/десериализация (Фаза 5): парсер/сериализатор для `node_id`-модели; поддержка `$ref` и `$base64`; Base64 кодек; функции: `node_to_string()`, `node_from_string()`, `node_parse()` |
| `pjson_db.h` | D | Менеджер персистной JSON-БД (Фазы 6–8, 13): единственный заголовок для конечного пользователя (Тр.18); path-адресация (`/a/b/0/c`), `put`/`get`/`erase`/`exists`, разыменование `$ref`, `resolve_all_refs()`, персистные метрики (`db_metrics`) через `/$metrics`, `update_metrics()`, pmap-интерфейс (`operator[]`, `find`, `insert`), сквозной поиск по строкам (`search_strings`, `search_node_strings`), сериализация, глубокое копирование (`clone()`) |
| `main.cpp` | — | Демонстрационная программа |
| `pam_migrate.cpp` | — | Утилита миграции `.pam` файлов со старого формата на PMM (Задача 14.9) |
| `tests/` | — | Тесты на Catch2 |
| `CMakeLists.txt` | — | Система сборки (CMake 3.16+, C++20) |

---

## Типы узлов

```cpp
enum class node_tag : uint32_t {
    null,       // null
    boolean,    // true / false
    integer,    // int64_t
    uinteger,   // uint64_t
    real,       // double
    string,     // pstring (readwrite, изменяемое строковое значение)
    binary,     // pvector<uint8_t> в ПАП ($base64 при сериализации)
    array,      // pvector<node_id>
    object,     // pmap<pstringview, node_id> — ключи readonly (pstringview)
    ref,        // pstringview path (readonly) + node_id target ($ref при сериализации)
};
```

---

## API (целевой интерфейс)

### Открытие базы данных

```cpp
#include "pjson_db.h"

// Открыть или создать базу данных
auto db = pjson_db::open("data.pam");
```

### Запись данных

```cpp
db.put("/users/alice/name",  "Alice");
db.put("/users/alice/age",   30);
db.put("/users/alice/active", true);

db.save(); // сохранить образ ПАП в файл
```

### Чтение данных

```cpp
node_view name = db.get("/users/alice/name");
// name.as_string() -> "Alice"

node_view age = db.get("/users/alice/age");
// age.as_int() -> 30
```

### Работа с `$ref`

```cpp
// Создать ссылку через put_ref()
db.put_ref("/link", "/users/alice");

// Или через parse_into() с JSON
db.parse_into("/link2", R"({"$ref": "/users/alice"})");

// Разрешить все $ref-узлы (устанавливает node_id цели по пути)
db.resolve_all_refs();

// Чтение автоматически разыменовывает ссылки
node_view linked_user = db.get("/link"); // → узел /users/alice

// Явное разыменование без следования по ссылке
node_view ref_node = db.get("/link", /*deref_ref=*/false);
```

### Работа с `$base64`

```cpp
// Парсинг бинарных данных
db.parse(R"({"data": {"$base64": "AAEC"}})");

// Получение бинарного узла
node_view bin = db.get("/data");
// bin.tag() == node_tag::binary
// bin.size() == 3, данные: [0x00, 0x01, 0x02]

// Сериализация обратно в JSON с $base64
std::string json = db.dump("/data");
// json == {"$base64":"AAEC"}
```

### Метрики (Фаза 7)

Метрики хранятся персистно в структуре `db_metrics` в ПАМ и обновляются при каждой мутации.

```cpp
// Метрики узлов
node_view node_count  = db.get("/$metrics/node_count_total");   // всего узлов в дереве
node_view free_count  = db.get("/$metrics/free_node_count");    // узлов в free-list пула
node_view used_count  = db.get("/$metrics/used_node_count");    // занятых узлов в пуле
node_view ref_cnt     = db.get("/$metrics/ref_count");          // ref-узлов
node_view arr_cnt     = db.get("/$metrics/array_count");        // array-узлов
node_view obj_cnt     = db.get("/$metrics/object_count");       // object-узлов
node_view bin_bytes   = db.get("/$metrics/binary_bytes_total"); // байт в binary-узлах

// Метрики строк
node_view str_count   = db.get("/$metrics/string_count_total"); // интернированных строк

// Метрики ПАМ
node_view bump        = db.get("/$metrics/pam_bump_offset");    // позиция bump-аллокатора
node_view free_blocks = db.get("/$metrics/pam_free_list_size"); // свободных блоков в ПАМ
node_view total_size  = db.get("/$metrics/pam_total_size");     // размер области данных ПАМ
node_view slot_cnt    = db.get("/$metrics/pam_slot_count");     // аллоцированных слотов
node_view named_cnt   = db.get("/$metrics/pam_named_count");    // именованных объектов

// Время сохранения
node_view save_time   = db.get("/$metrics/last_save_time");     // Unix timestamp последнего save()

// Явный пересчёт метрик (автоматически вызывается после каждой мутации)
db.update_metrics();

// Попытка записи в метрики — ошибка readonly
db.put("/$metrics/node_count_total", 0); // ошибка! возвращает false
```

### Поиск по строкам

На уровне ПАМ доступны функции словаря строк (после `#include "pstringview.h"`):

```cpp
// Интернировать строку через ПАМ (задача 2.2)
auto r = pam_intern_string("user_name");
// r.chars_offset != 0, r.length == 9

// Найти все строки, содержащие подстроку (задача 2.5)
auto results = pam_search_strings("user");
for (const auto& r : results) {
    // r.value — std::string, r.chars_offset, r.length
}

// Перебрать весь словарь строк (задача 2.5)
auto all = pam_all_strings();
for (const auto& r : all) {
    // r.value — интернированная строка
}
```

На уровне pjson_db (целевой API, фаза 6):

```cpp
// Найти все строки, содержащие подстроку (поиск по словарю интернированных ключей)
auto results = db.search_strings("alice");

// Перебрать весь словарь строк
for (auto sv : db.all_strings()) {
    // sv — pstringview
}
```

### Иерархический доступ и поиск по значениям (Фаза 8)

```cpp
// Задача 8.1: operator[] — доступ по пути; создаёт null-узел если путь не существует
node_view v = db["/config/host"]; // как std::map::operator[]

// Задача 8.1: find() — поиск без создания, без разыменования ref
node_view found = db.find("/config/host"); // node_view(0) если не найдено

// Задача 8.1: insert() — вставка JSON-значения по пути
node_view inserted = db.insert("/config/port", "8080");
node_view obj      = db.insert("/config/auth", R"({"enabled":true,"method":"jwt"})");

// Задача 8.2: search_node_strings() — поиск по pstring-ЗНАЧЕНИЯМ узлов (readwrite)
// (в отличие от search_strings(), который ищет по словарю КЛЮЧЕЙ pstringview)
auto val_results = db.search_node_strings("Alice");
for (node_id id : val_results) {
    std::string_view sv = node_view{id}.as_string(); // "Alice Smith" и т.п.
}

// Пустой pattern — все string-узлы в дереве
auto all_vals = db.search_node_strings("");
```

### Итерация по дереву JSON (Фаза 10)

```cpp
// Итерация по элементам массива (range-based for)
node_view scores = db.get("/user/scores");
for (node_view elem : scores)
    std::cout << elem.as_int() << "\n";

// Итерация по полям объекта через items()
node_view user = db.get("/user");
for (auto item : user.items())
    std::cout << item.key << ": " << item.value.as_string() << "\n";

// Structured bindings (C++17)
for (auto [key, val] : db.get("/config").items())
    std::cout << key << " = " << val.as_string() << "\n";
```

### Обработка ошибок (Фаза 11)

```cpp
// Узел не найден по пути
node_view v = db.get("/nonexistent/path");
if (v.is_error()) {
    if (v.error() == node_error::not_found)
        std::cerr << "Путь не найден\n";
}

// Навигация через скалярный узел
db.put("/flag", true);
node_view bad = db.get("/flag/sub");
// bad.is_error() == true
// bad.error() == node_error::wrong_type

// Выход индекса массива за границы
db.parse_into("/arr", "[1,2,3]");
node_view oob = db.get("/arr/99");
// oob.is_error() == true
// oob.error() == node_error::index_out_of_range

// Обнаружение цикла $ref
db.put_ref("/a", "/b");
db.put_ref("/b", "/a");
db.resolve_all_refs();
node_view cycle = db.get("/a");
// cycle.is_error() == true
// cycle.error() == node_error::ref_cycle

// Обратная совместимость: node_view{} — null, не ошибка
node_view null_v{};
// null_v.is_null() == true
// null_v.is_error() == false
// null_v.valid() == false

// Создание ошибочного view вручную (фабрика)
node_view err = node_view_error(node_error::not_found);
// err.is_error() == true
// err.error() == node_error::not_found
// err.valid() == false
```

#### Коды ошибок `node_error`

| Код | Значение |
|-----|----------|
| `node_error::none` | Нет ошибки |
| `node_error::not_found` | Узел не найден по пути |
| `node_error::wrong_type` | Неверный тип при навигации (не объект / не массив) |
| `node_error::index_out_of_range` | Индекс массива вне диапазона |
| `node_error::readonly` | Попытка записи в readonly-пространство (`/$metrics`) |
| `node_error::ref_cycle` | Обнаружен цикл или превышена глубина при разыменовании `$ref` |
| `node_error::parse_error` | Ошибка парсинга JSON |

#### Сообщения об ошибках (Фаза 12)

Функция `node_error_message()` и метод `node_view::error_message()` возвращают человекочитаемое описание ошибки:

```cpp
// Получить сообщение для кода ошибки
const char* msg = node_error_message(node_error::not_found);
// msg == "node not found"

// Получить сообщение через node_view
node_view v = db.get("/nonexistent");
if (v.is_error()) {
    std::cerr << "Ошибка: " << v.error_message() << "\n";
    // Вывод: "Ошибка: node not found"
}

// Для обычных и null node_view — "no error"
node_view ok{};
// ok.error_message() == "no error"
```

| Код ошибки | Сообщение |
|------------|-----------|
| `node_error::none` | `"no error"` |
| `node_error::not_found` | `"node not found"` |
| `node_error::wrong_type` | `"wrong node type for navigation"` |
| `node_error::index_out_of_range` | `"array index out of range"` |
| `node_error::readonly` | `"cannot modify read-only path"` |
| `node_error::ref_cycle` | `"cyclic $ref detected or max depth exceeded"` |
| `node_error::parse_error` | `"JSON parse error"` |

### Глубокое копирование (Фаза 13)

Функция `node_clone()` и метод `pjson_db::clone()` создают полную глубокую копию узла со всеми вложенными структурами:

```cpp
// Клонирование по пути (высокоуровневый API)
db.put("/original/name", "Alice");
db.put("/original/age", 30);

// Создать копию поддерева
bool ok = db.clone("/original", "/copy");
// ok == true

// Копия независима от оригинала
db.put("/copy/name", "Bob");
// db.get("/original/name").as_string() == "Alice" — не изменился
// db.get("/copy/name").as_string() == "Bob"

// Клонирование вложенных структур
db.parse_into("/data", R"({"users": [{"name": "Alice"}], "count": 1})");
db.clone("/data", "/backup");
// /backup содержит полную копию /data

// Низкоуровневый API: клонирование по node_id
node_id cloned = db.clone( db.get("/original").id );
// cloned — node_id независимой копии

// Или напрямую через node_clone()
node_id copy_id = node_clone( src_id );
```

**Особенности клонирования:**
- `ref`-узлы копируются как ref: путь копируется, но `target = 0` (ссылка не разрешена в копии)
- Строки (`pstring`) создаются как независимые копии
- Массивы и объекты копируются рекурсивно
- Нельзя клонировать из/в `/$metrics` (возвращает `false`)

---

## Новая модель узлов JSON: `pjson_node.h` (Фаза 3)

Фаза 3 вводит низкоуровневый API для работы с узлами JSON через `node_id`-адресацию.
Все узлы хранятся в ПАП как POD-структуры; доступ — через смещения (`node_id`).

### Типы узлов (`node_tag`)

```cpp
#include "pjson_node.h"

// Создать узел в ПАП
fptr<node> fn;
fn.New();
uintptr_t off = fn.addr();  // node_id — смещение узла в ПАП

// Инициализировать как нужный тип
node_set_bool( off, true );             // boolean
node_set_int( off, -42 );              // integer (int64_t)
node_set_uint( off, 100u );            // uinteger (uint64_t)
node_set_real( off, 3.14 );            // real (double)
node_set_string( off, "hello" );       // string (pstring, readwrite)
node_set_ref( off, "/path/to/node" );  // ref (pstringview path, readonly)
node_set_array( off );                 // array (pvector<node_id>)
node_set_object( off );                // object (pmap<pstringview, node_id>)
node_set_binary( off );                // binary (pvector<uint8_t>)
```

### `node_view` — безопасный accessor

```cpp
node_view v{ off };  // создать view по node_id

// Запросы типа
v.is_null();    v.is_boolean();  v.is_integer();
v.is_string();  v.is_array();    v.is_object();
v.is_ref();     v.is_binary();   v.is_number();

// Получение значений
bool     b = v.as_bool();
int64_t  i = v.as_int();
uint64_t u = v.as_uint();
double   d = v.as_double();
std::string_view s = v.as_string(); // вид на pstring (readwrite) в ПАП
std::string_view p = v.ref_path();  // путь ref-узла (pstringview, readonly)

// Навигация
uintptr_t sz = v.size();            // для array/object/string/binary
node_view elem = v.at( 0u );        // элемент массива по индексу
node_view field = v.at( "key" );    // поле объекта по ключу (pstringview)
std::string_view k = v.key_at( 0u );    // ключ i-го поля объекта (итерация)
node_view val = v.value_at( 0u );        // значение i-го поля объекта (итерация)

// Разыменование ref
node_view deref_v = v.deref( true, 32 ); // рекурсивное разыменование (max_depth=32)
node_view one_level = v.deref( false );   // только один уровень
```

### Работа с массивами

```cpp
node_set_array( arr_off );

// push_back возвращает node_id нового слота (инициализирован как null)
node_id slot = node_array_push_back( arr_off );
node_set_int( slot, 42 );

node_view arr_view{ arr_off };
REQUIRE( arr_view.size() == 1u );
REQUIRE( arr_view.at( 0u ).as_int() == 42 );
```

### Работа с объектами (ключи — `pstringview`, readonly)

```cpp
node_set_object( obj_off );

// node_object_insert интернирует ключ через pstringview_table (readonly)
node_id name_slot = node_object_insert( obj_off, "name" );
node_set_string( name_slot, "Alice" );

// Поиск по ключу (бинарный поиск по интернированному offset)
node_view obj_view{ obj_off };
REQUIRE( obj_view.at( "name" ).as_string() == "Alice" );
```

### Изменение строковых значений (`pstring`, readwrite)

```cpp
node_set_string( off, "original" );
node_assign_string( off, "updated" );  // освобождает старые данные, выделяет новые
// node_view{ off }.as_string() == "updated"
```

---

## Пул узлов JSON: `pjson_pool.h` (Фаза 4)

`pjson_pool` обеспечивает быструю аллокацию узлов `node` — O(1) амортизированно.

Вместо отдельного вызова `fptr<node>::New()` для каждого узла пул хранит непрерывный массив узлов (`pvector<node>`) в ПАП и управляет свободными слотами через free-list. Освобождённые слоты помечаются тегом `node_tag::_free` и повторно используются без обращения к аллокатору ПАМ.

### Создание пула

```cpp
#include "pjson_pool.h"

fptr<pjson_pool> pool;
pool.New();  // создать пул в ПАП
```

### API пула

```cpp
// Выделить новый узел (O(1) амортизированно)
node_id id = pool->alloc();

// Работать с узлом через стандартные хелперы
node_set_int( id, 42 );

// Читать узел через node_view
REQUIRE( node_view{ id }.as_int() == 42 );

// Вернуть слот в free-list (O(1))
pool->free( id );

// Прямой доступ по node_id
node& n = pool->get( id );
const node& cn = pool->get( id );  // const-версия

// Метрики
uintptr_t total   = pool->total_count();   // всего узлов в пуле
uintptr_t free_n  = pool->free_in_pool();  // в free-list
uintptr_t used_n  = pool->used_count();    // занято пользователем

// Освободить весь массив узлов (возвращает память в ПАМ)
pool->free_pool();

pool.Delete();
```

### Персистентность

Пул сохраняется вместе с образом ПАМ и восстанавливается без вызова конструкторов (Тр.10):

```cpp
// Сохранение
fptr<pjson_pool> pool;
pool.New( "my_pool" );
node_id id = pool->alloc();
node_set_string( id, "hello" );
pam_pmm_save();

// Загрузка
pam_pmm_init( "data.pam" );
uintptr_t off = pam_pmm_find( "my_pool" );
pjson_pool* p = pmm_resolve<pjson_pool>( off );
REQUIRE( node_view{ id }.as_string() == "hello" );
```

---

## Два типа строк в ПАП

В персистном адресном пространстве существуют ровно два типа строк с принципиально разными свойствами:

### Readonly строки (`pstringview`) — словарь ключей

Используются исключительно как **ключи `pmap`** (ключи объектов JSON, сегменты путей в `$ref`).

- Хранятся в едином внутреннем словаре (`pstringview_table`).
- **Никогда не удаляются** — только накапливаются.
- Одинаковые строки → один `chars_offset` (дедупликация).
- Сравнение ключей: **O(1)** через `chars_offset`.
- **Нет SSO**: любая строка, даже однобуквенная, хранится в ПАП.

### Readwrite строки (`pstring`) — строковые значения JSON

Используются исключительно как **JSON string-value узлы** (`node_tag::string`).

- Изменяемые: метод `assign()` позволяет заменить значение на месте в ПАП.
- **Нет SSO**: строки хранятся в ПАП через смещение, что обеспечивает сквозной поиск.
- Позволяют [jsonRVM](https://github.com/netkeep80/jsonRVM) работать непосредственно внутри базы данных, изменяя строковые значения узлов без пересоздания структуры.

### Полнотекстовый поиск

`pjson_db::search_strings(pattern)` охватывает **оба** типа:
- Словарь `pstringview` (ключи объектов и пути).
- Все `pstring`-значения в пуле узлов.

---

## Персистентность и управление памятью

### Бэкенд: PersistMemoryManager (PMM)

С Фазы 14 всё управление ПАП осуществляется через [PersistMemoryManager](https://github.com/netkeep80/PersistMemoryManager) (PMM). PMM предоставляет:

- Типобезопасные персистные указатели (`pptr<T>`)
- AVL-дерево свободных блоков (best-fit аллокатор)
- Настраиваемые адресные пространства (16/32/64-bit)
- Бэкенды хранения: `HeapStorage` / `StaticStorage` / `MMapStorage`

```
[PMM header]          — заголовок PMM (magic, stats, конфигурация)
[данные ПАП]
  [name_registry]     — реестр именованных объектов (pmap_pmm)
  [string_table]      — словарь интернированных строк (pmm::pstringview)
  [node_pool]         — пул узлов JSON (pjson_pool_pmm)
  [db_metrics]        — персистная структура метрик БД (db_metrics_pmm, фаза 7)
  [пользовательские данные]
```

### Управление памятью

- **AVL-аллокатор**: PMM использует AVL-дерево свободных блоков (best-fit), обеспечивая эффективную утилизацию памяти.
- **Гранулярность**: 16-байтовые гранулы для CacheManagerConfig.
- **Рост**: автоматическое расширение хранилища (25% для SingleThreadedHeap).
- **Строки накапливаются**: словарь строк только растёт, строки не освобождаются.

### Правила владения узлами

- Дерево **владеет** своими поддеревьями.
- `ref`-узел **не владеет** целевым узлом.
- При `erase` удаляется только ref-узел; цель не затрагивается.
- Shared-узлы допускаются **только** через `$ref`.

---

## Оптимизация производительности

### Предварительное резервирование

Перед массовой загрузкой данных используйте `ReserveSlots(n)`:

```cpp
db.ReserveSlots(100000); // зарезервировать для 100k узлов
db.parse_file("large_dataset.json");
```

Это устраняет многократные реаллокации и значительно ускоряет парсинг больших JSON-файлов.

### Сброс состояния

```cpp
pam_pmm_reset(); // очистить всё ПАП и пересоздать менеджер
```

Быстрее, чем удаление 100k+ узлов по одному (O(n²)).

---

## Требования

| Требование | Описание |
|---|---|
| Тр.1 | Персистные объекты используют только персистные указатели (смещения) |
| Тр.2 | Создание/удаление объектов — через методы аллокатора ПАМ |
| Тр.3 | При запуске аллокатор инициализируется именем файла-хранилища |
| Тр.4 | Единое ПАП для объектов всех типов |
| Тр.5 | `sizeof(fptr<T>) == sizeof(void*)` |
| Тр.6 | Все комментарии в коде — на русском языке |
| Тр.7 | Никакой логики с именами файлов в `persist<T>` |
| Тр.8 | `sizeof(persist<T>) == sizeof(T)` |
| Тр.9 | Никакой логики с именами файлов в `fptr<T>` |
| Тр.10 | При загрузке образа ПАП конструкторы/деструкторы не вызываются |
| Тр.11 | Объекты `persist<T>` живут только в образе ПАП |
| Тр.12 | Доступ к персистным объектам — только через `fptr<T>` или `node_id` |
| Тр.13 | `fptr<T>` может находиться как в обычной памяти, так и в ПАП |
| Тр.14 | ПАМ — персистный объект, хранит имена объектов и словарь строк |
| Тр.15 | `fptr<T>` инициализируется строковым именем объекта через ПАМ |
| Тр.16 | ПАМ хранит карту объектов, их имена и словарь интернированных строк |
| Тр.17 | Строки в ПАП не имеют SSO — `pstringview` и `pstring` хранятся через `chars_offset`, без inline-буферов |
| Тр.18 | `pjson_db.h` — единственный заголовок для конечного пользователя |
| Тр.19 | В ПАП ровно два типа строк: readonly (`pstringview`, только ключи/пути) и readwrite (`pstring`, только строковые значения JSON) |
| Тр.20 | `pstring`-узлы (`node_tag::string`) поддерживают изменение значения на месте для совместимости с [jsonRVM](https://github.com/netkeep80/jsonRVM) |

---

## Производительность (Фаза 9)

Производительность `pjson_db` при работе с 10k–100k узлов (информационные тесты в `tests/test_pjson_db_perf.cpp`):

| Операция | Кол-во | Время (ориентир) |
|---|---|---|
| `put(int)` | 10k | ~1–4 с |
| `put(string)` | 10k | ~1–4 с |
| `get()` | 100k запросов | < 50 мс |
| `parse_into(JSON)` | 1k объектов | < 100 мс |
| `erase()` | 10k | ~3–4 с |

**Примечание:** `put()` и `erase()` имеют накладные расходы O(depth) на обход пути при каждой операции.
Для массовой загрузки данных рекомендуется предварительно зарезервировать слоты через `ReserveSlots()`.

---

## Сборка и тестирование

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

---

## Миграция на PMM (Фаза 14)

Фаза 14 вводит новый бэкенд ПАП на основе [PersistMemoryManager](https://github.com/netkeep80/PersistMemoryManager). Формат файла `.pam` полностью несовместим между старым ПАМ и новым PMM.

### Утилита миграции

Для миграции существующих `.pam` файлов используйте утилиту `pam_migrate`:

```bash
# Сборка
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Миграция
./build/pam_migrate старый_файл.pam новый_файл.pam
```

Алгоритм миграции:
1. Загрузка старого `.pam` через PMM.
2. Экспорт дерева JSON в строку.
3. Создание нового `.pam` через PMM.
4. Импорт JSON в новую БД.
5. Сохранение.

**Важно:** Рекомендуется сделать резервную копию перед миграцией.

---

## Лицензия

Unlicense — общественное достояние. Подробности в файле `LICENSE`.
