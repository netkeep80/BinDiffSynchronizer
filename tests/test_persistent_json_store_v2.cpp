#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <filesystem>
#include <nlohmann/json.hpp>

#include "jgit/persistent_json_store.h"
#include "jgit/object_store.h"

// =============================================================================
// Task 3.2.4 — Tests for PersistentJsonStore(const std::filesystem::path&)
// =============================================================================
//
// Scope: verify the new path-based constructor that provides automatic pool
// persistence — data survives destruction and is reloaded on next open.
//
// Tests:
//   3.2.4.1 — Create empty store from path; initialises correctly.
//   3.2.4.2 — import_json + export_json round-trip for a simple object.
//   3.2.4.3 — Destroy and recreate with same path — previously imported data
//             is available (data persists across process restarts).
//   3.2.4.4 — Complex nested JSON (object inside array) survives restart.
//   3.2.4.5 — Two stores in different directories are independent.
//   3.2.4.6 — snapshot() + restore() work correctly with the new store.
// =============================================================================

namespace fs = std::filesystem;
using namespace jgit;
using json = nlohmann::json;

namespace {
    // Create a unique temp directory per test.
    fs::path make_v2_dir(const std::string& suffix) {
        fs::path tmp = fs::temp_directory_path() / ("jgit_v2_" + suffix);
        fs::remove_all(tmp);
        return tmp;
    }
} // anonymous namespace

// ---------------------------------------------------------------------------
// 3.2.4.1 — Create empty store at a new path; initialises correctly
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.2.4.1: PersistentJsonStore(path) initialises empty store",
          "[task3.2][persistent_json_store_v2]")
{
    auto dir = make_v2_dir("init");

    {
        PersistentJsonStore store(dir);
        // Newly created store has no nodes (only the sentinel slot 0)
        REQUIRE(store.node_count()   == 0u);
        REQUIRE(store.array_count()  == 0u);
        REQUIRE(store.object_count() == 0u);
    }

    // Pool files should now exist on disk
    REQUIRE(fs::exists(dir / "values.bin"));
    REQUIRE(fs::exists(dir / "arrays.bin"));
    REQUIRE(fs::exists(dir / "objects.bin"));

    fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// 3.2.4.2 — import_json + export_json round-trip for a simple object
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.2.4.2: PersistentJsonStore(path) import/export round-trip",
          "[task3.2][persistent_json_store_v2]")
{
    auto dir = make_v2_dir("roundtrip");

    json doc = {{"key", "value"}, {"num", 42}};

    uint32_t root_id = 0;
    {
        PersistentJsonStore store(dir);
        root_id = store.import_json(doc);
        json result = store.export_json(root_id);
        REQUIRE(result == doc);
    }

    fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// 3.2.4.3 — Destroy and recreate with same path — data survives restart
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.2.4.3: PersistentJsonStore(path) data persists across restart",
          "[task3.2][persistent_json_store_v2]")
{
    auto dir = make_v2_dir("restart");

    json doc = {{"hello", "world"}, {"answer", 42}, {"flag", true}};
    uint32_t root_id = 0;

    // Phase 1: import document and destroy store (triggers save)
    {
        PersistentJsonStore store(dir);
        root_id = store.import_json(doc);
        REQUIRE(root_id != 0u);
    }

    // Phase 2: recreate store from same path, verify data is still there
    {
        PersistentJsonStore store(dir);
        REQUIRE(store.node_count() > 0u);  // at least one real node

        json result = store.export_json(root_id);
        REQUIRE(result == doc);
    }

    fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// 3.2.4.4 — Complex nested JSON (array inside object) survives restart
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.2.4.4: PersistentJsonStore(path) nested JSON survives restart",
          "[task3.2][persistent_json_store_v2]")
{
    auto dir = make_v2_dir("nested");

    json doc = {
        {"name", "test"},
        {"items", json::array({1, 2, 3})},
        {"meta", {{"x", 10}, {"y", 20}}}
    };

    uint32_t root_id = 0;

    // Phase 1: import complex document
    {
        PersistentJsonStore store(dir);
        root_id = store.import_json(doc);
    }

    // Phase 2: reload and verify full tree
    {
        PersistentJsonStore store(dir);
        json result = store.export_json(root_id);
        REQUIRE(result == doc);

        // Spot-check navigation helpers still work after reload
        uint32_t name_id = store.get_field(root_id, "name");
        REQUIRE(name_id != 0u);
        REQUIRE(store.get_node(name_id).is_string());
        REQUIRE(store.get_node(name_id).get_string() == "test");

        uint32_t items_id = store.get_field(root_id, "items");
        REQUIRE(items_id != 0u);
        REQUIRE(store.get_node(items_id).is_array());

        // First element of items array should be 1
        uint32_t elem0_id = store.get_index(items_id, 0);
        REQUIRE(elem0_id != 0u);
        REQUIRE(store.get_node(elem0_id).is_int());
        REQUIRE(store.get_node(elem0_id).get_int() == 1);
    }

    fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// 3.2.4.5 — Two stores in different directories are independent
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.2.4.5: Two PersistentJsonStore instances in different dirs are independent",
          "[task3.2][persistent_json_store_v2]")
{
    auto dir1 = make_v2_dir("independent_a");
    auto dir2 = make_v2_dir("independent_b");

    json doc1 = {{"store", "A"}, {"value", 1}};
    json doc2 = {{"store", "B"}, {"value", 2}};

    uint32_t root1 = 0, root2 = 0;

    // Store different docs in different directories
    {
        PersistentJsonStore storeA(dir1);
        PersistentJsonStore storeB(dir2);
        root1 = storeA.import_json(doc1);
        root2 = storeB.import_json(doc2);
    }

    // Reload and verify each store only contains its own data
    {
        PersistentJsonStore storeA(dir1);
        json resultA = storeA.export_json(root1);
        REQUIRE(resultA == doc1);
    }

    {
        PersistentJsonStore storeB(dir2);
        json resultB = storeB.export_json(root2);
        REQUIRE(resultB == doc2);
    }

    fs::remove_all(dir1);
    fs::remove_all(dir2);
}

// ---------------------------------------------------------------------------
// 3.2.4.6 — snapshot() + restore() work with the new path-based store
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.2.4.6: PersistentJsonStore(path) snapshot and restore via ObjectStore",
          "[task3.2][persistent_json_store_v2]")
{
    auto store_dir  = make_v2_dir("snapshot_store");
    auto obj_dir    = fs::temp_directory_path() / "jgit_v2_snapshot_obj";
    fs::remove_all(obj_dir);

    json doc = {{"project", "jgit"}, {"version", 3}};

    ObjectId oid;
    uint32_t root_id = 0;

    // Phase 1: import, snapshot, destroy
    {
        PersistentJsonStore pjs(store_dir);
        auto obj_store = ObjectStore::init(obj_dir);

        root_id = pjs.import_json(doc);
        oid = pjs.snapshot(root_id, obj_store);
        REQUIRE(obj_store.exists(oid));
    }

    // Phase 2: recreate store from same path, restore from snapshot, verify
    {
        PersistentJsonStore pjs(store_dir);
        auto obj_store = ObjectStore(obj_dir);

        uint32_t restored_id = pjs.restore(oid, obj_store);
        json result = pjs.export_json(restored_id);
        REQUIRE(result == doc);
    }

    fs::remove_all(store_dir);
    fs::remove_all(obj_dir);
}
