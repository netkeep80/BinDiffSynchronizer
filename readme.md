# BinDiffSynchronizer

[English](#english) | [Русский](#русский)

---

<a name="русский"></a>
## Русский

### Описание

BinDiffSynchronizer — это C++ библиотека для бинарной дифференциальной синхронизации объектов. Проект предоставляет механизм автоматического отслеживания изменений объектов на уровне байтов и поддержку персистентного хранения данных.

Проект является фундаментом для разработки системы **jgit** — темпоральной базы данных для JSON-документов, аналогичной Git по модели версионирования, но специализированной для иерархических JSON-структур.

### Текущее состояние: Фаза 1 завершена ✓ · Фаза 2 завершена ✓ · Фаза 3 в процессе 🚀

Фаза 1 реализует минимальный жизнеспособный фундамент — компилируемую, кросс-платформенную, покрытую тестами кодовую базу с рабочим объектным хранилищем JSON-данных в бинарном формате.

**Что реализовано в Фазе 1:**

| Задача | Статус |
|--------|--------|
| Исправление ошибок (`PageDevice.h`, `persist.h`) | ✓ Готово |
| Переход на C++17 с CMake | ✓ Готово |
| Интеграция nlohmann/json (CBOR) | ✓ Готово |
| Объектное хранилище с контентной адресацией | ✓ Готово |
| Unit-тесты (29 тестов, все проходят) | ✓ Готово |
| CI с GitHub Actions (Linux GCC/Clang, Windows MSVC) | ✓ Готово |

**Прогресс Фазы 2 — Персистентное дерево объектов JSON:**

| Задача | Статус |
|--------|--------|
| 2.1: Исследование осуществимости: `persist<T>` для std-классов | ✓ Готово |
| 2.2: Проектирование персистентных аналогов std-типов | ✓ Готово |
| 2.3: Проектирование `jgit::persistent_json_value` | ✓ Готово |
| 2.4: Реализация `jgit::PersistentJsonStore` | ✓ Готово |
| 2.5: Интеграция с ObjectStore (Фаза 1) | ✓ Готово |
| 2.6: Unit-тесты (109 тестов, CI зелёный на GCC/Clang/MSVC) | ✓ Готово |
| 2.7: Сравнительный анализ производительности | ✓ Готово |

**Прогресс Фазы 3 — Завершение персистной обвязки и интеграция с nlohmann::json:**

| Задача | Статус |
|--------|--------|
| 3.1: Доработка, документация и тестирование персистной инфраструктуры (`persist<T>`, `fptr<T>`, `AddressManager`, `Cache`, `PageDevice`, `MemoryDevice`, `StaticPageDevice`) | ✓ Готово (161 тест, CI зелёный) |
| 3.2: Переписать `persistent_string`, `persistent_map`, `persistent_json_value`, `PersistentJsonStore` на `persist<T>` и `fptr<T>` | ✓ Готово (185 тестов, CI зелёный) |
| 3.3: Инстанцировать `nlohmann::basic_json<>` с персистными классами | 🔜 Запланировано |
| 3.4: Интеграционные тесты и CI | 🔜 Запланировано |
| 3.5: Производительность и бенчмарки | 🔜 Запланировано |

### Основные возможности

- **Бинарная синхронизация** — отслеживание изменений объектов путём сравнения снимков памяти
- **Персистентное хранение** — автоматическое сохранение и загрузка объектов из файлов
- **Страничная организация памяти** — гибкая система кэширования с настраиваемой политикой
- **Система протоколов** — макросы для декларативного описания межобъектного взаимодействия
- **Объектное хранилище jgit** — content-addressed хранение JSON в бинарном формате CBOR
- **Система коммитов jgit** — история версий JSON-документов с ветками и checkout

### Быстрый старт: ObjectStore (jgit)

```cpp
#include "jgit/object_store.h"
#include <nlohmann/json.hpp>

// Создать новый jgit репозиторий
auto store = jgit::ObjectStore::init("./my_repo");

// Сохранить JSON-объект
nlohmann::json doc = {{"name", "Alice"}, {"age", 30}};
jgit::ObjectId id = store.put(doc);

// Получить объект по хешу
auto retrieved = store.get(id);
// retrieved.value() == doc

// Проверить существование
bool exists = store.exists(id);  // true
```

### Быстрый старт: PersistentJsonStore + интеграция с ObjectStore (jgit)

```cpp
#include "jgit/persistent_json_store.h"
#include "jgit/object_store.h"

// Открыть (или создать) ObjectStore для хранения снимков
auto obj_store = jgit::ObjectStore::init("./my_repo");

// Рабочее дерево: импортировать JSON в PersistentJsonStore
jgit::PersistentJsonStore pjs;
nlohmann::json doc = {{"version", 1}, {"data", {1, 2, 3}}};
uint32_t root_id = pjs.import_json(doc);

// Редактировать узел напрямую (без парсинга JSON)
pjs.set_node(root_id, jgit::persistent_json_value::make_int(42));

// Создать снимок: рабочее дерево → CBOR → ObjectStore (аналог "git commit")
jgit::ObjectId snapshot_id = pjs.snapshot(root_id, obj_store);

// Восстановить из снимка в новом экземпляре (аналог "git checkout")
jgit::PersistentJsonStore pjs2;
uint32_t restored_id = pjs2.restore(snapshot_id, obj_store);
nlohmann::json restored = pjs2.export_json(restored_id);
```

### Быстрый старт: Repository — система коммитов (jgit)

```cpp
#include "jgit/repository.h"

// Создать новый репозиторий
auto repo = jgit::Repository::init("./my_repo");

// Записать первую версию документа
nlohmann::json v1 = {{"name", "Alice"}, {"version", 1}};
jgit::ObjectId c1 = repo.commit(v1, "Начальный коммит", "Alice");

// Изменить документ и записать новую версию
nlohmann::json v2 = {{"name", "Alice"}, {"version", 2}};
jgit::ObjectId c2 = repo.commit(v2, "Обновление версии", "Alice");

// Просмотр истории (от новых к старым)
for (const jgit::Commit& c : repo.log()) {
    // c.id.hex, c.message, c.author, c.timestamp
}

// Восстановить любую историческую версию
nlohmann::json old_doc = repo.checkout(c1);  // old_doc == v1

// Работа с ветками
repo.switch_branch("feature");
repo.commit({{"draft", true}}, "Черновик", "Bob");
repo.switch_branch("main");   // история main не изменилась
```

### Концепция jgit

Ключевая идея развития проекта — создание системы **jgit**: темпоральной базы данных для JSON, где:

- Каждый JSON-файл является отдельным **версионируемым репозиторием**
- Поддерживаются **ветки**, **теги** и **`$ref`-ссылки** для перекрёстных ссылок между репозиториями
- Данные хранятся в **бинарном формате** (CBOR/MessagePack через [nlohmann/json](https://github.com/nlohmann/json))
- Изменения представлены в виде **JSON Patch** (RFC 6902) — дельты, вычисляемые с помощью `BinDiffSynchronizer`

```
Git                     jgit
──────────────────────  ─────────────────────────────────────
blob / tree / commit    json-blob / json-tree / json-commit
объектное хранилище     бинарное хранилище (PageDevice + CBOR)
ветки / теги            ветки / теги для JSON-документа
diff (текстовый)        JSON Patch (RFC 6902)
merge                   JSON Merge Patch (RFC 7396)
submodules              $ref-ссылки между репозиториями
```

### Ключевые концепции

**Персистентный указатель vs указатель на персистентный объект:**

- Персистентный указатель (`fptr<T>`) — указатель, который сам хранится в персистентном хранилище
- Указатель на персистентный объект — обычный указатель, ссылающийся на объект в персистентном хранилище

**Создание и удаление:**

Создание и удаление персистентных объектов не отличается от обычных объектов — они создаются статически или динамически в обычной памяти. Управлением объектами в персистентном хранилище занимаются менеджеры адресных пространств (`AddressManager`), которые работают с различными физическими носителями: диск, сеть, БД и др.

### Компоненты

| Файл | Описание |
|------|----------|
| `BinDiffSynchronizer.h` | Основной класс для отслеживания изменений на уровне байтов |
| `persist.h` | Персистентные объекты (`persist<T>`, `fptr<T>`) и менеджер адресов |
| `PageDevice.h` | Страничное устройство с кэшированием (Policy-based design) |
| `StaticPageDevice.h` | Статическая реализация страничного устройства (in-memory) |
| `Protocol.h` | Макросы для создания протоколов межобъектного взаимодействия |
| `jgit/hash.h` | SHA-256 контентная адресация (`ObjectId`, `hash_object`) |
| `jgit/serialization.h` | JSON ↔ CBOR сериализация (`to_bytes`, `from_bytes`) |
| `jgit/object_store.h` | Объектное хранилище с контентной адресацией (`ObjectStore`) |
| `jgit/persistent_string.h` | Персистентная строка (`persistent_string`) — SSO + фиксированный буфер, совместима с `persist<T>` |
| `jgit/persistent_array.h` | Персистентный массив (`persistent_array<T>`) — фиксированный слэб с поддержкой цепочки слэбов |
| `jgit/persistent_map.h` | Персистентная карта (`persistent_map<V>`) — отсортированный массив пар ключ-значение |
| `jgit/persistent_json_value.h` | Персистентный узел JSON (`persistent_json_value`) — дискриминированный union всех 7 типов JSON, совместим с `persist<T>` |
| `jgit/persistent_json_store.h` | Хранилище JSON-дерева (`PersistentJsonStore`) — три плоских пула фиксированного размера, импорт/экспорт `nlohmann::json`; интеграция с `ObjectStore` через `snapshot()`/`restore()` |
| `jgit/commit.h` | Структура коммита (`Commit`) — метаданные версии: id, root_snapshot, parent_id, author, timestamp, message |
| `jgit/repository.h` | Репозиторий jgit (`Repository`) — система коммитов с `commit()`, `log()`, `checkout()`, управлением ветками |
| `third_party/nlohmann/json.hpp` | nlohmann/json v3.11.3 — JSON для Modern C++ |
| `third_party/sha256.hpp` | SHA-256 (public domain, single header) |

### Требования

- **Компилятор**: GCC 7+, Clang 5+, MSVC 2017+ (C++17)
- **Система сборки**: CMake 3.16+
- **Зависимости**: автоматически загружаются через CMake FetchContent (Catch2)

### Сборка

```bash
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

### Документация

- [Анализ проекта](analysis.md) — подробный анализ сильных и слабых сторон, оценка концепции jgit и интеграции с nlohmann/json
- [План развития](plan.md) — перспективные направления и задачи, детальный план реализации jgit
- [План Фазы 1](phase1-plan.md) — детальный план реализации Phase 1 (выполнен)
- [План Фазы 2](phase2-plan.md) — план Фазы 2: персистентное дерево объектов JSON с использованием `persist<T>` и `fptr<T>` (выполнен)
- [План Фазы 3](phase3-plan.md) — план Фазы 3: завершение персистной обвязки и интеграция с nlohmann::json (Задача 3.1 завершена)

---

<a name="english"></a>
## English

### Description

BinDiffSynchronizer is a C++ library for binary differential object synchronization. The project provides a mechanism for automatic tracking of object changes at the byte level and support for persistent data storage.

The project serves as the foundation for developing **jgit** — a temporal database for JSON documents, similar to Git in its versioning model, but specialized for hierarchical JSON structures.

### Current Status: Phase 1 Complete ✓ · Phase 2 Complete ✓ · Phase 3 In Progress 🚀

Phase 1 establishes the minimum viable foundation — a compilable, cross-platform, tested codebase with a working content-addressed object store for JSON data in binary format.

**What was implemented in Phase 1:**

| Task | Status |
|------|--------|
| Bug fixes (`PageDevice.h`, `persist.h`) | ✓ Done |
| Migration to C++17 with CMake | ✓ Done |
| nlohmann/json integration (CBOR) | ✓ Done |
| Content-addressed object store | ✓ Done |
| Unit tests (29 tests, all passing) | ✓ Done |
| CI with GitHub Actions (Linux GCC/Clang, Windows MSVC) | ✓ Done |

**Phase 2 Progress — Persistent JSON Object Tree:**

| Task | Status |
|------|--------|
| 2.1: Feasibility study: `persist<T>` for std classes | ✓ Done |
| 2.2: Design persistent analogs of std types | ✓ Done |
| 2.3: Design `jgit::persistent_json_value` | ✓ Done |
| 2.4: Implement `jgit::PersistentJsonStore` | ✓ Done |
| 2.5: Integration with ObjectStore (Phase 1) | ✓ Done |
| 2.6: Unit tests (109 tests, CI green on GCC/Clang/MSVC) | ✓ Done |
| 2.7: Performance benchmark | ✓ Done |

**Phase 3 Progress — Completing Persistence Layer and nlohmann::json Integration:**

| Task | Status |
|------|--------|
| 3.1: Fix, document and test persistence infrastructure (`persist<T>`, `fptr<T>`, `AddressManager`, `Cache`, `PageDevice`, `MemoryDevice`, `StaticPageDevice`) | ✓ Done (161 tests, CI green) |
| 3.2: Rewrite `persistent_string`, `persistent_map`, `persistent_json_value`, `PersistentJsonStore` using `persist<T>` and `fptr<T>` | ✓ Done (185 tests, CI green) |
| 3.3: Instantiate `nlohmann::basic_json<>` with persistent classes | 🔜 Planned |
| 3.4: Integration tests and CI | 🔜 Planned |
| 3.5: Performance benchmarks | 🔜 Planned |

### Key Features

- **Binary synchronization** — tracking object changes by comparing memory snapshots
- **Persistent storage** — automatic saving and loading of objects from files
- **Page-based memory organization** — flexible caching system with configurable policy
- **Protocol system** — macros for declarative description of inter-object interaction
- **jgit object store** — content-addressed JSON storage in binary CBOR format
- **jgit commit system** — full version history of JSON documents with branches and checkout

### Quick Start: ObjectStore (jgit)

```cpp
#include "jgit/object_store.h"
#include <nlohmann/json.hpp>

// Initialize a new jgit repository
auto store = jgit::ObjectStore::init("./my_repo");

// Store a JSON object
nlohmann::json doc = {{"name", "Alice"}, {"age", 30}};
jgit::ObjectId id = store.put(doc);

// Retrieve the object by hash
auto retrieved = store.get(id);
// retrieved.value() == doc

// Check existence
bool exists = store.exists(id);  // true
```

### Quick Start: PersistentJsonStore + ObjectStore integration (jgit)

```cpp
#include "jgit/persistent_json_store.h"
#include "jgit/object_store.h"

// Open (or create) an ObjectStore for immutable snapshots
auto obj_store = jgit::ObjectStore::init("./my_repo");

// Working tree: import JSON into PersistentJsonStore
jgit::PersistentJsonStore pjs;
nlohmann::json doc = {{"version", 1}, {"data", {1, 2, 3}}};
uint32_t root_id = pjs.import_json(doc);

// Edit a node directly (zero-parse access)
pjs.set_node(root_id, jgit::persistent_json_value::make_int(42));

// Snapshot: working tree → CBOR → ObjectStore (analogous to "git commit")
jgit::ObjectId snapshot_id = pjs.snapshot(root_id, obj_store);

// Restore from snapshot in a new instance (analogous to "git checkout")
jgit::PersistentJsonStore pjs2;
uint32_t restored_id = pjs2.restore(snapshot_id, obj_store);
nlohmann::json restored = pjs2.export_json(restored_id);
```

### Quick Start: Repository — Commit System (jgit)

```cpp
#include "jgit/repository.h"

// Create a new repository
auto repo = jgit::Repository::init("./my_repo");

// Commit the first version of a document
nlohmann::json v1 = {{"name", "Alice"}, {"version", 1}};
jgit::ObjectId c1 = repo.commit(v1, "Initial commit", "Alice");

// Update the document and commit a new version
nlohmann::json v2 = {{"name", "Alice"}, {"version", 2}};
jgit::ObjectId c2 = repo.commit(v2, "Update version", "Alice");

// Walk history (newest first)
for (const jgit::Commit& c : repo.log()) {
    // c.id.hex, c.message, c.author, c.timestamp
}

// Restore any historical version
nlohmann::json old_doc = repo.checkout(c1);  // old_doc == v1

// Branch management
repo.switch_branch("feature");
repo.commit({{"draft", true}}, "Draft", "Bob");
repo.switch_branch("main");   // main history is unchanged
```

### The jgit Concept

The key idea for developing this project is to create **jgit**: a temporal database for JSON, where:

- Each JSON file is a separate **versioned repository**
- **Branches**, **tags**, and **`$ref` links** are supported for cross-repository references
- Data is stored in **binary format** (CBOR/MessagePack via [nlohmann/json](https://github.com/nlohmann/json))
- Changes are represented as **JSON Patch** (RFC 6902) — deltas computed using `BinDiffSynchronizer`

```
Git                     jgit
──────────────────────  ─────────────────────────────────────
blob / tree / commit    json-blob / json-tree / json-commit
object store            binary store (PageDevice + CBOR)
branches / tags         branches / tags for JSON document
diff (text)             JSON Patch (RFC 6902)
merge                   JSON Merge Patch (RFC 7396)
submodules              $ref links between repositories
```

### Key Concepts

**Persistent pointer vs pointer to persistent object:**

- Persistent pointer (`fptr<T>`) — a pointer that is itself stored in persistent storage
- Pointer to persistent object — a regular pointer referencing an object in persistent storage

**Creation and deletion:**

Creation and deletion of persistent objects is no different from regular objects — they are created statically or dynamically in regular memory. Management of objects in persistent storage is handled by address space managers (`AddressManager`), which work with various physical media: disk, network, database, etc.

### Components

| File | Description |
|------|-------------|
| `BinDiffSynchronizer.h` | Main class for byte-level change tracking |
| `persist.h` | Persistent objects (`persist<T>`, `fptr<T>`) and address manager |
| `PageDevice.h` | Page device with caching (Policy-based design) |
| `StaticPageDevice.h` | Static implementation of page device (in-memory) |
| `Protocol.h` | Macros for creating inter-object interaction protocols |
| `jgit/hash.h` | SHA-256 content addressing (`ObjectId`, `hash_object`) |
| `jgit/serialization.h` | JSON ↔ CBOR serialization (`to_bytes`, `from_bytes`) |
| `jgit/object_store.h` | Content-addressed object store (`ObjectStore`) |
| `jgit/persistent_string.h` | Persistent string (`persistent_string`) — SSO + fixed buffer, compatible with `persist<T>` |
| `jgit/persistent_array.h` | Persistent array (`persistent_array<T>`) — fixed-capacity slab with multi-slab chaining |
| `jgit/persistent_map.h` | Persistent map (`persistent_map<V>`) — sorted array of key-value pairs |
| `jgit/persistent_json_value.h` | Persistent JSON node (`persistent_json_value`) — discriminated union of all 7 JSON types, compatible with `persist<T>` |
| `jgit/persistent_json_store.h` | Persistent JSON tree store (`PersistentJsonStore`) — three flat fixed-size pools, import/export `nlohmann::json`; integration with `ObjectStore` via `snapshot()`/`restore()` |
| `jgit/commit.h` | Commit structure (`Commit`) — version metadata: id, root_snapshot, parent_id, author, timestamp, message |
| `jgit/repository.h` | jgit repository (`Repository`) — commit system with `commit()`, `log()`, `checkout()`, branch management |
| `third_party/nlohmann/json.hpp` | nlohmann/json v3.11.3 — JSON for Modern C++ |
| `third_party/sha256.hpp` | SHA-256 (public domain, single header) |

### Requirements

- **Compiler**: GCC 7+, Clang 5+, MSVC 2017+ (C++17)
- **Build system**: CMake 3.16+
- **Dependencies**: automatically fetched via CMake FetchContent (Catch2)

### Build

```bash
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

### Documentation

- [Project Analysis](analysis.md) — detailed analysis of strengths and weaknesses, evaluation of the jgit concept and nlohmann/json integration
- [Development Plan](plan.md) — promising directions and tasks, detailed jgit implementation plan
- [Phase 1 Plan](phase1-plan.md) — detailed Phase 1 implementation plan (completed)
- [Phase 2 Plan](phase2-plan.md) — Phase 2 plan: persistent JSON object tree using `persist<T>` and `fptr<T>` (completed)
- [Phase 3 Plan](phase3-plan.md) — Phase 3 plan: completing persistence layer and nlohmann::json integration (Task 3.1 done)

---

## Лицензия / License

Unlicense
