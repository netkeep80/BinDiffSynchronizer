# План разработки pjson_db — персистная JSON-база данных

## Концепция проекта

**pjson_db** — C++20 header-only библиотека, реализующая персистное JSON-хранилище (база данных) поверх персистного адресного пространства (ПАП). Библиотека предоставляет runtime-API уровня `nlohmann::json`, но все узлы хранятся в ПАП (file-backed образ), и поддерживает расширения:

- **`$ref`** — настоящие указатели на другие узлы (не просто строковые пути)
- **`$base64`** — бинарные данные (внутри — байтовый массив, при сериализации — base64)

**Ключевые архитектурные принципы:**

1. Все объекты в ПАП — только POD-структуры, доступ через смещения (`node_id`).
2. В ПАП ровно два типа строк:
   - **readonly (`pstringview_pmm`)** — интернированные, только накапливаются в словаре, используются как ключи `pmap_pmm` и пути `$ref`. Сравнение O(1). Нет SSO.
   - **readwrite (`pstring_pmm`)** — изменяемые строковые значения JSON (`node_tag::string`), могут модифицироваться на лету. Необходимы для совместимости с [jsonRVM](https://github.com/netkeep80/jsonRVM). Нет SSO.
3. Структура менеджера разделена на слои (PMM → primitives → json model → db manager).
4. `pjson_db_pmm.h` — единственный заголовок для конечного пользователя.
5. Никаких `.cpp`, никаких внешних зависимостей (кроме PMM).
6. Бэкенд ПАП — [PersistMemoryManager](https://github.com/netkeep80/PersistMemoryManager) (PMM).

---

## Текущая архитектура

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
│   Слой B: pstringview_pmm + pstring_pmm     │
│   + pmem_array_pmm + pvector_pmm + pmap_pmm │
│   (readonly/readwrite строки, массивы)       │
├─────────────────────────────────────────────┤
│   Слой A: PMM                               │
│   PersistMemoryManager: бэкенд ПАП          │
│   pam_pmm_config.h: конфигурация PamManager │
│   pam_adapter.h: pptr<T> ↔ uintptr_t        │
│   pam_pmm.h: фасад ПАМ на PMM              │
│   fptr_pmm.h: персистный указатель          │
│   pallocator_pmm.h: STL аллокатор           │
└─────────────────────────────────────────────┘
```

### Файлы проекта

| Файл | Слой | Описание |
|------|------|----------|
| `pam_pmm_config.h` | A | Конфигурация менеджера PMM: определяет `PamManager` |
| `pam_adapter.h` | A | Адаптер pptr<T> ↔ uintptr_t: `pptr_to_offset()`, `offset_to_pptr()`, `pmm_resolve<T>()` |
| `pmem_array_pmm.h` | A | Персистный массив: `pmem_array_hdr_pmm`, шаблонные функции `pmem_array_pmm_*` |
| `pvector_pmm.h` | A | Динамический массив: `pvector_pmm<T>` |
| `pmap_pmm.h` | A | Персистная карта: `pmap_pmm<K,V>` — sorted array |
| `pstring_pmm.h` | A | Персистная изменяемая строка: `pstring_pmm` — readwrite с `assign()`, `clear()` |
| `pstringview_pmm.h` | A | Интернированная строка: `pstringview_pmm` — адаптер для `pmm::pstringview`, O(1) сравнение |
| `pjson_pool_pmm.h` | C | Пул узлов: `pjson_pool_pmm` — аллокация O(1) через PMM + free-list |
| `pam_pmm.h` | A | Фасад ПАМ на PMM: `pam_pmm_init()`, `pam_pmm_create<T>()`, `pam_pmm_find()`, `pam_pmm_save()`, реестр именованных объектов |
| `fptr_pmm.h` | A | Персистный указатель: `fptr_pmm<T>` — `New()`, `NewArray()`, `Delete()`, `find()` |
| `pallocator_pmm.h` | A | STL-аллокатор: `pallocator_pmm<T>` — совместим с `std::vector<T, pallocator_pmm<T>>` |
| `deps/pmm/pmm.h` | A | [PersistMemoryManager](https://github.com/netkeep80/PersistMemoryManager) — бэкенд ПАП |
| `pjson_node.h` | C | Модель узлов JSON: `node_tag`, `node_id`, `node`, `node_view`, `object_entry`; функции init/set/assign/push_back/insert; итераторы; коды ошибок; глубокое копирование (`node_clone()`) |
| `pjson_codec.h` | C | Сериализация/десериализация: парсер/сериализатор для `node_id`-модели; `$ref`, `$base64`, Base64 кодек |
| `pjson_db_pmm.h` | D | Менеджер персистной JSON-БД: единственный заголовок для конечного пользователя; path-адресация, `put`/`get`/`erase`/`exists`, `$ref`, метрики (`db_metrics_pmm`), поиск по строкам, глубокое копирование |
| `pjson_db_helpers.h` | D | Вспомогательные функции для обхода дерева JSON |
| `main.cpp` | — | Демонстрационная программа |
| `tests/` | — | Тесты на Catch2 |
| `CMakeLists.txt` | — | Система сборки (CMake 3.16+, C++20) |

---

## Открытые вопросы

### В1. Бэкенд хранилища: fread/fwrite vs mmap

**Текущее состояние:** PMM использует `HeapStorage` (загружает весь образ в heap-память).

**Вопрос:** Нужен ли настоящий `mmap`?

- `mmap` даёт ленивую загрузку и экономит RAM для больших БД.
- `HeapStorage` проще и надёжнее (нет проблем с выравниванием на разных ОС).
- PMM поддерживает `MMapStorage` как альтернативный бэкенд.

### В2. Правила владения узлами при `$ref`

**Решение:** `ref` не владеет целевым узлом. Shared-узлы только через `$ref`. Удаление `ref`-узла не удаляет цель.

### В3. Производительность объектов (object storage)

**Текущее решение:** Sorted array (`pmap_pmm`) — O(log n) поиск, простота, нет хеш-коллизий.

**Альтернатива:** Hash map — O(1) поиск, но сложнее в ПАП. Может быть реализован в будущем при необходимости.

---

## Возможные направления развития

### Многопоточность

PMM поддерживает `MultiThreadedHeap` с `SharedMutexLock`. При необходимости многопоточного доступа к БД можно переключить конфигурацию:

```cpp
// pam_pmm_config.h
using PamManager = pmm::presets::MultiThreadedHeap;
```

### Расширение API

- Транзакционный API (batch put/erase с одним пересчётом метрик)
- Подписки на изменения (watch/notify)
- Индексация для быстрого поиска

### Интеграция с jsonRVM

`pstring_pmm`-узлы (`node_tag::string`) поддерживают изменение значения на месте через `assign()`. Это позволяет [jsonRVM](https://github.com/netkeep80/jsonRVM) работать непосредственно внутри базы данных.

---

## Требования

| Требование | Описание |
|---|---|
| Тр.1 | Персистные объекты используют только персистные указатели (смещения) |
| Тр.2 | Создание/удаление объектов — через методы аллокатора PMM |
| Тр.3 | При запуске аллокатор инициализируется именем файла-хранилища |
| Тр.4 | Единое ПАП для объектов всех типов |
| Тр.5 | `sizeof(fptr_pmm<T>) == sizeof(void*)` |
| Тр.6 | Все комментарии в коде — на русском языке |
| Тр.9 | Никакой логики с именами файлов в `fptr_pmm<T>` |
| Тр.10 | При загрузке образа ПАП конструкторы/деструкторы не вызываются |
| Тр.12 | Доступ к персистным объектам — только через `fptr_pmm<T>` или `node_id` |
| Тр.13 | `fptr_pmm<T>` может находиться как в обычной памяти, так и в ПАП |
| Тр.14 | ПАМ — персистный объект, хранит имена объектов и словарь строк |
| Тр.15 | `fptr_pmm<T>` инициализируется строковым именем объекта через ПАМ |
| Тр.16 | ПАМ хранит карту объектов, их имена и словарь интернированных строк |
| Тр.17 | Строки в ПАП не имеют SSO — `pstringview_pmm` и `pstring_pmm` хранятся через `chars_offset`, без inline-буферов |
| Тр.18 | `pjson_db_pmm.h` — единственный заголовок для конечного пользователя |
| Тр.19 | В ПАП ровно два типа строк: readonly (`pstringview_pmm`, только ключи/пути) и readwrite (`pstring_pmm`, только строковые значения JSON) |
| Тр.20 | `pstring_pmm`-узлы (`node_tag::string`) поддерживают изменение значения на месте для совместимости с [jsonRVM](https://github.com/netkeep80/jsonRVM) |

---

## Тесты

Все тесты используют Catch2. Текущий набор: 568 тестов, все проходят.

| Файл | Описание |
|------|----------|
| `test_pmm_basic.cpp` | Базовые тесты PMM и адаптера `pptr ↔ uintptr_t` |
| `test_pmem_array_pmm.cpp` | Тесты персистного массива и `pvector_pmm` |
| `test_pmap_pmm.cpp` | Тесты персистной карты (sorted array) |
| `test_pstring_pmm.cpp` | Тесты readwrite строк |
| `test_pstringview_pmm.cpp` | Тесты readonly строк и словаря |
| `test_pjson_pool_pmm.cpp` | Тесты пула узлов |
| `test_pjson_node.cpp` | Тесты модели узлов JSON и итераторов |
| `test_pjson_codec.cpp` | Тесты сериализации/десериализации, `$ref`, `$base64` |
| `test_pjson_serial.cpp` | Тесты round-trip сериализации |
| `test_pjson_db_pmm.cpp` | Тесты менеджера БД: put/get/erase, метрики, поиск, персистентность |
| `test_pjson_db_errors.cpp` | Тесты кодов ошибок и сообщений об ошибках |
| `test_pjson_db_perf.cpp` | Бенчмарки производительности (put, get, parse, erase) |
| `test_pjson_clone.cpp` | Тесты глубокого копирования узлов |
| `test_pjson.cpp` | Тесты модели узлов (node_id API) |
| `test_pjson_opt.cpp` | Оптимизационные тесты |
| `test_pjson_bench.cpp` | Бенчмарки парсинга/сериализации |
| `test_pjson_large.cpp` | Тесты на больших JSON-документах |
| `test_fptr_pmm.cpp` | Тесты персистного указателя |
| `test_pallocator_pmm.cpp` | Тесты STL-аллокатора |
| `test_pam_pmm.cpp` | Тесты фасада ПАМ |
| `test_pam.cpp` | Тесты ПАМ (реестр, слоты, строки) |
| `test_pam_dynamic.cpp` | Тесты динамических строк в ПАМ |
| `test_pam_metrics.cpp` | Тесты метрик ПАМ |
| `test_pam_perf.cpp` | Бенчмарки ПАМ |
| `test_pvector.cpp` | Тесты динамического массива |

---

## Производительность

Производительность `pjson_db_pmm` при работе с 10k–100k узлов:

| Операция | Кол-во | Время (ориентир) |
|---|---|---|
| `put(int)` | 10k | ~1–4 с |
| `put(string)` | 10k | ~1–4 с |
| `get()` | 100k запросов | < 50 мс |
| `parse_into(JSON)` | 1k объектов | < 100 мс |
| `erase()` | 10k | ~3–4 с |

**Примечание:** `put()` и `erase()` имеют накладные расходы O(depth) на обход пути при каждой операции.
Для массовой загрузки данных рекомендуется предварительно зарезервировать слоты через `ReserveSlots()`.
