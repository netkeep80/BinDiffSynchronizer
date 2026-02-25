# Phase 2 Plan: Persistent nlohmann::json Object Tree

**Status:** In Progress (Tasks 2.1, 2.2, 2.3 and 2.4 Complete)

## Goal

Convert the nlohmann::json internal representation to use persistent objects and persistent pointers from `persist.h` / `fptr<T>`, so that the JSON tree is stored as a persistent in-memory database — eliminating the need for JSON/CBOR parsing on every load.

The vision from Issue #14: **nlohmann::json becomes a persistent database**. When the process starts, the persistent store is memory-mapped or directly loaded from files — the entire object tree is immediately accessible with zero deserialization overhead.

## Background: How nlohmann::json Stores Data Internally

nlohmann/json uses a `json_value` union internally, where each JSON node stores one of the following C++ types:

| JSON type    | C++ type stored inside `json_value`                |
|--------------|----------------------------------------------------|
| `object`     | `std::map<std::string, basic_json>*`               |
| `array`      | `std::vector<basic_json>*`                         |
| `string`     | `std::string*`                                     |
| `boolean`    | `bool`                                             |
| `number_int` | `int64_t`                                          |
| `number_float`| `double`                                          |
| `null`       | —                                                  |

The key challenge: these C++ standard library types use heap-allocated memory with transient pointers (invalidated on process restart). To make them persistent, we need either:

1. **Option A**: Wrap std classes with `persist<T>` where possible, and replace internal pointers with `fptr<T>` persistent pointers.
2. **Option B**: Write custom persistent analogs that replace `std::map`, `std::vector`, `std::string` with persistence-aware variants backed by `AddressManager<T>` or `PageDevice`.

---

## Phase 2 Tasks

### Task 2.1 — Feasibility Study: Wrapping std Classes with `persist<T>` ✓ DONE

**Objective:** Verify whether `persist<T>` can wrap the std classes used by nlohmann/json.

**Challenge:** `persist<T>` uses `sizeof(_T)` to save/load the object as raw bytes. This works for **POD (Plain Old Data)** types and fixed-size structs, but breaks for types with internal heap pointers (like `std::string`, `std::vector`, `std::map`) because:
- Their size is fixed (e.g., `sizeof(std::string) == 32` on most platforms), but they **own heap memory** not captured by raw byte copy.
- Saving raw bytes saves dangling pointers, not the pointed-to data.

**Results:**

| Type | Works? | Reason |
|------|--------|--------|
| `persist<bool>` | ✓ YES | POD — raw bytes == value, no heap allocation |
| `persist<int64_t>` | ✓ YES | POD — raw bytes == value, no heap allocation |
| `persist<double>` | ✓ YES | POD — raw bytes == value, no heap allocation |
| `persist<std::string>` | ✗ NO | `std::string` is not trivially copyable; long strings have data on the heap |
| `persist<std::vector<int>>` | ✗ NO | Elements always on heap; `std::vector` is not trivially copyable |
| `persist<std::map<std::string,int>>` | ✗ NO | Red-black tree nodes always on heap; not trivially copyable |

**Deliverables committed:**
- `experiments/test_persist_std.cpp` — standalone executable documenting the feasibility study
- `tests/test_persist_std.cpp` — 7 Catch2 tests integrated into the CI test suite (all passing)

**Conclusion:** `persist<T>` cannot directly wrap `std::string`, `std::vector`, or `std::map`. Custom persistent analogs are required (Task 2.2).

---

### Task 2.2 — Design Persistent Analogs of std Types ✓ DONE

**Objective:** Design drop-in persistent replacements for the three core std types used by nlohmann/json.

#### 2.2.1 — `jgit::persistent_string` (replaces `std::string`)

Requirements:
- Fixed-size in-place storage for short strings (SSO — Short String Optimization, up to 23 chars inline).
- For longer strings: store the string content in `ObjectStore` (content-addressed), keep only the `ObjectId` (64-char hex, fixed-size) as the persistent reference.
- Compatible with `persist<persistent_string>` (no heap pointers in the stored struct).

Design:
```cpp
namespace jgit {
    struct persistent_string {
        static constexpr size_t SSO_SIZE = 23;
        char   sso_buf[SSO_SIZE + 1];  // inline storage
        bool   is_long;                // flag: true if ObjectId is used
        char   object_id[64 + 1];      // SHA-256 hex when is_long == true
        // Methods: assign, c_str, size, operator==, etc.
    };
}
```

#### 2.2.2 — `jgit::persistent_array` (replaces `std::vector<basic_json>`)

Requirements:
- Fixed-capacity array stored inline in a `PageDevice` slab.
- For dynamic growth: use a linked-list of fixed-size slabs managed by `AddressManager`.
- Each element is a `jgit::persistent_json_value` (see Task 2.3).

Design:
```cpp
namespace jgit {
    template<typename T, size_t Capacity = 16>
    struct persistent_array_slab {
        T       data[Capacity];
        size_t  size;
        uint32_t next_slab_id;  // fptr-style index into AddressManager, 0 = none
    };
}
```

#### 2.2.3 — `jgit::persistent_map` (replaces `std::map<std::string, basic_json>`)

Requirements:
- Sorted key-value store where keys are `persistent_string` and values are `persistent_json_value`.
- Backed by a B-tree or sorted array of fixed-size nodes in `AddressManager`.
- Each node fits in a fixed-size block (no heap allocation).

**Deliverables committed:**
- `jgit/persistent_string.h` — SSO-based inline string (≤23 chars inline, longer in `long_buf[]`); trivially copyable; compatible with `persist<T>`
- `jgit/persistent_array.h` — Fixed-capacity inline array with `next_slab_id` for multi-slab chaining
- `jgit/persistent_map.h` — Sorted array of key-value pairs with binary search; keys are `persistent_string`
- `tests/test_persistent_string.cpp` — 8 Catch2 tests (layout, SSO, long path, assignment, comparison, conversion, raw-byte round-trip)
- `tests/test_persistent_array.cpp` — 10 Catch2 tests (layout, push/pop, access, erase, clear, iteration, chaining, raw-byte round-trip)
- `tests/test_persistent_map.cpp` — 11 Catch2 tests (layout, insert, sorted order, update, find, erase, capacity, iteration, raw-byte round-trip)

All 65 tests pass (29 from Phase 1 + 36 new Task 2.2 tests) on Linux GCC.

---

### Task 2.3 — Design `jgit::persistent_json_value` (Core Node) ✓ DONE

**Objective:** Design the core persistent node type that replaces `nlohmann::json::json_value`.

```cpp
namespace jgit {
    enum class json_type : uint8_t {
        null, boolean, number_int, number_float, string, array, object
    };

    struct persistent_json_value {
        json_type type;
        union {
            bool     boolean_val;
            int64_t  int_val;
            double   float_val;
            persistent_string string_val;   // inline or ObjectId reference
            uint32_t array_id;   // fptr index into AddressManager<persistent_array_slab>
            uint32_t object_id;  // fptr index into AddressManager<persistent_map_node>
        };
    };
}
```

Key property: **`sizeof(persistent_json_value)` is fixed** — no hidden heap pointers. This makes it compatible with `persist<persistent_json_value>` and `AddressManager<persistent_json_value>`.

---

### Task 2.4 — Implement `jgit::PersistentJsonStore` ✓ DONE

**Objective:** Implement a persistent JSON document store that:
1. Stores a JSON document as a tree of `persistent_json_value` nodes.
2. Loads the entire tree from disk into memory on startup with zero parsing overhead.
3. Uses flat pools of fixed-size structs to manage node lifetimes.
4. Provides a read/write API compatible with nlohmann::json's interface.

**Deliverables committed:**
- `jgit/persistent_json_value.h` — Task 2.3 implementation: fixed-size node with `json_type` discriminator, trivially copyable, compatible with `persist<T>`
- `jgit/persistent_json_store.h` — the unified store class with three flat pools (value_pool, array_pool, object_pool); multi-slab chaining for arrays/objects with > 16 entries
- `tests/test_persistent_json_value.cpp` — 14 Catch2 tests (all 7 JSON types, layout, raw-byte round-trip)
- `tests/test_persistent_json_store.cpp` — 20 Catch2 tests (import/export round-trip, get/set, navigation helpers, multi-slab, pool counters)

**Implemented API:**
```cpp
namespace jgit {
    class PersistentJsonStore {
    public:
        PersistentJsonStore();                         // in-memory store, ready to use

        // Import from nlohmann::json (one-time conversion)
        uint32_t import_json(const nlohmann::json& doc);

        // Read as nlohmann::json (for compatibility / export)
        nlohmann::json export_json(uint32_t root_id) const;

        // Direct persistent access (zero-parse)
        persistent_json_value& get_node(uint32_t id);
        uint32_t set_node(uint32_t id, const persistent_json_value& val);

        // Navigation
        uint32_t get_field(uint32_t obj_id, const char* key) const;
        uint32_t get_index(uint32_t arr_id, size_t index) const;

        // Pool counters (for testing / debugging)
        size_t node_count()   const noexcept;
        size_t array_count()  const noexcept;
        size_t object_count() const noexcept;
    };
}
```

All 99 tests pass (65 from Tasks 2.1–2.2 + 34 new from Tasks 2.3–2.4) on Linux GCC.

---

### Task 2.5 — Integration with Phase 1 ObjectStore

The Phase 1 `ObjectStore` (CBOR + SHA-256) stores immutable, content-addressed blobs — ideal for **commit history** and **diffs** (git-like versioning). The Phase 2 `PersistentJsonStore` manages the **live working tree** of mutable JSON nodes.

Integration model (analogous to Git's working tree vs. object store):

```
Working Tree (PersistentJsonStore)  →  Stage (diff)  →  Object Store (immutable commits)
```

- `PersistentJsonStore` is the "file system" where edits happen.
- When committing, the live tree is serialized to CBOR and stored in `ObjectStore` as an immutable snapshot.
- On checkout/restore, the snapshot is loaded from `ObjectStore` and imported back into `PersistentJsonStore`.

This mirrors Git's model: fast live edits in the working tree, with commit history stored immutably.

---

### Task 2.6 — Write Unit Tests

All new types must have unit tests before implementation is considered complete.

Test files to create in `tests/`:

| File | Tests |
|------|-------|
| `test_persist_std.cpp` | Feasibility of `persist<std::string>` etc. (expected failures documented) |
| `test_persistent_string.cpp` | SSO path, long string path, round-trip |
| `test_persistent_array.cpp` | Single slab, multi-slab growth, index access |
| `test_persistent_map.cpp` | Insert, lookup, iterate, missing key |
| `test_persistent_json_value.cpp` | All 7 types (null, bool, int, float, string, array, object) |
| `test_persistent_json_store.cpp` | import/export round-trip, zero-parse reload, integration with ObjectStore |

---

### Task 2.7 — Performance Benchmark

**Objective:** Quantitatively demonstrate the benefit of the persistent store over CBOR+ObjectStore for repeated loads.

Benchmark scenarios:
1. **Cold load via CBOR**: Parse a 1 MB JSON file → serialize to CBOR → store in ObjectStore → retrieve → parse CBOR → measure latency.
2. **Warm load via PersistentJsonStore**: Load the same document from the persistent store with zero parsing → measure latency.
3. **Memory comparison**: Compare peak memory usage for both approaches.

Expected result: PersistentJsonStore should show **O(1) startup time** (memory-map or direct load) vs. **O(n) parsing time** for CBOR.

Deliverable: `experiments/benchmark_persistent_vs_cbor.cpp`

---

## Implementation Order

```
Task 2.1  →  Task 2.2  →  Task 2.3  →  Task 2.4  →  Task 2.5  →  Task 2.6  →  Task 2.7
Feasibility  Analogs     Core node   Store API   Integration   Tests        Benchmark
```

Each task should be committed as a separate commit so progress is preserved incrementally.

---

## Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| `AddressManager` fixed size (1024 entries) limits document size | Extend to dynamic size or use `PageDevice` for unlimited capacity |
| `persistent_map` B-tree complexity | Start with sorted array of nodes; optimize later |
| ABI/alignment issues with packed structs | Add `static_assert(sizeof(...) == expected)` for all persistent types |
| Platform differences in type sizes | Use `<cstdint>` fixed-width types only; no `int`/`long` in persistent structs |
| Large `persistent_string` SSO wastes space for short JSON keys | Tune SSO_SIZE based on real JSON key distribution |

---

## Success Criteria

- [x] Phase 2.1: Feasibility experiment script committed and results documented.
- [x] Phase 2.2: All three persistent analogs implemented (`persistent_string`, `persistent_array`, `persistent_map`) with 36 unit tests passing.
- [x] Phase 2.3–2.4: `persistent_json_value` and `PersistentJsonStore` implemented.
- [x] Phase 2.4: `PersistentJsonStore` can import any `nlohmann::json` and export it back identically.
- [ ] Phase 2.5: `PersistentJsonStore` snapshots integrate with Phase 1 `ObjectStore`.
- [ ] Phase 2.6: All unit tests pass (CI green on Linux GCC, Linux Clang, Windows MSVC).
- [ ] Phase 2.7: Benchmark shows measurable reload speedup for a 1 MB JSON document.

---

## Relation to Issue #14

This plan directly addresses the requirements stated in Issue #14:

> "Использовать динамическое представление JSON в дереве объектов nlohmann::json, но с персистными объектами и персистными указателями."
> *(Use dynamic JSON representation in nlohmann::json object tree, but with persistent objects and persistent pointers.)*

> "Это позволит jgit мгновенно загружать персистное хранилище из файлов в память."
> *(This will allow jgit to instantly load the persistent store from files into memory.)*

> "nlohmann::json становится персистной базой данных."
> *(nlohmann::json becomes a persistent database.)*

> "Верификация возможности обеспечения персистности для std классов используемых nlohmann::json или написание собственных персистных аналогов."
> *(Verification of the ability to provide persistence for std classes used by nlohmann::json, or writing custom persistent analogs.)*

The plan:
- Starts with a **feasibility study** (Task 2.1) of wrapping std classes — this is the "verification" step.
- Proceeds to **custom persistent analogs** (Tasks 2.2–2.3) as the expected outcome.
- Delivers a complete **persistent JSON store** (Task 2.4) with **instant load** capability.
- Integrates with Phase 1's **content-addressed commit history** (Task 2.5).
