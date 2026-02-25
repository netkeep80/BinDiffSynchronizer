# BinDiffSynchronizer

[English](#english) | [Русский](#русский)

---

<a name="русский"></a>
## Русский

### Описание

BinDiffSynchronizer — это C++ библиотека для бинарной дифференциальной синхронизации объектов. Проект предоставляет механизм автоматического отслеживания изменений объектов на уровне байтов и поддержку персистентного хранения данных.

Проект является фундаментом для разработки системы **jgit** — темпоральной базы данных для JSON-документов, аналогичной Git по модели версионирования, но специализированной для иерархических JSON-структур.

### Основные возможности

- **Бинарная синхронизация** — отслеживание изменений объектов путём сравнения снимков памяти
- **Персистентное хранение** — автоматическое сохранение и загрузка объектов из файлов
- **Страничная организация памяти** — гибкая система кэширования с настраиваемой политикой
- **Система протоколов** — макросы для декларативного описания межобъектного взаимодействия

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

### Требования

- Visual Studio 2008 или новее (текущая версия)
- Windows (для текущей реализации)
- *В разработке*: кросс-платформенная поддержка через CMake (Linux, macOS)

### Использование

```cpp
#include "persist.h"
#include "BinDiffSynchronizer.h"

// Создание персистентного указателя
fptr<double> value("my_value");

if (value == NULL) {
    value.New("my_value");
    *value = 0.0;
}

*value += 1.0;  // Изменения автоматически сохраняются при выходе из области видимости
```

### Документация

- [Анализ проекта](analysis.md) — подробный анализ сильных и слабых сторон, оценка концепции jgit и интеграции с nlohmann/json
- [План развития](plan.md) — перспективные направления и задачи, детальный план реализации jgit

---

<a name="english"></a>
## English

### Description

BinDiffSynchronizer is a C++ library for binary differential object synchronization. The project provides a mechanism for automatic tracking of object changes at the byte level and support for persistent data storage.

The project serves as the foundation for developing **jgit** — a temporal database for JSON documents, similar to Git in its versioning model, but specialized for hierarchical JSON structures.

### Key Features

- **Binary synchronization** — tracking object changes by comparing memory snapshots
- **Persistent storage** — automatic saving and loading of objects from files
- **Page-based memory organization** — flexible caching system with configurable policy
- **Protocol system** — macros for declarative description of inter-object interaction

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

### Requirements

- Visual Studio 2008 or later (current version)
- Windows (for current implementation)
- *In development*: cross-platform support via CMake (Linux, macOS)

### Usage

```cpp
#include "persist.h"
#include "BinDiffSynchronizer.h"

// Create a persistent pointer
fptr<double> value("my_value");

if (value == NULL) {
    value.New("my_value");
    *value = 0.0;
}

*value += 1.0;  // Changes are automatically saved when going out of scope
```

### Documentation

- [Project Analysis](analysis.md) — detailed analysis of strengths and weaknesses, evaluation of the jgit concept and nlohmann/json integration
- [Development Plan](plan.md) — promising directions and tasks, detailed jgit implementation plan

---

## Лицензия

Unlicense
