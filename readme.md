# BinDiffSynchronizer

[English](#english) | [Русский](#русский)

---

## Русский

### Описание

BinDiffSynchronizer — это C++ библиотека для бинарной дифференциальной синхронизации объектов. Проект предоставляет механизм автоматического отслеживания изменений объектов на уровне байтов и поддержку персистентного хранения данных.

### Основные возможности

- **Бинарная синхронизация** — отслеживание изменений объектов путём сравнения снимков памяти
- **Персистентное хранение** — автоматическое сохранение и загрузка объектов из файлов
- **Страничная организация памяти** — гибкая система кэширования с настраиваемой политикой
- **Система протоколов** — макросы для декларативного описания межобъектного взаимодействия

### Ключевые концепции

**Персистентный указатель vs указатель на персистентный объект:**

- Персистентный указатель (`fptr<T>`) — указатель, который сам хранится в персистентном хранилище
- Указатель на персистентный объект — обычный указатель, ссылающийся на объект в персистентном хранилище

**Создание и удаление:**

Создание и удаление персистентных объектов не отличается от обычных объектов — они создаются статически или динамически в обычной памяти. Управлением объектами в персистентном хранилище занимаются менеджеры адресных пространств (`AddressManager`), которые работают с различными физическими носителями: диск, сеть, БД и др.

### Компоненты

| Файл | Описание |
|------|----------|
| `BinDiffSynchronizer.h` | Основной класс для отслеживания изменений |
| `persist.h` | Персистентные объекты и менеджер адресов |
| `PageDevice.h` | Страничное устройство с кэшированием |
| `StaticPageDevice.h` | Статическая реализация страничного устройства |
| `Protocol.h` | Макросы для создания протоколов |

### Требования

- Visual Studio 2008 или новее
- Windows (для текущей реализации)

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

*value += 1.0;  // Изменения автоматически сохраняются
```

### Документация

- [Анализ проекта](analysis.md) — подробный анализ сильных и слабых сторон
- [План развития](plan.md) — перспективные направления и задачи

---

## English

### Description

BinDiffSynchronizer is a C++ library for binary differential object synchronization. The project provides a mechanism for automatic tracking of object changes at the byte level and support for persistent data storage.

### Key Features

- **Binary synchronization** — tracking object changes by comparing memory snapshots
- **Persistent storage** — automatic saving and loading of objects from files
- **Page-based memory organization** — flexible caching system with configurable policy
- **Protocol system** — macros for declarative description of inter-object interaction

### Key Concepts

**Persistent pointer vs pointer to persistent object:**

- Persistent pointer (`fptr<T>`) — a pointer that is itself stored in persistent storage
- Pointer to persistent object — a regular pointer referencing an object in persistent storage

**Creation and deletion:**

Creation and deletion of persistent objects is no different from regular objects — they are created statically or dynamically in regular memory. Management of objects in persistent storage is handled by address space managers (`AddressManager`), which work with various physical media: disk, network, database, etc.

### Components

| File | Description |
|------|-------------|
| `BinDiffSynchronizer.h` | Main class for tracking changes |
| `persist.h` | Persistent objects and address manager |
| `PageDevice.h` | Page device with caching |
| `StaticPageDevice.h` | Static implementation of page device |
| `Protocol.h` | Macros for creating protocols |

### Requirements

- Visual Studio 2008 or later
- Windows (for current implementation)

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

*value += 1.0;  // Changes are automatically saved
```

### Documentation

- [Project Analysis](analysis.md) — detailed analysis of strengths and weaknesses
- [Development Plan](plan.md) — promising directions and tasks

---

## License

This project is open source. See the repository for license details.
