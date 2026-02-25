# Phase 3 Plan: jgit Commit System

**Status:** In Progress — Task 3.1 Done

## Goal

Implement the **commit system** for jgit — the first item in the medium-term plan from `plan.md`.
This gives jgit the ability to record a history of JSON document versions, identified by
content-addressed hashes, and to navigate that history.

The Phase 2 `PersistentJsonStore` provides the working tree (mutable live state).
Phase 3 adds the **version history layer** on top: commits, branches, and checkout.

## Background: Git Analogy

```
Phase 1  ObjectStore    ←→  Git object store      (content-addressed blobs)
Phase 2  PersistentJsonStore ←→  Git working tree (live, mutable state)
Phase 3  Repository     ←→  Git repository        (commits, branches, HEAD)
```

A `json-commit` in jgit is directly analogous to a Git commit:

| Git commit field | jgit json-commit field |
|------------------|------------------------|
| `tree`           | `root_snapshot_id`     |
| `parent`         | `parent_id`            |
| `author`         | `author`               |
| `committer`      | `author`               |
| `committed date` | `timestamp`            |
| commit message   | `message`              |

The root snapshot is stored as a CBOR blob in Phase 1 `ObjectStore`.
The commit metadata is itself stored as a JSON object in the same `ObjectStore`,
making it content-addressed and immutable.

---

## Phase 3 Tasks

### Task 3.1 — Commit System ✓ DONE

**Objective:** Implement the core commit/log/checkout workflow.

#### 3.1.1 — `jgit::Commit` struct (`jgit/commit.h`)

Fields:
```cpp
struct Commit {
    ObjectId id;              // SHA-256 of the serialized commit JSON (self-referential)
    ObjectId root_snapshot;   // ObjectId of the CBOR blob with the JSON document content
    ObjectId parent_id;       // ObjectId of parent commit; empty string "" = root commit
    std::string author;       // free-form author string
    int64_t     timestamp;    // Unix timestamp (seconds since epoch)
    std::string message;      // commit message

    // Serialise / deserialise to nlohmann::json for storage in ObjectStore.
    nlohmann::json to_json() const;
    static Commit from_json(const nlohmann::json& j, const ObjectId& id);

    bool is_root() const { return parent_id.hex.empty(); }
};
```

#### 3.1.2 — `jgit::Repository` class (`jgit/repository.h`)

Wraps `ObjectStore` and adds:
- A `refs/heads/<branch>` file (plain text, contains a commit ObjectId hex)
- A `HEAD` file (plain text, `ref: refs/heads/main`)
- Methods: `commit()`, `log()`, `checkout()`, `get_head()`, `create_branch()`, `switch_branch()`

```cpp
class Repository {
public:
    // Open or initialise a repository at path.
    static Repository init(const std::filesystem::path& path);
    explicit Repository(const std::filesystem::path& path);

    // Commit: snapshot doc → ObjectStore, create Commit object, update HEAD branch.
    // Returns the ObjectId of the new commit.
    ObjectId commit(const nlohmann::json& doc,
                    const std::string& message,
                    const std::string& author = "");

    // Log: return list of commits from HEAD back to root (oldest last).
    std::vector<Commit> log() const;

    // Checkout: load and return the JSON document from a given commit.
    // Does not update HEAD (read-only inspection).
    nlohmann::json checkout(const ObjectId& commit_id) const;

    // HEAD: return the ObjectId of the commit that HEAD currently points to.
    // Returns an ObjectId with empty hex if the branch has no commits yet.
    ObjectId get_head() const;

    // Branch management.
    void create_branch(const std::string& name);
    void switch_branch(const std::string& name);
    std::string current_branch() const;

    // Access underlying ObjectStore (for advanced use).
    ObjectStore& object_store() { return store_; }
    const ObjectStore& object_store() const { return store_; }
};
```

#### 3.1.3 — Unit tests (`tests/test_commit.cpp`)

Test scenarios:
1. `Commit::to_json()` / `Commit::from_json()` round-trip for root commit
2. `Commit::to_json()` / `Commit::from_json()` round-trip for child commit
3. `Repository::commit()` creates a commit object in the object store
4. `Repository::commit()` updates HEAD branch to point to the new commit
5. `Repository::get_head()` returns empty ObjectId on fresh repository
6. `Repository::log()` returns empty vector for a fresh repository
7. `Repository::log()` returns single commit after one commit
8. `Repository::log()` returns commits in reverse chronological order (newest first)
9. `Repository::checkout()` returns the original JSON document
10. Multiple commits preserve all snapshots (old commits not overwritten)
11. Two commits with same content produce the same root_snapshot but different commits
12. `Repository::create_branch()` + `switch_branch()` changes HEAD ref
13. `Repository::current_branch()` returns correct branch name
14. Commits on different branches are independent
15. `Repository` persists across separate instances (reopen same path)

**Deliverables committed:**
- `jgit/commit.h` — `Commit` struct with `to_json()` / `from_json()`
- `jgit/repository.h` — `Repository` class
- `tests/test_commit.cpp` — 16 Catch2 tests (all passing)

All 125 tests pass (109 from Phases 1–2 + 16 new Task 3.1 tests) on Linux GCC.

---

### Task 3.2 — JSON Patch (RFC 6902) Delta Computation (TODO)

**Objective:** Use `BinDiffSynchronizer` and nlohmann/json to compute JSON Patch diffs
between successive versions of a JSON document.

```cpp
// Returns JSON Patch (RFC 6902) describing the difference between `from` and `to`.
nlohmann::json compute_diff(const nlohmann::json& from, const nlohmann::json& to);

// Apply a JSON Patch to a document.
nlohmann::json apply_patch(const nlohmann::json& doc, const nlohmann::json& patch);
```

Store the patch as a `json-patch` object alongside each commit.

---

### Task 3.3 — Branches and Tags (TODO)

**Objective:** Full branch/tag management.

- `refs/heads/<branch>` files already created in Task 3.1.
- Add `refs/tags/<tag>` files.
- Commands: `tag_create`, `tag_list`, `branch_list`.

---

### Task 3.4 — Unit Tests for All Phase 3 Components (TODO)

Included incrementally as each task is implemented.

---

### Task 3.5 — Performance Benchmark for Commit Operations (TODO)

Measure: commits per second, log walk time for 100/1000 commits.

---

## Implementation Order

```
Task 3.1  →  Task 3.2  →  Task 3.3  →  Task 3.4  →  Task 3.5
Commits      Diff/Patch   Branches     Tests        Benchmark
```

---

## Success Criteria

- [x] Phase 3.1: `Commit` and `Repository` implemented; 16 unit tests pass.
- [ ] Phase 3.2: `compute_diff` / `apply_patch` implemented; tests pass.
- [ ] Phase 3.3: Full branch/tag management; tests pass.
- [ ] Phase 3.4: All unit tests pass (CI green on GCC/Clang/MSVC).
- [ ] Phase 3.5: Benchmark committed; results documented.

---

## Relation to plan.md (Medium-Term Plan)

| plan.md item | Phase 3 task |
|---|---|
| Реализовать систему коммитов и веток | Tasks 3.1, 3.3 |
| Интегрировать JSON Patch (RFC 6902) | Task 3.2 |
| Поддержка `$ref`-ссылок | Future (Phase 4) |
| Создать CLI jgit | Future (Phase 4) |

---

*Document created: 2026-02-25*
