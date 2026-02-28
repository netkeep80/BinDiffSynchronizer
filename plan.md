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

## Фаза 2. Словарь строк: readonly (`pstringview`) и readwrite (`pstring`)

**Цель:** Закрепить двухтиповую архитектуру строк в ПАП. Readonly строки (`pstringview`) — только для ключей `pmap` и путей `$ref`, интернированы, сравнение O(1). Readwrite строки (`pstring`) — для JSON string-value узлов, изменяемые на лету (необходимо для [jsonRVM](https://github.com/netkeep80/jsonRVM)).

### Задача 2.1. Расширить `pstringview` для работы со словарём

- `pstringview` хранит `length` и `chars_offset` (уже сделано).
- Добавить полную `pstringview_table` — персистный объект (живёт в ПАП).
- `pstringview_table` — это `pmap<key_hash, chars_offset>` или sorted array пар.

### Задача 2.2. Реализовать метод `intern` на уровне ПАМ

- `PersistentAddressSpace::InternString(const char* s, size_t len) -> pstringview`
- Ищет в таблице: если найдена — возвращает существующую.
- Если не найдена — аллоцирует char-массив в ПАП, добавляет в таблицу, возвращает.
- Таблица — часть заголовочной секции ПАМ (хранится в образе).
- Интернированные строки только накапливаются; освобождения нет.

### Задача 2.3. Убрать SSO из `pstringview` и `pstring`

- `pstringview` НЕ содержит inline-буфера (no SSO).
- `pstring` также НЕ содержит inline-буфера (no SSO).
- Любая строка, даже из 1 символа, хранится в ПАП через `chars_offset`.
- Это необходимо для сквозного поиска по всем строкам ПАП.

### Задача 2.4. Чётко разграничить области применения двух типов строк

| Тип | Применение | Изменяемость |
|-----|-----------|--------------|
| `pstringview` | Ключи `pmap<pstringview, node_id>`, путь в `$ref`, сегменты path-адресации | readonly (interned) |
| `pstring` | JSON string-value узлы (`node_tag::string`) | readwrite (изменяются на лету) |

- `pstring` используется в узлах `node_tag::string` (JSON строковые значения).
- `pstring` НЕ используется как ключ `pmap` — только `pstringview`.
- [jsonRVM](https://github.com/netkeep80/jsonRVM) работает непосредственно в БД и может модифицировать `pstring`-узлы "на месте", не затрагивая словарь `pstringview`.

### Задача 2.5. Поддержка полнотекстового поиска по обоим типам строк

- `PersistentAddressSpace::SearchStrings(pattern) -> vector<result>` — поиск по всем интернированным `pstringview` в словаре.
- `pjson_db::search_strings(pattern)` — поиск по словарю `pstringview` И по всем `pstring`-значениям в пуле узлов.
- `PersistentAddressSpace::AllStrings() -> итератор` — перебор всех `pstringview` в словаре.

**Критерии приёмки фазы 2:**
- `pstringview_table` хранится в ПАП и восстанавливается при загрузке образа.
- Два одинаковых `intern("hello")` дают одинаковый `chars_offset`.
- Сравнение строк-ключей `==` через `chars_offset` (O(1)).
- `pstring`-значения изменяемы: `node.string_val.assign("new_value")` работает без пересоздания узла.
- Тесты `test_pstringview.cpp` и `test_pstring.cpp` проходят.

---

## Фаза 3. Новая модель узлов JSON (`pjson_node`)

**Цель:** Переработать `pjson` на `node_id`-адресацию. Добавить типы `ref` и `binary`.

### Задача 3.1. Определить расширенный `node_tag`

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

### Задача 3.2. Определить структуру `node`

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

### Задача 3.3. Определить тип `node_id`

```cpp
using node_id = uintptr_t; // смещение узла в ПАП; 0 = null/invalid
```

### Задача 3.4. Реализовать `node_view` — безопасный accessor

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

### Задача 3.5. Написать тесты для новой модели узлов

- Тест создания каждого типа узла.
- Тест `node_view::deref()` — рекурсивное и нерекурсивное разыменование.
- Тест `object_val` — вставка/поиск по ключу `pstringview` (readonly).
- Тест `array_val` — push_back, at, size.
- Тест двух типов строк:
  - `string_val` (pstring, readwrite): создание, `assign("new")`, изменение без пересоздания узла.
  - `ref_val.path` (pstringview, readonly): интернирование, сравнение через `chars_offset`.
  - Проверка, что ключи объектов — только `pstringview`, значения типа `string` — только `pstring`.

**Критерии приёмки фазы 3:**
- Новый файл `pjson_node.h`.
- Все типы узлов работают.
- `node_view` корректно работает с ПАП.

---

## Фаза 4. Пул узлов (`pjson_pool`)

**Цель:** Быстрая аллокация узлов (O(1) амортизированно) с поддержкой free-list внутри пула.

### Задача 4.1. Переработать `pjson_node_pool.h`

- Пул хранит `pvector<node>` — компактный массив узлов в ПАП.
- Free-list в пуле: удалённые узлы помечаются специальным тегом (например, `node_tag::_free`) и добавляются в список свободных слотов.

### Задача 4.2. Реализовать API пула

```cpp
class pjson_pool {
public:
    node_id alloc();                 // O(1) амортизированно
    void    free(node_id id);        // возвращает в free-list
    node&   get(node_id id);         // доступ по node_id
    const node& get(node_id id) const;
};
```

### Задача 4.3. Написать тесты для пула

- Аллокация 10000 узлов.
- Освобождение каждого второго, повторная аллокация — без роста ПАП.
- Сохранение/загрузка образа с пулом.

**Критерии приёмки фазы 4:**
- Новый файл `pjson_pool.h`.
- Аллокация O(1), повторное использование работает.
- Тесты проходят.

---

## Фаза 5. Сериализация/десериализация и поддержка `$ref`/`$base64` (`pjson_codec`)

**Цель:** Парсер и сериализатор, работающие с новой моделью узлов. Корректная обработка `$ref` и `$base64`.

### Задача 5.1. Переработать `pjson_serializer.h` → `pjson_codec.h`

- Переработать десериализатор для работы с `node_id` и `pjson_pool`.
- Ключи объектов интернируются через `PersistentAddressSpace::InternString` → `pstringview`.
- Строковые значения JSON создаются как `pstring`-узлы (readwrite) через `pjson_pool::alloc_string(value)`.
- Сегменты путей `$ref` интернируются как `pstringview` (readonly).

### Задача 5.2. Реализовать распознавание `$ref`

- При парсинге: объект строго вида `{ "$ref": "<path>" }` (ровно 1 ключ) → создаётся `ref`-узел.
- `ref_val.path_*` = интернированный `pstringview` пути.
- `ref_val.target` = 0 (будет разрешён при первом обращении или при `resolve_all()`).

### Задача 5.3. Реализовать распознавание `$base64`

- При парсинге: объект строго вида `{ "$base64": "<base64>" }` → `binary`-узел.
- Base64 декодируется в `pvector<uint8_t>` в ПАП.

### Задача 5.4. Реализовать сериализатор

- `binary`-узел → `{ "$base64": "..." }`.
- `ref`-узел → `{ "$ref": "<path>" }` (использует сохранённый `path`).
- Остальные типы — стандартный JSON.

### Задача 5.5. Написать тесты сериализации

- Round-trip: `parse → dump` для всех типов узлов.
- Специфично `$ref`: `{ "$ref": "/a/b" }` сохраняется и восстанавливается.
- Специфично `$base64`: `{ "$base64": "AAEC" }` → bytes `[0, 1, 2]` → обратно в base64.
- Большой JSON (>10 000 узлов) с `ReserveSlots`.

**Критерии приёмки фазы 5:**
- Новый файл `pjson_codec.h`.
- Все тесты `test_pjson_serial.cpp` проходят с новым кодеком.
- `$ref` и `$base64` корректно парсятся и сериализуются.

---

## Фаза 6. Менеджер БД и path-адресация (`pjson_db`)

**Цель:** Реализовать высокоуровневый API доступа к данным через строковые пути.

### Задача 6.1. Определить класс `pjson_db`

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

### Задача 6.2. Реализовать парсер путей

- Синтаксис пути: абсолютный `/a/b/0/c`.
- Сегменты: строки для объектов, десятичные числа для массивов.
- Зарезервированные пространства:
  - `/$metrics/...` — только для чтения
  - `/$sys/...` — зарезервировано

### Задача 6.3. Реализовать `get`, `put`, `erase`

- `get`: обходит дерево по сегментам пути. По умолчанию разыменовывает `ref`.
- `put`: создаёт промежуточные объекты/массивы (по первому символу следующего сегмента определяет тип).
- `erase`: рекурсивно удаляет поддерево (кроме `ref`-целей).

### Задача 6.4. Реализовать детектирование цикличных ссылок

- При `resolve_ref()`: отслеживать посещённые `node_id`.
- При обнаружении цикла — возвращать ошибку `ref_cycle`.
- Параметр `max_depth` как дополнительная защита.

### Задача 6.5. Реализовать `resolve_all_refs()`

- После загрузки образа обойти все `ref`-узлы и установить `target` по `path`.
- Логировать неразрешённые ссылки.

### Задача 6.6. Написать тесты менеджера БД

- Персистентность: создать БД, записать, сохранить, загрузить, проверить.
- Path-адресация: get/put/erase по `/a/b/0/c`.
- Ошибки: попытка `put` в `/$metrics` → ошибка `readonly`.
- `$ref` разыменование: `{ "$ref": "/a/b" }` → возвращает узел `/a/b`.
- Цикл: `a → ref b`, `b → ref a` → `ref_cycle`.

**Критерии приёмки фазы 6:**
- Единый файл `pjson_db.h` включает всё необходимое.
- Все тесты проходят.
- Метрики доступны через `/$metrics/...`.

---

## Фаза 7. Метрики ПАП и метрики БД

**Цель:** Хранить и обновлять метрики в ПАП, доступ через `/$metrics`.

### Задача 7.1. Определить структуру `db_metrics`

```cpp
struct db_metrics {
    uint64_t node_count_total;      // всего узлов в пуле
    uint64_t string_count_total;    // всего интернированных строк
    uint64_t binary_bytes_total;    // всего байт в binary-узлах
    uint64_t ref_count;             // всего ref-узлов
    uint64_t array_count;           // всего массивов
    uint64_t object_count;          // всего объектов
    uint64_t last_save_time;        // время последнего сохранения (Unix timestamp)
    // Метрики PAM (проксируются из PersistentAddressSpace)
    uint64_t pam_bump_offset;
    uint64_t pam_free_list_size;
    uint64_t pam_total_size;
};
```

### Задача 7.2. Интегрировать обновление метрик в мутации

- Каждая операция `put` / `erase` / `alloc node` / `intern string` обновляет соответствующий счётчик.
- Обновление атомарно в рамках одной операции (нет частичного состояния).

### Задача 7.3. Реализовать read-only доступ через пути

- `get("/$metrics/node_count_total")` → `node_view` типа `uinteger`.
- `put("/$metrics/...")` → ошибка `readonly`.

**Критерии приёмки фазы 7:**
- Метрики обновляются корректно.
- Доступ через `/$metrics` работает.

---

## Фаза 8. Иерархическое адресное пространство (интерфейс `pmap<pstringview, pjson>`)

**Цель:** Менеджер ПАП сам реализует интерфейс `pmap<pstringview, pjson>` для внешней интеграции.

### Задача 8.1. Реализовать интерфейс `pjson_db` как `pmap<pstringview, node_id>`

- `operator[](pstringview path) -> node_view` — доступ по пути.
- `find(pstringview path) -> node_view` — поиск без создания.
- `insert(pstringview path, node_id value) -> bool` — вставка.
- `erase(pstringview path) -> bool` — удаление.

### Задача 8.2. Реализовать интерфейс поиска по строкам

- `pjson_db::search_strings(const char* pattern) -> std::vector<search_result>` — поиск по всем интернированным ключам (`pstringview`) И по всем `pstring`-значениям в пуле узлов.
- `pjson_db::all_strings() -> итератор` — перебор всех строк словаря `pstringview`.
- `search_result` содержит найденную строку и `node_id` узла (если это `pstring`-значение) или путь ключа (если это `pstringview`-ключ).

### Задача 8.3. Написать интеграционные тесты

- Создать БД с вложенными объектами, найти значение через `search_strings`.
- Использовать `operator[]` для навигации.

---

## Фаза 9. Совместимость, документация и финальная очистка

### Задача 9.1. Обновить `main.cpp`

- Демонстрация всех возможностей: открытие БД, put/get, `$ref`, `$base64`, метрики.

### Задача 9.2. Обновить тесты

- Привести все тесты к работе с новым API.
- Добавить тесты производительности: 100k узлов за приемлемое время.

### Задача 9.3. Проверить CI

- Убедиться, что CI проходит для Linux (GCC, Clang) и Windows (MSVC).
- Проверить clang-format.

### Задача 9.4. Обновить `readme.md`

- Отразить новую архитектуру и API.
- Добавить примеры кода.

### Задача 9.5. Удалить устаревший код

- Удалить или переместить файлы, которые заменены новыми.
- Убедиться в отсутствии мёртвого кода.

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
```

Фазы 1 и 3 можно выполнять параллельно (нет зависимостей между pmem_array и node model).
Фазы 7 и 8 можно выполнять параллельно (оба зависят от Фазы 6).
