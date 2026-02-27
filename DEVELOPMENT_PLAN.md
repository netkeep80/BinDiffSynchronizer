# Development Plan: Phase 1 — Persistent Infrastructure for pjson

## Overview

This document describes the first phase of preparing the project for implementing `pjson` — a persistent analogue of `nlohmann::json`. The goal is to build a solid, well-tested persistent infrastructure that can eventually support the data structures required by a JSON value type.

---

## Background: The Problem

`nlohmann::json` uses a discriminated union internally:

```cpp
union json_value {
    object_t*        object;
    array_t*         array;
    string_t*        string;
    binary_t*        binary;
    boolean_t        boolean;
    number_integer_t number_integer;
    number_unsigned_t number_unsigned;
    number_float_t   number_float;
};
```

To implement a persistent analogue (`pjson`), we need:
1. Persistent strings (`pstring`) — to replace `string_t*`
2. Persistent vectors (`pvector<T>`) — to replace `array_t*`
3. Persistent maps (`pmap<K, V>`) — to replace `object_t*`
4. A persistent allocator (`pallocator<T>`) — to integrate with STL-compatible containers

All of these must live in a **single persistent address space** managed by `AddressManager`.

---

## Phase 1 Tasks

### Task 1: Refine, Document, and Test Core Infrastructure

#### 1.1 `persist<T>` (persist.h)
- **Status**: Implemented and documented.
- **Constraint**: `T` must be `std::is_trivially_copyable`.
- **Files**: Saves raw `sizeof(T)` bytes to a named file.
- **Issue**: Address-derived filename depends on ASLR — only the named constructors provide stable persistence across process restarts.
- **Action**: Add unit tests verifying save/load roundtrip.

#### 1.2 `AddressManager<T, AddressSpace>` (persist.h)
- **Status**: Implemented and documented.
- **Supports**: Single-object and array allocation via `Create()` / `CreateArray()`.
- **Slot 0**: Reserved as null/invalid.
- **Files**: Single objects go into `<typename>.extend`; arrays into `<typename>_arr_<index>.extend`.
- **Action**: Add unit tests for Create/Delete, CreateArray/DeleteArray, Find, and persistence across manager restart.

#### 1.3 `fptr<T>` (persist.h)
- **Status**: Implemented. Trivially copyable (stores only a `uint` slot index).
- **Operations**: `New()`, `NewArray()`, `Delete()`, `DeleteArray()`, `operator*`, `operator->`, `operator[]`, `count()`, `addr()`, `set_addr()`.
- **Action**: Add unit tests.

#### 1.4 `Cache<T, CacheSize, SpaceSize>` (PageDevice.h)
- **Status**: Implemented. Virtual `Load`/`Save`. `Flush()` saves dirty pages.
- **Note**: `Flush()` must be called by the concrete leaf destructor (not by `Cache::~Cache()`).
- **Action**: Add unit tests using `StaticPageDevice`.

#### 1.5 `PageDevice<PageSize, PoolSize, SpaceSize, CachePolicy>` (PageDevice.h)
- **Status**: Implemented. Abstract base for paged storage.
- **Action**: Document template parameters.

#### 1.6 `MemoryDevice<MemorySize, PageSize, PoolSize, CachePolicy, PageDevice>` (PageDevice.h)
- **Status**: Implemented. `Read()`/`Write()` byte ranges across page boundaries.
- **Action**: Add unit tests for cross-page reads/writes.

#### 1.7 `StaticPageDevice` (StaticPageDevice.h)
- **Status**: Implemented. In-memory page store. Calls `Flush()` in its destructor.
- **Action**: Use as test backend; verify Flush behaviour.

---

### Task 2: Implement Persistent Container Classes

#### 2.1 `pstring` — Persistent String

A persistent string stores its character data in the `AddressManager<char>` address space. The string header (length + pointer slot) is itself a trivially-copyable struct usable with `persist<>`.

**Design**:
```cpp
struct pstring_data {
    unsigned  length;     // number of characters (excluding NUL)
    fptr<char> chars;     // slot index into AddressManager<char>
};
// pstring_data is trivially copyable → can be used with persist<pstring_data>
```

**Operations**: `assign(const char*)`, `c_str()`, `size()`, `operator==`, `operator[]`, `clear()`.

**Tests**: Construct, assign, reload after process restart, compare.

#### 2.2 `pvector<T>` — Persistent Vector

A persistent vector stores its element array in `AddressManager<T>`. The header (size, capacity, data slot) is trivially-copyable.

**Design**:
```cpp
template<typename T>
struct pvector_data {
    unsigned   size;       // current number of elements
    unsigned   capacity;   // allocated capacity
    fptr<T>    data;       // slot index into AddressManager<T>
};
static_assert(std::is_trivially_copyable<pvector_data<T>>::value);
```

**Operations**: `push_back()`, `pop_back()`, `operator[]`, `size()`, `capacity()`, `clear()`, `begin()`/`end()` iterators.

**Tests**: Push/pop, index access, reload after restart, iteration.

#### 2.3 `pmap<K, V>` — Persistent Map

A persistent map stores key-value pairs in a sorted array (for simplicity in Phase 1). Uses `pvector` internally.

**Design**:
```cpp
template<typename K, typename V>
struct pmap_entry {
    K key;
    V value;
};
// pmap<K,V> wraps pvector<pmap_entry<K,V>>
```

**Operations**: `insert(K, V)`, `find(K)`, `erase(K)`, `size()`, `operator[]`, `begin()`/`end()`.

**Constraint**: `K` and `V` must be trivially copyable for storage in `AddressManager`.

**Tests**: Insert, find, erase, reload after restart.

#### 2.4 `pallocator<T>` — Persistent STL Allocator

- **Status**: Implemented, bug fixed, and tested.

An STL-compatible allocator backed by `AddressManager<T>`. Allows passing persistent containers to standard library algorithms.

**Design**:
```cpp
template<typename T>
class pallocator {
public:
    using value_type = T;
    T* allocate(std::size_t n);
    void deallocate(T* p, std::size_t n);
};
```

**Bug fixed**: `deallocate()` was accessing `AddressManager<T>::__itable` directly, which is a private member. Fixed by adding a public static `FindByPtr(_T* p)` method to `AddressManager` that encapsulates the reverse-lookup from raw pointer to slot index.

**Tests**: `tests/test_pallocator.cpp` — type aliases, rebind, construct/destroy, allocate/deallocate, `std::vector` integration, no-alias, nullptr safety.

**Limitation**: Standard STL containers (`std::basic_string`, `std::vector`, `std::map`) require allocators that return raw pointers into a flat, uniform address space. Since `AddressManager` uses slot indices (not raw pointers), direct use of `pallocator` with `std::basic_json` is not possible without translating slot indices to pointers. This is handled at the `pjson` level (see Phase 2).

---

### Task 3: pjson Design Decision

After implementing `pstring`, `pvector`, and `pmap`, we have two options:

**Option A: Instantiate `nlohmann::basic_json` with persistent types**
- `nlohmann::basic_json<pmap, pvector, pstring, bool, int64_t, uint64_t, double, pallocator>`
- This requires the custom types to match exactly the interface contracts expected by `nlohmann::json`.
- **Risk**: `nlohmann::json` assumes its contained types live in the normal heap. Persistent pointers are slot indices, not real C++ pointers, so raw-pointer-based operations inside `nlohmann::json` will likely break.

**Option B: Write a custom `pjson`**
- Implement `pjson` as a persistent discriminated union directly using `fptr<pstring>`, `fptr<pvector<pjson>>`, `fptr<pmap<pstring, pjson>>`.
- **Advantage**: Full control; no dependency on `nlohmann`'s internal pointer model.
- **Recommended** for Phase 1 → Phase 2 transition.

**Decision**: Option B chosen. `nlohmann::basic_json` internally dereferences raw C++ pointers and assumes heap allocation, which is incompatible with the slot-index-based `AddressManager`. A custom `pjson` was implemented directly on top of `fptr<T>`, `pstring`, `pvector`, and `pmap` primitives.

#### pjson implementation summary

`pjson_data` is a trivially-copyable 16-byte struct containing:
- a `pjson_type` discriminant (4 bytes),
- a payload union holding either a primitive value (bool, int64, uint64, double) or two unsigned integers (length/size + slot index) for string/array/object types.

Objects are stored as sorted arrays of `pjson_kv_pair` (pstring_data key + pjson_data value) in `AddressManager<pjson_kv_pair>`. Sorted order enables O(log n) lookup via binary search.

Arrays are stored as `pjson_data[]` in `AddressManager<pjson_data>` with doubling growth.

`pjson` is a thin non-owning wrapper around a `pjson_data&` reference, providing `set_*`/`get_*` accessors, `push_back`, `operator[]`, `obj_insert`, `obj_find`, `obj_erase`, and a recursive `free()`.

**Bug found and fixed during implementation**: In `obj_insert`, the shift-right loop performs a shallow copy of `pjson_kv_pair`. After the shift, the source and destination entries both hold the same `chars_slot` index for the key string. When `_assign_key` was then called to write the new key, it freed the shared slot — invalidating the shifted neighbor's key. Fixed by zeroing `new_pair.key.chars` before calling `_assign_key` to mark the slot as unowned at that position before the new allocation.

---

### Task 4: Testing Strategy

All tests use [Catch2](https://github.com/catchorg/Catch2) or a minimal custom framework to stay dependency-light.

#### Test file layout:
```
tests/
  test_persist.cpp          — tests for persist<T>
  test_address_manager.cpp  — tests for AddressManager<T>
  test_fptr.cpp             — tests for fptr<T>
  test_cache.cpp            — tests for Cache / StaticPageDevice
  test_memory_device.cpp    — tests for MemoryDevice
  test_pstring.cpp          — tests for pstring
  test_pvector.cpp          — tests for pvector<T>
  test_pmap.cpp             — tests for pmap<K,V>
  test_pallocator.cpp       — tests for pallocator<T>
```

Each test file has an independent `main()` and is registered as a CTest test.

---

### Task 5: Build System

CMakeLists.txt will:
1. Set `CXX_STANDARD 17` (required for `std::filesystem` in `persist.h`).
2. Build a `main` executable from `main.cpp`.
3. Build and register individual test executables via `add_test()`.

---

## Milestones

| Milestone | Deliverable | Status |
|-----------|-------------|--------|
| M1 | `CMakeLists.txt`, CI passing for `main.cpp` | ✅ Done |
| M2 | Unit tests for core infrastructure (`persist`, `AddressManager`, `fptr`, `Cache`, `MemoryDevice`) passing | ✅ Done |
| M3 | `pstring` implemented and tested | ✅ Done |
| M4 | `pvector<T>` implemented and tested | ✅ Done |
| M5 | `pmap<K, V>` implemented and tested | ✅ Done |
| M6 | `pallocator<T>` implemented and tested | ✅ Done |
| M7 | `pjson` design decision and prototype | ✅ Done |
| M8 | Updated `readme.md` and merged PR | ✅ Done |

---

## File Inventory After Phase 1

```
BinDiffSynchronizer.h     — unchanged
PageDevice.h              — unchanged (previously fixed bugs documented)
StaticPageDevice.h        — unchanged
persist.h                 — UPDATED: added FindByPtr() to AddressManager
pstring.h                 — persistent string
pvector.h                 — persistent vector
pmap.h                    — persistent map
pallocator.h              — UPDATED: fixed deallocate() to use FindByPtr()
pjson.h                   — NEW: persistent JSON discriminated union (M7)
CMakeLists.txt            — build system
tests/
  test_persist.cpp        — tests for persist<T>, AddressManager<T>, fptr<T>
  test_pstring.cpp        — tests for pstring
  test_pvector.cpp        — tests for pvector<T>
  test_pmap.cpp           — tests for pmap<K,V>
  test_pallocator.cpp     — tests for pallocator<T>
  test_pjson.cpp          — NEW: tests for pjson (M7)
readme.md                 — UPDATED: persistent infrastructure description + pjson
DEVELOPMENT_PLAN.md       — UPDATED: milestones and status
```

---

## Constraints and Design Principles

1. **Trivial copyability**: Every struct that will be stored via `persist<T>` or in `AddressManager<T>` **must** be `std::is_trivially_copyable`. This means: no vtables, no non-trivial constructors/destructors, no non-trivial copy/move, no raw heap pointers (use `fptr<T>` slot indices instead).

2. **No raw pointers in persistent structs**: Use `fptr<T>` (a `uint` slot index) wherever a pointer would normally appear. The actual raw pointer is resolved at runtime via `AddressManager<T>::GetManager()[slot]`.

3. **Load/Save only in constructors/destructors**: Persistent object constructors and destructors must only load/save state. Allocation and deallocation are performed by explicit `AddressManager` static methods (`Create`, `CreateArray`, `Delete`, `DeleteArray`).

4. **Single address space per type**: Each type `T` has its own `AddressManager<T>` singleton. Cross-type references use `fptr<T>` slot indices.

5. **Binary portability**: All persistent data is stored as raw bytes. This means the layout must be stable across compilers and platforms (use fixed-width types where endianness matters — see Phase 2).

---

# Фаза 2 — Рефакторинг `persist<>` и `fptr<>`: Единое персистное адресное пространство

## Обзор

Данный документ описывает вторую фазу развития проекта — архитектурный рефакторинг классов `persist<>` и `fptr<>`. Цель — реализовать единое персистное адресное пространство (ПАП) с унифицированным менеджером, поддержкой именованных объектов, строгими ограничениями на размер типов и полностью переписанными комментариями на русском языке.

Требования взяты из задачи [#45](https://github.com/netkeep80/BinDiffSynchronizer/issues/45).

---

## Анализ текущего состояния (Phase 1) и необходимых изменений

### Что сделано в Phase 1

- `persist<T>` — обёртка для тривиально-копируемого типа `T`, сохраняет/загружает сырые байты в файл. Внутри хранит имя файла (`_fname[64]`) и данные (`_data[sizeof(T)]`). **Проблема**: `sizeof(persist<T>) != sizeof(T)` — нарушает требование 8.
- `AddressManager<T>` — менеджер слотов. Хранит таблицу `__info[AddressSpace]` через `persist<__info[AddressSpace]>`. Привязан к конкретному типу `T`, что создаёт отдельное АП для каждого типа. **Проблема**: нет единого ПАП для всех типов.
- `fptr<T>` — персистный указатель, хранит только `unsigned __addr`. **Проблема**: `sizeof(fptr<T>) == sizeof(unsigned) == 4` — на 64-битных платформах `sizeof(void*) == 8`, требование 5 нарушено.
- Комментарии частично на английском языке — нарушает требование 6.
- `persist<T>` и `fptr<T>` содержат логику работы с именами файлов — нарушает требования 7 и 9.

---

## Требования Phase 2 (из задачи #45)

| № | Требование |
|---|-----------|
| 1 | Персистные объекты могут использовать только персистные указатели |
| 2 | Для создания и удаления объектов используются специальные методы персистного аллокатора |
| 3 | При запуске программы аллокатор инициализируется именем файла с персистным хранилищем |
| 4 | Одно персистное адресное пространство для объектов разного типа |
| 5 | `sizeof(fptr<T>) == sizeof(void*)` — жёсткое требование совместимости |
| 6 | Все комментарии в `persist.h` и связанных файлах — только на русском языке |
| 7 | Из `persist<T>` удалить всё, связанное с именами файлов; работает только со своим менеджером |
| 8 | `sizeof(persist<T>) == sizeof(T)` — жёсткое требование совместимости |
| 9 | Из `fptr<T>` удалить всё, связанное с именами файлов; адресуется только своим индексом размером `sizeof(void*)` |
| 10 | При загрузке образа ПАП конструкторы и деструкторы объектов не вызываются |
| 11 | Объекты `persist<T>` располагаются ТОЛЬКО в образе ПАП; запрещено создавать локальные и статические `persist<T>` |
| 12 | Вся работа с персистными объектами — только через персистные указатели `fptr<T>` |
| 13 | `fptr<T>` может располагаться как в обычной памяти, так и в образе ПАП |
| 14 | Менеджер ПАП — персистный объект, может хранить имена персистных объектов (но не имена файлов) |
| 15 | `fptr<T>` можно инициализировать строковыми именами объектов; менеджер осуществляет поиск |
| 16 | Менеджер ПАП хранит карту объектов и их имён; является персистным объектом |

---

## Архитектурные решения

### Задача 1: `sizeof(fptr<T>) == sizeof(void*)` (требование 5)

**Проблема**: Текущий `fptr<T>` хранит `unsigned __addr` (4 байта). На 64-битных платформах `sizeof(void*) == 8`.

**Решение**: Заменить тип адресного поля на `uintptr_t` (тип, равный `sizeof(void*)` на целевой платформе).

```cpp
// БЫЛО:
unsigned __addr;   // 4 байта

// СТАНЕТ:
uintptr_t __addr;  // 8 байт на 64-бит, 4 байта на 32-бит
                   // sizeof(fptr<T>) == sizeof(void*) ✓
```

**Проверка**: `static_assert(sizeof(fptr<T>) == sizeof(void*), "...");`

### Задача 2: `sizeof(persist<T>) == sizeof(T)` (требование 8)

**Проблема**: Текущий `persist<T>` хранит:
- `unsigned char _data[sizeof(T)]` — данные объекта
- `char _fname[64]` — имя файла
- Итого: `sizeof(persist<T>) == sizeof(T) + 64`

**Решение**: `persist<T>` должен содержать **только** данные объекта — то есть быть прямым `union`/реинтерпретацией `T`. Имя файла полностью уходит из класса.

```cpp
// БЫЛО:
template<class T> class persist {
    char _fname[64];           // имя файла — убрать!
    unsigned char _data[sizeof(T)];
};

// СТАНЕТ:
template<class T> class persist {
    unsigned char _data[sizeof(T)];  // только данные
    // sizeof(persist<T>) == sizeof(T) ✓
};
static_assert(sizeof(persist<T>) == sizeof(T));
```

**Следствие**: Вся работа с файлами (загрузка/сохранение) переходит к менеджеру ПАП (см. Задачу 4).

### Задача 3: Единое персистное адресное пространство (требование 4)

**Проблема**: Текущий `AddressManager<T>` создаёт отдельное АП для каждого типа `T`. В единое ПАП объекты разных типов не входят.

**Решение**: Ввести единый менеджер персистного адресного пространства `PersistentAddressSpace` (ПАП-менеджер), который:
- Управляет плоским буфером байт (образом ПАП) в оперативной памяти.
- Загружает и сохраняет образ ПАП из/в файл (один файл на всё ПАП — аналог памяти процесса).
- Хранит таблицу дескрипторов (descriptor table), в которой каждая запись содержит:
  - Адрес объекта в ПАП (смещение в образе).
  - Размер объекта в байтах.
  - Идентификатор типа (`type_id` — строка имени класса, например из `typeid(T).name()`).
  - Имя объекта (строка, не имя файла — требование 14, 15, 16).
  - Флаг занятости слота.

```
Образ ПАП (бинарный файл):
  [заголовок: magic, версия, размер образа]
  [таблица дескрипторов: N записей]
  [данные объектов: непрерывный пул байт]
```

**Индексирование**: `fptr<T>` хранит смещение (offset) в образе ПАП, а не слот `AddressManager<T>`. Это позволяет адресовать объекты разных типов в едином пространстве.

### Задача 4: Удаление работы с файлами из `persist<T>` и `fptr<T>` (требования 7, 9)

- `persist<T>`: убрать конструкторы с именами файлов, убрать деструктор с записью в файл, убрать `_fname`. Оставить только `_data[sizeof(T)]`.
- `fptr<T>`: убрать конструктор `fptr(char* __faddress)` и оператор `operator=(char* __faddress)`, которые делали поиск по имени файла. Инициализация по имени объекта — через менеджер ПАП (требование 15).

### Задача 5: Менеджер ПАП как персистный объект (требования 14, 16)

**Решение**: Сам менеджер `PersistentAddressSpace` является персистным объектом. Его заголовок (корень) хранится в начале образа ПАП. При инициализации аллокатор загружает образ ПАП из файла; корень менеджера — первый объект в образе.

Менеджер хранит:
- **Карту объектов**: `pmap<pstring, pap_slot_index>` — имя → индекс слота в таблице дескрипторов.
- **Таблицу дескрипторов**: массив `slot_descriptor` фиксированного размера в образе ПАП.

### Задача 6: Инициализация по имени объекта в `fptr<T>` (требование 15)

```cpp
// БЫЛО (имя файла):
fptr<T> p("myfile.persist");   // искало в AddressManager<T> по имени файла

// СТАНЕТ (имя персистного объекта):
fptr<T> p;
p.find("my_object_name");      // поиск через PersistentAddressSpace::Find()
// или: p = PersistentAddressSpace::Get().find<T>("my_object_name");
```

### Задача 7: Запрет локальных и статических `persist<T>` (требование 11)

**Механизм обнаружения** (статический анализ / runtime):
- `persist<T>` должен содержать `static_assert` или SFINAE-проверку, предотвращающую случайное создание на стеке.
- Практически это реализуется либо через документацию + code review, либо через `[[nodiscard]]` и нестандартные расширения компилятора.
- Рекомендуется: объявить конструктор по умолчанию и конструктор из значения как `= delete` в публичной части, оставив только фабричные методы через менеджер ПАП. Тогда `persist<T> x;` на стеке не скомпилируется.

### Задача 8: Загрузка без вызова конструкторов (требование 10)

Текущая реализация использует `std::memcpy`/`std::memset` для загрузки сырых байт — конструкторы объектов при этом не вызываются. Это уже выполнено. В Phase 2 необходимо:
- Сохранить данное поведение в `PersistentAddressSpace::Load()`.
- Явно задокументировать на русском языке.
- Убедиться, что при `mmap`/`fread` образа ПАП никакого placement-new не происходит.

---

## Детальный план задач Phase 2

### Задача 2.1: Переписать `fptr<T>` — размер и удаление файловой логики

**Файл**: `persist.h`

**Шаги**:
1. Заменить поле `unsigned __addr` на `uintptr_t __addr`.
2. Добавить `static_assert(sizeof(fptr<T>) == sizeof(void*), "fptr<T> должен иметь размер указателя void*");`
3. Удалить конструктор `fptr(char* __faddress)`.
4. Удалить `operator=(char* __faddress)`.
5. Добавить метод `find(const char* name)` — инициализация через менеджер ПАП по имени объекта.
6. Переписать все комментарии на русский язык.

**Тесты** (`tests/test_fptr.cpp`):
- `sizeof(fptr<T>) == sizeof(void*)` — статическая проверка.
- Инициализация нулём, копирование, назначение через `set_addr`.
- `find()` по имени объекта — проверка через заглушку менеджера.

### Задача 2.2: Переписать `persist<T>` — размер и удаление файловой логики

**Файл**: `persist.h`

**Шаги**:
1. Удалить поле `char _fname[64]`.
2. Удалить метод `get_name()`.
3. Удалить все конструкторы, принимающие имена файлов.
4. Удалить деструктор, записывающий в файл.
5. Запретить стековое/статическое создание: сделать конструктор по умолчанию `private` или `= delete`, добавить `friend class PersistentAddressSpace`.
6. Добавить `static_assert(sizeof(persist<T>) == sizeof(T), "persist<T> должен иметь размер T");`
7. Переписать все комментарии на русский язык.

**Тесты** (`tests/test_persist.cpp`):
- `sizeof(persist<T>) == sizeof(T)` — статическая проверка.
- Прямое чтение/запись данных через `operator _Tref()` и `operator=`.
- Проверка, что компилятор отклоняет создание `persist<T>` на стеке (негативный тест через `static_assert` или SFINAE).

### Задача 2.3: Разработать структуру `slot_descriptor` для таблицы дескрипторов

**Файл**: `persist.h` или новый `pam.h` (Persistent Address Manager)

**Структура** (все поля фиксированного размера, тривиально копируемые):

```cpp
// Максимальная длина строки-идентификатора типа и имени объекта
constexpr unsigned PAP_TYPE_ID_SIZE = 64;
constexpr unsigned PAP_NAME_SIZE    = 64;

// Дескриптор одного слота в таблице дескрипторов ПАП
struct slot_descriptor
{
    bool       used;                        // слот занят
    uintptr_t  offset;                      // смещение данных объекта в образе ПАП
    uintptr_t  size;                        // размер объекта в байтах
    uintptr_t  count;                       // кол-во элементов (0 = одиночный объект, >0 = массив)
    char       type_id[PAP_TYPE_ID_SIZE];   // идентификатор типа объекта (typeid(T).name())
    char       name[PAP_NAME_SIZE];         // имя объекта (не имя файла!)
};
static_assert(std::is_trivially_copyable<slot_descriptor>::value);
```

**Проектное решение**: `uintptr_t offset` используется как индекс в образе ПАП — это одновременно и значение, хранящееся в `fptr<T>::__addr`. Таким образом `sizeof(fptr<T>) == sizeof(uintptr_t) == sizeof(void*)`.

### Задача 2.4: Разработать `PersistentAddressSpace` — единый менеджер ПАП

**Файл**: новый `pam.h` (Persistent Address Manager)

**Заголовок образа ПАП**:

```cpp
constexpr uint32_t PAP_MAGIC   = 0x50415000;  // "PAP\0"
constexpr uint32_t PAP_VERSION = 1;
constexpr unsigned PAP_MAX_SLOTS = 4096;

struct pap_header
{
    uint32_t magic;            // магическое число для проверки формата
    uint32_t version;          // версия формата
    uintptr_t data_area_size;  // размер области данных объектов в байтах
    unsigned  slot_count;      // количество использованных слотов
    // Зарезервировано для будущего: контрольная сумма, флаги шифрования и т.д.
};
static_assert(std::is_trivially_copyable<pap_header>::value);
```

**Класс `PersistentAddressSpace`**:

```cpp
class PersistentAddressSpace
{
public:
    // Инициализация: загрузить образ ПАП из файла filename.
    // Если файл не существует — создать пустой образ.
    // Вызывается однократно при старте программы (требование 3).
    static void Init(const char* filename);

    // Получить синглтон менеджера.
    static PersistentAddressSpace& Get();

    // Создать одиночный объект типа T с необязательным именем name.
    // Возвращает смещение в образе ПАП (значение для fptr<T>::__addr).
    template<class T>
    uintptr_t Create(const char* name = nullptr);

    // Создать массив count объектов типа T с необязательным именем name.
    template<class T>
    uintptr_t CreateArray(unsigned count, const char* name = nullptr);

    // Освободить слот по смещению.
    void Delete(uintptr_t offset);

    // Найти объект по имени. Возвращает смещение или 0 если не найден.
    uintptr_t Find(const char* name) const;

    // Найти объект по имени с проверкой типа.
    template<class T>
    uintptr_t FindTyped(const char* name) const;

    // Получить указатель на данные объекта по смещению.
    template<class T>
    T* Resolve(uintptr_t offset);

    // Сохранить образ ПАП в файл.
    void Save();

    // Деструктор: автоматически сохраняет образ перед выходом.
    ~PersistentAddressSpace();

private:
    std::string      _filename;            // имя файла образа ПАП
    std::vector<char> _data;               // образ ПАП в оперативной памяти
    slot_descriptor  _slots[PAP_MAX_SLOTS]; // таблица дескрипторов
    unsigned         _slot_count;          // количество используемых слотов

    PersistentAddressSpace() = default;
    void _load(const char* filename);
    uintptr_t _alloc(uintptr_t size);      // выделить байты в области данных
};
```

**Ключевые свойства**:
- Сам менеджер является персистным объектом: его заголовок (`pap_header` + таблица слотов) хранится в начале файла образа ПАП.
- `_data` — это образ ПАП, загруженный в оперативную память через `fread`/`mmap` без вызова конструкторов.
- `Resolve<T>(offset)` возвращает `reinterpret_cast<T*>(&_data[offset])`.

### Задача 2.5: Переписать `AddressManager<T>` как тонкий адаптер над `PersistentAddressSpace`

**Цель**: Обратная совместимость с кодом Phase 1 (контейнеры `pstring`, `pvector`, `pmap`, `pjson`), которые используют `AddressManager<T>::CreateArray()` и т.д.

**Решение**: `AddressManager<T>` становится тонкой обёрткой, делегирующей в `PersistentAddressSpace`. Логика хранения слотов переходит в ПАП-менеджер.

```cpp
// Тонкий адаптер Phase 2 — делегирует в PersistentAddressSpace
template<class T, unsigned AddressSpace = PAP_MAX_SLOTS>
class AddressManager
{
public:
    static unsigned CreateArray(unsigned count, char* name)
    {
        uintptr_t offset = PersistentAddressSpace::Get().CreateArray<T>(count, name);
        return static_cast<unsigned>(offset);  // для обратной совместимости
    }
    // ...остальные методы аналогично
};
```

**Примечание**: В долгосрочной перспективе (Phase 3) контейнеры Phase 1 должны быть переписаны для прямого использования `fptr<T>` с `uintptr_t`-адресами. В Phase 2 адаптер обеспечивает совместимость.

### Задача 2.6: Переписать все комментарии на русский язык (требование 6)

**Файлы**: `persist.h`, `pstring.h`, `pvector.h`, `pmap.h`, `pallocator.h`, `pjson.h`

**Шаги**:
1. Перевести все `//`-комментарии и `/* */`-блоки.
2. Технические термины (имена типов, методов) оставить без перевода.
3. Примеры кода оставить как есть.
4. Обновить словарь терминов в начале `persist.h`.

---

## Стратегия тестирования Phase 2

### Задача 2.7: Статические проверки размеров

В каждом заголовке добавить `static_assert`:

```cpp
// в persist.h после определения persist<T>:
// static_assert(sizeof(persist<T>) == sizeof(T)) — нельзя на шаблоне напрямую,
// проверяется через тест:
static_assert(sizeof(persist<int>) == sizeof(int),
              "persist<T> должен иметь размер T");
static_assert(sizeof(fptr<int>) == sizeof(void*),
              "fptr<T> должен иметь размер void*");
```

### Задача 2.8: Тесты для `PersistentAddressSpace`

**Файл**: `tests/test_pam.cpp`

| Тест | Проверяемое поведение |
|------|----------------------|
| `test_init_empty` | `Init("test.pap")` на несуществующем файле создаёт пустой образ |
| `test_create_object` | `Create<int>()` возвращает ненулевое смещение |
| `test_resolve` | `Resolve<int>(offset)` возвращает указатель, `*p = 42` работает |
| `test_find_by_name` | `Create<int>("counter")` → `Find("counter")` возвращает то же смещение |
| `test_save_reload` | `Save()` + повторный `Init()` → объекты восстанавливаются |
| `test_delete` | `Delete(offset)` → `Find(name) == 0` |
| `test_array_create` | `CreateArray<char>(100)` → `Resolve<char>` даёт массив 100 байт |
| `test_no_constructors_on_load` | При перезагрузке объекта счётчик вызовов конструктора не растёт |
| `test_single_address_space` | `Create<int>()` и `Create<double>()` — оба в одном образе |

### Задача 2.9: Регрессионные тесты Phase 1

Все существующие тесты Phase 1 должны продолжать проходить после рефакторинга:
- `test_persist.cpp` — адаптировать под новый API.
- `test_pstring.cpp`, `test_pvector.cpp`, `test_pmap.cpp`, `test_pallocator.cpp`, `test_pjson.cpp` — должны проходить без изменений (через адаптер `AddressManager`).

---

## Порядок реализации (этапы)

| Этап | Задачи | Зависимости |
|------|--------|-------------|
| Этап A | 2.3: `slot_descriptor` | нет |
| Этап B | 2.4: `PersistentAddressSpace` (базовая реализация) | Этап A |
| Этап C | 2.1: рефакторинг `fptr<T>` | Этап B |
| Этап D | 2.2: рефакторинг `persist<T>` | Этап B |
| Этап E | 2.5: тонкий адаптер `AddressManager<T>` | Этапы C, D |
| Этап F | 2.7, 2.8: тесты `PersistentAddressSpace` | Этап B |
| Этап G | 2.6: перевод комментариев | любой |
| Этап H | 2.9: регрессия Phase 1 | Этапы E, G |

---

## Вехи Phase 2

| Веха | Результат | Статус |
|------|-----------|--------|
| M9  | `slot_descriptor` определён и задокументирован | ✅ Выполнено |
| M10 | `PersistentAddressSpace` реализован: Init, Create, Resolve, Find, Save | ✅ Выполнено |
| M11 | `fptr<T>` рефакторинг: `sizeof(fptr<T>)==sizeof(void*)`, удалена файловая логика | ✅ Выполнено |
| M12 | `persist<T>` рефакторинг: `sizeof(persist<T>)==sizeof(T)`, удалена файловая логика | ✅ Выполнено |
| M13 | Тонкий адаптер `AddressManager<T>` над ПАП | ✅ Выполнено |
| M14 | Все тесты Phase 1 проходят с новым ПАП | ✅ Выполнено |
| M15 | `test_pam.cpp` — новые тесты для ПАП проходят | ✅ Выполнено |
| M16 | Все комментарии переведены на русский язык | ✅ Выполнено |
| M17 | Обновлены `readme.md` и `DEVELOPMENT_PLAN.md` | ✅ Выполнено |

---

## Инвентаризация файлов после Phase 2

```
persist.h                 — РЕФАКТОРИНГ: fptr<T> и persist<T> по требованиям Phase 2
pam.h                     — НОВЫЙ: PersistentAddressSpace, slot_descriptor, pap_header
pstring.h                 — комментарии на русском
pvector.h                 — комментарии на русском
pmap.h                    — комментарии на русском
pallocator.h              — комментарии на русском
pjson.h                   — комментарии на русском
tests/
  test_pam.cpp            — НОВЫЙ: тесты PersistentAddressSpace
  test_persist.cpp        — ОБНОВЛЁН: проверки размеров, новый API
  test_pstring.cpp        — регрессия
  test_pvector.cpp        — регрессия
  test_pmap.cpp           — регрессия
  test_pallocator.cpp     — регрессия
  test_pjson.cpp          — регрессия
readme.md                 — ОБНОВЛЁН: описание Phase 2
DEVELOPMENT_PLAN.md       — ОБНОВЛЁН: план Phase 2
```

---

## Открытые вопросы — ЗАКРЫТЫ (задача #47)

1. **Размер образа ПАП**: Размер динамически регулируется через bump-аллокатор с автоматическим
   удвоением буфера (`realloc`). Начальный размер — 1 МБ (`PAP_INITIAL_DATA_SIZE`). **Закрыт.**
2. **Конкурентный доступ**: Многопоточный доступ к ПАП — **следующая фаза** (Phase 3). **Закрыт.**
3. **Шифрование и архивация**: Шифрованное хранилище — **следующая фаза** (Phase 3). **Закрыт.**
4. **Фрагментация памяти**: Дефрагментатор для Phase 2 **не нужен** — используется простой
   bump-аллокатор. Дефрагментация — **следующая фаза** (Phase 3). **Закрыт.**
5. **Обратная совместимость файлов Phase 1**: Миграция `.extend`-файлов Phase 1 в формат `.pap`
   **не требуется** — старый код удалён без сожаления. **Закрыт.**

### Дополнения к плану (из задачи #47)

- Удаление устаревшего кода: удалена старая файловая логика из `persist<T>` и `fptr<T>`,
  удалён старый `main.cpp` с устаревшим API.
- Перевод всех комментариев на русский язык: выполнен в `persist.h`, `pam.h`, `pstring.h`,
  `pvector.h`, `pmap.h`, `pallocator.h`, `pjson.h`.
- `readme.md` переписан на русском языке.

---

# Фаза 3 — Приведение персистных контейнеров в соответствие с архитектурой Phase 2

## Обзор

После Phase 2 все смещения объектов в ПАП хранятся как `uintptr_t` (8 байт на 64-битных платформах).
Однако контейнеры Phase 1 (`pstring`, `pvector`, `pmap`, `pjson`) по-прежнему используют
`unsigned` (4 байта) для хранения смещений и размеров. Это приводит к:
1. Несоответствию типов: `uintptr_t` vs `unsigned` при обращении к ПАП.
2. Потенциальному переполнению: при размере ПАП > 4 ГБ смещения не помещаются в `unsigned`.
3. Нарушению инвариантов Phase 2: `fptr<T>::addr()` возвращает `uintptr_t`, но поле
   `chars_slot`/`data_slot`/`pairs_slot` в payload — `unsigned`.

**Цель Phase 3**: привести все поля контейнеров к типу `uintptr_t`, обеспечив полную
корректность на 64-битных платформах и согласованность с Phase 2 PAM API.

---

## Анализ несоответствий (выявлены при аудите кода Phase 1 → Phase 2)

### `pstring_data` (`pstring.h`)

```cpp
// Phase 1 (проблема):
struct pstring_data {
    unsigned   length;   // 4 байта — ограничивает строки до ~4 млрд символов
    fptr<char> chars;    // 8 байт (uintptr_t) — адрес в ПАП
};
// Итого: 12 байт с выравниванием до 16 байт.
// Нарушение: length должен быть uintptr_t для согласованности с fptr<char>.
```

### `pvector_data<T>` (`pvector.h`)

```cpp
// Phase 1 (проблема):
template<typename T>
struct pvector_data {
    unsigned  size;      // 4 байта
    unsigned  capacity;  // 4 байта
    fptr<T>   data;      // 8 байт (uintptr_t)
};
// Нарушение: size и capacity должны быть uintptr_t.
```

### `pjson_data` (`pjson.h`)

```cpp
// Phase 1 (проблема):
union payload_t {
    struct { unsigned length; unsigned chars_slot; } string_val;  // 8 байт
    struct { unsigned size;   unsigned data_slot;  } array_val;   // 8 байт
    struct { unsigned size;   unsigned pairs_slot; } object_val;  // 8 байт
};
// Нарушение: chars_slot/data_slot/pairs_slot должны быть uintptr_t,
// так как они хранят смещения из PersistentAddressSpace (uintptr_t).
// На 64-бит платформах текущий код усекает адреса до 32 бит!
```

---

## Задачи Phase 3

### Задача 3.1: Привести `pstring_data` к `uintptr_t`

**Файл**: `pstring.h`

**Изменения**:
- Поле `unsigned length` → `uintptr_t length`.
- Обновить все методы `pstring`, использующие `length`, для работы с `uintptr_t`.
- Добавить `static_assert(sizeof(pstring_data) == 2 * sizeof(void*))`.

**Тесты** (`tests/test_pstring.cpp`):
- Добавить тест `pstring_data: sizeof == 2 * sizeof(void*)`.
- Регрессионные тесты: все существующие тесты должны проходить без изменений.

### Задача 3.2: Привести `pvector_data<T>` к `uintptr_t`

**Файл**: `pvector.h`

**Изменения**:
- Поля `unsigned size` и `unsigned capacity` → `uintptr_t size`, `uintptr_t capacity`.
- Обновить все методы `pvector`, использующие `size` и `capacity`.
- Добавить `static_assert(sizeof(pvector_data<int>) == 3 * sizeof(void*))` (только если выравнивание позволяет).

**Тесты** (`tests/test_pvector.cpp`):
- Добавить тест `pvector_data: size and capacity are uintptr_t`.
- Регрессионные тесты.

### Задача 3.3: Привести `pmap_data<K,V>` к `uintptr_t`

**Файл**: `pmap.h`

**Изменения**:
- `pmap` использует `pvector_data` внутри — обновится автоматически после задачи 3.2.
- Проверить, что все явно используемые `unsigned`-поля заменены.
- Добавить тест `pmap_data: size and capacity are uintptr_t`.

### Задача 3.4: Привести `pjson_data` к `uintptr_t`

**Файл**: `pjson.h`

**Изменения**:
- Поля `string_val.length`, `string_val.chars_slot` → `uintptr_t length`, `uintptr_t chars_slot`.
- Поля `array_val.size`, `array_val.data_slot` → `uintptr_t size`, `uintptr_t data_slot`.
- Поля `object_val.size`, `object_val.pairs_slot` → `uintptr_t size`, `uintptr_t pairs_slot`.
- Обновить все методы `pjson`, использующие эти поля.
- Добавить `static_assert` для проверки размера `pjson_data`.

**Тесты** (`tests/test_pjson.cpp`):
- Добавить тест `pjson_data: slot fields are uintptr_t`.
- Регрессионные тесты: все 34 существующих теста должны проходить.

### Задача 3.5: Обновить `pallocator.h` при необходимости

**Проверка**: `pallocator<T>` использует `uintptr_t` через `AddressManager<T>` — проверить,
нет ли скрытых `unsigned` в критических путях.

### Задача 3.6: Регрессионное тестирование

После всех изменений запустить полный тестовый набор (109 тестов) и убедиться, что все проходят.

---

## Стратегия реализации

| Этап | Задача | Зависимости |
|------|--------|-------------|
| Этап A | 3.1: `pstring_data` | нет |
| Этап B | 3.2: `pvector_data` | нет |
| Этап C | 3.3: `pmap_data` | Этап B |
| Этап D | 3.4: `pjson_data` | Этапы A, B, C |
| Этап E | 3.5: `pallocator` | нет |
| Этап F | 3.6: регрессия | Этапы A–E |

---

## Вехи Phase 3

| Веха | Результат | Статус |
|------|-----------|--------|
| M18 | `pstring_data.length` → `uintptr_t`, тесты проходят | ✅ Выполнено |
| M19 | `pvector_data.size/capacity` → `uintptr_t`, тесты проходят | ✅ Выполнено |
| M20 | `pmap_data` обновлён через pvector (регрессия), тесты проходят | ✅ Выполнено |
| M21 | `pjson_data` поля slot/size → `uintptr_t`, тесты проходят | ✅ Выполнено |
| M22 | Все 112 тестов проходят после рефакторинга | ✅ Выполнено |
| M23 | Обновлены `readme.md` и `DEVELOPMENT_PLAN.md` | ✅ Выполнено |

---

## Инвентаризация файлов после Phase 3

```
pstring.h         — ОБНОВЛЁН: length: uintptr_t
pvector.h         — ОБНОВЛЁН: size, capacity: uintptr_t
pmap.h            — ОБНОВЛЁН (через pvector)
pjson.h           — ОБНОВЛЁН: chars_slot, data_slot, pairs_slot: uintptr_t
tests/
  test_pstring.cpp — ОБНОВЛЁН: тест sizeof pstring_data
  test_pvector.cpp — ОБНОВЛЁН: тест sizeof pvector_data
  test_pmap.cpp    — регрессия
  test_pjson.cpp   — ОБНОВЛЁН: тест sizeof pjson_data
readme.md         — ОБНОВЛЁН: описание Phase 3
DEVELOPMENT_PLAN.md — ОБНОВЛЁН: план Phase 3
```

---

# Фаза 4 — Code review и оптимизация памяти ПАМ (задача #58)

## Обзор

Данная фаза устраняет избыточный расход памяти в таблице слотов ПАМ, исправляет
ошибку в `ResolveElement`, уточняет документацию и обновляет версию формата файла.

---

## Анализ проблем (задача #58)

### Проблема 1: Дублирование имени типа в каждом слоте

В Phase 3 структура `slot_descriptor` содержала:
```cpp
char      type_id[PAM_TYPE_ID_SIZE];  // 64 байта — копия имени типа в КАЖДОМ слоте
uintptr_t size;                       // 8 байт — размер элемента в КАЖДОМ слоте
```
При создании N объектов одного типа (например, N pstring) каждый из N слотов хранил
идентичную 64-байтную строку. Для 10 000 объектов — 640 000 байт только на имена типов.

### Проблема 2: Отсутствие проверки границ в `ResolveElement`

Метод `ResolveElement<T>(offset, index)` вычислял указатель без проверки,
укладывается ли `offset + index * sizeof(T) + sizeof(T)` в пределы буфера данных ПАМ.
При ошибочном индексе происходил выход за пределы выделенной памяти.

### Проблема 3: Неточная документация слотов

Комментарий в Phase 3 содержал противоречие: в одном месте говорилось «слоты только
для именованных объектов», в другом — «всегда создаётся запись в таблице слотов».
Правильное поведение: слоты создаются для ВСЕХ объектов, `slot_count` в заголовке
считает только именованные (`name[0] != '\0'`).

---

## Решения Phase 4

### Задача 4.1: Таблица типов (type_registry)

**Файл**: `pam.h`

**Новая структура** `type_info_entry`:
```cpp
struct type_info_entry
{
    bool      used;                   ///< Запись занята
    uintptr_t elem_size;              ///< Размер одного элемента типа в байтах
    char      name[PAM_TYPE_ID_SIZE]; ///< Имя типа (из typeid(T).name())
};
```

**Изменения в `slot_descriptor`**:
- Удалены поля: `char type_id[PAM_TYPE_ID_SIZE]` (64 байт) и `uintptr_t size` (8 байт).
- Добавлено поле: `uintptr_t type_idx` (8 байт) — индекс в таблицу типов.
- Экономия: 64 байта на каждый слот.

**Изменения в `pam_header`**:
- Добавлены поля: `unsigned type_count`, `unsigned type_capacity`.
- Версия формата: `PAM_VERSION = 3u`.

**Изменения в `PersistentAddressSpace`**:
- Добавлены поля: `type_info_entry* _types`, `unsigned _type_capacity`.
- Добавлен метод `_find_or_register_type(type_name, elem_size)` — возвращает индекс типа.
- Добавлен публичный метод `GetElemSize(offset)` — возвращает размер элемента через таблицу типов.
- Метод `Save()` сохраняет таблицу типов перед таблицей слотов.
- Метод `_load()` загружает таблицу типов из файла.
- Метод `_init_empty()` инициализирует пустую таблицу типов.

**Формат файла ПАМ (версия 3)**:
```
[pam_header]                  — заголовок (добавлены type_count, type_capacity)
[type_info_entry * type_cap]  — таблица типов
[slot_descriptor * slot_cap]  — таблица слотов (type_idx вместо char type_id[64])
[байты объектов]              — область данных
```

### Задача 4.2: Исправление `ResolveElement`

В методах `ResolveElement<T>(offset, index)` и `const ResolveElement<T>(offset, index) const`
добавлена проверка:
```cpp
uintptr_t byte_offset = offset + index * sizeof(T);
if( offset == 0 || byte_offset + sizeof(T) > data_size )
    return *reinterpret_cast<T*>(_data);  // аварийный адрес вместо UB
```

### Задача 4.3: Уточнение документации

Все комментарии, связанные со слотами, обновлены: явно указано, что слоты создаются
для **всех** аллоцированных объектов; `slot_count` считает только именованные.

### Задача 4.4: Рассмотрение pstring/pmap для имён объектов

Рассмотрен вариант хранения имён объектов в `pstring` (сэкономило бы память для
безымянных слотов) и поиска через `pmap` (O(log n) вместо O(n)). Однако:
- `pstring.h` включает `persist.h`, который включает `pam.h` — циклическая зависимость.
- Использование `pmap` внутри `pam.h` потребовало бы включить `pmap.h` в `pam.h`,
  что нарушило бы принцип минимальных зависимостей базового уровня инфраструктуры.
- Текущий `char name[PAM_NAME_SIZE]` (64 байта) решение достаточно для задачи:
  при N объектах N*64 байт — приемлемо для типичных рабочих нагрузок.

Принятое решение: таблица типов устраняет основной источник потерь памяти (64 байта
на имя типа × N слотов), а имена объектов остаются inline-полями в слотах.

---

## Тесты Phase 4 (добавлены в `tests/test_pam.cpp`)

| Тест | Проверяемое поведение |
|------|----------------------|
| `type_info_entry: is trivially copyable` | `type_info_entry` тривиально копируем |
| `same type not duplicated in type registry` | Три объекта одного типа — одна запись в таблице типов |
| `different types have different registry entries` | `int` и `double` имеют разные записи |
| `GetElemSize returns element size for arrays` | `GetElemSize` возвращает размер одного элемента |
| `Save and Init -- type registry is restored` | Таблица типов восстанавливается после перезагрузки |

---

## Стратегия реализации

| Этап | Задача | Зависимости |
|------|--------|-------------|
| Этап A | 4.1: `type_info_entry`, обновление `slot_descriptor` и `pam_header` | нет |
| Этап B | 4.1: обновление `PersistentAddressSpace` | Этап A |
| Этап C | 4.2: исправление `ResolveElement` | нет |
| Этап D | 4.3: обновление документации | нет |
| Этап E | 4.4: тесты Phase 4 | Этапы A–D |
| Этап F | регрессия: все 123 теста проходят | Этап E |

---

## Вехи Phase 4

| Веха | Результат | Статус |
|------|-----------|--------|
| M24 | `type_info_entry` определён, `slot_descriptor` обновлён (убран `type_id[64]`, добавлен `type_idx`) | ✅ Выполнено |
| M25 | `pam_header` обновлён (`type_count`, `type_capacity`), `PAM_VERSION = 3` | ✅ Выполнено |
| M26 | `PersistentAddressSpace` обновлён: таблица типов, `GetElemSize`, `Save`/`_load`/`_init_empty` | ✅ Выполнено |
| M27 | `ResolveElement` — добавлена проверка границ | ✅ Выполнено |
| M28 | Документация слотов уточнена | ✅ Выполнено |
| M29 | 5 новых тестов в `test_pam.cpp`, все 123 теста проходят | ✅ Выполнено |
| M30 | Обновлены `readme.md` и `DEVELOPMENT_PLAN.md` | ✅ Выполнено |

---

## Инвентаризация файлов после Phase 4

```
pam.h               — ОБНОВЛЁН: type_info_entry, slot_descriptor (type_idx),
                      pam_header (type_count/capacity), PersistentAddressSpace
                      (таблица типов, GetElemSize, ResolveElement с проверкой границ)
tests/
  test_pam.cpp      — ОБНОВЛЁН: 5 новых тестов для таблицы типов (Phase 4)
readme.md           — ОБНОВЛЁН: описание Phase 4
DEVELOPMENT_PLAN.md — ОБНОВЛЁН: план Phase 4
```

---

# Фаза 5 — Таблица имён объектов (задача #58)

## Обзор

По запросу владельца репозитория (комментарий к PR #59): имена объектов должны быть
вынесены в отдельную структуру `name_info_entry` с двусторонней связью со слотом,
обеспечивающей поиск по имени и получение имени по слоту. Таблица имён динамическая
(аналогично таблицам слотов и типов). Уникальность имён гарантируется ПАМ.

---

## Анализ проблем (задача #58, продолжение)

### Проблема 1: Дублирование памяти на имена в безымянных слотах

В Phase 4 структура `slot_descriptor` содержала `char name[PAM_NAME_SIZE]` (64 байта)
в КАЖДОМ слоте — включая безымянные. При создании N безымянных объектов каждый слот
тратит 64 байта на поле имени, которое всегда пусто.

### Проблема 2: Нет уникальности имён

В Phase 4 можно было создать два объекта с одинаковым именем: `Find()` вернул бы
первый найденный. Это нарушает ожидаемую семантику именованных объектов.

### Проблема 3: Нет обратной ссылки «слот → имя»

В Phase 4 не было способа получить имя объекта по его смещению, не перебирая все слоты.

---

## Решения Phase 5

### Задача 5.1: Структура `name_info_entry`

**Файл**: `pam.h`

**Новая структура** `name_info_entry`:
```cpp
struct name_info_entry
{
    bool      used;                 ///< Запись занята
    uintptr_t slot_idx;             ///< Индекс слота в таблице slot_descriptor
    char      name[PAM_NAME_SIZE];  ///< Имя объекта (уникально среди занятых записей)
};
```

### Задача 5.2: Изменения в `slot_descriptor`

- Удалено поле: `char name[PAM_NAME_SIZE]` (64 байта).
- Добавлено поле: `uintptr_t name_idx` (8 байт) — индекс в таблицу имён;
  `PAM_INVALID_IDX` (= `uintptr_t(-1)`) для безымянных объектов.

### Задача 5.3: Изменения в `pam_header`

- Добавлены поля: `unsigned name_count`, `unsigned name_capacity`.
- Версия формата: `PAM_VERSION = 4u`.

### Задача 5.4: Изменения в `PersistentAddressSpace`

- Добавлены поля: `name_info_entry* _names`, `unsigned _name_capacity`.
- Добавлена константа `PAM_INITIAL_NAME_CAPACITY = 16u`.
- Добавлен метод `_register_name(name, slot_idx)` — регистрирует имя, проверяет
  уникальность, возвращает индекс или `PAM_INVALID_IDX`.
- Добавлен публичный метод `GetName(offset)` — возвращает имя объекта по смещению
  через двустороннюю связь `slot_descriptor.name_idx → name_info_entry.name`.
- Обновлён `_alloc()`: вместо записи имени в слот — вызов `_register_name()`;
  при конфликте имён слот откатывается и возвращается 0.
- Обновлён `Delete()`: освобождает запись в таблице имён при удалении именованного объекта.
- Обновлён `Find()`: поиск через таблицу имён (`name_info_entry → slot_idx → offset`).
- Обновлён `FindTyped()`: аналогично `Find()`, с дополнительной проверкой типа.
- Обновлены `Save()`, `_load()`, `_init_empty()` для поддержки таблицы имён.

**Формат файла ПАМ (версия 4)**:
```
[pam_header]                  — заголовок (добавлены name_count, name_capacity)
[type_info_entry * type_cap]  — таблица типов
[name_info_entry * name_cap]  — таблица имён (новая секция)
[slot_descriptor * slot_cap]  — таблица слотов (name_idx вместо char name[64])
[байты объектов]              — область данных
```

---

## Тесты Phase 5 (добавлены в `tests/test_pam.cpp`)

| Тест | Проверяемое поведение |
|------|----------------------|
| `name_info_entry: is trivially copyable` | `name_info_entry` тривиально копируем |
| `GetName returns object name by offset` | Двусторонняя связь slot → name; безымянный → nullptr |
| `duplicate name returns 0 (name uniqueness)` | Уникальность: дубликат возвращает 0; после Delete имя освобождается |
| `Find uses name table for lookup` | Find через таблицу имён; после Delete имя недоступно |
| `Delete frees name table entry` | Delete освобождает name_info_entry; имя можно использовать снова |
| `Save and Init -- name registry is restored` | Таблица имён восстанавливается после перезагрузки |
| `multiple named objects have independent name entries` | N объектов — N независимых записей; Find и GetName верны для каждого |

---

## Стратегия реализации

| Этап | Задача | Зависимости |
|------|--------|-------------|
| Этап A | 5.1: `name_info_entry`, обновление `slot_descriptor`, `pam_header` | нет |
| Этап B | 5.2: обновление `PersistentAddressSpace` | Этап A |
| Этап C | 5.3: тесты Phase 5 | Этапы A–B |
| Этап D | регрессия: все тесты проходят | Этап C |

---

## Вехи Phase 5

| Веха | Результат | Статус |
|------|-----------|--------|
| M31 | `name_info_entry` определён, `slot_descriptor` обновлён (`name_idx` вместо `char name[64]`) | ✅ Выполнено |
| M32 | `pam_header` обновлён (`name_count`, `name_capacity`), `PAM_VERSION = 4` | ✅ Выполнено |
| M33 | `PersistentAddressSpace` обновлён: таблица имён, `GetName`, уникальность, двусторонняя связь | ✅ Выполнено |
| M34 | 7 новых тестов в `test_pam.cpp`, все тесты проходят | ✅ Выполнено |
| M35 | Обновлены `readme.md` и `DEVELOPMENT_PLAN.md` | ✅ Выполнено |

---

## Инвентаризация файлов после Phase 5

```
pam.h               — ОБНОВЛЁН: name_info_entry, slot_descriptor (name_idx),
                      pam_header (name_count/capacity), PAM_VERSION=4,
                      PersistentAddressSpace (таблица имён, GetName, уникальность)
tests/
  test_pam.cpp      — ОБНОВЛЁН: 7 новых тестов для таблицы имён (Phase 5)
readme.md           — ОБНОВЛЁН: описание Phase 5
DEVELOPMENT_PLAN.md — ОБНОВЛЁН: план Phase 5
```
