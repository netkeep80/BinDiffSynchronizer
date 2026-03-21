# pjson_db_pmm — персистная JSON-база данных

C++20 header-only библиотека для работы с JSON в персистном адресном пространстве (ПАП).

---

## Быстрый старт

```cpp
#include "pjson_db_pmm.h"
using namespace pjson;

int main() {
    // Открыть или создать базу данных
    auto db = pjson_db_pmm::open("data.pam");

    // Записать данные
    db.put("/users/alice/name", "Alice");
    db.put("/users/alice/age",  30);
    db.put("/users/alice/active", true);

    // Прочитать данные
    node_view name = db.get("/users/alice/name");
    printf("Name: %.*s\n", (int)name.as_string().size(), name.as_string().data());

    // Сохранить образ ПАП в файл
    db.save();
}
```

---

## Концепция

**pjson_db_pmm** позволяет работать с JSON-данными так же, как с `nlohmann::json`, но с одним принципиальным отличием: все объекты хранятся в **персистном адресном пространстве** — двоичном образе файла, отображённом в память. Это превращает JSON в полноценную базу данных: данные переживают перезапуск программы без сериализации и десериализации.

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
| **Два типа строк** | readonly (`pstringview_pmm`): ключи объектов, пути `$ref`, интернированы, сравнение O(1); readwrite (`PamManager::pstring`): строковые значения JSON, изменяемые на лету |
| **Нет SSO** | Ни `pstringview_pmm`, ни `PamManager::pstring` не используют SSO — все строки хранятся в ПАП (необходимо для сквозного поиска) |
| **jsonRVM-совместимость** | `pstring`-узлы могут модифицироваться непосредственно в БД библиотекой [jsonRVM](https://github.com/netkeep80/jsonRVM); `node_id`-ссылки стабильны при resize array/object |
| **Path-адресация** | Доступ к узлам через строковые пути вида `/a/b/0/c` |
| **$ref как указатели** | `{ "$ref": "/path" }` при разборе становится прямым указателем в ПАП |
| **Метрики** | Персистная структура `db_metrics_pmm` в ПАМ; обновляется при каждой мутации; доступ через `/$metrics/...` |
| **pmap-интерфейс** | `operator[]`, `find`, `insert` для доступа по пути без явного указания типа |
| **Поиск по строкам** | `search_strings` — по словарю ключей (pstringview_pmm); `search_node_strings` — по значениям узлов (pstring) |
| **Итераторы** | `node_view` поддерживает range-based for: `begin()`/`end()` для массивов, `items()` для объектов |
| **Коды ошибок** | `node_error` enum + `is_error()` / `error()` в `node_view`; `get()` возвращает типизированные ошибки (`not_found`, `wrong_type`, `index_out_of_range`, `ref_cycle`) |
| **Глубокое копирование** | `node_clone()` + `pjson_db_pmm::clone()` — создание полных копий поддеревьев JSON в ПАП |
| **PMM** | Библиотека [PersistMemoryManager](https://github.com/netkeep80/PersistMemoryManager) — единственный бэкенд ПАП |
| **Пакетные операции** | `batch_begin()`/`batch_end()` и RAII-обёртка `batch_guard` — откладывают пересчёт метрик при массовых мутациях; поддержка вложенности |

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
│   Слой D: pjson_db_pmm                      │
│   (path-адресация, $ref, метрики, API)      │
│   db_metrics_pmm: персистные метрики в ПАП  │
│   operator[], find, insert,                 │
│   search_node_strings, node_clone           │
├─────────────────────────────────────────────┤
│   Слой C: pjson_node + pjson_pool_pmm       │
│   (модель узлов, пул аллокации)             │
├─────────────────────────────────────────────┤
│   Слой C: pjson_codec                       │
│   (парсинг, сериализация, base64)           │
├─────────────────────────────────────────────┤
│   Слой B: pmap_pmm                          │
│   + pstringview_pmm (тонкая обёртка)        │
│   (sorted map, readonly строки)             │
├─────────────────────────────────────────────┤
│   Слой A: PMM                               │
│   PersistMemoryManager: бэкенд ПАП          │
│   pam_pmm_config.h: конфигурация PamManager │
│   pam_pmm.h: фасад ПАМ на PMM              │
│   fptr_pmm.h: персистный указатель          │
└─────────────────────────────────────────────┘
```

### Файлы проекта

| Файл | Слой | Описание |
|------|------|----------|
| `pam_pmm_config.h` | A | Конфигурация менеджера PMM: определяет `PamManager` |
| `pam_pmm.h` | A | Фасад ПАМ на PMM: init, create, find, save, resolve; реестр именованных объектов |
| `fptr_pmm.h` | A | Персистный указатель: `fptr_pmm<T>` — тонкая обёртка над `pptr<T>` |
| `pmap_pmm.h` | B | Персистная карта: sorted array на `PamManager::parray<Entry>`, бинарный поиск O(log n) |
| `pstringview_pmm.h` | B | Тонкая обёртка: hooks для персистентности AVL-корня, поиск по словарю строк |
| `pjson_pool_pmm.h` | C | Пул узлов: `PamManager::ppool<node>` — чанковая аллокация O(1) |
| `pjson_node.h` | C | Модель узлов JSON: `node_tag`, `node_id`, `node`, `node_view`, `object_entry`; итераторы; `node_clone()` |
| `pjson_codec.h` | C | Сериализация/десериализация: парсер/сериализатор с поддержкой `$ref`, `$base64`, Base64 кодек |
| `pjson_db_helpers.h` | D | Вспомогательные функции: обход дерева, подсчёт узлов, поиск |
| `pjson_db_pmm.h` | D | Менеджер персистной JSON-БД: path-адресация, `put`/`get`/`erase`, `$ref`, метрики, поиск, клонирование |
| `deps/pmm/pmm.h` | A | [PersistMemoryManager](https://github.com/netkeep80/PersistMemoryManager) — бэкенд ПАП |
| `main.cpp` | — | Демонстрационная программа |
| `tests/` | — | Тесты на Catch2 (648 тестов, ~360 000 assertion) |
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
    string,     // PamManager::pstring (readwrite, изменяемое строковое значение)
    binary,     // PamManager::parray<uint8_t> в ПАП ($base64 при сериализации)
    array,      // PamManager::parray<node_id>
    object,     // PamManager::parray<object_entry> — sorted array, ключи readonly (pstringview_pmm)
    ref,        // pstringview_pmm path (readonly) + node_id target ($ref при сериализации)
};
```

---

## API

### Открытие базы данных

```cpp
#include "pjson_db_pmm.h"
using namespace pjson;

// Открыть или создать базу данных
auto db = pjson_db_pmm::open("data.pam");
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

### Парсинг и сериализация JSON

```cpp
// Парсинг JSON во вложенный путь
db.parse_into("/config", R"({"host":"localhost","port":8080,"debug":true})");

// Сериализация поддерева
std::string json = db.dump( db.get("/config").id );
// json == {"debug":true,"host":"localhost","port":8080}

// Полный дамп корневого объекта
std::string full = db.dump();
```

### Метрики

Метрики хранятся персистно в структуре `db_metrics_pmm` в ПАМ и обновляются при каждой мутации.

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
node_view total_size  = db.get("/$metrics/pam_total_size");     // размер области данных ПАМ
node_view slot_cnt    = db.get("/$metrics/pam_slot_count");     // аллоцированных слотов
node_view named_cnt   = db.get("/$metrics/pam_named_count");    // именованных объектов

// Время сохранения
node_view save_time   = db.get("/$metrics/last_save_time");     // Unix timestamp последнего save()

// Попытка записи в метрики — ошибка readonly
db.put("/$metrics/node_count_total", 0); // ошибка! возвращает false
```

### Поиск по строкам

```cpp
// Поиск по словарю интернированных ключей (pstringview_pmm)
auto results = db.search_strings("alice");

// Поиск по pstring-ЗНАЧЕНИЯМ узлов (readwrite)
auto val_results = db.search_node_strings("Alice");
for (node_id id : val_results) {
    std::string_view sv = node_view{id}.as_string(); // "Alice Smith" и т.п.
}

// Пустой pattern — все string-узлы в дереве
auto all_vals = db.search_node_strings("");
```

### Иерархический доступ

```cpp
// operator[] — доступ по пути; создаёт null-узел если путь не существует
node_view v = db["/config/host"]; // как std::map::operator[]

// find() — поиск без создания, без разыменования ref
node_view found = db.find("/config/host"); // node_view(0) если не найдено

// insert() — вставка JSON-значения по пути
node_view inserted = db.insert("/config/port", "8080");
node_view obj      = db.insert("/config/auth", R"({"enabled":true,"method":"jwt"})");
```

### Итерация по дереву JSON

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

### Обработка ошибок

```cpp
node_view v = db.get("/nonexistent/path");
if (v.is_error()) {
    std::cerr << "Ошибка: " << v.error_message() << "\n";
    // "Ошибка: node not found"
}

// node_view{} — null, не ошибка
node_view null_v{};
// null_v.is_null() == true, null_v.is_error() == false
```

#### Коды ошибок `node_error`

| Код | Значение | Сообщение |
|-----|----------|-----------|
| `none` | Нет ошибки | `"no error"` |
| `not_found` | Узел не найден по пути | `"node not found"` |
| `wrong_type` | Неверный тип при навигации | `"wrong node type for navigation"` |
| `index_out_of_range` | Индекс массива вне диапазона | `"array index out of range"` |
| `readonly` | Попытка записи в `/$metrics` | `"cannot modify read-only path"` |
| `ref_cycle` | Цикл или превышена глубина `$ref` | `"cyclic $ref detected or max depth exceeded"` |
| `parse_error` | Ошибка парсинга JSON | `"JSON parse error"` |

### Глубокое копирование

```cpp
db.put("/original/name", "Alice");
db.put("/original/age", 30);

// Создать полную копию поддерева
bool ok = db.clone("/original", "/copy");

// Копия независима от оригинала
db.put("/copy/name", "Bob");
// db.get("/original/name").as_string() == "Alice" — не изменился
```

### Пакетные операции (batch)

При массовых мутациях каждая операция по умолчанию пересчитывает метрики. Пакетные операции откладывают пересчёт:

```cpp
// RAII-обёртка (рекомендуемый способ)
{
    auto guard = db.batch();
    for (int i = 0; i < 10000; ++i) {
        std::string path = "/item/" + std::to_string(i);
        db.put(path.c_str(), i);
    }
} // метрики пересчитываются один раз здесь
```

---

## Два типа строк в ПАП

### Readonly строки (`pstringview_pmm`) — словарь ключей

Используются как **ключи объектов JSON** и **сегменты путей `$ref`**.

- Хранятся в едином внутреннем словаре.
- **Никогда не удаляются** — только накапливаются.
- Одинаковые строки → один `chars_offset` (дедупликация).
- Сравнение ключей: **O(1)** через `chars_offset`.

### Readwrite строки (`PamManager::pstring`) — строковые значения JSON

Используются как **JSON string-value узлы** (`node_tag::string`).

- Изменяемые: метод `assign()` позволяет заменить значение на месте в ПАП.
- Позволяют [jsonRVM](https://github.com/netkeep80/jsonRVM) работать непосредственно внутри базы данных.

---

## Персистентность и управление памятью

### Бэкенд: PersistMemoryManager (PMM)

Всё управление ПАП осуществляется через [PersistMemoryManager](https://github.com/netkeep80/PersistMemoryManager) (PMM). PMM предоставляет:

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
  [db_metrics]        — персистная структура метрик БД (db_metrics_pmm)
  [пользовательские данные]
```

### Управление памятью

- **AVL-аллокатор**: PMM использует AVL-дерево свободных блоков (best-fit).
- **Гранулярность**: 16-байтовые гранулы для CacheManagerConfig.
- **Рост**: автоматическое расширение хранилища (25% для SingleThreadedHeap).
- **Строки накапливаются**: словарь строк только растёт, строки не освобождаются.

### Правила владения узлами

- Дерево **владеет** своими поддеревьями.
- `ref`-узел **не владеет** целевым узлом.
- При `erase` удаляется только ref-узел; цель не затрагивается.
- Shared-узлы допускаются **только** через `$ref`.

---

## Известные ограничения

- **Глобальное состояние PMM** — в одном процессе может быть открыта только одна БД (см. [plan.md](plan.md), Проблема 3)
- **Нет escaping `/` в путях** — ключи объектов, содержащие `/`, недоступны через path-адресацию (см. [plan.md](plan.md), Проблема 7)
- ~~**Утечка временных узлов метрик**~~ — **Исправлено** в Этапе 8.4: один pre-allocated узел переиспользуется для всех вызовов метрик
- **Не потокобезопасно** — CacheManagerConfig (по умолчанию) использует NoLock; для многопоточности нужен PersistentDataConfig
- **Строки не освобождаются** — словарь `pstringview_pmm` только растёт

---

## Производительность

| Операция | Кол-во | Время (ориентир) |
|---|---|---|
| `put(int)` | 10k | ~1–4 с |
| `put(string)` | 10k | ~1–4 с |
| `get()` | 100k запросов | < 50 мс |
| `parse_into(JSON)` | 1k объектов | < 100 мс |
| `erase()` | 10k | ~3–4 с |

**Совет:** Для массовой загрузки данных используйте `batch()` и `ReserveSlots()`.

---

## Сборка и тестирование

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

---

## План развития

Подробный анализ текущих проблем реализации и план рефакторинга — в [plan.md](plan.md).

---

## Лицензия

Unlicense — общественное достояние. Подробности в файле `LICENSE`.
