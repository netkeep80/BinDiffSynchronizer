# pjson_db_pmm — персистная JSON-база данных

C++20 header-only библиотека для работы с JSON в персистном адресном пространстве (ПАП).

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
| **jsonRVM-совместимость** | `pstring`-узлы могут модифицироваться непосредственно в БД библиотекой [jsonRVM](https://github.com/netkeep80/jsonRVM) |
| **Path-адресация** | Доступ к узлам через строковые пути вида `/a/b/0/c` |
| **$ref как указатели** | `{ "$ref": "/path" }` при разборе становится прямым указателем в ПАП |
| **Метрики** | Персистная структура `db_metrics_pmm` в ПАМ; обновляется при каждой мутации; доступ через `/$metrics/...` |
| **pmap-интерфейс** | `operator[]`, `find`, `insert` для доступа по пути без явного указания типа |
| **Поиск по строкам** | `search_strings` — по словарю ключей (pstringview_pmm); `search_node_strings` — по значениям узлов (pstring) |
| **Итераторы** | `node_view` поддерживает range-based for: `begin()`/`end()` для массивов, `items()` для объектов |
| **Коды ошибок** | `node_error` enum + `is_error()` / `error()` в `node_view`; `get()` возвращает типизированные ошибки (`not_found`, `wrong_type`, `index_out_of_range`, `ref_cycle`) |
| **Сообщения об ошибках** | `node_error_message()` + `node_view::error_message()` — человекочитаемые описания ошибок |
| **Глубокое копирование** | `node_clone()` + `pjson_db_pmm::clone()` — создание полных копий поддеревьев JSON в ПАП |
| **PMM** | Библиотека [PersistMemoryManager](https://github.com/netkeep80/PersistMemoryManager) — единственный бэкенд ПАП |
| **Пакетные операции** | `batch_begin()`/`batch_end()` и RAII-обёртка `batch_guard` — откладывают пересчёт метрик при массовых мутациях; поддержка вложенности |
| **Метапрограммирование** | Шаблонные helpers для устранения дублирования кода: `node_resolve_and_set_tag()`, `node_set_container_empty()`, `node_object_find_key()`, `node_resolve_checked()`, реестр метрик |

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
│   Слой B: pmap_pmm                            │
│   + pstringview_pmm (тонкая обёртка)        │
│   (sorted map, readonly строки)             │
├─────────────────────────────────────────────┤
│   Слой A: PMM                               │
│   PersistMemoryManager: бэкенд ПАП          │
│   pam_pmm_config.h: конфигурация PamManager │
│   pam_adapter.h: pptr<T> ↔ uintptr_t        │
│   pam_pmm.h: фасад ПАМ на PMM              │
│   fptr_pmm.h: персистный указатель          │
└─────────────────────────────────────────────┘
```

### Файлы проекта

| Файл | Слой | Описание |
|------|------|----------|
| `pam_pmm_config.h` | A | Конфигурация менеджера PMM: определяет `PamManager` |
| `pam_adapter.h` | A | Адаптер pptr<T> ↔ uintptr_t: `pptr_to_offset()`, `offset_to_pptr()`, `pmm_resolve<T>()` |
| ~~`pmem_array_pmm.h`~~ | A | Удалён (Issue #145, План 2.2): используйте `PamManager::parray<T>` напрямую |
| ~~`pvector_pmm.h`~~ | A | Удалён (Issue #167, План 2.6): используйте `PamManager::parray<T>` напрямую |
| `pmap_pmm.h` | A | Персистная карта: `pmap_pmm<K,V>` — sorted array на `PamManager::parray<Entry>`, бинарный поиск O(log n); выбран вместо pmm::pmap (AVL-дерево), т.к. `rebuild_free_tree()` при загрузке файла сбрасывает AVL-поля пользовательских деревьев (Issue #166, План 2.3) |
| ~~`pstring_pmm.h`~~ | A | Удалён (Issue #144, План 2.1): используйте `PamManager::pstring` напрямую |
| `pstringview_pmm.h` | A | Тонкая обёртка: типовые алиасы `pmm_pstringview`/`pmm_pstringview_pptr`, hooks для персистентности AVL-корня, `pstringview_pmm_reset()`, AVL-обход для поиска; структура `pstringview_pmm` удалена (Issue #167, План 2.5) |
| `pjson_pool_pmm.h` | C | Пул узлов: `pjson_pool_pmm` = `PamManager::ppool<node>` — чанковая аллокация O(1) через pmm::ppool, node_id-совместимый API (Issue #166, План 2.4) |
| `pam_pmm.h` | A | Фасад ПАМ на PMM: `pam_pmm_init()`, `pam_pmm_create<T>()`, `pam_pmm_find()`, `pam_pmm_save()`, реестр именованных объектов; корневая структура `pam_pmm_root` через `PamManager::set_root()`/`get_root()` (Issue #163, План 1.2) |
| `fptr_pmm.h` | A | Персистный указатель: `fptr_pmm<T>` — тонкая обёртка над `pptr<T>` с `New()`, `Delete()`, `find()` |
| ~~`pallocator_pmm.h`~~ | A | Удалён (Issue #143, План 1.3): используйте `PamManager::pallocator<T>` напрямую |
| `deps/pmm/pmm.h` | A | [PersistMemoryManager](https://github.com/netkeep80/PersistMemoryManager) — бэкенд ПАП |
| `pjson_node.h` | C | Модель узлов JSON: `node_tag`, `node_id`, `node`, `node_view`, `object_entry`; функции init/set/assign/push_back/insert; итераторы; коды ошибок; глубокое копирование (`node_clone()`) |
| `pjson_codec.h` | C | Сериализация/десериализация: парсер/сериализатор для `node_id`-модели; `$ref`, `$base64`, Base64 кодек |
| `pjson_db_pmm.h` | D | Менеджер персистной JSON-БД: единственный заголовок для конечного пользователя; path-адресация, `put`/`get`/`erase`/`exists`, `$ref`, метрики (`db_metrics_pmm`), поиск по строкам, глубокое копирование |
| `pjson_db_helpers.h` | D | Вспомогательные функции для обхода дерева JSON |
| `main.cpp` | — | Демонстрационная программа |
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

На уровне ПАМ доступны функции словаря строк (после `#include "pam_pmm.h"`):

```cpp
// Интернировать строку через ПАМ
auto r = pam_intern_string("user_name");
// r.chars_offset != 0, r.length == 9

// Найти все строки, содержащие подстроку
auto results = pam_search_strings("user");
for (const auto& r : results) {
    // r.value — std::string, r.chars_offset, r.length
}

// Перебрать весь словарь строк
auto all = pam_all_strings();
for (const auto& r : all) {
    // r.value — интернированная строка
}
```

На уровне pjson_db_pmm:

```cpp
// Найти все строки, содержащие подстроку (поиск по словарю интернированных ключей)
auto results = db.search_strings("alice");

// Перебрать весь словарь строк
for (auto sv : db.all_strings()) {
    // sv — pstringview_pmm
}
```

### Иерархический доступ и поиск по значениям

```cpp
// operator[] — доступ по пути; создаёт null-узел если путь не существует
node_view v = db["/config/host"]; // как std::map::operator[]

// find() — поиск без создания, без разыменования ref
node_view found = db.find("/config/host"); // node_view(0) если не найдено

// insert() — вставка JSON-значения по пути
node_view inserted = db.insert("/config/port", "8080");
node_view obj      = db.insert("/config/auth", R"({"enabled":true,"method":"jwt"})");

// search_node_strings() — поиск по pstring-ЗНАЧЕНИЯМ узлов (readwrite)
// (в отличие от search_strings(), который ищет по словарю КЛЮЧЕЙ pstringview_pmm)
auto val_results = db.search_node_strings("Alice");
for (node_id id : val_results) {
    std::string_view sv = node_view{id}.as_string(); // "Alice Smith" и т.п.
}

// Пустой pattern — все string-узлы в дереве
auto all_vals = db.search_node_strings("");
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

// node_view{} — null, не ошибка
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

#### Сообщения об ошибках

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

### Глубокое копирование

Функция `node_clone()` и метод `pjson_db_pmm::clone()` создают полную глубокую копию узла со всеми вложенными структурами:

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

### Пакетные операции (batch)

При выполнении множества мутирующих операций (put, erase, parse_into) каждая операция по умолчанию пересчитывает метрики, что включает полный обход дерева. Пакетные операции позволяют отложить пересчёт до завершения всех операций.

```cpp
// RAII-обёртка (рекомендуемый способ)
{
    auto guard = db.batch();
    for (int i = 0; i < 10000; ++i) {
        std::string path = "/item/" + std::to_string(i);
        db.put(path.c_str(), i);
    }
} // метрики пересчитываются один раз здесь

// Или ручное управление
db.batch_begin();
db.put("/a", 1);
db.put("/b", 2);
db.erase("/c");
db.batch_end(); // метрики пересчитываются здесь

// Проверка состояния
bool active = db.in_batch(); // true внутри пакетной операции

// Вложенные пакетные операции
{
    auto outer = db.batch();
    db.put("/x", 1);
    {
        auto inner = db.batch();
        db.put("/y", 2);
    } // внутренний batch_end — метрики ещё не пересчитываются
} // внешний batch_end — метрики пересчитываются один раз
```

**Особенности:**
- Вызовы `batch_begin()`/`batch_end()` могут быть вложенными; пересчёт выполняется только при возврате глубины к нулю
- `batch_guard` — RAII-класс, вызывающий `batch_begin()` в конструкторе и `batch_end()` в деструкторе
- Данные доступны для чтения сразу после записи, даже внутри пакетной операции
- Метрики (`/$metrics/...`) могут быть неактуальны внутри пакетной операции

---

## Два типа строк в ПАП

В персистном адресном пространстве существуют ровно два типа строк с принципиально разными свойствами:

### Readonly строки (`pstringview_pmm`) — словарь ключей

Используются исключительно как **ключи `pmap_pmm`** (ключи объектов JSON, сегменты путей в `$ref`).

- Хранятся в едином внутреннем словаре.
- **Никогда не удаляются** — только накапливаются.
- Одинаковые строки → один `chars_offset` (дедупликация).
- Сравнение ключей: **O(1)** через `chars_offset`.
- **Нет SSO**: любая строка, даже однобуквенная, хранится в ПАП.

### Readwrite строки (`PamManager::pstring`) — строковые значения JSON

Используются исключительно как **JSON string-value узлы** (`node_tag::string`).

- Изменяемые: метод `assign()` позволяет заменить значение на месте в ПАП.
- **Нет SSO**: строки хранятся в ПАП через смещение, что обеспечивает сквозной поиск.
- Позволяют [jsonRVM](https://github.com/netkeep80/jsonRVM) работать непосредственно внутри базы данных, изменяя строковые значения узлов без пересоздания структуры.

### Полнотекстовый поиск

`pjson_db_pmm::search_strings(pattern)` охватывает **оба** типа:
- Словарь `pstringview_pmm` (ключи объектов и пути).
- Все `pstring`-значения в пуле узлов.

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

## Производительность

Производительность `pjson_db_pmm` при работе с 10k–100k узлов (информационные тесты в `tests/test_pjson_db_perf.cpp`):

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

## Состояние миграции на PMM

Библиотека проходит миграцию на типы [PersistMemoryManager](https://github.com/netkeep80/PersistMemoryManager). Подробности — в [plan.md](plan.md).

### Выполненные задачи

| Задача | Описание | Issue |
|--------|----------|-------|
| 1.2 | `pam_pmm.h` — корневой объект через `PamManager::set_root()`/`get_root()` | #163 |
| 1.3 | Удалён `pallocator_pmm.h` — используется `PamManager::pallocator<T>` | #143 |
| 1.4 | `fptr_pmm<T>` — тонкая обёртка над `pptr<T>` | #143 |
| 2.1 | Заменён `pstring_pmm` на `PamManager::pstring` | #144 |
| 2.2 | Заменён `pmem_array_pmm` на `PamManager::parray<T>` | #145 |
| 2.3 | `pmap_pmm` — sorted array на `PamManager::parray<Entry>` | #166 |
| 2.4 | `pjson_pool_pmm` → `PamManager::ppool<node>` | #166 |
| 2.5 | Упрощён `pstringview_pmm.h` — удалена структура `pstringview_pmm` | #167 |
| 2.6 | Удалён `pvector_pmm.h` | #167 |
| 3.1 | `node` union — все типы используют PMM (`pstring`, `parray`) | #144, #145 |
| 3.2 | `pjson_codec` — парсер/сериализатор обновлён | #144, #145 |
| 3.3 | `pjson_db_pmm` — публичный API обновлён | #144, #145, #166 |

### Оставшиеся задачи

| Задача | Описание | Блокировка |
|--------|----------|------------|
| 1.1 | Удаление `pam_adapter.h` (конверсия `pptr<T>` ↔ `uintptr_t`) | Ожидает pmm 4.4: `pptr::byte_offset()`, `pptr_from_byte_offset<T>()` |

---

## Сборка и тестирование

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

---

## Лицензия

Unlicense — общественное достояние. Подробности в файле `LICENSE`.
