# План разработки pjson_db — персистное адресное пространство для расширенного JSON

## Концепция проекта

**pjson_db** — C++17 header-only библиотека, реализующая персистное JSON-хранилище (база данных) поверх персистного адресного пространства (ПАП). Библиотека предоставляет runtime-API уровня `nlohmann::json`, но все узлы хранятся в ПАП (memory-mapped или file-backed образ), и поддерживает расширения:

- **`$ref`** — настоящие указатели на другие узлы (не просто строковые пути)
- **`$base64`** — бинарные данные (внутри — байтовый массив, при сериализации — base64)

**Ключевые архитектурные принципы:**

1. Все объекты в ПАП — только POD-структуры, доступ через смещения (`node_id`).
2. В ПАП ровно два типа строк:
   - **readonly (`pstringview`)** — интернированные, только накапливаются в словаре, используются как ключи `pmap` и пути `$ref`. Сравнение O(1). Нет SSO.
   - **readwrite (`pstring`)** — изменяемые строковые значения JSON (`node_tag::string`), могут модифицироваться на лету. Необходимы для совместимости с [jsonRVM](https://github.com/netkeep80/jsonRVM). Нет SSO.
3. Структура менеджера разделена на слои (storage → primitives → json model → db manager).
4. `pjson_db.h` — единственный заголовок для конечного пользователя.
5. Никаких `.cpp`, никаких внешних зависимостей.

---

## Фаза 0. Аудит и очистка текущего кода

### Задача 0.1. Инвентаризация существующих файлов

Составить список всех файлов, их текущей роли и статуса:

| Файл | Статус | Роль |
|------|--------|------|
| `pam_core.h` | Сохранить, доработать | Ядро ПАМ: аллокатор, слоты, имена, realloc |
| `pam.h` | Сохранить | Фасад: включает pvector, pmap, pstring |
| `persist.h` | Пересмотреть | fptr<T>, persist<T>, AddressManager<T> |
| `pvector.h` | Сохранить | Персистный динамический массив |
| `pmap.h` | Сохранить | Персистная карта (sorted array) |
| `pstring.h` | Сохранить, расширить | Персистная readwrite строка (JSON string-value узлы); убрать SSO |
| `pstringview.h` | Расширить | Интернированная read-only строка + таблица |
| `pjson.h` | Рефакторинг | Персистный JSON (переработать на node_id) |
| `pjson_interning.h` | Объединить с pstringview | Интернирование строк |
| `pjson_node_pool.h` | Перенести в pjson_pool.h | Пул узлов |
| `pjson_serializer.h` | Перенести в pjson_codec.h | Сериализация/десериализация |
| `pallocator.h` | Сохранить | STL-совместимый аллокатор |
| `main.cpp` | Обновить | Демонстрационная программа |

### Задача 0.2. Определить публичное API текущего кода

- Задокументировать все публичные структуры и функции, которые используются в тестах.
- Отметить, что из них ломается при переходе к новой архитектуре.
- Сформировать список миграционных задач для тестов.

### Задача 0.3. Анализ текущих узких мест

- Определить, где происходят ненужные аллокации строк.
- Где есть дублирование кода между `pvector` / `pmap` / внутренними массивами ПАМ.
- Где `pstring` использовалась там, где должна быть `pstringview`.

---

## Фаза 1. Общий примитив персистного массива (`pmem_array`) ✅ ЗАВЕРШЕНА

**Цель:** Ликвидировать дублирование кода для массивов в ПАП. Сейчас похожие паттерны `grow/copy/sync` повторяются в `pvector`, `pmap`, и во внутренних структурах ПАМ (`type_vec`, `slot_map`, `name_map`, `free_list`).

### Задача 1.1. Определить заголовок `pmem_array_hdr` ✅ ВЫПОЛНЕНО

```cpp
// pmem_array.h
struct pmem_array_hdr {
    uintptr_t size;     // текущее количество элементов
    uintptr_t capacity; // ёмкость (число элементов)
    uintptr_t data_off; // смещение массива данных в ПАП
};
```

### Задача 1.2. Реализовать шаблонные функции работы с массивом ✅ ВЫПОЛНЕНО

- `pmem_array_init<T>(hdr_off, init_cap)` — инициализация
- `pmem_array_reserve<T>(hdr_off, min_cap)` — предварительное резервирование
- `pmem_array_push_back<T>(hdr_off) -> T&` — добавить элемент
- `pmem_array_pop_back<T>(hdr_off)` — удалить последний
- `pmem_array_at<T>(hdr_off, idx) -> T&` — доступ по индексу
- `pmem_array_insert_sorted<T, KeyOf, Less>(hdr_off, key)` — вставка в отсортированный массив
- `pmem_array_find_sorted<T, KeyOf, Less>(hdr_off, key) -> T*` — бинарный поиск
- `pmem_array_erase_at<T>(hdr_off, idx)` — удаление по индексу

Реализовано в `pmem_array.h`. Тесты в `tests/test_pmem_array.cpp` (273 тестов, все проходят).

### Задача 1.3. Переписать `pvector` через `pmem_array` ✅ ВЫПОЛНЕНО

- `pvector<T>` становится тонкой обёрткой над `pmem_array_hdr`.
- Сохранить совместимый layout (size, capacity, data_off).
- Все тесты `test_pvector.cpp` должны пройти.

### Задача 1.4. Переписать `pmap` через `pmem_array` ✅ ВЫПОЛНЕНО

- `pmap<K,V>` — sorted array пар `{K, V}`, поиск бинарным поиском.
- Использует `pmem_array_insert_sorted` и `pmem_array_find_sorted`.
- Все тесты `test_pmap.cpp` должны пройти.

### Задача 1.5. Переписать внутренние массивы ПАМ через `pmem_array` ✅ ВЫПОЛНЕНО

- `type_vec`, `slot_map`, `name_map`, `free_list` в `pam_core.h` — через `pam_array_hdr` (идентичный `pmem_array_hdr`).
- Введён `pam_array_hdr` (forward-definition без #include, чтобы избежать циклической зависимости).
- Убраны 12 зеркальных переменных (`_xxx_size`, `_xxx_capacity`, `_xxx_entries_off` × 4 массива).
- Удалены дублирующиеся `_sync_*_mirrors` и `_flush_*_mirrors` функции (8 штук).
- Введён единый шаблон `_raw_grow_array<T>` вместо 4 повторяющихся `_ensure_*_capacity`.
- Все тесты `test_pam.cpp` и `test_pallocator.cpp` проходят. Итого: 273/273.

**Критерии приёмки фазы 1:**
- Новый файл `pmem_array.h`.
- Все существующие тесты проходят.
- Дублированного кода grow/copy/sync нет.

---

## Фаза 2. Словарь строк: readonly (`pstringview`) и readwrite (`pstring`) ✅ ЗАВЕРШЕНА

**Цель:** Закрепить двухтиповую архитектуру строк в ПАП. Readonly строки (`pstringview`) — только для ключей `pmap` и путей `$ref`, интернированы, сравнение O(1). Readwrite строки (`pstring`) — для JSON string-value узлов, изменяемые на лету (необходимо для [jsonRVM](https://github.com/netkeep80/jsonRVM)).

### Задача 2.1. Расширить `pstringview` для работы со словарём ✅ ВЫПОЛНЕНО

- `pstringview` хранит `length` и `chars_offset` (сделано ранее).
- `pstringview_table` — персистный объект (живёт в ПАП) — реализована как хэш-таблица с открытой адресацией (linear probing).
- Смещение `pstringview_table` хранится в `pam_header.string_table_offset` (фаза 10, PAM_VERSION 10).
- При `Save()` смещение таблицы записывается в заголовок; при `Load()` — восстанавливается.
- `pstringview_manager::get_table()` синхронизирует `_table_offset` с `pam.GetStringTableOffset()` при первом вызове после перезагрузки.

### Задача 2.2. Реализовать функцию `intern` на уровне ПАМ ✅ ВЫПОЛНЕНО

- `pam_intern_string(const char* s) -> pstringview_table::InternResult` — определена в `pstringview.h`.
- Ищет строку в `pstringview_table`: если найдена — возвращает `{chars_offset, length}` существующего массива.
- Если не найдена — аллоцирует char-массив в ПАП, добавляет в таблицу, возвращает смещение.
- `PersistentAddressSpace::GetStringTableOffset()` / `SetStringTableOffset()` — публичный API для доступа к смещению таблицы из ПАМ.
- Интернированные строки только накапливаются; освобождения нет.

### Задача 2.3. Убрать SSO из `pstringview` и `pstring` ✅ ВЫПОЛНЕНО

- `pstringview` НЕ содержит inline-буфера (no SSO): хранит только `length` и `chars_offset`.
- `pstring` НЕ содержит inline-буфера (no SSO): хранит только `length` и `chars` (fptr<char>).
- Любая строка, даже из 1 символа, хранится в ПАП через `chars_offset`.

### Задача 2.4. Чётко разграничить области применения двух типов строк ✅ ВЫПОЛНЕНО

| Тип | Применение | Изменяемость |
|-----|-----------|--------------|
| `pstringview` | Ключи `pmap<pstringview, node_id>`, путь в `$ref`, сегменты path-адресации | readonly (interned) |
| `pstring` | JSON string-value узлы (`node_tag::string`) | readwrite (изменяются на лету) |

- `pstring` используется в узлах `node_tag::string` (JSON строковые значения).
- `pstring` НЕ используется как ключ `pmap` — только `pstringview`.
- [jsonRVM](https://github.com/netkeep80/jsonRVM) работает непосредственно в БД и может модифицировать `pstring`-узлы "на месте", не затрагивая словарь `pstringview`.

### Задача 2.5. Поддержка полнотекстового поиска по словарю pstringview ✅ ВЫПОЛНЕНО

- `pam_search_strings(pattern) -> vector<pstringview_search_result>` — поиск по всем интернированным строкам в `pstringview_table` (определена в `pstringview.h`).
- `pam_all_strings() -> vector<pstringview_search_result>` — перебор всех строк словаря (обёртка над `pam_search_strings("")`).
- `pstringview_search_result` содержит `value`, `chars_offset`, `length`.

**Критерии приёмки фазы 2:** ✅
- `pstringview_table` хранится в ПАП и восстанавливается при загрузке образа (через `pam_header.string_table_offset`, PAM_VERSION 10). Тест `pstringview_table: survives PAM Save and Load`.
- Два одинаковых `intern("hello")` дают одинаковый `chars_offset`. Тест `pstringview_table: two intern(same) calls give identical chars_offset`.
- Сравнение строк-ключей `==` через `chars_offset` (O(1)). Тест `pstringview: operator== compares by chars_offset`.
- `pstring`-значения изменяемы: `node.string_val.assign("new_value")` работает без пересоздания узла. Тест `pstring: reassigning frees old allocation and stores new content`.
- Тесты `test_pstringview.cpp` (284 всего, включая 11 новых Phase 2 тестов) и `test_pstring.cpp` проходят.

---

## Фаза 3. Новая модель узлов JSON (`pjson_node`) ✅ ЗАВЕРШЕНА

**Цель:** Переработать `pjson` на `node_id`-адресацию. Добавить типы `ref` и `binary`.

### Задача 3.1. Определить расширенный `node_tag` ✅ ВЫПОЛНЕНО

```cpp
// pjson_node.h
enum class node_tag : uint32_t {
    null     = 0,
    boolean  = 1,
    integer  = 2,   // int64_t
    uinteger = 3,   // uint64_t
    real     = 4,   // double
    string   = 5,   // pstring (readwrite, изменяемое строковое значение JSON)
    binary   = 6,   // pvector<uint8_t> в ПАП
    array    = 7,   // pvector<node_id>
    object   = 8,   // pmap<pstringview, node_id> — ключи readonly (pstringview)
    ref      = 9,   // pstringview path (readonly) + node_id target
};
```

### Задача 3.2. Определить структуру `node` ✅ ВЫПОЛНЕНО

```cpp
struct node {
    node_tag tag;    // 4 байта — дискриминант

    union {
        uint32_t  boolean_val;
        int64_t   int_val;
        uint64_t  uint_val;
        double    real_val;

        // string: pstring (readwrite, length + chars_offset)
        // Изменяемые строковые значения — для поддержки jsonRVM,
        // который может модифицировать строковые узлы "на лету".
        struct { uintptr_t length; uintptr_t chars_offset; } string_val; // совместим с pstring

        // binary: pvector<uint8_t>-совместимая раскладка
        struct { uintptr_t size; uintptr_t cap; uintptr_t data_off; } binary_val;

        // array: pvector<node_id>-совместимая раскладка
        struct { uintptr_t size; uintptr_t cap; uintptr_t data_off; } array_val;

        // object: pmap<pstringview, node_id>-совместимая раскладка
        // Ключи — readonly pstringview (интернированные)
        struct { uintptr_t size; uintptr_t cap; uintptr_t data_off; } object_val;

        // ref: path (readonly pstringview) + target (node_id)
        struct {
            uintptr_t path_length;
            uintptr_t path_chars_offset; // указывает в словарь pstringview (readonly)
            uintptr_t target;            // node_id (0 = не разрешён)
        } ref_val;
    };
};
```

### Задача 3.3. Определить тип `node_id` ✅ ВЫПОЛНЕНО

```cpp
using node_id = uintptr_t; // смещение узла в ПАП; 0 = null/invalid
```

### Задача 3.4. Реализовать `node_view` — безопасный accessor ✅ ВЫПОЛНЕНО

```cpp
struct node_view {
    node_id id;
    // Запросы типа
    node_tag tag() const;
    bool is_null() const;
    bool is_ref() const;
    // ...

    // Получение значений
    bool        as_bool() const;
    int64_t     as_int() const;
    double      as_double() const;
    std::string_view as_string() const; // возвращает вид на pstring (readwrite значение)

    // Навигация
    node_view at(pstringview key) const;  // для object
    node_view at(size_t idx) const;       // для array
    size_t    size() const;               // для array/object

    // Разыменование ref
    node_view deref(bool recursive = true, size_t max_depth = 32) const;
};
```

### Задача 3.5. Написать тесты для новой модели узлов ✅ ВЫПОЛНЕНО

- Тест создания каждого типа узла.
- Тест `node_view::deref()` — рекурсивное и нерекурсивное разыменование.
- Тест `object_val` — вставка/поиск по ключу `pstringview` (readonly).
- Тест `array_val` — push_back, at, size.
- Тест двух типов строк:
  - `string_val` (pstring, readwrite): создание, `assign("new")`, изменение без пересоздания узла.
  - `ref_val.path` (pstringview, readonly): интернирование, сравнение через `chars_offset`.
  - Проверка, что ключи объектов — только `pstringview`, значения типа `string` — только `pstring`.

**Критерии приёмки фазы 3:** ✅
- Новый файл `pjson_node.h`. ✅
- Все типы узлов работают. ✅
- `node_view` корректно работает с ПАП. ✅
- Тесты `tests/test_pjson_node.cpp` (46 тестов) проходят. Итого: 327/327. ✅

---

## Фаза 4. Пул узлов (`pjson_pool`) ✅ ЗАВЕРШЕНА

**Цель:** Быстрая аллокация узлов (O(1) амортизированно) с поддержкой free-list внутри пула.

### Задача 4.1. Переработать `pjson_node_pool.h` ✅ ВЫПОЛНЕНО

- Пул хранит `pvector<node>` — компактный массив узлов в ПАП.
- Free-list в пуле: удалённые узлы помечаются специальным тегом (например, `node_tag::_free`) и добавляются в список свободных слотов.

### Задача 4.2. Реализовать API пула ✅ ВЫПОЛНЕНО

```cpp
class pjson_pool {
public:
    node_id alloc();                 // O(1) амортизированно
    void    free(node_id id);        // возвращает в free-list
    node&   get(node_id id);         // доступ по node_id
    const node& get(node_id id) const;
};
```

### Задача 4.3. Написать тесты для пула ✅ ВЫПОЛНЕНО

- Аллокация 10000 узлов.
- Освобождение каждого второго, повторная аллокация — без роста ПАП.
- Сохранение/загрузка образа с пулом.

**Критерии приёмки фазы 4:** ✅
- Новый файл `pjson_pool.h`. ✅
- Аллокация O(1), повторное использование работает. ✅
- Тесты `tests/test_pjson_pool.cpp` (22 теста) проходят. ✅

---

## Фаза 5. Сериализация/десериализация и поддержка `$ref`/`$base64` (`pjson_codec`) ✅ ЗАВЕРШЕНА

**Цель:** Парсер и сериализатор, работающие с новой моделью узлов. Корректная обработка `$ref` и `$base64`.

### Задача 5.1. Переработать `pjson_serializer.h` → `pjson_codec.h` ✅ ВЫПОЛНЕНО

- Создан новый файл `pjson_codec.h` с поддержкой `node_id`-адресации.
- Ключи объектов интернируются через `pam_intern_string` → `pstringview`.
- Строковые значения JSON создаются как `pstring`-узлы (readwrite) через `node_set_string`.
- Сегменты путей `$ref` интернируются как `pstringview` (readonly).
- Реализован полный Base64 кодек (encode/decode) в `pjson_codec_detail`.

### Задача 5.2. Реализовать распознавание `$ref` ✅ ВЫПОЛНЕНО

- При парсинге: объект строго вида `{ "$ref": "<path>" }` (ровно 1 ключ) → создаётся `ref`-узел.
- `ref_val.path_*` = интернированный `pstringview` пути.
- `ref_val.target` = 0 (будет разрешён при первом обращении или при `resolve_all()`).
- Объекты с `$ref` и дополнительными ключами обрабатываются как обычные объекты.

### Задача 5.3. Реализовать распознавание `$base64` ✅ ВЫПОЛНЕНО

- При парсинге: объект строго вида `{ "$base64": "<base64>" }` → `binary`-узел.
- Base64 декодируется в массив `uint8_t` через `node_binary_push_back`.
- Объекты с `$base64` и дополнительными ключами обрабатываются как обычные объекты.

### Задача 5.4. Реализовать сериализатор ✅ ВЫПОЛНЕНО

- `binary`-узел → `{ "$base64": "..." }`.
- `ref`-узел → `{ "$ref": "<path>" }` (использует сохранённый `path`).
- Остальные типы — стандартный JSON.
- Функции: `node_to_string(node_id)`, `node_serialize_to(node_id, std::string&)`.

### Задача 5.5. Написать тесты сериализации ✅ ВЫПОЛНЕНО

- Round-trip: `parse → dump` для всех типов узлов.
- Специфично `$ref`: `{ "$ref": "/a/b" }` сохраняется и восстанавливается.
- Специфично `$base64`: `{ "$base64": "AAEC" }` → bytes `[0, 1, 2]` → обратно в base64.
- 45 тестов в `test_pjson_codec.cpp`, все проходят.

**Критерии приёмки фазы 5:** ✅
- Новый файл `pjson_codec.h` (1018 строк). ✅
- Новый файл `tests/test_pjson_codec.cpp` (45 тестов). ✅
- `$ref` и `$base64` корректно парсятся и сериализуются. ✅
- Все 394 теста проекта проходят. ✅

---

## Фаза 6. Менеджер БД и path-адресация (`pjson_db`) ✅ ЗАВЕРШЕНА

**Цель:** Реализовать высокоуровневый API доступа к данным через строковые пути.

### Задача 6.1. Определить класс `pjson_db` ✅ ВЫПОЛНЕНО

```cpp
// pjson_db.h — единственный заголовок для конечного пользователя
class pjson_db {
public:
    // Открыть/создать базу данных
    static pjson_db open(const char* pam_file,
                         const char* root_name = "db.root");

    node_id   root_id() const;
    node_view root() const;

    // Path-адресация
    node_view get(const char* path, bool deref_ref = true) const;
    bool      put(const char* path, /* значение */);
    bool      erase(const char* path);
    bool      exists(const char* path) const;

    // Явное разыменование ref
    node_view resolve_ref(node_id id, size_t max_depth = 32) const;

    // Разрешить все ref после загрузки
    void resolve_all_refs();

    // Метрики
    node_view metrics(const char* subpath = nullptr) const;

    // Сериализация
    std::string dump(node_id id) const;
    std::string dump() const; // дамп от корня

    // Сохранение
    void save();
};
```

### Задача 6.2. Реализовать парсер путей ✅ ВЫПОЛНЕНО

- Синтаксис пути: абсолютный `/a/b/0/c`.
- Сегменты: строки для объектов, десятичные числа для массивов.
- Зарезервированные пространства:
  - `/$metrics/...` — только для чтения
  - `/$sys/...` — зарезервировано

### Задача 6.3. Реализовать `get`, `put`, `erase` ✅ ВЫПОЛНЕНО

- `get`: обходит дерево по сегментам пути. По умолчанию разыменовывает `ref`.
- `put`: создаёт промежуточные объекты/массивы (по первому символу следующего сегмента определяет тип).
- `erase`: рекурсивно удаляет поддерево (кроме `ref`-целей).

### Задача 6.4. Реализовать детектирование цикличных ссылок ✅ ВЫПОЛНЕНО

- При `resolve_ref()`: ограничение глубиной `max_depth` (защита от зависания).
- Дополнительная проверка `target == cur.id` в `node_view::deref()`.
- При обнаружении цикла или превышении `max_depth` — возвращает `node_view(0)`.

### Задача 6.5. Реализовать `resolve_all_refs()` ✅ ВЫПОЛНЕНО

- После загрузки образа обходит все `ref`-узлы и устанавливает `target` по `path`.
- Реализовано через рекурсивный обход `_resolve_refs_in_subtree()`.

### Задача 6.6. Написать тесты менеджера БД ✅ ВЫПОЛНЕНО

- Персистентность: создать БД, записать, сохранить, загрузить, проверить.
- Path-адресация: get/put/erase по `/a/b/0/c`.
- Ошибки: попытка `put` в `/$metrics` → запрет (возвращает false).
- `$ref` разыменование: `{ "$ref": "/a/b" }` → возвращает узел `/a/b`.
- Цикл: `a → ref b`, `b → ref a` → `deref()` возвращает `node_view(0)`.
- 45 тестов в `test_pjson_db.cpp`, все проходят.

**Критерии приёмки фазы 6:** ✅
- Новый файл `pjson_db.h`. ✅
- Единый заголовок для конечного пользователя (Тр.18). ✅
- Все тесты проходят (439/439). ✅
- Метрики доступны через `/$metrics/...`. ✅

---

## Фаза 7. Метрики ПАП и метрики БД ✅ ЗАВЕРШЕНА

**Цель:** Хранить и обновлять метрики в ПАП, доступ через `/$metrics`.

### Задача 7.1. Определить структуру `db_metrics` ✅ ВЫПОЛНЕНО

```cpp
struct db_metrics {
    uint64_t node_count_total;   // всего узлов в дереве (или пуле, если используется)
    uint64_t string_count_total; // всего интернированных строк в словаре
    uint64_t binary_bytes_total; // всего байт в binary-узлах
    uint64_t ref_count;          // всего ref-узлов
    uint64_t array_count;        // всего массивов (array-узлов)
    uint64_t object_count;       // всего объектов (object-узлов)
    uint64_t last_save_time;     // Unix-время последнего сохранения (0 = не сохранялось)
    // Метрики PAM (проксируются из PersistentAddressSpace)
    uint64_t pam_bump_offset;    // текущая позиция bump-аллокатора (байт)
    uint64_t pam_free_list_size; // число свободных блоков в free-list ПАМ
    uint64_t pam_total_size;     // полный размер области данных ПАМ (байт)
    uint64_t pam_slot_count;     // число аллоцированных слотов в ПАМ
    uint64_t pam_named_count;    // число именованных объектов в ПАМ
};
```

Структура `db_metrics` хранится персистно в ПАМ под именем `"pjson_db.metrics"`.

### Задача 7.2. Интегрировать обновление метрик в мутации ✅ ВЫПОЛНЕНО

- Каждая операция `put` / `parse` / `erase` / `parse_into` / `put_ref` / `put_null` вызывает `_update_metrics_after_mutation()`.
- `_update_metrics_after_mutation()` выполняет полный пересчёт:
  - Счётчики типов узлов (ref_count, array_count, object_count, binary_bytes_total) — обходом дерева от корня.
  - Метрики ПАМ — через `GetBump()`, `GetFreeListSize()`, `GetDataSize()`, `GetSlotCount()`, `GetNamedCount()`.
  - Метрика строк — через `pam_all_strings().size()`.
- `save()` обновляет `last_save_time` (Unix timestamp) перед записью на диск.
- Метод `update_metrics()` доступен пользователю для явного пересчёта.

### Задача 7.3. Реализовать read-only доступ через пути ✅ ВЫПОЛНЕНО

- `get("/$metrics/node_count_total")` → `node_view` типа `uinteger`.
- `get("/$metrics/pam_bump_offset")` → позиция bump-аллокатора.
- `get("/$metrics/pam_free_list_size")` → число свободных блоков.
- `get("/$metrics/pam_total_size")` → размер области данных ПАМ.
- `get("/$metrics/pam_slot_count")` → число слотов в ПАМ.
- `get("/$metrics/pam_named_count")` → число именованных объектов.
- `get("/$metrics/string_count_total")` (псевдоним `string_count`) → число строк в словаре.
- `get("/$metrics/ref_count")`, `array_count`, `object_count`, `binary_bytes_total` → счётчики по типам.
- `get("/$metrics/last_save_time")` → время последнего сохранения.
- `put("/$metrics/...")` → ошибка `readonly` (возвращает `false`).
- Неизвестная метрика → `node_view` типа `null`.

**Критерии приёмки фазы 7:** ✅
- Новая структура `db_metrics` в `pjson_db.h`. ✅
- Метрики обновляются при каждой мутации. ✅
- Доступ через `/$metrics` работает для всех полей. ✅
- 15 новых тестов `test_pjson_db.cpp` (тег `[phase7]`). ✅
- Все 454 теста проходят. ✅

---

## Фаза 8. Иерархическое адресное пространство (интерфейс `pmap<pstringview, pjson>`) ✅ ЗАВЕРШЕНА

**Цель:** Менеджер ПАП сам реализует интерфейс `pmap<pstringview, pjson>` для внешней интеграции.

### Задача 8.1. Реализовать интерфейс `pjson_db` как `pmap<pstringview, node_id>` ✅ ВЫПОЛНЕНО

- `operator[](const char* path) -> node_view` — доступ по пути; создаёт null-узел если не существует.
- `find(const char* path) -> node_view` — поиск без создания и без разыменования ref.
- `insert(const char* path, const char* json_value) -> node_view` — вставка/перезапись JSON-значения по пути.
- `erase(const char* path) -> bool` — удаление (реализовано в Фазе 6).

### Задача 8.2. Реализовать интерфейс поиска по строкам ✅ ВЫПОЛНЕНО

- `pjson_db::search_node_strings(const char* pattern) -> std::vector<node_id>` — поиск по всем `pstring`-значениям (readwrite строки JSON-узлов) обходом дерева от корня.
- `pjson_db::search_strings(const char* pattern)` (из Фазы 6) — поиск по словарю интернированных ключей (`pstringview`).
- Два метода дополняют друг друга: `search_strings` охватывает словарь ключей, `search_node_strings` — строковые значения узлов.

### Задача 8.3. Написать интеграционные тесты ✅ ВЫПОЛНЕНО

- 21 новый тест в `test_pjson_db.cpp` (тег `[phase8]`).
- Тесты `operator[]`: доступ к существующему пути, создание нового узла, доступ к метрикам.
- Тесты `find()`: поиск существующего, несуществующего, без создания, без разыменования ref.
- Тесты `insert()`: вставка строки, числа, объекта, перезапись, запрет в `/$metrics`.
- Тесты `search_node_strings()`: поиск по значениям, пустой pattern, вложенные объекты/массивы.
- Интеграционные тесты: комбинирование `operator[]` + `find` + `insert` + `search_node_strings`.

**Критерии приёмки фазы 8:** ✅
- Методы `operator[]`, `find`, `insert` добавлены в `pjson_db.h`. ✅
- Метод `search_node_strings` ищет по pstring-значениям (readwrite) в дереве узлов. ✅
- `search_strings` (pstringview) и `search_node_strings` (pstring) дополняют друг друга. ✅
- 21 новый тест `[phase8]` проходят. ✅
- Все 475 тестов проекта проходят. ✅

---

## Фаза 9. Совместимость, документация и финальная очистка ✅ ЗАВЕРШЕНА

### Задача 9.1. Обновить `main.cpp` ✅ ВЫПОЛНЕНО

- Демонстрация всех возможностей: открытие БД, put/get, `$ref`, `$base64`, метрики.
- `main.cpp` переработан на высокоуровневый API `pjson_db::open()`.
- Удалены демо на старом API (`fptr<T>`, `pjson`, `pallocator`).
- Добавлены 8 демо-функций: открытие/put/get, вложенные объекты, `$ref`, `$base64`, метрики, `operator[]`/`find`/`insert`, поиск по строкам, удаление и `resolve_all_refs()`.

### Задача 9.2. Обновить тесты ✅ ВЫПОЛНЕНО

- Добавлены тесты производительности в `tests/test_pjson_db_perf.cpp` (7 тестов, тег `[phase9]`).
- Покрытие: put() 10k int и string узлов, get() 100k запросов, parse_into() 1k объектов, erase() 10k узлов, полный жизненный цикл, ReserveSlots vs без резервирования.
- Все тесты информационные (не ограничены по времени) — распечатывают измеренное время.
- Итого: 477 тестов проходят (после миграции Задачи 9.5).

### Задача 9.3. Проверить CI ✅ ВЫПОЛНЕНО

- Локально: cmake + build + ctest — 482/482 тестов проходят.
- clang-format: все `.cpp` и `.h` файлы соответствуют форматированию.
- Размер файлов: все файлы в пределах 1500 строк.
- CI на GitHub Actions: будет запущен при push в ветку issue-76-f80e85412858.

### Задача 9.4. Обновить `readme.md` ✅ ВЫПОЛНЕНО

- Обновлена архитектурная схема: Фазы 6–8 → Фазы 6–9 ✅.
- Добавлен раздел «Производительность (Фаза 9)» с ориентировочными показателями.

### Задача 9.5. Удалить устаревший код ✅ ВЫПОЛНЕНО

- Мигрированы 5 тестовых файлов с устаревшего API `pjson.h` на новый API (`pjson_node.h`, `pjson_codec.h`, `pjson_pool.h`):
  - `test_pjson.cpp` → использует `node_id`, `node_set_*`, `node_view` из `pjson_node.h`
  - `test_pjson_serial.cpp` → использует `node_from_string`, `node_to_string` из `pjson_codec.h`
  - `test_pjson_opt.cpp` → F6 через `pjson_codec.h`, F3 через `pam_intern_string` / `pstringview_table`, F2 через `pjson_pool`
  - `test_pjson_bench.cpp` → бенчмарки переписаны на новый API
  - `test_pjson_large.cpp` → загрузка большого JSON через `node_from_string` / `node_to_string`
- Удалены 4 файла устаревшего API: `pjson.h`, `pjson_serializer.h`, `pjson_interning.h`, `pjson_node_pool.h`.
- Новый код (`pjson_db.h`) никогда не зависел от устаревших файлов.
- Все 477 тестов проходят после миграции.

**Критерии приёмки фазы 9:** ✅
- `main.cpp` обновлён на `pjson_db::open()` API. ✅
- Тесты производительности добавлены (7 тестов `[phase9]`). ✅
- CI проверен. ✅
- `readme.md` обновлён. ✅
- Устаревшие файлы удалены; тесты мигрированы на новый API. ✅
- Все 477 тестов проходят. ✅

---

## Открытые вопросы

### В1. Бэкенд хранилища: fread/fwrite vs mmap

**Текущее состояние:** ПАМ использует `fread/fwrite` (загружает весь образ в malloc-память).

**Вопрос:** Нужен ли настоящий `mmap`?

- `mmap` даёт ленивую загрузку и экономит RAM для больших БД.
- `fread/fwrite` проще и надёжнее (нет проблем с выравниванием на разных ОС).
- Предложение: оставить `fread/fwrite` как основной бэкенд. `mmap` — опциональный бэкенд через абстракцию `storage_backend`.

### В2. Правила владения узлами при `$ref`

**Вопрос:** Как определяется владение при наличии `$ref`?

- Вариант A (предпочтительный): `ref` не владеет целевым узлом. Shared-узлы только через `$ref`. Удаление `ref`-узла не удаляет цель.
- Вариант B: reference counting для каждого узла.

**Рекомендация:** Принять вариант A как более простой. Добавить в документацию явное предупреждение о dangling refs.

### В3. Поведение `persist<T>` в новой архитектуре

**Вопрос:** Нужен ли `persist<T>` вообще?

- Пункт 6 требований: "скорее всего класс `persist<T>` больше не понадобится".
- В текущем коде `persist<T>` используется в тестах и `main.cpp`.
- Предложение: оставить `persist<T>` как compatibility shim, но не использовать в новом коде `pjson_db`.

### В4. Двойные типы строк: readonly vs readwrite ✅ РЕШЕНО

**Решение:** В ПАП существуют ровно два типа строк с принципиально разными свойствами:

- **readonly (`pstringview`)** — интернированные строки, только накапливаются в словаре, используются исключительно как ключи `pmap` (объектные ключи, сегменты путей `$ref`). Сравнение O(1) через `chars_offset`. Никогда не изменяются и не освобождаются.

- **readwrite (`pstring`)** — изменяемые строки, являются JSON string-value узлами. Могут модифицироваться на лету — это критически важно для совместимости с [jsonRVM](https://github.com/netkeep80/jsonRVM), который работает непосредственно внутри базы данных и может менять строковые значения узлов "на месте".

**Последствия для архитектуры:**

- Тип `string` в `node_tag` хранит `pstring` (readwrite, offset + length), не `pstringview`.
- `pstringview` используется только как ключ в `pmap<pstringview, node_id>` и в `ref_val.path`.
- Сквозной поиск по строкам охватывает **оба** типа: словарь `pstringview` и все `pstring`-узлы.
- NO SSO в `pstringview` — обязательно. NO SSO в `pstring` — требуется для сквозного поиска по значениям.
- `pjson_db::search_strings(pattern)` должен искать по словарю И по всем `pstring`-значениям в пуле узлов.

### В5. Конвертация индексов массива в путях

**Вопрос:** Как определяется, что сегмент пути — это индекс массива, а не ключ объекта?

- Вариант A: если текущий узел — массив, сегмент интерпретируется как число.
- Вариант B: специальный синтаксис, например `[0]` для индексов.
- Рекомендация: Вариант A (проще, как в JSON Pointer RFC 6901).

### В6. Обработка ошибок

**Вопрос:** Как сообщать об ошибках?

- Вариант A: `node_view` с `is_error()` и кодом ошибки.
- Вариант B: исключения C++.
- Вариант C: `std::expected<node_view, error>` (C++23).
- Рекомендация: Вариант A (совместимо с C++17, без исключений).

### В7. Производительность объектов (object storage)

**Вопрос:** Sorted array vs hash map для объектов?

- Sorted array (`pmap`): O(log n) поиск, простота, нет хеш-коллизий.
- Hash map: O(1) поиск, но сложнее в ПАП (нет перехэшивания указателей).
- Рекомендация: Sorted array как сейчас. При необходимости — hash map в будущей фазе.

### В8. Граница между `pstringview` как ключ и `pstring` как значение ✅ РЕШЕНО

**Решение:** Используются два разных типа с принципиально разными свойствами.

- **`pstringview`** — только для ключей `pmap<pstringview, node_id>` и путей `$ref`. Интернированная, readonly. Сравнение по `chars_offset` — O(1). Только накапливается в словаре.
- **`pstring`** — только для JSON string-value узлов (`node_tag::string`). Readwrite, изменяемая. Позволяет [jsonRVM](https://github.com/netkeep80/jsonRVM) модифицировать строковые значения непосредственно в БД.

Это принципиальное архитектурное решение, а не просто семантическое: `pstring` допускает `assign()`, `pstringview` — нет.

---

---

## Фаза 10. Итераторы для обхода дерева JSON ✅ ЗАВЕРШЕНА

**Цель:** Добавить поддержку range-based for и C++ итераторов для `node_view`, позволяющую удобно обходить массивы и объекты без явной индексации.

### Задача 10.1. Реализовать итератор элементов массива (`node_view_iterator`) ✅ ВЫПОЛНЕНО

- `node_view_iterator` — forward-итератор по элементам array-узла.
- Разыменование (`operator*()`) возвращает `node_view` для текущего элемента.
- Поддержка префиксного и постфиксного `operator++`.
- Операторы сравнения `==` / `!=`.
- Интеграция в `node_view`: методы `begin()` и `end()` для range-based for.
- Для не-массивов `begin() == end()` (пустой диапазон).

```cpp
// Итерация по элементам массива
node_view arr = db.get("/scores");
for (node_view elem : arr)
    std::cout << elem.as_int() << "\n";
```

### Задача 10.2. Реализовать итератор пар ключ-значение объекта (`object_iterator`) ✅ ВЫПОЛНЕНО

- `object_item` — структура `{key: string_view, value: node_view}` для итерации.
- `object_iterator` — forward-итератор по полям object-узла.
- `object_items_range` — вспомогательный диапазон, возвращаемый `node_view::items()`.
- Поддержка C++17 structured bindings (`auto [key, val] : obj.items()`).
- Для не-объектов `items()` возвращает пустой диапазон.

```cpp
// Итерация по полям объекта с structured bindings
node_view user = db.get("/user");
for (auto [key, val] : user.items())
    std::cout << key << ": " << val.as_string() << "\n";
```

### Задача 10.3. Написать тесты для итераторов ✅ ВЫПОЛНЕНО

- 10 новых тестов в `tests/test_pjson_node.cpp` (тег `[phase10][iterator]`).
- `node_view_iterator`: пустой массив, одноэлементный массив, три элемента в порядке, не-массив возвращает пустой диапазон, постфиксный инкремент.
- `object_iterator`: пустой объект, пары ключ-значение в правильном порядке, не-объект возвращает пустой диапазон, structured bindings, постфиксный инкремент.

**Критерии приёмки фазы 10:** ✅
- Новые типы `node_view_iterator`, `object_item`, `object_iterator`, `object_items_range`, `array_range` в `pjson_node.h`. ✅
- `node_view` поддерживает `begin()` / `end()` для массивов и `items()` для объектов. ✅
- 10 новых тестов `[phase10][iterator]` проходят. ✅
- Все 487 тестов проекта проходят. ✅

---

## Фаза 11. Коды ошибок в `node_view` (Error Codes) ✅ ЗАВЕРШЕНА

**Цель:** Разграничить «узел не найден / ошибка» от «JSON null-значение». Сейчас `node_view(0)` используется и как «узел не существует», и как дефолтное возвращаемое значение при ошибках. Это затрудняет диагностику: нельзя узнать, почему `get()` вернул пустой результат. Ввести `node_error` enum и поддержку `is_error()` / `error()` в `node_view`.

### Задача 11.1. Определить перечисление `node_error` ✅ ВЫПОЛНЕНО

```cpp
// pjson_node.h
enum class node_error : uintptr_t
{
    none               = 0, ///< Нет ошибки
    not_found          = 1, ///< Узел не найден по пути
    wrong_type         = 2, ///< Неверный тип узла при навигации
    index_out_of_range = 3, ///< Индекс массива вне диапазона
    readonly           = 4, ///< Попытка записи в readonly-пространство (/$metrics)
    ref_cycle          = 5, ///< Обнаружен цикл при разыменовании $ref
    parse_error        = 6, ///< Ошибка парсинга JSON
};
```

### Задача 11.2. Добавить поддержку ошибок в `node_view` ✅ ВЫПОЛНЕНО

- `node_view::is_error() -> bool` — проверяет, является ли view ошибкой.
- `node_view::error() -> node_error` — возвращает код ошибки.
- Ошибочный `node_view` кодируется через специальные sentinel-значения `id`:
  - `id == NODE_ERROR_BASE + static_cast<uintptr_t>(node_error::...)` — ошибочный view.
  - `NODE_ERROR_BASE` = `~uintptr_t(255)` — область, недостижимая реальным ПАП.
- Фабричная функция `node_view_error(node_error) -> node_view` определена в `pjson_node.h`.
- `is_null()` и `valid()` для ошибочного `node_view` возвращают `false`.

### Задача 11.3. Вернуть типизированные ошибки из `pjson_db::get()` ✅ ВЫПОЛНЕНО

- При отсутствии узла → `node_view_error(node_error::not_found)`.
- При неверном типе при навигации (попытка индекс в не-массив, ключ в не-объект) → `node_view_error(node_error::wrong_type)`.
- При выходе индекса за границы массива → `node_view_error(node_error::index_out_of_range)`.
- При обнаружении цикла ref → `node_view_error(node_error::ref_cycle)`.

### Задача 11.4. Написать тесты ✅ ВЫПОЛНЕНО

- 10 новых тестов в `tests/test_pjson_db_errors.cpp` (тег `[phase11][error]`).
- Тест `node_view{}` (id==0) — не ошибка, `is_null()` == true, `is_error()` == false.
- Тест `node_view_error(not_found).is_error()` == true.
- Тест `node_view_error(X).error()` == X для всех кодов ошибок.
- Тест все error views `is_error()` == true.
- Тест valid node_view не является ошибкой.
- Тест `db.get("/nonexistent").error()` == `node_error::not_found`.
- Тест навигации через скалярный узел → `wrong_type`.
- Тест индекс вне диапазона → `index_out_of_range`.
- Тест цикл ref → `ref_cycle`.
- Тест существующий путь → valid, no error.

**Критерии приёмки фазы 11:** ✅
- `node_error` enum определён в `pjson_node.h`. ✅
- `node_view::is_error()` / `node_view::error()` работают. ✅
- `node_view_error()` фабричная функция в `pjson_node.h`. ✅
- `pjson_db::get()` возвращает типизированные ошибки при отсутствии пути или неверной навигации. ✅
- Новый файл `tests/test_pjson_db_errors.cpp` (10 тестов). ✅
- 10 новых тестов `[phase11][error]` проходят. ✅
- Все 497 тестов проекта проходят. ✅

---

## Фаза 12. Сообщения об ошибках (`node_error_message`) ✅ ЗАВЕРШЕНА

**Цель:** Добавить человекочитаемые сообщения для кодов ошибок `node_error`, позволяющие отображать пользователю понятное описание ошибки вместо числового кода.

### Задача 12.1. Реализовать функцию `node_error_message` ✅ ВЫПОЛНЕНО

```cpp
// pjson_node.h
const char* node_error_message( node_error err );
```

- Возвращает статическую C-строку (не требует освобождения памяти).
- Для `node_error::none` → `"no error"`.
- Для `node_error::not_found` → `"node not found"`.
- Для `node_error::wrong_type` → `"wrong node type for navigation"`.
- Для `node_error::index_out_of_range` → `"array index out of range"`.
- Для `node_error::readonly` → `"cannot modify read-only path"`.
- Для `node_error::ref_cycle` → `"cyclic $ref detected or max depth exceeded"`.
- Для `node_error::parse_error` → `"JSON parse error"`.
- Для неизвестных кодов → `"unknown error"`.

### Задача 12.2. Добавить метод `node_view::error_message()` ✅ ВЫПОЛНЕНО

```cpp
// pjson_node.h
struct node_view {
    // ...
    const char* error_message() const;  // Возвращает node_error_message(error())
};
```

- Для обычных и null `node_view` возвращает `"no error"`.
- Для ошибочных `node_view` возвращает описание соответствующей ошибки.

### Задача 12.3. Написать тесты ✅ ВЫПОЛНЕНО

- 6 новых тестов в `tests/test_pjson_db_errors.cpp` (тег `[phase12][error]`).
- Тест `node_error_message`: возвращает правильное сообщение для каждого кода ошибки.
- Тест `node_error_message`: возвращает `"unknown error"` для неизвестного кода.
- Тест `node_view::error_message()`: возвращает правильное сообщение для ошибочных view.
- Тест `node_view::error_message()`: возвращает `"no error"` для null view.
- Тест `node_view::error_message()`: возвращает `"no error"` для valid view.
- Тест `node_view::error_message()`: возвращает правильное сообщение для ошибки из `db.get()`.

**Критерии приёмки фазы 12:** ✅
- Функция `node_error_message()` определена в `pjson_node.h`. ✅
- Метод `node_view::error_message()` работает. ✅
- 6 новых тестов `[phase12][error]` проходят. ✅
- Все 503 теста проекта проходят. ✅

---

## Фаза 13. Глубокое копирование узлов (`node_clone`) ✅ ЗАВЕРШЕНА

**Цель:** Добавить возможность глубокого копирования (клонирования) поддеревьев JSON в персистном адресном пространстве. Функция `node_clone` создаёт полную копию узла со всеми вложенными структурами.

### Задача 13.1. Реализовать функцию `node_clone` ✅ ВЫПОЛНЕНО

```cpp
// pjson_node.h
node_id node_clone( node_id src_id );
```

- Создаёт глубокую копию узла по `src_id`.
- Рекурсивно копирует все вложенные узлы (array, object).
- Для `string`-узлов создаётся независимая копия строки (pstring).
- Для `binary`-узлов копируются все байты.
- Для `ref`-узлов копируется путь (интернированный через pstringview), но `target = 0` (ссылка не разрешена).
- Возвращает `node_id` нового узла; 0 при ошибке.

### Задача 13.2. Добавить метод `pjson_db::clone` ✅ ВЫПОЛНЕНО

```cpp
// pjson_db.h
bool clone( const char* src_path, const char* dest_path );
node_id clone( node_id src_id );
```

- `clone(src_path, dest_path)` — клонирует поддерево из `src_path` в `dest_path`.
- `clone(node_id)` — обёртка над `node_clone()`.
- Возвращает `false` при ошибке (несуществующий путь, метрики-пространство).
- После клонирования вызывает `_update_metrics_after_mutation()`.

### Задача 13.3. Написать тесты ✅ ВЫПОЛНЕНО

- 18 новых тестов в `tests/test_pjson_clone.cpp` (тег `[phase13][clone]`).
- Тесты `node_clone`: null, boolean, integer, uinteger, real, string, binary, array, object, ref, вложенные структуры.
- Тесты `pjson_db::clone`: клонирование по пути, изменение копии не влияет на оригинал, вложенные структуры, ошибочные пути.

**Критерии приёмки фазы 13:** ✅
- Функция `node_clone()` определена в `pjson_node.h`. ✅
- Методы `pjson_db::clone()` добавлены в `pjson_db.h`. ✅
- 18 новых тестов `[phase13][clone]` проходят. ✅
- Все тесты проекта проходят. ✅

---

## Порядок выполнения фаз

```
Фаза 0 (аудит) → параллельно:
  Фаза 1 (pmem_array)     → Фаза 2 (pstringview)
  Фаза 3 (node model)     → Фаза 4 (pool)
                          ↓
                    Фаза 5 (codec)
                          ↓
                    Фаза 6 (db manager)
                          ↓
               Фаза 7 (метрики) + Фаза 8 (интерфейс)
                          ↓
                    Фаза 9 (финализация)
                          ↓
                    Фаза 10 (итераторы) ✅
                          ↓
                    Фаза 11 (коды ошибок) ✅
                          ↓
                    Фаза 12 (сообщения об ошибках) ✅
                          ↓
                    Фаза 13 (глубокое копирование) ✅
                          ↓
                    Фаза 14 (переход на PersistMemoryManager)
```

Фазы 1 и 3 можно выполнять параллельно (нет зависимостей между pmem_array и node model).
Фазы 7 и 8 можно выполнять параллельно (оба зависят от Фазы 6).

---

## Фаза 14. Переход на новый менеджер ПАП (`PersistMemoryManager`)

**Цель:** Заменить текущий встроенный менеджер ПАП (`pam_core.h` / `persist.h` / `pam.h`) на внешнюю библиотеку [PersistMemoryManager](https://github.com/netkeep80/PersistMemoryManager) (далее — PMM). PMM предоставляет типобезопасные персистные указатели (`pptr<T>`), AVL-деревья свободных блоков, настраиваемые адресные пространства и готовые персистные структуры данных (`pmap`, `pstringview`).

### Предпосылки перехода

| Аспект | Текущий ПАМ (`pam_core.h`) | PMM (`PersistMemoryManager`) |
|--------|---------------------------|------------------------------|
| **Модель указателей** | Сырые `uintptr_t` смещения | Типобезопасные `pptr<T, Manager>` |
| **Аллокатор** | Bump-аллокатор + линейный free-list | AVL-дерево свободных блоков (best-fit) |
| **Многопоточность** | Не поддерживается | Настраиваемая политика блокировок (`NoLock` / `SharedMutexLock`) |
| **Адресные пространства** | Только 64-bit | 16-bit / 32-bit / 64-bit через `AddressTraits` |
| **Бэкенды хранения** | `fread`/`fwrite` в `malloc`-буфер | `HeapStorage` / `StaticStorage` / `MMapStorage` |
| **Метаданные объектов** | `type_vec` + `slot_map` + `name_map` внутри ПАП | Заголовок блока (32 байта) + приложение управляет реестрами |
| **Гранулярность** | Побайтовые смещения | 16/64-байтовые гранулы |
| **API-модель** | Синглтон `PersistentAddressSpace::Get()` | Статические методы `Mgr::allocate_typed<T>()` |
| **Персистные контейнеры** | `pvector<T>`, `pmap<K,V>` (sorted array) | `pmap<K,V>` (AVL-дерево), `pstringview` (оптимизированная) |
| **Расширение памяти** | Удвоение буфера | Политика роста (25% / 50% / 100%) |

### Ключевые архитектурные решения

**Р14.1. Выбор конфигурации PMM.**
Для pjson_db рекомендуется пресет `SingleThreadedHeap` (32-bit `DefaultAddressTraits`, `HeapStorage`, `NoLock`, 25% рост) — соответствует текущему однопоточному сценарию использования. При необходимости многопоточности — `MultiThreadedHeap`.

```cpp
// Рекомендуемый тип менеджера:
using PamManager = pmm::presets::SingleThreadedHeap;
// Альтернатива для многопоточности:
// using PamManager = pmm::presets::MultiThreadedHeap;
```

**Р14.2. Подключение PMM как зависимости.**
PMM предоставляет single-header вариант (`single_include/pmm/pmm.h`). Варианты подключения:
- **(a) Vendoring**: скопировать `single_include/pmm/pmm.h` в проект (самый простой вариант, сохраняет header-only принцип).
- **(b) git submodule**: `git submodule add https://github.com/netkeep80/PersistMemoryManager deps/pmm` и добавить `deps/pmm/include` в `include_directories()`.
- **(c) CMake FetchContent**: автоматическое скачивание при сборке.

Рекомендация: вариант (a) для минимального изменения инфраструктуры сборки.

**Р14.3. Реестр именованных объектов.**
PMM не имеет встроенной карты имён (`name_map`). Необходимо реализовать реестр поверх `pmm::pmap<pptr<pstringview>, pptr<void>>` на уровне приложения. Это заменит текущие `PersistentAddressSpace::Find()` / `Create<T>(name)`.

**Р14.4. Формат файла — несовместимость.**
Формат файла `.pam` полностью несовместим между текущим ПАМ и PMM (разные заголовки, разная гранулярность, разные метаданные блоков). Потребуется:
- Утилита миграции: загрузка старого `.pam` → экспорт JSON → создание нового `.pam` через PMM → импорт JSON.
- Либо пересоздание БД из JSON-дампа.

---

### Задача 14.0. Подготовка: подключение PMM

- [x] Скопировать `single_include/pmm/pmm.h` (или пресет `pmm_single_threaded_heap.h`) в проект.
- [x] Добавить в `CMakeLists.txt` путь к заголовку PMM.
- [x] Убедиться, что проект компилируется с PMM (без использования — только `#include`).
- [x] Определить тип менеджера: `using PamManager = pmm::PersistMemoryManager<pmm::CacheManagerConfig>;`

**Примечание:** PMM требует C++20. Проект обновлён с C++17 до C++20. Тип `CacheManagerConfig` соответствует однопоточному сценарию (NoLock, HeapStorage, 16B гранула, рост 25%). Добавлены базовые тесты интеграции PMM в `tests/test_pmm_basic.cpp`.

---

### Задача 14.1. Адаптер `pptr<T>` ↔ `uintptr_t` (слой совместимости) ✅ ВЫПОЛНЕНО

**Цель:** Обеспечить плавный переход, временно поддерживая оба стиля указателей.

- [x] Создать файл `pam_adapter.h` с маппингом:
  ```cpp
  // Преобразование pptr → uintptr_t (для совместимости с node_id)
  template <typename T>
  uintptr_t pptr_to_offset(PamManager::pptr<T> p);

  // Преобразование uintptr_t → pptr (для новых вызовов)
  template <typename T>
  PamManager::pptr<T> offset_to_pptr(uintptr_t off);
  ```
- [x] Определить `node_id` через `pptr<node>` или оставить `uintptr_t` с конверсиями.
- [x] Написать тесты конверсии `pptr ↔ uintptr_t`.

**Примечание:** Создан файл `pam_adapter.h` с функциями конверсии. Формула: `byte_offset = granule_index * granule_size` (16 байт для CacheManagerConfig). На этапе миграции `node_id` сохранён как `uintptr_t` для минимизации изменений. Добавлены вспомогательные функции: `is_aligned_offset()`, `align_offset_up()`, `get_granule_size()`. Определены типы `pmm_index_type`, `node_pptr`, `char_pptr`, `byte_pptr`. Добавлено 10 тестов в `tests/test_pmm_basic.cpp` (тег `[task14.1][adapter]`). Все 539 тестов проходят.

---

### Задача 14.2. Миграция `pmem_array` и `pvector` на PMM ✅ ВЫПОЛНЕНО

**Цель:** Переписать `pmem_array.h` и `pvector.h` для работы с аллокатором PMM.

- [x] Заменить вызовы `PersistentAddressSpace::Get().Create<T>()` / `Resolve<T>()` на `PamManager::allocate_typed<T>()` / `pptr<T>::resolve()`.
- [x] Обновить `pmem_array_hdr`:
  ```cpp
  struct pmem_array_hdr_pmm {
      uintptr_t size;
      uintptr_t capacity;
      uintptr_t data_off;  // байтовое смещение (кратно 16) для совместимости с node_id
  };
  ```
- [x] Обновить `pmem_array_reserve<T>` — использовать `PamManager::allocate_typed<T>(new_cap)` и `PamManager::deallocate_typed<T>()`.
- [x] Обновить `pvector<T>` — тонкая обёртка должна делегировать в обновлённый `pmem_array`.
- [x] Прогнать все тесты `test_pmem_array.cpp` и `test_pvector.cpp`.

**Примечание:** Реализована инкрементальная стратегия миграции:
- Созданы новые файлы `pmem_array_pmm.h` и `pvector_pmm.h` с PMM-реализациями.
- Оригинальные `pmem_array.h` и `pvector.h` сохранены для обратной совместимости.
- Новые типы: `pjson::pmem_array_hdr_pmm`, `pjson::pvector_pmm<T>`.
- Новые функции: `pmem_array_pmm_init`, `pmem_array_pmm_reserve`, `pmem_array_pmm_push_back`, и др.
- Вспомогательные функции: `pjson::pmm_resolve<T>()`, `pjson::pmm_resolve_const<T>()`.
- Добавлено 21 тест в `tests/test_pmem_array_pmm.cpp` (тег `[task14.2]`).
- Все 560 тестов проходят.

---

### Задача 14.3. Миграция `pmap` на PMM ✅ ВЫПОЛНЕНО

**Цель:** Решить, использовать ли собственный sorted-array `pmap` или AVL-дерево `pmm::pmap`.

**Вариант A (рекомендуемый на первом этапе):** Оставить sorted-array `pmap`, но переключить аллокатор на PMM.
- [x] Заменить вызовы аллокатора в `pmem_array_*` функциях (которые использует `pmap`).
- [x] Тесты `test_pmap.cpp` должны пройти без изменений.

**Вариант B (в будущем):** Перейти на `pmm::pmap` (AVL-дерево).
- [ ] Обеспечить совместимость API: `insert()`, `find()`, `contains()`, итерация.
- [ ] Обновить `pjson_node.h` — `object_val` должен использовать новый `pmap`.
- [ ] Обновить итераторы объектов (`object_iterator` из фазы 10).
- [ ] Прогнать все тесты `test_pmap.cpp` и `test_pjson_db.cpp`.

**Примечание:** Реализована инкрементальная стратегия миграции (Вариант A):
- Создан новый файл `pmap_pmm.h` с PMM-реализацией персистной карты.
- Оригинальный `pmap.h` сохранён для обратной совместимости.
- Новые типы: `pjson::pmap_entry_pmm<K,V>`, `pjson::pmap_pmm<K,V>`.
- API совместим с оригинальным `pmap`: `insert()`, `find()`, `erase()`, `operator[]`, итераторы.
- Добавлено 18 тестов в `tests/test_pmap_pmm.cpp` (тег `[task14.3]`).
- Все 578 тестов проходят.

---

### Задача 14.4. Миграция `pstring` и `pstringview` на PMM ✅ ВЫПОЛНЕНО

**Цель:** Адаптировать строковые типы для работы с аллокатором PMM.

**14.4.1. `pstringview` (readonly, интернированные).**

PMM v0.21.0 уже содержит оптимизированный `pmm::pstringview` (single-block, с `pptr<pstringview>` как ключ `pmap`). Реализован **Вариант A (рекомендуемый):**

- [x] Создан файл `pstringview_pmm.h` — адаптер для `pmm::pstringview<PamManager>`.
- [x] `pstringview_pmm` делегирует интернирование в `PamManager::pstringview::intern()`.
- [x] `pstringview_pmm_intern()` — хелпер для интернирования строк через PMM.
- [x] Тесты в `tests/test_pstringview_pmm.cpp` (19 тестов, тег `[task14.4]`).

**14.4.2. `pstring` (readwrite, изменяемые строки JSON).**

PMM не имеет аналога `pstring`. Создана новая реализация на базе PMM:

- [x] Создан файл `pstring_pmm.h` — персистная изменяемая строка на PMM.
- [x] `pstring_pmm::assign()` использует `PamManager::allocate_typed<char>()`.
- [x] `pstring_pmm_create()` / `pstring_pmm_destroy()` — хелперы для создания/удаления.
- [x] Тесты в `tests/test_pstring_pmm.cpp` (17 тестов, тег `[task14.4]`).

**Примечание:** PMM-версии строковых типов (`pstring_pmm`, `pstringview_pmm`) созданы параллельно
с оригинальными (`pstring`, `pstringview`). Полная интеграция в `pjson_node.h` и `pjson_codec.h`
будет выполнена в задачах 14.5–14.8. Все 614 тестов проходят.

---

### Задача 14.5. Миграция `pjson_pool` на PMM

**Цель:** Пул узлов (`pjson_pool`) должен аллоцировать массив `node` через PMM.

- [ ] В `pjson_pool`: заменить рост массива узлов на `PamManager::allocate_typed<node>(new_cap)`.
- [ ] Free-list внутри пула не меняется (внутренняя логика на основе `node_tag::_free`).
- [ ] Тесты `test_pjson_pool.cpp`.

---

### Задача 14.6. Миграция `PersistentAddressSpace` → PMM API

**Цель:** Заменить синглтон `PersistentAddressSpace` на статический API PMM.

Таблица соответствия методов:

| Текущий API (`PersistentAddressSpace`) | Новый API (`PamManager`) |
|----------------------------------------|--------------------------|
| `Init(filename)` | `PamManager::create(initial_size)` + `pmm::load_manager_from_file()` |
| `Get()` | Не нужен (статические методы) |
| `Create<T>(name)` | `PamManager::allocate_typed<T>()` + реестр имён |
| `CreateArray<T>(count, name)` | `PamManager::allocate_typed<T>(count)` + реестр имён |
| `Resolve<T>(offset)` | `pptr<T>::resolve()` |
| `Delete(offset)` | `PamManager::deallocate_typed<T>(pptr)` |
| `Find(name)` | Поиск в пользовательском реестре имён (`pmap`) |
| `FindTyped<T>(name)` | Поиск в реестре имён + приведение типа |
| `Realloc(off, old, new, size)` | Аллокация нового блока + копирование + деаллокация старого |
| `Save()` | `pmm::save_manager<PamManager>(filename)` |
| `Load()` | `pmm::load_manager_from_file<PamManager>(filename)` |
| `GetBump()` | `PamManager::used_size()` |
| `GetDataSize()` | `PamManager::total_size()` |
| `GetFreeListSize()` | `PamManager::free_size()` / `PamManager::get_stats()` |
| `GetSlotCount()` | Подсчёт записей в пользовательском реестре |
| `GetNamedCount()` | Подсчёт записей в пользовательском реестре имён |
| `GetStringTableOffset()` | Не нужен (PMM хранит `pstringview` автоматически) |

- [ ] Создать фасад `pam_pmm.h`, реализующий совместимый API поверх PMM.
- [ ] Реализовать реестр именованных объектов через `pmm::pmap<pptr<pstringview>, uintptr_t>`.
- [ ] Реализовать сохранение/загрузку через `pmm::save_manager()` / `pmm::load_manager_from_file()`.
- [ ] Обновить `pjson_db::open()` — использовать новый фасад.
- [ ] Обновить `pjson_db::save()`.
- [ ] Тесты `test_pam.cpp`, `test_pam_dynamic.cpp`, `test_pam_metrics.cpp`.

---

### Задача 14.7. Миграция `persist<T>` и `fptr<T>`

**Цель:** Определить судьбу `persist<T>` и `fptr<T>` в новой архитектуре.

- `persist<T>` — обёртка для POD-типов в ПАП. В PMM не нужна: PMM сам гарантирует работу с тривиально копируемыми типами.
- `fptr<T>` — персистный указатель (offset-based). Заменяется на `pptr<T, PamManager>`.

- [ ] Определить `fptr<T>` как алиас или тонкую обёртку над `pptr<T>`:
  ```cpp
  template <typename T>
  using fptr = PamManager::pptr<T>;
  ```
- [ ] Удалить `persist<T>` (или оставить как deprecated alias).
- [ ] Обновить `pallocator.h` — STL-совместимый аллокатор через PMM.
- [ ] Тесты `test_persist.cpp`, `test_pallocator.cpp`.

---

### Задача 14.8. Миграция `pjson_db` (высокоуровневый API)

**Цель:** Обновить `pjson_db.h` для работы с новым бэкендом.

- [ ] `pjson_db::open()` — инициализация PMM вместо `PersistentAddressSpace::Init()`.
- [ ] `pjson_db::save()` — сохранение через `pmm::save_manager()`.
- [ ] Обновить метрики (`db_metrics`) — маппинг полей ПАМ на PMM stats:
  ```
  pam_bump_offset    → PamManager::used_size()
  pam_free_list_size → PamManager::get_stats().free_count
  pam_total_size     → PamManager::total_size()
  pam_slot_count     → PamManager::get_stats().alloc_count
  pam_named_count    → name_registry.size()
  ```
- [ ] Все тесты `test_pjson_db.cpp`, `test_pjson_db_errors.cpp`, `test_pjson_db_perf.cpp`, `test_pjson_clone.cpp`.

---

### Задача 14.9. Утилита миграции старых `.pam` файлов

**Цель:** Обеспечить переход для существующих баз данных.

- [ ] Создать утилиту `pam_migrate.cpp`:
  1. Загрузить старый `.pam` файл через текущий `PersistentAddressSpace`.
  2. Экспортировать всё дерево JSON в строку: `node_to_string(root_id)`.
  3. Создать новый `.pam` файл через PMM: `PamManager::create(size)`.
  4. Импортировать JSON в новую БД: `pjson_db::parse_into(root, json_str)`.
  5. Сохранить: `pmm::save_manager<PamManager>(new_filename)`.
- [ ] Написать тест миграции: создать БД в старом формате → мигрировать → проверить данные.

---

### Задача 14.10. Удаление устаревшего кода

**Цель:** После успешной миграции всех тестов — удалить старый ПАМ.

- [ ] Удалить `pam_core.h` (1500 строк).
- [ ] Удалить `persist.h` (374 строки) — или оставить только алиасы `fptr<T>` → `pptr<T>`.
- [ ] Удалить `pam.h` (88 строк) — заменить на новый фасад.
- [ ] Обновить `CMakeLists.txt` — убрать устаревшие зависимости.
- [ ] Обновить `readme.md` — документация нового ПАМ.
- [ ] Финальный прогон всех тестов.

---

### Задача 14.11. Обновить тесты и демонстрации

- [ ] `test_pam.cpp` — адаптировать под новый API.
- [ ] `test_persist.cpp` — обновить или удалить (если `persist<T>` удалён).
- [ ] `test_pallocator.cpp` — обновить под PMM-аллокатор.
- [ ] `test_pam_dynamic.cpp`, `test_pam_metrics.cpp`, `test_pam_perf.cpp` — адаптировать.
- [ ] `main.cpp` — обновить демонстрацию на новый бэкенд.
- [ ] Все 521+ тестов проекта должны пройти.

---

### Порядок выполнения задач Фазы 14

```
Задача 14.0 (подключение PMM)
        ↓
Задача 14.1 (адаптер pptr ↔ uintptr_t)
        ↓
  ┌─────┼─────────┐
  ↓     ↓         ↓
14.2  14.4.2    14.7
(array, (pstring) (fptr)
vector)
  ↓     ↓
14.3  14.4.1
(pmap) (pstringview)
  └─────┼─────────┘
        ↓
Задача 14.5 (pjson_pool)
        ↓
Задача 14.6 (PersistentAddressSpace → PMM)
        ↓
Задача 14.8 (pjson_db)
        ↓
  ┌─────┴─────┐
  ↓           ↓
14.9        14.11
(миграция)  (тесты)
  └─────┬─────┘
        ↓
Задача 14.10 (удаление устаревшего кода)
```

Задачи 14.2, 14.4.2 и 14.7 можно выполнять параллельно (независимые модули с общей зависимостью от 14.1).
Задачи 14.9 и 14.11 можно выполнять параллельно (одна создаёт утилиту, другая обновляет тесты).

---

### Критерии приёмки Фазы 14

- [ ] PMM подключён как зависимость (single-header или submodule).
- [ ] Все персистные типы (`pvector`, `pmap`, `pstring`, `pstringview`) работают через аллокатор PMM.
- [ ] `pjson_db::open()` / `save()` используют PMM для управления ПАП.
- [ ] Реестр именованных объектов реализован поверх `pmm::pmap`.
- [ ] `fptr<T>` = `pptr<T>` (алиас или обёртка).
- [ ] Утилита миграции `.pam` → новый формат работает.
- [ ] Все 521+ тестов проходят.
- [ ] Старый `pam_core.h` / `persist.h` удалены или содержат только совместимые алиасы.
- [ ] `readme.md` обновлён.
- [ ] CI проходит.

---

### Риски и смягчение

| Риск | Вероятность | Влияние | Смягчение |
|------|-------------|---------|-----------|
| Несовместимость формата файла | Высокая | Среднее | Утилита миграции (задача 14.9) |
| Различие в семантике `Realloc` | Средняя | Высокое | PMM не имеет `Realloc` — нужно alloc+copy+dealloc; проверить производительность `pvector::push_back` |
| Overhead гранульного выравнивания | Низкая | Низкое | 16-байтовые гранулы дают ≤15 байт overhead на аллокацию; для мелких объектов пул сглаживает |
| Потеря `type_vec` (runtime type info) | Средняя | Низкое | В pjson_db type info хранится в `node_tag`; `type_vec` нужен только для legacy API |
| Регрессия производительности | Средняя | Среднее | AVL-аллокатор O(log n) vs bump O(1); компенсируется лучшей утилизацией памяти; бенчмарки из фазы 9 |
