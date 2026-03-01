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
| **C++17** | Без внешних зависимостей |
| **Персистность** | Данные в ПАП переживают перезапуск без явной сериализации |
| **Два типа строк** | readonly (`pstringview`): ключи объектов, пути `$ref`, интернированы, сравнение O(1); readwrite (`pstring`): строковые значения JSON, изменяемые на лету |
| **Нет SSO** | Ни `pstringview`, ни `pstring` не используют SSO — все строки хранятся в ПАП (необходимо для сквозного поиска) |
| **jsonRVM-совместимость** | `pstring`-узлы могут модифицироваться непосредственно в БД библиотекой [jsonRVM](https://github.com/netkeep80/jsonRVM) |
| **Path-адресация** | Доступ к узлам через строковые пути вида `/a/b/0/c` |
| **$ref как указатели** | `{ "$ref": "/path" }` при разборе становится прямым указателем в ПАП |
| **Метрики** | Статистика БД доступна через `/$metrics/...` |
| **Поиск по строкам** | Сквозной поиск по всем строкам словаря ПАП |

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
│   Слой D: pjson_db                          │
│   (path-адресация, $ref, метрики, API)      │
├─────────────────────────────────────────────┤
│   Слой C: pjson_node + pjson_pool           │
│   (модель узлов, пул аллокации)             │
├─────────────────────────────────────────────┤
│   Слой C: pjson_codec                       │
│   (парсинг, сериализация, base64)           │
├─────────────────────────────────────────────┤
│   Слой B: pstringview + pstring + pmem_array │
│   (readonly/readwrite строки, массивы)       │
├─────────────────────────────────────────────┤
│   Слой A: pam_core + pam                    │
│   (ПАП: аллокатор, слоты, realloc)         │
└─────────────────────────────────────────────┘
```

### Файлы проекта

| Файл | Слой | Описание |
|------|------|----------|
| `pam_core.h` | A | Ядро ПАМ: аллокатор, слоты, карта имён, realloc; внутренние массивы через pam_array_hdr (≡ pmem_array_hdr) |
| `pam.h` | A | Фасад: включает pvector, pmap, pstring |
| `persist.h` | A | Базовые типы: fptr<T>, persist<T>, AddressManager |
| `pmem_array.h` | B | Общий примитив персистного массива: pmem_array_hdr + шаблонные функции init/reserve/push_back/pop_back/at/insert_sorted/find_sorted/erase_at/free/clear |
| `pvector.h` | B | Персистный динамический массив (тонкая обёртка над pmem_array_hdr) |
| `pmap.h` | B | Персистная карта (sorted array, тонкая обёртка над pmem_array_hdr) |
| `pstring.h` | B | Персистная readwrite строка для JSON string-value узлов; нет SSO; `assign()` изменяет значение на месте |
| `pstringview.h` | B | Интернированная read-only строка + персистный словарь (`pstringview_table`); смещение таблицы хранится в `pam_header.string_table_offset`; содержит `pam_intern_string()`, `pam_search_strings()`, `pam_all_strings()` |
| `pallocator.h` | B | STL-совместимый аллокатор поверх ПАМ |
| `pjson.h` | C | Персистный JSON (старый API): узлы, типы, layout |
| `pjson_node.h` | C | Новая модель узлов JSON (Фаза 3): `node_tag`, `node_id`, `node`, `node_view`, `object_entry`; вспомогательные функции init/set/assign/push_back/insert |
| `pjson_interning.h` | B | Интернирование строк для pjson |
| `pjson_node_pool.h` | C | Пул узлов для быстрой аллокации |
| `pjson_serializer.h` | C | Сериализация/десериализация pjson |
| `main.cpp` | — | Демонстрационная программа |
| `tests/` | — | Тесты на Catch2 |
| `CMakeLists.txt` | — | Система сборки (CMake 3.16+, C++17) |

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
// Создать ссылку (парсинг JSON)
db.parse(R"({"link": {"$ref": "/users/alice"}})");

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
// bin.binary_size() == 3, данные: [0x00, 0x01, 0x02]

// Сериализация обратно в JSON с $base64
std::string json = db.dump("/data");
// json == {"$base64":"AAEC"}
```

### Метрики

```cpp
// Метрики доступны через зарезервированное пространство /$metrics
node_view node_count = db.get("/$metrics/node_count_total");
node_view str_count  = db.get("/$metrics/string_count_total");

// Попытка записи в метрики — ошибка readonly
db.put("/$metrics/node_count_total", 0); // ошибка!
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
// Найти все строки, содержащие подстроку
auto results = db.search_strings("alice");

// Перебрать весь словарь строк
for (auto sv : db.all_strings()) {
    // sv — pstringview
}
```

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

### Структура файла ПАМ

```
[pam_header]          — заголовок (magic, version=10, offsets, bump, string_table_offset)
[данные ПАП]
  [type_vec]          — вектор типов TypeInfo
  [slot_map]          — карта слотов
  [name_map]          — карта имён объектов
  [free_list]         — список свободных областей (reuse)
  [string_table]      — словарь интернированных строк (pstringview_table, фаза 2)
  [node_pool]         — пул узлов JSON
  [пользовательские данные]
```

Смещение `string_table` хранится в поле `pam_header.string_table_offset` (фаза 2, PAM_VERSION 10) и восстанавливается при загрузке образа без вызова конструкторов.

### Управление памятью

- **Bump-allocator**: новые объекты выделяются линейно в конце ПАП.
- **Free-list**: удалённые блоки возвращаются в список свободных, повторно используются (first-fit).
- **Realloc**: `pvector::grow` / `pmap::grow` расширяют последний блок ПАП без копирования.
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
db.pam().ReserveSlots(100000); // зарезервировать для 100k узлов
db.parse_file("large_dataset.json");
```

Это устраняет многократные реаллокации и значительно ускоряет парсинг больших JSON-файлов.

### Сброс состояния

```cpp
db.pam().Reset(); // очистить всё ПАП за O(1)
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

## Сборка и тестирование

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

---

## Лицензия

Unlicense — общественное достояние. Подробности в файле `LICENSE`.
