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

**Decision**: Attempt Option A first with a compatibility shim. If `nlohmann::basic_json` cannot be instantiated cleanly (e.g., it internally dereferences raw pointers that differ from slot-based pointers), fall back to Option B.

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

| Milestone | Deliverable |
|-----------|-------------|
| M1 | `CMakeLists.txt`, CI passing for `main.cpp` |
| M2 | Unit tests for core infrastructure (`persist`, `AddressManager`, `fptr`, `Cache`, `MemoryDevice`) passing |
| M3 | `pstring` implemented and tested |
| M4 | `pvector<T>` implemented and tested |
| M5 | `pmap<K, V>` implemented and tested |
| M6 | `pallocator<T>` implemented and tested |
| M7 | `pjson` design decision and prototype |
| M8 | Updated `readme.md` and merged PR |

---

## File Inventory After Phase 1

```
BinDiffSynchronizer.h     — unchanged
PageDevice.h              — unchanged (previously fixed bugs documented)
StaticPageDevice.h        — unchanged
persist.h                 — unchanged (previously fixed bugs documented)
pstring.h                 — NEW: persistent string
pvector.h                 — NEW: persistent vector
pmap.h                    — NEW: persistent map
pallocator.h              — NEW: persistent STL allocator
CMakeLists.txt            — NEW: build system
tests/
  test_persist.cpp        — NEW
  test_address_manager.cpp — NEW
  test_fptr.cpp           — NEW
  test_pstring.cpp        — NEW
  test_pvector.cpp        — NEW
  test_pmap.cpp           — NEW
readme.md                 — UPDATED: persistent infrastructure description
DEVELOPMENT_PLAN.md       — NEW: this document
```

---

## Constraints and Design Principles

1. **Trivial copyability**: Every struct that will be stored via `persist<T>` or in `AddressManager<T>` **must** be `std::is_trivially_copyable`. This means: no vtables, no non-trivial constructors/destructors, no non-trivial copy/move, no raw heap pointers (use `fptr<T>` slot indices instead).

2. **No raw pointers in persistent structs**: Use `fptr<T>` (a `uint` slot index) wherever a pointer would normally appear. The actual raw pointer is resolved at runtime via `AddressManager<T>::GetManager()[slot]`.

3. **Load/Save only in constructors/destructors**: Persistent object constructors and destructors must only load/save state. Allocation and deallocation are performed by explicit `AddressManager` static methods (`Create`, `CreateArray`, `Delete`, `DeleteArray`).

4. **Single address space per type**: Each type `T` has its own `AddressManager<T>` singleton. Cross-type references use `fptr<T>` slot indices.

5. **Binary portability**: All persistent data is stored as raw bytes. This means the layout must be stable across compilers and platforms (use fixed-width types where endianness matters — see Phase 2).
