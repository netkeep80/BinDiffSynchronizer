# jgit — Phase 1 Implementation Plan

## Overview

This document defines the detailed implementation plan for **Phase 1** of the jgit project — a temporal JSON database inspired by Git's versioning model. It is based on the analysis of the current BinDiffSynchronizer codebase ([analysis.md](analysis.md)), the development roadmap ([plan.md](plan.md)), and the project description ([readme.md](readme.md)).

**Goal of Phase 1**: Establish the foundational infrastructure — a compilable, cross-platform, tested codebase with a working content-addressed object store for JSON data in binary format.

**Duration**: 1–3 months (short-term plan from `plan.md`).

---

## Phase 1 Scope

Phase 1 corresponds to the "Краткосрочный план (1-3 месяца)" from `plan.md` and the "Высокий приоритет" tasks from Directions 1 and 2. It is deliberately limited to the minimum viable foundation:

| # | Task | Source in plan.md |
|---|------|-------------------|
| 1 | Fix existing bugs | Direction 2, High priority, #1 |
| 2 | Migrate to C++17 with CMake | Direction 2, High priority, #2–3 |
| 3 | Add nlohmann/json integration | Direction 1, High priority, #2 |
| 4 | Implement content-addressed object store | Direction 1, High priority, #1 |
| 5 | Write unit tests for existing and new code | Direction 2, Medium priority, #5 |

---

## Step 1: Fix Existing Bugs

**Rationale**: The codebase contains known bugs that prevent correct compilation and runtime behavior. These must be fixed before any new development.

### 1.1 Fix bit-shift operator in `PageDevice.h`

- **File**: `PageDevice.h`, lines ~117 and ~131
- **Bug**: `unsigned Index = (Address & PageMask) > PageSize;` — uses `>` (comparison) instead of `>>` (right bit-shift)
- **Fix**: Change `>` to `>>` in both `Read` and `Write` methods of `MemoryDevice`
- **Test**: Add a unit test that verifies correct page index calculation for a known address

### 1.2 Fix `NULL` used as integer in `persist.h`

- **File**: `persist.h`, `AddressManager::Create`
- **Bug**: `unsigned addr = NULL;` — uses `NULL` macro (defined as `0` or `(void*)0` depending on context) for an unsigned integer variable; semantically incorrect
- **Fix**: Change to `unsigned addr = 0;`
- **Test**: Add a unit test that verifies object creation at address 0 is handled correctly

### 1.3 Fix missing return in `fptr<_T>::operator=`

- **File**: `persist.h`
- **Bug**: `operator=` does not return `*this`, which is undefined behavior in C++
- **Fix**: Add `return *this;` at the end of `fptr<_T>::operator=(const char* pName)`
- **Test**: Verify chained assignment compiles and works correctly

### 1.4 Verify `persist(const _T& ref)` constructor

- **File**: `persist.h`
- **Issue**: The copy constructor may be missing placement new initialization (noted in `analysis.md`)
- **Action**: Review the constructor, add placement new if needed, add unit test covering this path

---

## Step 2: Migrate to Modern C++17 with CMake

**Rationale**: The current code uses MSVC-specific extensions and deprecated headers. Moving to standard C++17 with CMake enables cross-platform development and opens the project to contributors on Linux and macOS.

### 2.1 Replace deprecated and MSVC-specific constructs

| Old code | Replacement | File(s) |
|----------|-------------|---------|
| `#include <strstream>` | `#include <sstream>` | `persist.h` |
| `__forceinline` | `inline` | `persist.h`, `PageDevice.h` |
| `typeof(x)` | `decltype(x)` | `persist.h` |
| `_ultoa(addr, buf, 16)` | `std::to_string` + `std::hex` via `std::ostringstream` | `persist.h` |
| `strcpy` / `strcat` | `std::string` operations | `persist.h` |
| `unsigned addr = NULL` | `unsigned addr = 0` | `persist.h` |
| Windows path `".\\"` | `std::filesystem::path` (C++17) | `persist.h` |

### 2.2 Add `std::filesystem` support for file paths

- Replace all hardcoded `".\"` Windows path prefixes with `std::filesystem::path`
- Add proper error checking when opening/closing files (`std::ofstream`/`std::ifstream`)
- Handle the case where the storage directory does not exist (create it automatically)

### 2.3 Create `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.16)
project(BinDiffSynchronizer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Main library (header-only)
add_library(bindiff INTERFACE)
target_include_directories(bindiff INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# Tests (added in Step 5)
enable_testing()
add_subdirectory(tests)
```

**Compiler support targets**: GCC 7+, Clang 5+, MSVC 2017+

### 2.4 Validate build on multiple platforms

- Verify compilation on Linux (GCC and Clang) — these are immediately testable in CI
- Verify compilation on Windows (MSVC) — document requirements

---

## Step 3: Integrate nlohmann/json

**Rationale**: nlohmann/json is the chosen JSON library (from `plan.md` and `analysis.md`). It provides JSON Pointer, JSON Patch, and binary serialization formats (CBOR, MessagePack) — all required primitives for jgit.

### 3.1 Add nlohmann/json as a header-only dependency

- Download `nlohmann/json.hpp` (single-header version, current stable release)
- Place in `third_party/nlohmann/json.hpp`
- Update `CMakeLists.txt` to add `third_party/` to include paths

```cmake
# In CMakeLists.txt
target_include_directories(bindiff INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
```

### 3.2 Evaluate and select binary storage format

Evaluate the following nlohmann/json binary formats for jgit object storage:

| Format | Pros | Cons |
|--------|------|------|
| **CBOR** (RFC 7049) | Compact, self-describing, supports all JSON types, streaming | Less human-readable |
| **MessagePack** | Very compact, fast, widely supported | Less self-describing than CBOR |
| **BSON** | MongoDB standard, datetime support | Larger than CBOR for typical JSON |
| **UBJSON** | Simple, clean spec | Less widely used |

**Recommendation**: Use **CBOR** as the primary format. It is the most self-describing binary format, has an RFC standard, and is natively supported by nlohmann/json via `nlohmann::json::to_cbor()` / `nlohmann::json::from_cbor()`.

### 3.3 Write serialization utilities

Create `jgit/serialization.h` with helper functions:

```cpp
namespace jgit {

// Serialize JSON to CBOR bytes
std::vector<uint8_t> to_bytes(const nlohmann::json& doc);

// Deserialize JSON from CBOR bytes
nlohmann::json from_bytes(const std::vector<uint8_t>& data);

} // namespace jgit
```

### 3.4 Write unit tests for serialization

- Round-trip test: `json → CBOR → json` preserves all data types
- Test with all JSON types: null, bool, number (int/float), string, array, object
- Test with nested structures (depth ≥ 3)
- Test with empty object and empty array

---

## Step 4: Implement Content-Addressed Object Store

**Rationale**: The object store is the heart of jgit (Direction 1, High priority, #1 in `plan.md`). It maps a content hash to a binary object, enabling deduplication and immutable history.

### 4.1 Hash function

Use **SHA-256** for content addressing (aligned with modern Git's direction and `plan.md`).

- Add `third_party/sha256.hpp` — a public-domain single-header SHA-256 implementation, or use `std::hash` + a simple hex encoding as a placeholder for Phase 1 (to be replaced with SHA-256 in Phase 2)
- **Phase 1 decision**: use SHA-256 directly to avoid rework later

Create `jgit/hash.h`:

```cpp
namespace jgit {

// Returns hex-encoded SHA-256 of the input bytes
std::string sha256_hex(const std::vector<uint8_t>& data);

// Shorthand: hash string → first 2 chars (dir) + rest (filename), like Git
struct ObjectId {
    std::string hex;  // 64-char hex string
    std::string dir() const;   // hex.substr(0, 2)
    std::string file() const;  // hex.substr(2)
};

ObjectId hash_object(const std::vector<uint8_t>& data);

} // namespace jgit
```

### 4.2 Object store structure on disk

Following Git's layout (as described in `plan.md`):

```
.jgit/
├── objects/
│   ├── ab/
│   │   └── cd1234ef...  ← CBOR-encoded JSON object
│   └── ...
├── refs/
│   └── heads/
│       └── main         ← text file with current commit hash
└── HEAD                 ← text file: "ref: refs/heads/main"
```

### 4.3 Implement `ObjectStore` class

Create `jgit/object_store.h`:

```cpp
namespace jgit {

class ObjectStore {
public:
    // Initialize or open a jgit repository at `root_path`
    explicit ObjectStore(const std::filesystem::path& root_path);

    // Store a JSON object, return its ObjectId
    ObjectId put(const nlohmann::json& object);

    // Retrieve a JSON object by hash; returns std::nullopt if not found
    std::optional<nlohmann::json> get(const ObjectId& id) const;

    // Check if an object exists
    bool exists(const ObjectId& id) const;

    // Initialize an empty repository (create .jgit/ structure)
    static ObjectStore init(const std::filesystem::path& path);

private:
    std::filesystem::path root_;

    std::filesystem::path object_path(const ObjectId& id) const;
};

} // namespace jgit
```

**Implementation notes**:
- `put()`: serialize to CBOR, compute SHA-256, write to `.jgit/objects/{2-char-dir}/{rest-of-hash}`; skip write if file already exists (idempotent)
- `get()`: read file, deserialize from CBOR, return JSON
- Use `std::filesystem::create_directories()` to create parent dirs on first write

### 4.4 Unit tests for ObjectStore

- `test_put_and_get_roundtrip`: store any JSON, retrieve by hash, verify equality
- `test_put_is_idempotent`: calling `put()` twice with the same object gives the same hash and doesn't error
- `test_get_missing_returns_nullopt`: `get()` on a non-existent hash returns `std::nullopt`
- `test_exists`: verify `exists()` returns false before `put()` and true after
- `test_init_creates_directory_structure`: verify `.jgit/objects/`, `.jgit/refs/heads/`, `HEAD` exist after `init()`
- `test_different_objects_have_different_hashes`: verify two distinct JSON objects produce different hashes

---

## Step 5: Unit Tests

**Rationale**: `plan.md` (Direction 2, Medium priority, #5) calls for unit tests. Tests are essential for verifying bug fixes and new functionality, and for enabling CI.

### 5.1 Testing framework

Use **Catch2** (header-only, v3):
- Simple to integrate (single header or CMake FetchContent)
- Widely used in C++ projects
- No need for separate test runner binary configuration

Add to `CMakeLists.txt`:
```cmake
include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)
```

### 5.2 Test directory structure

```
tests/
├── CMakeLists.txt
├── test_bugs.cpp           # Tests for bug fixes (Steps 1.1–1.4)
├── test_serialization.cpp  # Tests for CBOR serialization (Step 3.4)
├── test_hash.cpp           # Tests for SHA-256 hashing (Step 4.1)
└── test_object_store.cpp   # Tests for ObjectStore (Step 4.4)
```

### 5.3 CI with GitHub Actions

Create `.github/workflows/ci.yml`:

```yaml
name: CI

on: [push, pull_request]

jobs:
  build-and-test:
    strategy:
      matrix:
        os: [ubuntu-latest, windows-latest]
        compiler: [gcc, clang]
        exclude:
          - os: windows-latest
            compiler: clang

    runs-on: ${{ matrix.os }}

    steps:
      - uses: actions/checkout@v4
      - name: Configure CMake
        run: cmake -B build -DCMAKE_BUILD_TYPE=Release
      - name: Build
        run: cmake --build build
      - name: Test
        run: ctest --test-dir build --output-on-failure
```

---

## New Files to Create in Phase 1

```
BinDiffSynchronizer/
├── CMakeLists.txt              ← New: cross-platform build system
├── .github/
│   └── workflows/
│       └── ci.yml              ← New: CI pipeline
├── jgit/
│   ├── hash.h                  ← New: SHA-256 content addressing
│   ├── serialization.h         ← New: JSON ↔ CBOR conversion
│   └── object_store.h          ← New: content-addressed object store
├── tests/
│   ├── CMakeLists.txt          ← New: test build config
│   ├── test_bugs.cpp           ← New: bug fix regression tests
│   ├── test_serialization.cpp  ← New: serialization tests
│   ├── test_hash.cpp           ← New: hash function tests
│   └── test_object_store.cpp   ← New: object store tests
└── third_party/
    ├── nlohmann/
    │   └── json.hpp            ← New: nlohmann/json (single header)
    └── sha256.hpp              ← New: SHA-256 implementation
```

---

## Files Modified in Phase 1

| File | Changes |
|------|---------|
| `PageDevice.h` | Fix `>` → `>>` in `MemoryDevice::Read` and `MemoryDevice::Write` |
| `persist.h` | Fix `NULL` → `0`, fix missing `return *this`, replace MSVC extensions, add `std::filesystem` |

---

## Out of Scope for Phase 1

The following are explicitly deferred to Phase 2 and beyond:

- Commit/checkout system (`json-commit` objects)
- Branch and tag management (`refs/`)
- JSON Patch generation and diff computation
- `$ref` cross-repository links
- Merge algorithm
- Network replication and synchronization
- WAL (Write-Ahead Log) and crash recovery
- CLI interface (`jgit init`, `jgit commit`, etc.)
- Python/Rust bindings

---

## Acceptance Criteria for Phase 1

Phase 1 is complete when all of the following are true:

1. **All known bugs are fixed** and covered by regression unit tests
2. **The project compiles cleanly** with CMake on Linux (GCC 7+, Clang 5+) with `-Wall -Wextra` and no errors or warnings
3. **All unit tests pass** (test_bugs, test_serialization, test_hash, test_object_store)
4. **CI runs automatically** on every push and pull request
5. **`ObjectStore::put` and `ObjectStore::get`** work correctly end-to-end: any JSON value can be stored and retrieved by its SHA-256 hash
6. **nlohmann/json** is integrated and JSON↔CBOR round-trip is verified by tests
7. **No MSVC-specific code** remains in the header files (`persist.h`, `PageDevice.h`)

---

## Dependencies

| Dependency | Version | Purpose | How to include |
|------------|---------|---------|----------------|
| [nlohmann/json](https://github.com/nlohmann/json) | ≥ 3.11 | JSON parsing, CBOR serialization | Single header in `third_party/` |
| [Catch2](https://github.com/catchorg/Catch2) | v3.5.x | Unit testing framework | CMake FetchContent |
| SHA-256 | public domain | Content addressing | Single header in `third_party/` |

All dependencies are header-only or fetched via CMake, maintaining the project's zero-installation philosophy.

---

## References

- [analysis.md](analysis.md) — project analysis: strengths, weaknesses, jgit concept evaluation
- [plan.md](plan.md) — full development roadmap, all phases
- [readme.md](readme.md) — project description and jgit overview
- [nlohmann/json](https://github.com/nlohmann/json) — JSON for Modern C++
- [RFC 6902: JSON Patch](https://datatracker.ietf.org/doc/html/rfc6902) — delta format for Phase 2
- [RFC 6901: JSON Pointer](https://datatracker.ietf.org/doc/html/rfc6901) — node addressing
- [CBOR (RFC 7049)](https://cbor.io/) — binary storage format
- [Git Internals](https://git-scm.com/book/en/v2/Git-Internals-Plumbing-and-Porcelain) — architecture reference

---

*Document created: 2026-02-25*
