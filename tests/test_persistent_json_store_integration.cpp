#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <filesystem>
#include <nlohmann/json.hpp>
#include "jgit/persistent_json_store.h"
#include "jgit/object_store.h"

// =============================================================================
// Task 2.5 — Integration tests: PersistentJsonStore <-> ObjectStore
//
// Tests cover:
//   - snapshot() stores a persistent tree into the ObjectStore as a CBOR blob
//   - restore() loads a blob from the ObjectStore and imports it into the store
//   - snapshot + restore produces an identical document (round-trip)
//   - Multiple snapshots of different documents produce different ObjectIds
//   - Same document snaphotted twice produces the same ObjectId (idempotent)
//   - Restore throws on a missing ObjectId
//   - Working-tree edits are independent of the committed snapshot
//   - Snapshot / restore across separate PersistentJsonStore instances
// =============================================================================

namespace fs = std::filesystem;
using namespace jgit;
using json = nlohmann::json;

// Helper: create a unique temp directory per test.
static fs::path make_temp_dir(const std::string& suffix) {
    fs::path tmp = fs::temp_directory_path() / ("jgit_integ_" + suffix);
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    return tmp;
}

TEST_CASE("Task 2.5.1: snapshot stores document in ObjectStore",
          "[integration][snapshot]")
{
    auto tmp = make_temp_dir("snap_stores");
    auto obj_store = ObjectStore::init(tmp);

    PersistentJsonStore pjs;
    json doc = {{"name", "Alice"}, {"age", 30}};
    uint32_t root_id = pjs.import_json(doc);

    ObjectId oid = pjs.snapshot(root_id, obj_store);

    REQUIRE(obj_store.exists(oid));

    fs::remove_all(tmp);
}

TEST_CASE("Task 2.5.2: snapshot + restore round-trip for all JSON types",
          "[integration][snapshot][restore]")
{
    auto tmp = make_temp_dir("roundtrip");
    auto obj_store = ObjectStore::init(tmp);

    json docs[] = {
        json(nullptr),
        json(true),
        json(false),
        json(42),
        json(-100),
        json(3.14),
        json("hello"),
        json(std::string(50, 'Z')),  // long string
        json::array({1, 2, 3}),
        json::object(),
        {{"x", 1}, {"y", {{"nested", true}}}},
    };

    for (const auto& doc : docs) {
        PersistentJsonStore pjs;
        uint32_t root_id = pjs.import_json(doc);
        ObjectId oid = pjs.snapshot(root_id, obj_store);

        PersistentJsonStore pjs2;
        uint32_t restored_id = pjs2.restore(oid, obj_store);
        json result = pjs2.export_json(restored_id);

        REQUIRE(result == doc);
    }

    fs::remove_all(tmp);
}

TEST_CASE("Task 2.5.3: snapshot is idempotent (same doc -> same ObjectId)",
          "[integration][snapshot][idempotent]")
{
    auto tmp = make_temp_dir("idempotent");
    auto obj_store = ObjectStore::init(tmp);

    PersistentJsonStore pjs;
    json doc = {{"key", "value"}};
    uint32_t id = pjs.import_json(doc);

    ObjectId oid1 = pjs.snapshot(id, obj_store);
    ObjectId oid2 = pjs.snapshot(id, obj_store);

    REQUIRE(oid1 == oid2);

    fs::remove_all(tmp);
}

TEST_CASE("Task 2.5.4: different documents produce different ObjectIds",
          "[integration][snapshot]")
{
    auto tmp = make_temp_dir("different");
    auto obj_store = ObjectStore::init(tmp);

    PersistentJsonStore pjs;
    uint32_t id1 = pjs.import_json({{"a", 1}});
    uint32_t id2 = pjs.import_json({{"b", 2}});

    ObjectId oid1 = pjs.snapshot(id1, obj_store);
    ObjectId oid2 = pjs.snapshot(id2, obj_store);

    REQUIRE(oid1 != oid2);

    fs::remove_all(tmp);
}

TEST_CASE("Task 2.5.5: restore throws on missing ObjectId",
          "[integration][restore]")
{
    auto tmp = make_temp_dir("missing");
    auto obj_store = ObjectStore::init(tmp);

    PersistentJsonStore pjs;
    ObjectId fake_id{"0000000000000000000000000000000000000000000000000000000000000000"};

    REQUIRE_THROWS_AS(pjs.restore(fake_id, obj_store), std::runtime_error);

    fs::remove_all(tmp);
}

TEST_CASE("Task 2.5.6: working-tree edits do not affect committed snapshot",
          "[integration][snapshot][independence]")
{
    auto tmp = make_temp_dir("independence");
    auto obj_store = ObjectStore::init(tmp);

    PersistentJsonStore pjs;
    json original = {{"count", 0}};
    uint32_t root_id = pjs.import_json(original);

    // Commit the original state.
    ObjectId oid = pjs.snapshot(root_id, obj_store);

    // Mutate the working tree.
    pjs.set_node(root_id, persistent_json_value::make_int(999));

    // The snapshot in ObjectStore still holds the original document.
    PersistentJsonStore pjs2;
    uint32_t restored_id = pjs2.restore(oid, obj_store);
    json result = pjs2.export_json(restored_id);

    REQUIRE(result == original);

    fs::remove_all(tmp);
}

TEST_CASE("Task 2.5.7: snapshot + restore across separate store instances",
          "[integration][snapshot][restore]")
{
    auto tmp = make_temp_dir("cross_instance");

    json doc = {{"persistent", true}, {"data", json::array({10, 20, 30})}};
    ObjectId stored_oid;

    // First process: import, snapshot, done.
    {
        auto obj_store = ObjectStore::init(tmp);
        PersistentJsonStore pjs;
        uint32_t root_id = pjs.import_json(doc);
        stored_oid = pjs.snapshot(root_id, obj_store);
    }

    // Second process: open existing store, restore.
    {
        ObjectStore obj_store(tmp);
        PersistentJsonStore pjs;
        uint32_t restored_id = pjs.restore(stored_oid, obj_store);
        json result = pjs.export_json(restored_id);
        REQUIRE(result == doc);
    }

    fs::remove_all(tmp);
}

TEST_CASE("Task 2.5.8: snapshot of deeply nested document round-trips correctly",
          "[integration][snapshot][nested]")
{
    auto tmp = make_temp_dir("nested");
    auto obj_store = ObjectStore::init(tmp);

    json doc = {
        {"level1", {
            {"level2", {
                {"level3", {
                    {"value", 42},
                    {"tags", json::array({"a", "b", "c"})}
                }}
            }}
        }}
    };

    PersistentJsonStore pjs;
    uint32_t root_id = pjs.import_json(doc);
    ObjectId oid = pjs.snapshot(root_id, obj_store);

    PersistentJsonStore pjs2;
    uint32_t restored_id = pjs2.restore(oid, obj_store);
    json result = pjs2.export_json(restored_id);

    REQUIRE(result == doc);

    fs::remove_all(tmp);
}

TEST_CASE("Task 2.5.9: snapshot of large multi-slab document round-trips correctly",
          "[integration][snapshot][multi_slab]")
{
    auto tmp = make_temp_dir("multi_slab");
    auto obj_store = ObjectStore::init(tmp);

    // Build an object with > 16 keys (triggers object slab chaining).
    json doc = json::object();
    for (int i = 0; i < 32; ++i) {
        doc[std::string("key") + std::to_string(i)] = i * 10;
    }

    PersistentJsonStore pjs;
    uint32_t root_id = pjs.import_json(doc);
    ObjectId oid = pjs.snapshot(root_id, obj_store);

    PersistentJsonStore pjs2;
    uint32_t restored_id = pjs2.restore(oid, obj_store);
    json result = pjs2.export_json(restored_id);

    REQUIRE(result == doc);

    fs::remove_all(tmp);
}

TEST_CASE("Task 2.5.10: multiple snapshots can be restored independently",
          "[integration][snapshot][restore]")
{
    auto tmp = make_temp_dir("multi_snap");
    auto obj_store = ObjectStore::init(tmp);

    PersistentJsonStore pjs;
    json doc1 = {{"version", 1}};
    json doc2 = {{"version", 2}};
    uint32_t id1 = pjs.import_json(doc1);
    uint32_t id2 = pjs.import_json(doc2);

    ObjectId oid1 = pjs.snapshot(id1, obj_store);
    ObjectId oid2 = pjs.snapshot(id2, obj_store);

    REQUIRE(oid1 != oid2);

    PersistentJsonStore pjs2;
    REQUIRE(pjs2.export_json(pjs2.restore(oid1, obj_store)) == doc1);
    REQUIRE(pjs2.export_json(pjs2.restore(oid2, obj_store)) == doc2);

    fs::remove_all(tmp);
}
