# jgit — Phase 2 Implementation Plan

## Status: ✓ Task 1 COMPLETED (2026-02-25)

Task 1 (commit system and branch management) is fully implemented and tested.
61/61 unit tests pass. See progress table below.

## Overview

This document defines the implementation plan for **Phase 2** of the jgit project — the commit system, branch management, and JSON Patch integration. It builds directly on the Phase 1 foundation (content-addressed object store, CBOR serialization, SHA-256 hashing).

**Goal of Phase 2**: Implement the temporal versioning layer — commits, branches, tags, checkout, history log — making jgit a true version-controlled JSON database.

**Duration**: 3–6 months (medium-term plan from `plan.md`).

---

## Phase 2 Scope

Phase 2 corresponds to the "Среднесрочный план (3-6 месяца)" from `plan.md`.

| # | Task | Source in plan.md | Status |
|---|------|-------------------|--------|
| 1 | Implement commit system and branches | Medium-term, #1 | ✓ Done |
| 2 | Integrate JSON Patch (RFC 6902) as delta format | Medium-term, #2 | Pending |
| 3 | Support `$ref` links through `fptr<T>` | Medium-term, #3 | Pending |
| 4 | Create CLI jgit (init, commit, log, diff, checkout) | Medium-term, #4 | Pending |
| 5 | Publish article / documentation about jgit concept | Medium-term, #5 | Pending |

---

## Task 1: Commit System and Branch Management ✓

### What was implemented

#### `jgit/commit.h` — Commit object

The `Commit` struct represents a versioned snapshot of a JSON document:

```cpp
struct Commit {
    ObjectId root;                    // SHA-256 of the committed JSON blob
    std::optional<ObjectId> parent;   // Parent commit (nullopt = root commit)
    std::string author;
    int64_t     timestamp;            // Unix epoch seconds
    std::string message;

    nlohmann::json to_json() const;
    static Commit from_json(const nlohmann::json& j);
};
```

Commits are stored in the object store as CBOR-encoded JSON with `"type": "commit"`.

#### `jgit/refs.h` — Reference management

The `Refs` class manages HEAD, branches (`refs/heads/`), and tags (`refs/tags/`):

```cpp
class Refs {
    std::optional<std::string> current_branch() const;
    void set_head_to_branch(const std::string& name);
    void set_head_detached(const ObjectId& commit_id);
    std::optional<ObjectId> resolve_head() const;

    std::optional<ObjectId> branch_tip(const std::string& name) const;
    void set_branch(const std::string& name, const ObjectId& commit_id);
    void delete_branch(const std::string& name);
    std::vector<std::string> list_branches() const;

    std::optional<ObjectId> tag_target(const std::string& name) const;
    void set_tag(const std::string& name, const ObjectId& commit_id);
    void delete_tag(const std::string& name);
    std::vector<std::string> list_tags() const;
};
```

#### `jgit/repository.h` — High-level API

The `Repository` class provides the user-facing interface:

```cpp
class Repository {
    static Repository init(const std::filesystem::path& path);

    // Versioning
    ObjectId commit(const nlohmann::json& data, const std::string& message,
                    const std::string& author = "unknown");
    std::optional<nlohmann::json> get_data(const ObjectId& commit_id) const;
    std::optional<Commit> get_commit(const ObjectId& commit_id) const;
    std::optional<ObjectId> head() const;
    std::optional<Commit> head_commit() const;

    // History
    std::vector<std::pair<ObjectId, Commit>> log(std::size_t max_count = 0) const;
    std::vector<std::pair<ObjectId, Commit>> log(const ObjectId& start_id,
                                                  std::size_t max_count = 0) const;

    // Checkout
    std::optional<nlohmann::json> checkout(const ObjectId& commit_id);

    // Branches
    void create_branch(const std::string& name, const ObjectId& commit_id);
    void switch_branch(const std::string& name);
    void delete_branch(const std::string& name);
    std::vector<std::string> list_branches() const;
    std::optional<std::string> current_branch() const;

    // Tags
    void create_tag(const std::string& name, const ObjectId& commit_id);
    void delete_tag(const std::string& name);
    std::vector<std::string> list_tags() const;
    std::optional<ObjectId> resolve_tag(const std::string& name) const;
};
```

### New files created in Task 1

| File | Description |
|------|-------------|
| `jgit/commit.h` | Commit object with JSON (de)serialization |
| `jgit/refs.h` | HEAD, branch, and tag reference management |
| `jgit/repository.h` | High-level repository API |
| `tests/test_commit.cpp` | 5 unit tests for Commit |
| `tests/test_refs.cpp` | 10 unit tests for Refs |
| `tests/test_repository.cpp` | 17 unit tests for Repository |

### Unit tests — Task 1

**test_commit.cpp** (5 tests):
- `to_json` produces correct keys (type, root, parent, author, timestamp, message)
- `to_json` encodes parent ObjectId correctly
- `from_json` round-trip without parent
- `from_json` round-trip with parent
- `from_json` throws `std::invalid_argument` on non-commit object

**test_refs.cpp** (10 tests):
- `current_branch` returns "main" from default HEAD
- `resolve_head` returns nullopt when branch has no commits
- `set_branch` and `branch_tip`
- `resolve_head` after writing branch
- `set_head_to_branch` updates `current_branch`
- `set_head_detached` causes `nullopt` `current_branch`
- `list_branches` returns sorted names
- `delete_branch` removes the branch
- `branch_tip` returns nullopt for non-existent branch
- Tags: set, get, list, delete

**test_repository.cpp** (17 tests):
- `init` creates valid `.jgit` structure
- `head` is nullopt before first commit
- Single commit sets HEAD
- Commit stores and retrieves data (round-trip)
- Consecutive commits form a parent chain
- `log` returns all commits in reverse chronological order
- `log` respects `max_count`
- `checkout` returns data and detaches HEAD
- `checkout` non-existent commit returns nullopt
- `create_branch` and `list_branches`
- `switch_branch` changes current branch
- `switch_branch` throws for non-existent branch
- `delete_branch` removes it
- Deleting current branch throws `std::runtime_error`
- Tags: create, resolve, list, delete
- Persists across `Repository` instances
- Commit metadata (author, message, timestamp) is stored correctly

**Total: 61/61 tests pass** (29 from Phase 1 + 32 new from Phase 2 Task 1)

---

## Task 2: JSON Patch Integration (Pending)

**Goal**: Extend jgit to compute and store the diff between consecutive commits as a JSON Patch (RFC 6902) object, using `BinDiffSynchronizer` as the byte-level engine.

### Planned scope

- `jgit/patch.h` — `compute_patch(from, to)` → `json-patch` object (RFC 6902 array)
- Store patch objects in the object store linked from commit objects
- `Repository::diff(commit_id_a, commit_id_b)` — return JSON Patch between two commits
- Unit tests for patch computation, application, and round-trip

---

## Task 3: `$ref` Link Support (Pending)

**Goal**: Implement cross-repository JSON references using `fptr<T>` as the storage primitive.

---

## Task 4: CLI Interface (Pending)

**Goal**: Command-line interface with commands: `jgit init`, `jgit commit`, `jgit log`, `jgit diff`, `jgit checkout`, `jgit branch`, `jgit tag`.

---

## New Files to Create in Phase 2

```
BinDiffSynchronizer/
├── jgit/
│   ├── commit.h          ← ✓ Done: commit object
│   ├── refs.h            ← ✓ Done: branch/tag/HEAD management
│   ├── repository.h      ← ✓ Done: high-level repository API
│   ├── patch.h           ← Pending: JSON Patch generation (Task 2)
│   └── cli.cpp           ← Pending: CLI interface (Task 4)
└── tests/
    ├── test_commit.cpp   ← ✓ Done
    ├── test_refs.cpp     ← ✓ Done
    ├── test_repository.cpp ← ✓ Done
    └── test_patch.cpp    ← Pending: patch tests (Task 2)
```

---

## Acceptance Criteria for Phase 2 Task 1

1. ✓ **Commit objects** can be created, serialized to JSON/CBOR, stored in the object store, and deserialized back
2. ✓ **Commit chain**: each commit (except the first) references its parent by ObjectId
3. ✓ **Branches**: create, switch, delete, list; HEAD tracks the current branch
4. ✓ **Tags**: create, delete, list, resolve
5. ✓ **Checkout**: retrieve data at any historical commit, detach HEAD
6. ✓ **History log**: traverse the commit chain from HEAD or any commit
7. ✓ **Persistence**: all state survives across `Repository` instances
8. ✓ **All tests pass**: 61/61 (29 from Phase 1 + 32 new)

---

## References

- [phase1-plan.md](phase1-plan.md) — Phase 1 implementation (completed)
- [plan.md](plan.md) — full development roadmap
- [readme.md](readme.md) — project description
- [RFC 6902: JSON Patch](https://datatracker.ietf.org/doc/html/rfc6902) — planned for Task 2
- [RFC 6901: JSON Pointer](https://datatracker.ietf.org/doc/html/rfc6901) — node addressing
- [Git Internals](https://git-scm.com/book/en/v2/Git-Internals-Plumbing-and-Porcelain) — architecture reference

---

*Document created: 2026-02-25. Phase 2 Task 1 completed: 2026-02-25*
