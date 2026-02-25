# BinDiffSynchronizer

[English](#english) | [Русский](#русский)

---

<a name="русский"></a>
## Русский

### Описание

BinDiffSynchronizer — это C++ библиотека для бинарной дифференциальной синхронизации объектов. Проект предоставляет механизм автоматического отслеживания изменений объектов на уровне байтов и поддержку персистентного хранения данных.

Проект является фундаментом для разработки системы **jgit** — темпоральной базы данных для JSON-документов, аналогичной Git по модели версионирования, но специализированной для иерархических JSON-структур.

### Текущее состояние: Фаза 1 завершена ✓, Фаза 2 в процессе

Фаза 1 реализует минимальный жизнеспособный фундамент — компилируемую, кросс-платформенную, покрытую тестами кодовую базу с рабочим объектным хранилищем JSON-данных в бинарном формате.

Фаза 2 добавляет систему версионирования — коммиты, ветки, теги и историю изменений.

**Что реализовано в Фазе 1:**

| Задача | Статус |
|--------|--------|
| Исправление ошибок (`PageDevice.h`, `persist.h`) | ✓ Готово |
| Переход на C++17 с CMake | ✓ Готово |
| Интеграция nlohmann/json (CBOR) | ✓ Готово |
| Объектное хранилище с контентной адресацией | ✓ Готово |
| Unit-тесты (29 тестов, все проходят) | ✓ Готово |
| CI с GitHub Actions (Linux GCC/Clang, Windows MSVC) | ✓ Готово |

**Что реализовано в Фазе 2:**

| Задача | Статус |
|--------|--------|
| Система коммитов (`jgit/commit.h`) | ✓ Готово |
| Управление ветками и тегами (`jgit/refs.h`) | ✓ Готово |
| Высокоуровневый API репозитория (`jgit/repository.h`) | ✓ Готово |
| Unit-тесты (61 тест, все проходят) | ✓ Готово |
| Интеграция JSON Patch (RFC 6902) | В планах |
| Поддержка `$ref`-ссылок | В планах |
| CLI-интерфейс jgit | В планах |

### Основные возможности

- **Бинарная синхронизация** — отслеживание изменений объектов путём сравнения снимков памяти
- **Персистентное хранение** — автоматическое сохранение и загрузка объектов из файлов
- **Страничная организация памяти** — гибкая система кэширования с настраиваемой политикой
- **Система протоколов** — макросы для декларативного описания межобъектного взаимодействия
- **Объектное хранилище jgit** — content-addressed хранение JSON в бинарном формате CBOR

### Быстрый старт: Repository (jgit)

```cpp
#include "jgit/repository.h"
#include <nlohmann/json.hpp>

// Создать новый jgit репозиторий
auto repo = jgit::Repository::init("./my_repo");

// Сохранить первую версию JSON-документа
nlohmann::json v1 = {{"name", "Alice"}, {"age", 30}};
jgit::ObjectId cid1 = repo.commit(v1, "Initial commit", "alice");

// Сохранить обновлённую версию
nlohmann::json v2 = {{"name", "Alice"}, {"age", 31}};
jgit::ObjectId cid2 = repo.commit(v2, "Happy birthday!", "alice");

// Просмотреть историю
auto history = repo.log();
// history[0] → (cid2, Commit{message="Happy birthday!", parent=cid1, ...})
// history[1] → (cid1, Commit{message="Initial commit", parent=nullopt, ...})

// Восстановить данные из произвольного коммита
auto old_data = repo.get_data(cid1);
// old_data.value() == v1

// Вернуться к старой версии
repo.checkout(cid1);

// Работа с ветками
repo.create_branch("feature", cid2);
repo.switch_branch("feature");
repo.commit({{"name", "Alice"}, {"age", 31}, {"role", "admin"}}, "Add role", "alice");

// Теги
repo.create_tag("v1.0.0", cid1);
```

### Быстрый старт: ObjectStore (низкоуровневый API)

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
| `jgit/commit.h` | Объект коммита с сериализацией (`Commit`) |
| `jgit/refs.h` | Управление ссылками: HEAD, ветки, теги (`Refs`) |
| `jgit/repository.h` | Высокоуровневый API репозитория (`Repository`) |
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
- [План Фазы 2](phase2-plan.md) — детальный план реализации Phase 2 (Task 1 выполнен)

---

<a name="english"></a>
## English

### Description

BinDiffSynchronizer is a C++ library for binary differential object synchronization. The project provides a mechanism for automatic tracking of object changes at the byte level and support for persistent data storage.

The project serves as the foundation for developing **jgit** — a temporal database for JSON documents, similar to Git in its versioning model, but specialized for hierarchical JSON structures.

### Current Status: Phase 1 Complete ✓, Phase 2 In Progress

Phase 1 establishes the minimum viable foundation — a compilable, cross-platform, tested codebase with a working content-addressed object store for JSON data in binary format.

Phase 2 adds the temporal versioning layer — commits, branches, tags, checkout, and history log.

**What was implemented in Phase 1:**

| Task | Status |
|------|--------|
| Bug fixes (`PageDevice.h`, `persist.h`) | ✓ Done |
| Migration to C++17 with CMake | ✓ Done |
| nlohmann/json integration (CBOR) | ✓ Done |
| Content-addressed object store | ✓ Done |
| Unit tests (29 tests, all passing) | ✓ Done |
| CI with GitHub Actions (Linux GCC/Clang, Windows MSVC) | ✓ Done |

**What was implemented in Phase 2:**

| Task | Status |
|------|--------|
| Commit system (`jgit/commit.h`) | ✓ Done |
| Branch and tag management (`jgit/refs.h`) | ✓ Done |
| High-level repository API (`jgit/repository.h`) | ✓ Done |
| Unit tests (61 tests, all passing) | ✓ Done |
| JSON Patch integration (RFC 6902) | Planned |
| `$ref` link support | Planned |
| CLI interface for jgit | Planned |

### Key Features

- **Binary synchronization** — tracking object changes by comparing memory snapshots
- **Persistent storage** — automatic saving and loading of objects from files
- **Page-based memory organization** — flexible caching system with configurable policy
- **Protocol system** — macros for declarative description of inter-object interaction
- **jgit object store** — content-addressed JSON storage in binary CBOR format

### Quick Start: Repository (jgit)

```cpp
#include "jgit/repository.h"
#include <nlohmann/json.hpp>

// Initialize a new jgit repository
auto repo = jgit::Repository::init("./my_repo");

// Commit the first version of a JSON document
nlohmann::json v1 = {{"name", "Alice"}, {"age", 30}};
jgit::ObjectId cid1 = repo.commit(v1, "Initial commit", "alice");

// Commit an updated version
nlohmann::json v2 = {{"name", "Alice"}, {"age", 31}};
jgit::ObjectId cid2 = repo.commit(v2, "Happy birthday!", "alice");

// Browse history (newest first)
auto history = repo.log();
// history[0] → (cid2, Commit{message="Happy birthday!", parent=cid1, ...})
// history[1] → (cid1, Commit{message="Initial commit", parent=nullopt, ...})

// Retrieve data at any historical commit
auto old_data = repo.get_data(cid1);
// old_data.value() == v1

// Restore an older version (detaches HEAD)
repo.checkout(cid1);

// Branch management
repo.create_branch("feature", cid2);
repo.switch_branch("feature");
repo.commit({{"name", "Alice"}, {"age", 31}, {"role", "admin"}}, "Add role", "alice");

// Tags
repo.create_tag("v1.0.0", cid1);
```

### Quick Start: ObjectStore (low-level API)

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
| `jgit/commit.h` | Commit object with JSON serialization (`Commit`) |
| `jgit/refs.h` | HEAD, branch, and tag reference management (`Refs`) |
| `jgit/repository.h` | High-level repository API (`Repository`) |
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
- [Phase 2 Plan](phase2-plan.md) — detailed Phase 2 implementation plan (Task 1 completed)

---

## Лицензия / License

Unlicense
