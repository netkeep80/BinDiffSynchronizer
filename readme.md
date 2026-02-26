# BinDiffSynchronizer

## Overview

BinDiffSynchronizer is a C++ project providing a persistent object infrastructure and binary diff synchronization utilities. The primary goal is to implement `pjson` — a persistent analogue of [`nlohmann::json`](https://github.com/nlohmann/json) — where all JSON values live in a single persistent address space and survive process restarts.

---

## Persistent Infrastructure

### Core Classes

#### `persist<T>` (`persist.h`)

A wrapper that saves and loads the raw bytes of a trivially-copyable type `T` to/from a file.

**Constraint**: `T` must satisfy `std::is_trivially_copyable`. No heap-allocated members are allowed inside `T` — use `fptr<T>` (a slot index) instead of raw pointers.

**Constructors**:
- `persist()` — derive filename from `this` address (ASLR-dependent; different on each run).
- `persist(const T& val)` — initialise from value, save to address-derived file on destruction.
- `persist(const char* filename)` — load from / save to a named file (stable across restarts).
- `persist(const std::string& filename)` — convenience overload.

**Example**:
```cpp
persist<double> counter("counter.persist");
counter = counter + 1.0;   // increments on every run
```

---

#### `AddressManager<T, AddressSpace>` (`persist.h`)

Manages a persistent address space of up to `AddressSpace` (default: 1024) slots of type `T`. Each slot stores one object or one contiguous array of objects.

- Slot 0 is reserved (null/invalid).
- The slot table itself is stored via `persist<__info[AddressSpace]>`.
- Single objects are stored in `<typename>.extend`; arrays in `<typename>_arr_<index>.extend`.

**Static methods**:
- `Create(name)` — allocate a named single-object slot; returns slot index.
- `CreateArray(count, name)` — allocate a named array slot; returns slot index.
- `Delete(index)` — free a single-object slot.
- `DeleteArray(index)` — free an array slot.
- `Find(name)` — look up a slot by name; returns 0 if not found.
- `GetCount(index)` — returns element count for an array slot (0 = single object).

---

#### `fptr<T>` (`persist.h`)

A persistent pointer: stores a slot index (`unsigned`) into `AddressManager<T>`. Because it stores only a `uint`, `fptr<T>` is itself trivially copyable and can be embedded in persistent structs.

**Operations**:
- `New(name)` — allocate a new persistent object and point to it.
- `NewArray(count, name)` — allocate a new persistent array and point to it.
- `Delete()` — free the pointed-to object.
- `DeleteArray()` — free the pointed-to array.
- `operator*`, `operator->` — dereference.
- `operator[](idx)` — array element access.
- `count()` — element count of the array (0 for single objects).
- `addr()` — raw slot index.
- `set_addr(a)` — directly set slot index (advanced use).

**Example**:
```cpp
fptr<double> a("main.a");
if (a == NULL) {
    a.New("main.a");
    *a = 0.0;
}
*a += 1.0;   // persists across process restarts
```

---

#### `Cache<T, CacheSize, SpaceSize>` (`PageDevice.h`)

An LRU-style virtual page cache. Provides `GetData(index, forWrite)` for page access. Subclasses must implement `Load(index, page)` and `Save(index, page)`.

**Note**: `Flush()` saves all dirty pages to backing store. It must be called from the concrete leaf destructor — not from `Cache::~Cache()` — because vtable dispatch is already partially reset by then.

---

#### `PageDevice<PageSize, PoolSize, SpaceSize, CachePolicy>` (`PageDevice.h`)

Abstract paged storage device. Each page is `2^PageSize` bytes; the device holds `2^SpaceSize` pages; up to `PoolSize` pages are held in the cache pool.

---

#### `MemoryDevice<MemorySize, PageSize, PoolSize, CachePolicy, PageDevice>` (`PageDevice.h`)

Byte-addressable memory over a `PageDevice`. Provides:
- `Read(address, data, size)` — read bytes across page boundaries.
- `Write(address, data, size)` — write bytes across page boundaries.
- `ReadObject<T>(address, obj)` — typed convenience wrapper.
- `WriteObject<T>(address, obj)` — typed convenience wrapper.

---

#### `StaticPageDevice` (`StaticPageDevice.h`)

Concrete `PageDevice` backed by a static in-memory array of pages. Calls `Flush()` in its destructor. Suitable for in-process testing.

---

### Persistent Container Classes (Phase 1)

#### `pstring` (`pstring.h`)

A persistent string. Stores its character data in `AddressManager<char>`. The header struct is trivially copyable for use with `persist<pstring_data>`.

```cpp
pstring s;
s.assign("hello");
// persists across restarts
```

---

#### `pvector<T>` (`pvector.h`)

A persistent dynamic array. Stores element data in `AddressManager<T>`. Supports `push_back`, `pop_back`, `operator[]`, `size()`, `capacity()`, `clear()`.

```cpp
pvector<int> v;
v.push_back(42);
// persists across restarts
```

---

#### `pmap<K, V>` (`pmap.h`)

A persistent key-value map backed by a sorted `pvector<pmap_entry<K, V>>`. Supports `insert`, `find`, `erase`, `operator[]`, iteration.

**Constraint**: `K` and `V` must be trivially copyable.

---

#### `pallocator<T>` (`pallocator.h`)

An STL-compatible allocator backed by `AddressManager<T>`. Enables passing persistent containers to standard library algorithms.

```cpp
std::vector<int, pallocator<int>> v;
v.push_back(42);
// memory lives in AddressManager<int>; slot index must be persisted separately
```

**Constraint**: `T` must be trivially copyable (enforced by `AddressManager<T>`).

---

#### `pjson` (`pjson.h`)

A persistent discriminated-union JSON value — the persistent analogue of `nlohmann::json`. All JSON values live in persistent address spaces managed by `AddressManager` and survive process restarts when the backing `pjson_data` header is stored via `persist<pjson_data>`.

**Design**: Option B — a custom persistent discriminated union built directly on `fptr<T>`, `pstring`, `pvector`, and `pmap` primitives.  `nlohmann::basic_json` was not used directly because it assumes heap-allocated raw pointers, which are incompatible with the slot-index-based `AddressManager`.

**Value types**: `null`, `boolean`, `integer` (int64), `uinteger` (uint64), `real` (double), `string`, `array`, `object`.

**Layout**: `pjson_data` is a trivially-copyable 16-byte header.  Arrays are backed by `AddressManager<pjson_data>`; objects by `AddressManager<pjson_kv_pair>` (sorted by key for O(log n) lookup).

```cpp
pjson_data d{};
pjson v(d);

// Primitive values
v.set_bool(true);
v.set_int(-42);
v.set_real(3.14);
v.set_string("hello");

// Arrays
v.set_array();
pjson(v.push_back()).set_int(1);
pjson(v.push_back()).set_string("two");

// Objects
v.set_object();
pjson(v.obj_insert("name")).set_string("Alice");
pjson(v.obj_insert("age")).set_int(30);
pjson_data* age = v.obj_find("age");   // O(log n) binary search
v.obj_erase("name");

// Release all heap allocations
v.free();
```

**Constraint**: `pjson_data` must be stored with zero-initialisation before first use (`pjson_data d{};`).

---

### Design Principles

1. **Trivial copyability**: Every struct stored via `persist<T>` or `AddressManager<T>` must satisfy `std::is_trivially_copyable`. No vtables, no non-trivial constructors/destructors, no raw heap pointers.

2. **No raw pointers in persistent structs**: Use `fptr<T>` (a `uint` slot index) wherever a pointer would normally appear. The raw pointer is resolved at runtime.

3. **Load/Save only in constructors/destructors**: Persistent object constructors and destructors must only load/save state. Allocation and deallocation are performed by explicit `AddressManager` static methods.

4. **Single address space per type**: Each type `T` has its own `AddressManager<T>` singleton.

---

## Build

Requires C++17 and CMake 3.15+.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --build-config Release --output-on-failure
```

Or on Windows:
```bat
mkdir build
cd build
cmake ..
cmake --build . --config=Release
ctest -C Release
```

---

## Лицензия / License

Unlicense
