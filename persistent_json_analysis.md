# Анализ применимости persistent_json для jgit и jdb

## Содержание

1. [Плюсы и минусы persistent_json для jgit](#1-плюсы-и-минусы-persistent_json-для-jgit)
2. [Идеальная альтернативная система хранения JSON для jgit](#2-идеальная-альтернативная-система-хранения-json-для-jgit)
3. [Сравнение: persistent_json vs идеальная альтернатива для jgit](#3-сравнение-persistent_json-vs-идеальная-альтернатива-для-jgit)
4. [Анализ применения persistent_json для jdb (JSON Database)](#4-анализ-применения-persistent_json-для-jdb-json-database)
5. [Сравнение двух вариантов реализации: кастомный vs basic_json инстанцирование](#5-сравнение-двух-вариантов-реализации-кастомный-vs-basic_json-инстанцирование)
6. [Итоговые выводы: лучшие варианты применения persist<T> и fptr<T> для JSON](#6-итоговые-выводы-лучшие-варианты-применения-persisttpersistt-и-fptrtfptrt-для-json)

---

## 1. Плюсы и минусы persistent_json для jgit

### Контекст

В данном репозитории реализованы два варианта JSON-хранилища:

1. **`PersistentJsonStore`** (`jgit/persistent_json_store.h`) — кастомное хранилище на трёх плоских пулах (`value_pool_`, `array_pool_`, `object_pool_`) из `persist<T>`-совместимых типов.
2. **`jgit::persistent_json`** (`jgit/persistent_basic_json.h`) — инстанция `nlohmann::basic_json<>` с заменёнными `ObjectType`, `ArrayType`, `StringType`.

**jgit** — система версионирования JSON-данных в стиле Git: commit-history, branches, content-addressed ObjectStore.

### ✅ Плюсы persistent_json для jgit

| # | Плюс | Объяснение |
|---|------|-----------|
| 1 | **API совместимость с nlohmann::json** | `persistent_json j; j["key"] = 42; j.dump();` — полностью стандартный nlohmann-API. Любая библиотека, принимающая `nlohmann::json`, может работать с `persistent_json` через `json::parse(pj.dump())`. |
| 2 | **JSON Patch / Merge Patch / Diff** | `nlohmann::json::diff()`, `merge_patch()`, `patch()` работают с `persistent_json` из коробки — это готовые примитивы для «git diff» и «git merge» в jgit. Тест `test_phase3_integration.cpp` подтверждает: `merge_patch` и `diff` работают корректно. |
| 3 | **JSON Pointer (RFC 6901)** | `j["/user/name"_json_pointer]` — гранулярная адресация узлов для инкрементальных обновлений. Аналог пути к файлу в Git-дереве. |
| 4 | **Сериализация в CBOR/MessagePack** | `nlohmann::json::to_cbor(j)` → готовые байты для хранения в `ObjectStore`. Тест `test_persistent_json_store_integration.cpp` подтверждает round-trip через `ObjectStore`. |
| 5 | **persistent_string как StringType** | Ключи и строковые значения хранятся в `jgit::persistent_string` — SSO для коротких строк, `fptr<char>` для длинных. `sizeof` фиксирован. Совместим с `persist<T>`. |
| 6 | **Экосистема nlohmann/json** | JSON Schema validation, SAX parser, итераторы, итерирование по ключам — всё доступно без написания собственного кода. |
| 7 | **Простота snapshot/restore** | `store.put(pj)` / `store.get(id)` работает немедленно, т.к. `nlohmann::json::to_cbor` доступен напрямую. |

### ❌ Минусы persistent_json для jgit

| # | Минус | Объяснение |
|---|-------|-----------|
| 1 | **Не тривиально копируем** | `nlohmann::basic_json` содержит нетривиальный конструктор/деструктор (управление памятью union + динамические аллокации). `std::is_trivially_copyable<persistent_json> == false`. Поэтому `persist<persistent_json>` **не компилируется** — нельзя напрямую сохранить через `persist<T>`. Требуется обходной путь через `snapshot()`. |
| 2 | **Внутренние данные в heap** | `nlohmann::basic_json` хранит значения через `new`/`delete` (даже с кастомным аллокатором). При замене `ObjectType` и `ArrayType` адаптерами, унаследованными от `std::map`/`std::vector`, фактические данные по-прежнему в heap, а не в `AddressManager`-управляемых слотах. |
| 3 | **fptr<persistent_json> требует осторожности** | `fptr<T>` использует `persist<T>` внутри `AddressManager<T>`, что требует тривиальной копируемости. `fptr<persistent_json>` работает (тест `test_persist_json_final.cpp`), но только через `store.snapshot(j)` / `store.restore(id, j)` — не через прямой `fptr<persistent_json>::New()`. |
| 4 | **Граф объектов в heap — нет zero-parse reload** | При перезапуске процесса весь граф `persistent_json` нужно десериализовать из CBOR/JSON заново. Нет возможности просто `mmap` бинарный файл и начать работать. Это важный минус для jgit, где хотелось бы мгновенно «открыть репозиторий». |
| 5 | **Большой sizeof** | `sizeof(nlohmann::json)` = 24 байта (тэг + union с 8-байтными данными). Каждый узел — отдельная heap-аллокация. При работе с большими JSON-деревьями (тысячи узлов) это значит тысячи мелких alloc/free — давление на аллокатор. |
| 6 | **ObjectType/ArrayType адаптеры — std::map/std::vector** | Текущие `persistent_map_adapter` и `persistent_array_adapter` (Task 3.3.4) унаследованы от `std::map`/`std::vector`. Они удовлетворяют интерфейсу `basic_json`, но не дают реальной персистности — только именованное оборачивание. |
| 7 | **Трудоёмкая замена ObjectType на реально персистный** | Заменить `std::map` на `AddressManager<persistent_map<V,N>>` как ObjectType невозможно напрямую, потому что значения `V` в карте — это сами узлы `basic_json`, которые нетривиально копируемы. Замкнутый круг: `persistent_map` требует тривиально копируемых значений, а `basic_json`-узлы не являются таковыми. |

### Итог по разделу 1

`persistent_json` **подходит для jgit** как API-слой и рабочий документ в памяти, но **не подходит** как нативное персистное хранилище с mmap и zero-parse reload. Основная причина — фундаментальное противоречие: `nlohmann::basic_json` управляет памятью через heap, тогда как jgit-персистность требует тривиально копируемых объектов фиксированного размера.

---

## 2. Идеальная альтернативная система хранения JSON для jgit

### Требования к идеальной системе

Для jgit (версионирование JSON, аналог Git) идеальная система хранения JSON должна:

1. **Тривиально копируемые узлы** → `persist<T>` / `fptr<T>` / `mmap` работают без сериализации.
2. **Фиксированный размер узла** → `sizeof(Node)` — константа времени компиляции.
3. **Content-addressed immutable snapshots** → каждое состояние идентифицируется SHA-256 хешем.
4. **Гранулярные diff/merge** → delta между двумя снимками вычислима без полного обхода.
5. **Zero-parse reload** → открыть репозиторий = `mmap` файл, указать на корневой узел.
6. **Совместимость с nlohmann::json** → import/export для взаимодействия с внешним миром.

### Архитектура идеальной системы

```
jgit Ideal JSON Storage
═══════════════════════

┌─────────────────────────────────────────────────────────┐
│                 Working Tree Layer                       │
│  PersistentJsonStore                                     │
│  ┌──────────────────────────────────────────────────┐   │
│  │  value_pool_:  AddressManager<persistent_json_value> │
│  │  array_pool_:  AddressManager<json_array_slab>   │   │
│  │  object_pool_: AddressManager<json_object_slab>  │   │
│  │                                                  │   │
│  │  All types: trivially copyable, fixed sizeof     │   │
│  │  Slab chaining via fptr<T>                       │   │
│  └──────────────────────────────────────────────────┘   │
│                        ↕ import/export                   │
│                   nlohmann::json API                     │
└───────────────────────────┬─────────────────────────────┘
                            │ snapshot() / restore()
                            ↓
┌─────────────────────────────────────────────────────────┐
│              Object Store Layer                          │
│  ObjectStore (content-addressed)                         │
│  .jgit/objects/<2hex>/<62hex>                           │
│  Format: CBOR blob of exported nlohmann::json           │
│  Identity: SHA-256(content)                              │
└───────────────────────────┬─────────────────────────────┘
                            │
                            ↓
┌─────────────────────────────────────────────────────────┐
│              Commit History Layer                        │
│  Repository: commit graph, branches, HEAD               │
│  Commit = {root_snapshot_id, parent_id, author, msg}   │
└─────────────────────────────────────────────────────────┘
```

### Ключевые отличия от persistent_json

| Аспект | persistent_json (basic_json) | PersistentJsonStore (идеальная) |
|--------|-----------------------------|---------------------------------|
| Узел | `basic_json` (нетривиальный, heap) | `persistent_json_value` (тривиальный, 72 байта) |
| Массив | `std::vector<basic_json>` (heap) | `persistent_array<uint32_t, 16>` (fixed slab) |
| Объект | `std::map<string, basic_json>` (heap) | `persistent_map<uint32_t, 16>` (sorted slab) |
| Адресация | Указатели C++ (ASLR, volatile) | Integer IDs в пулах (stable, mmappable) |
| Персистность | CBOR round-trip на каждый commit | mmap / binary dump пулов (zero-parse) |
| `persist<T>` совместимость | ❌ (нетривиально копируем) | ✅ (тривиально копируем) |
| `fptr<T>` для ссылок | ❌ (требует обходного пути) | ✅ (нативно) |
| JSON Patch / Diff | ✅ из коробки | ❌ нужно реализовать отдельно |
| Экосистема nlohmann | ✅ полная | только через export/import |

### Сильные стороны идеальной системы для jgit

1. **Zero-parse reload**: пул `value_pool_` сохраняется как плоский бинарный файл. При открытии репозитория: `mmap("values.bin")` → указатель на корневой узел → работа. Никакого JSON-парсинга.

2. **Гранулярная delta**: между двумя снимками пулов можно вычислить delta на уровне `persistent_json_value`-слотов. `BinDiffSynchronizer<persistent_json_value>` работает нативно.

3. **Тривиальная копируемость**: `persist<persistent_json_value>`, `fptr<json_object_slab>` и `fptr<json_array_slab>` работают без дополнительного кода.

4. **Единый адресный граф**: все ссылки между узлами — целочисленные ID. Нет висящих указателей при перезагрузке.

5. **Бенчмарк-подтверждение**: `PersistentJsonStore::import_json` в ~5.8× быстрее `nlohmann::json::parse` за счёт плоских slab-аллокаций (см. `experiments/benchmark_persist_vs_std_results.md`).

---

## 3. Сравнение: persistent_json vs идеальная альтернатива для jgit

| Критерий | persistent_json (basic_json<>) | PersistentJsonStore (идеальная) | Победитель |
|----------|-------------------------------|--------------------------------|------------|
| Скорость import | Умеренно быстро | ~5.8× быстрее | PersistentJsonStore |
| Скорость reload | CBOR parse (медленно) | mmap (практически мгновенно) | PersistentJsonStore |
| API богатство | JSON Patch, Merge, Pointer, SAX | Только get/set по ID | persistent_json |
| Совместимость с внешним кодом | Полная (nlohmann-совместима) | Только через export | persistent_json |
| `persist<T>` нативность | ❌ | ✅ | PersistentJsonStore |
| Гранулярный diff (BinDiff) | ❌ | ✅ | PersistentJsonStore |
| Объём кода для разработчика | Минимален (экосистема nlohmann) | Большой (кастомные итераторы, поиск) | persistent_json |
| Версионирование jgit | Через CBOR blob | Нативное (pул-delta) | PersistentJsonStore |
| Долгосрочная поддержка | Зависит от nlohmann | Полный контроль | PersistentJsonStore |
| Порог вхождения | Низкий | Высокий | persistent_json |

### Рекомендуемая архитектура для jgit

**Двухслойный подход**:

```
Внешний API (для пользователей jgit)
    ↓  nlohmann::json  (удобный API, JSON Patch, сериализация)
    ↓  import_json() / export_json()
Внутреннее хранилище
    ↓  PersistentJsonStore (тривиальная персистность, zero-parse reload)
    ↓  ObjectStore (snapshot/restore через CBOR)
Commit History (Repository)
```

Это позволяет:
- Пользователям работать с удобным `nlohmann::json` API.
- Внутри использовать быструю и компактную `PersistentJsonStore` для нативной персистности.
- Снимки (commits) создавать через `ObjectStore` (CBOR от `nlohmann::json`).

---

## 4. Анализ применения persistent_json для jdb (JSON Database)

### Что такое гипотетическая jdb?

**jdb** (JSON Database) — темпоральная мультидокументная база данных для JSON, где:
- Каждый документ — отдельный именованный репозиторий jgit.
- Поддерживается индексирование по полям JSON.
- Возможен запрос (query) по значениям, путям JSON Pointer.
- Поддерживается полная история версий каждого документа.

### Почему persistent_json особенно ценна для jdb

В отличие от jgit (где хранилище — архив истории изменений), **jdb оперирует с «живыми» документами в памяти**:

| Сценарий | jgit | jdb |
|----------|------|-----|
| Частота чтения | Редко (checkout) | Постоянно (query) |
| Частота записи | При commit | Постоянно (update) |
| Размер рабочего набора | Один снимок | Все активные документы |
| Требования к API | CBOR/binary | JSON Query, индексы, транзакции |

### ✅ Где persistent_json выигрывает для jdb

1. **Богатый API запросов**: `nlohmann::json::items()`, `at()`, `contains()`, `flatten()`, JSON Pointer — это инструменты для query-движка jdb без написания собственного кода.

2. **JSON Schema валидация**: через внешние библиотеки, работающие с `nlohmann::json`, можно добавить валидацию схемы при вставке документов.

3. **Стандартные сериализационные форматы**: CBOR, MessagePack, BSON — для компактного хранения результатов query.

4. **Merge Patch для обновлений**: `merge_patch` как нативная операция UPDATE без ручного обхода дерева.

5. **Простота индексирования**: итерирование по `persistent_json::items()` для построения инвертированных индексов.

### ❌ Где persistent_json проигрывает для jdb

1. **Горячий путь — heap allocation**: каждое обновление документа в `persistent_json` = new/delete узлов в heap. При высокой частоте запросов это создаёт давление на аллокатор и GC-паузы (если аллокатор использует пул).

2. **Нет нативной индексной структуры**: `basic_json` — документ-ориентированный, нет B-tree или LSM для индексов по полям.

3. **Персистность только через snapshot**: нельзя быстро сохранить «рабочую» копию документа между запросами без полного CBOR round-trip.

4. **Конкурентный доступ**: `nlohmann::json` не thread-safe. Для jdb с параллельными запросами нужна внешняя синхронизация.

### Оптимальное применение persistent_json в jdb

```
jdb архитектура с persistent_json

┌─────────────────────────────────────────┐
│  Query Engine                            │
│  (работает с nlohmann::json / pjson)    │
│  - JSON Pointer для path lookup         │
│  - merge_patch для updates              │
│  - Итераторы для индексирования         │
└─────────────────┬───────────────────────┘
                  │ JSON API
┌─────────────────▼───────────────────────┐
│  Document Cache (in-memory)              │
│  map<DocId, persistent_json>             │
│  (LRU eviction, hot documents in memory) │
└─────────────────┬───────────────────────┘
                  │ snapshot/restore
┌─────────────────▼───────────────────────┐
│  jgit Repository per Document           │
│  (PersistentJsonStore + ObjectStore)     │
│  (версионирование, история)              │
└─────────────────────────────────────────┘
```

---

## 5. Сравнение двух вариантов реализации: кастомный vs basic_json инстанцирование

### Вариант 1: Кастомная реализация на `persist<T>` и `fptr<T>`

Представлена в `jgit/persistent_json_store.h` + `jgit/persistent_json_value.h` + `jgit/persistent_map.h` + `jgit/persistent_array.h`.

**Принцип**: трёхпуловая архитектура, где все узлы хранятся как `persistent_json_value` с integer-ID вместо указателей.

```cpp
// Кастомный подход
PersistentJsonStore store("/path/to/store");
uint32_t root_id = store.import_json(nlohmann_doc);
uint32_t field_id = store.get_field(root_id, "key");
nlohmann::json doc = store.export_json(root_id);
ObjectId snap = store.snapshot(root_id, object_store);
```

**Плюсы кастомного варианта:**

| # | Плюс |
|---|------|
| 1 | Все типы тривиально копируемы: `persist<persistent_json_value>` работает. |
| 2 | Zero-parse reload: пулы — плоские бинарные файлы, mmappable. |
| 3 | BinDiffSynchronizer<persistent_json_value> работает нативно. |
| 4 | Полный контроль над layout памяти и алгоритмами. |
| 5 | ~5.8× быстрее nlohmann::json::parse при import_json (см. бенчмарк). |
| 6 | fptr<T>-ссылки между узлами (граф вместо дерева) — аналог $ref. |
| 7 | Независимость от версий nlohmann/json — нет риска поломки при апгрейде. |

**Минусы кастомного варианта:**

| # | Минус |
|---|-------|
| 1 | Ограниченный API: нет JSON Patch, Merge Patch, JSON Pointer из коробки. |
| 2 | Большой объём кода: ~3000 строк для базовых типов и store. |
| 3 | Нет готовых сериализаторов: CBOR реализуется вручную (или через export+nlohmann). |
| 4 | Сложность интеграции: внешний код не может работать с пулами напрямую. |
| 5 | fixed capacity в slabs (16 элементов): нужно chaining для больших объектов. |

---

### Вариант 2: Инстанцирование `basic_json<>` с персистными классами

Представлена в `jgit/persistent_basic_json.h` как:
```cpp
using persistent_json = nlohmann::basic_json<
    jgit::persistent_map_adapter,    // ObjectType
    jgit::persistent_array_adapter,  // ArrayType
    jgit::persistent_string          // StringType
>;
```

**Плюсы basic_json инстанцирования:**

| # | Плюс |
|---|------|
| 1 | Полный nlohmann::json API сразу: dump, parse, at, diff, merge_patch, patch. |
| 2 | persistent_string как StringType — ключи и строки в SSO/fptr<char>. |
| 3 | Экосистема nlohmann: JSON Schema, SAX, потоковый парсинг. |
| 4 | Минимальный объём кода: ~60 строк для объявления. |
| 5 | Простой переход для пользователей nlohmann::json: `using json = persistent_json`. |
| 6 | JSON Patch совместимость: `diff()`, `patch()`, `merge_patch()` для jgit merge. |
| 7 | Бинарные форматы: `to_cbor()`, `to_msgpack()`, `from_cbor()` — готовые. |

**Минусы basic_json инстанцирования:**

| # | Минус |
|---|-------|
| 1 | `is_trivially_copyable == false` → `persist<persistent_json>` не компилируется. |
| 2 | ObjectType/ArrayType адаптеры — std::map/std::vector, не реально персистные. |
| 3 | Данные в heap: каждый узел — отдельный `new`/`delete`. |
| 4 | Zero-parse reload невозможен: нужна полная десериализация CBOR. |
| 5 | fptr<persistent_json> требует обходного пути (snapshot/restore). |
| 6 | Полная замена ObjectType на `persistent_map` невозможна (circular dependency): значения карты — сами узлы `basic_json`, которые нетривиально копируемы. |

---

### Детальное сравнение двух вариантов

| Критерий | Кастомный (persist<T>/fptr<T>) | basic_json инстанцирование |
|----------|-------------------------------|---------------------------|
| `persist<T>` нативность | ✅ полная | ❌ невозможна |
| `fptr<T>` для ссылок | ✅ нативная | ❌ только через обходной путь |
| Zero-parse reload | ✅ | ❌ |
| Гранулярный BinDiff | ✅ | ❌ |
| nlohmann::json API | только через export | ✅ полный |
| JSON Patch / Merge | ❌ кастомно | ✅ из коробки |
| Объём кода | Большой | Минимальный |
| CBOR/binary | через export | ✅ нативно |
| StringType персистна | ✅ (persistent_string) | ✅ (persistent_string) |
| Индексация (jdb) | Ручная | Через nlohmann API |
| Версионирование (jgit) | Нативное | Через ObjectStore |
| Порог вхождения | Высокий | Низкий |
| Реальная persistency | Полная (mmap) | Частичная (только строки) |
| Производительность import | ~5.8× быстрее | Базовая |

---

## 6. Итоговые выводы: лучшие варианты применения `persist<T>` и `fptr<T>` для JSON

### 6.1 Когда использовать кастомный подход (PersistentJsonStore / persist<T> / fptr<T>)

**Оптимален для:**

1. **Внутреннего хранилища jgit** — версионирование JSON-документов с zero-parse reload и нативным BinDiff. Именно в этой роли реализован `PersistentJsonStore`.

2. **Встроенных систем и IoT** — где heap allocation запрещён или ограничен. `persist<persistent_json_value>` работает без malloc, все объекты в `AddressManager`-управляемых слотах.

3. **High-performance индексов jdb** — внутренние индексные структуры (B-tree по integer-ключам) могут строиться из `persist<T>`-объектов с нативной персистностью.

4. **Репликации и синхронизации** — `BinDiffSynchronizer<persistent_json_value>` вычисляет дельты между снимками JSON-дерева на уровне байтов.

5. **Долгоживущих серверных процессов** — рабочий набор документов в `AddressManager`, который mmap-ится при старте процесса и сохраняется при завершении.

**Ключевое правило**: Если нужна нативная работа `persist<T>` или `fptr<T>` с JSON-данными → используй **кастомный подход**.

---

### 6.2 Когда использовать `persistent_json` (basic_json инстанцирование)

**Оптимален для:**

1. **API-слоя jgit и jdb** — пользователь работает с привычным `nlohmann::json`-совместимым объектом. Внутри commit/checkout происходит конвертация в `PersistentJsonStore`.

2. **JSON Patch / Merge операций** — `persistent_json::diff()`, `merge_patch()`, `patch()` — готовые примитивы для jgit merge без написания кода алгоритмов слияния.

3. **Промежуточного представления** — «рабочая копия» документа в памяти между операциями. Загружается через `import_json`, редактируется через nlohmann API, сохраняется через `snapshot()`.

4. **Совместимости с внешними системами** — код, принимающий `nlohmann::json`, автоматически совместим с `persistent_json` (через `static_cast` или `json::parse(pj.dump())`).

5. **Прототипирования и разработки** — низкий порог вхождения, богатый API, минимальный объём boilerplate кода.

**Ключевое правило**: Если нужна богатая nlohmann-экосистема (Patch, Merge, Pointer, Schema) → используй **`persistent_json` (basic_json инстанцирование)**.

---

### 6.3 Рекомендованная матрица применения

| Задача | Рекомендованный вариант | Обоснование |
|--------|------------------------|-------------|
| Хранилище jgit (working tree) | `PersistentJsonStore` | Zero-parse, тривиально копируем |
| Snapshot jgit commit | `ObjectStore` (CBOR от nlohmann) | Content-addressed, blob |
| API для пользователей jgit | `persistent_json` | nlohmann-совместимость |
| Merge в jgit | `persistent_json::merge_patch()` | Готовый алгоритм RFC 7396 |
| Diff в jgit | `persistent_json::diff()` | Готовый алгоритм RFC 6902 |
| jdb Document Cache | `persistent_json` (in-memory) | Богатый API для queries |
| jdb Persistent Storage | `PersistentJsonStore` | mmap, zero-parse |
| jdb Indexes | `persist<T>` / `fptr<T>` custom | Тривиально копируемые B-tree |
| BinDiff репликация | `BinDiffSynchronizer<persistent_json_value>` | Нативно тривиально копируем |
| Embed/IoT | `PersistentJsonStore` (pool-only, без fstream) | Без heap, фиксированный sizeof |

---

### 6.4 Архитектурные выводы

1. **`persist<T>` и `fptr<T>` — фундамент для хранилища, не для API.** Они идеальны для тривиально копируемых типов фиксированного размера: `persistent_json_value`, `persistent_map<V,N>`, `persistent_array<T,N>`, `persistent_string`. Не пытайтесь сделать `persist<nlohmann::json>` — это нарушает контракт тривиальной копируемости.

2. **`basic_json<>` инстанцирование — правильное место для `persistent_string`.** Замена `StringType = jgit::persistent_string` приносит реальные выгоды: ключи объектов хранятся в SSO (нет heap-аллокации для коротких ключей) или в `fptr<char>` (нет heap для длинных ключей). Это единственный компонент `basic_json<>`, который реально «персистен».

3. **`ObjectType`/`ArrayType` адаптеры — именованные обёртки, не реальная персистность.** `persistent_map_adapter` и `persistent_array_adapter` (Task 3.3.4) унаследованы от `std::map`/`std::vector`. Они удовлетворяют интерфейсу `basic_json` и дают ясное имя для будущего расширения, но не дают персистности сами по себе. Это правильный выбор, т.к. полная замена `ObjectType` на `persistent_map<V,N>` невозможна из-за циклической зависимости.

4. **Двухслойная архитектура — оптимальный компромисс для jgit и jdb:**
   - Слой 1: `persistent_json` (basic_json инстанцирование) — рабочая копия, API, queries.
   - Слой 2: `PersistentJsonStore` (persist<T>/fptr<T>) — дисковое хранилище, zero-parse reload.
   - Связь: `import_json()` / `export_json()` / `snapshot()` / `restore()`.

5. **BinDiffSynchronizer интегрируется с Слоем 2**, а не с Слоем 1. `BinDiffSynchronizer<persistent_json_value>` работает нативно с тривиально копируемыми узлами и позволяет вычислять дельты на уровне байтов между снимками пулов.

---

### 6.5 Таблица: лучшие случаи применения `persist<T>` и `fptr<T>` для JSON

| Паттерн | Конкретная реализация | Когда применять |
|---------|----------------------|-----------------|
| `persist<persistent_json_value>` | Отдельные JSON-узлы с именованной персистностью | Простые хранилища конфигурации |
| `fptr<persistent_json_value>` | Граф JSON-узлов в AddressManager | Сложные вложенные структуры |
| `AddressManager<persistent_map<V,N>>` | Объектные slab-пулы | Рабочее дерево jgit |
| `AddressManager<persistent_array<T,N>>` | Массивные slab-пулы | Рабочее дерево jgit |
| `persist<persistent_string>` | Строковые значения/ключи | Конфигурационные записи |
| `fptr<char>` (через persistent_string) | Длинные строки > 23 символов | Длинные JSON string-значения |
| `BinDiffSynchronizer<persistent_json_value>` | Дельты между снимками | Репликация jgit |
| `ObjectStore` (CBOR от nlohmann::json) | Content-addressed immutable snapshots | Commit history jgit |
| `persistent_json` (basic_json<>) | Рабочий документ в памяти | API пользователя, queries |
| `persistent_json::diff()` / `merge_patch()` | Алгоритмы слияния | jgit merge операции |

---

*Документ подготовлен в рамках issue #34. Дата: 2026-02-26.*
*Анализ основан на реализованном коде в ветке `issue-34-10f4c07abb3b`: 212 тестов, CI зелёный.*
