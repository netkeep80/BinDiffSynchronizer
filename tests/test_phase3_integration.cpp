#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <type_traits>

#include <nlohmann/json.hpp>

#include "jgit/persistent_json_store.h"
#include "jgit/object_store.h"
#include "jgit/persistent_basic_json.h"
#include "jgit/repository.h"

// =============================================================================
// Task 3.5 — Integration tests
//
// Tests cover the end-to-end scenarios combining all Phase 3 components:
//
//   3.5.1: Full cycle — PersistentJsonStore → snapshot → ObjectStore →
//          destroy store → restore from snapshot
//   3.5.2: Persistence restart cycle — PersistentJsonStore saves to disk,
//          destroyed, re-opened, previously imported data accessible
//   3.5.3: persistent_json round-trip via PersistentJsonStore
//   3.5.4: nlohmann::json algorithms (merge_patch, diff) work with
//          persistent_json instead of standard json
//   3.5.5: Alias pattern: using json = jgit::persistent_json; works as expected
// =============================================================================

namespace fs = std::filesystem;
using namespace jgit;

// Helper: create a unique temp directory per test and clean it up.
static fs::path make_test_dir(const char* tag) {
    fs::path p = fs::temp_directory_path() / (std::string("test_phase3_integ_") + tag);
    fs::remove_all(p);
    fs::create_directories(p);
    return p;
}

// ---------------------------------------------------------------------------
// 3.5.1 — Full cycle: PersistentJsonStore → snapshot → destroy → restore
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.5.1: full cycle — import, snapshot, destroy store, restore from snapshot",
          "[task3.5][integration]")
{
    auto tmp = make_test_dir("full_cycle");

    nlohmann::json doc = {
        {"name", "Alice"},
        {"age", 30},
        {"active", true},
        {"scores", nlohmann::json::array({10, 20, 30})},
        {"meta", {{"version", 1}}}
    };

    // Phase A: import into PersistentJsonStore, take snapshot
    ObjectId snapshot_id;
    {
        auto obj_store = ObjectStore::init(tmp / "objects");
        PersistentJsonStore pjs;
        uint32_t root_id = pjs.import_json(doc);
        REQUIRE(root_id != 0u);

        snapshot_id = pjs.snapshot(root_id, obj_store);
        REQUIRE(!snapshot_id.hex.empty());
        REQUIRE(obj_store.exists(snapshot_id));
    }
    // obj_store and pjs are destroyed; only ObjectStore files on disk remain

    // Phase B: open a new ObjectStore, restore from snapshot
    {
        ObjectStore obj_store(tmp / "objects");
        PersistentJsonStore pjs2;
        uint32_t restored_id = pjs2.restore(snapshot_id, obj_store);
        REQUIRE(restored_id != 0u);

        nlohmann::json result = pjs2.export_json(restored_id);
        REQUIRE(result == doc);
    }

    fs::remove_all(tmp);
}

// ---------------------------------------------------------------------------
// 3.5.2 — Persistence restart: PersistentJsonStore saves to disk, then is
//          re-opened and previously imported data is accessible
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.5.2: persistence restart — store saves on destruction and reloads on open",
          "[task3.5][integration]")
{
    auto store_dir = make_test_dir("restart") / "pjs";

    nlohmann::json doc = {
        {"key", "value"},
        {"list", nlohmann::json::array({1, 2, 3})},
        {"nested", {{"inner", 42}}}
    };

    uint32_t root_id = 0;

    // First "process": create store, import data, destroy (saves to disk)
    {
        PersistentJsonStore pjs(store_dir);
        root_id = pjs.import_json(doc);
        REQUIRE(root_id != 0u);
    }  // destructor saves pools to disk

    // Second "process": open same store directory, data should still be there
    {
        PersistentJsonStore pjs(store_dir);
        nlohmann::json result = pjs.export_json(root_id);
        REQUIRE(result == doc);
    }

    fs::remove_all(make_test_dir("restart"));
}

// ---------------------------------------------------------------------------
// 3.5.3 — persistent_json round-trip via PersistentJsonStore
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.5.3: persistent_json data round-trips via PersistentJsonStore",
          "[task3.5][integration]")
{
    auto tmp = make_test_dir("pjson_pjs");

    // Build a persistent_json document
    jgit::persistent_json pj;
    pj["version"] = 2;
    pj["label"]   = "integration-test";
    pj["values"]  = jgit::persistent_json::array({100, 200, 300});
    pj["nested"]  = jgit::persistent_json::object();
    pj["nested"]["flag"] = true;

    // Convert to nlohmann::json for import into PersistentJsonStore
    // (PersistentJsonStore uses nlohmann::json, not persistent_json directly)
    nlohmann::json nlohmann_doc = nlohmann::json::parse(
        std::string(pj.dump().c_str()));

    uint32_t root_id = 0;
    {
        PersistentJsonStore store(tmp);
        root_id = store.import_json(nlohmann_doc);
        REQUIRE(root_id != 0u);
    }  // saved to disk

    // Reload and verify the data survives through PersistentJsonStore persistence
    {
        PersistentJsonStore store(tmp);
        nlohmann::json result = store.export_json(root_id);

        // Re-wrap in persistent_json to use its interface
        jgit::persistent_json loaded = jgit::persistent_json::parse(result.dump());

        REQUIRE(loaded["version"].get<int>() == 2);
        REQUIRE(loaded["label"].get<std::string>() == "integration-test");
        REQUIRE(loaded["values"].size() == 3u);
        REQUIRE(loaded["values"][0].get<int>() == 100);
        REQUIRE(loaded["values"][2].get<int>() == 300);
        REQUIRE(loaded["nested"]["flag"].get<bool>() == true);
    }

    fs::remove_all(tmp);
}

// ---------------------------------------------------------------------------
// 3.5.4 — nlohmann::json algorithms (merge_patch, diff) work with
//          persistent_json instead of standard json
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.5.4: nlohmann algorithms merge_patch and diff work with persistent_json",
          "[task3.5][integration]")
{
    using pjson = jgit::persistent_json;

    // merge_patch: RFC 7396
    {
        pjson base  = {{"a", 1}, {"b", 2}, {"c", 3}};
        pjson patch = {{"b", 20}, {"c", nullptr}, {"d", 4}};

        base.merge_patch(patch);

        REQUIRE(base["a"].get<int>() == 1);
        REQUIRE(base["b"].get<int>() == 20);   // updated
        REQUIRE(base.find("c") == base.end()); // deleted (null in patch)
        REQUIRE(base["d"].get<int>() == 4);    // added
    }

    // diff + patch: RFC 6902 JSON Patch
    {
        pjson source = {{"x", 1}, {"y", 2}};
        pjson target = {{"x", 1}, {"y", 3}, {"z", 4}};

        pjson diff_patch = pjson::diff(source, target);
        REQUIRE(diff_patch.is_array());
        REQUIRE(!diff_patch.empty());

        // Apply the generated patch to source and verify it matches target
        pjson result = source.patch(diff_patch);
        REQUIRE(result["x"].get<int>() == 1);
        REQUIRE(result["y"].get<int>() == 3);
        REQUIRE(result["z"].get<int>() == 4);
    }
}

// ---------------------------------------------------------------------------
// 3.5.5 — Alias correctness: using json = jgit::persistent_json
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.5.5: alias 'using json = jgit::persistent_json' works correctly",
          "[task3.5][integration]")
{
    using json = jgit::persistent_json;

    // Construction from JSON literal
    json j = {{"name", "Bob"}, {"score", 99}, {"active", false}};
    REQUIRE(j.is_object());
    REQUIRE(j["name"].get<std::string>() == "Bob");
    REQUIRE(j["score"].get<int>() == 99);
    REQUIRE(j["active"].get<bool>() == false);

    // Array construction
    json arr = json::array({1, 2, 3, 4, 5});
    REQUIRE(arr.is_array());
    REQUIRE(arr.size() == 5u);
    REQUIRE(arr[4].get<int>() == 5);

    // dump() / parse() round-trip
    std::string dumped = std::string(j.dump().c_str());
    json loaded = json::parse(dumped);
    REQUIRE(loaded["name"].get<std::string>() == "Bob");
    REQUIRE(loaded["score"].get<int>() == 99);

    // Null and boolean types
    json nullval = nullptr;
    REQUIRE(nullval.is_null());

    json boolval = true;
    REQUIRE(boolval.get<bool>() == true);

    // Nested access
    json nested = {{"outer", {{"inner", 42}}}};
    REQUIRE(nested["outer"]["inner"].get<int>() == 42);
}
