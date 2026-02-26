#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <string>
#include <type_traits>

#include "jgit/persistent_basic_json.h"
#include "jgit/object_store.h"
#include "jgit/persistent_json_store.h"

// =============================================================================
// Task 3.3.5 — Tests for persist<json> and fptr<json>
// =============================================================================
//
// Scope:
//   - Verify jgit::persistent_json (basic_json with persistent_string) is
//     usable for all standard JSON operations.
//   - Verify round-trip via dump()/parse() preserves all value types.
//   - Verify nlohmann::json algorithms (merge_patch, diff) work with
//     persistent_json as the target type.
//   - Verify snapshot()/restore() via ObjectStore works with persistent_json
//     data when converted via nlohmann::json.
//   - Verify alias pattern: using json = jgit::persistent_json; works as
//     expected.
//
// Note: jgit::persistent_json is NOT trivially copyable (it inherits from
// nlohmann::basic_json which has non-trivial members).  Direct persist<T>
// is not applicable.  Persistence across process restarts is achieved via
// PersistentJsonStore::import_json()/export_json() or snapshot()/restore().
// =============================================================================

// Convenience alias — mirrors the usage pattern from the plan
using json = jgit::persistent_json;

namespace {

std::filesystem::path tmp_dir(const char* tag)
{
    return std::filesystem::temp_directory_path() / ("test_pjson_" + std::string(tag));
}

void rm_dir(const std::filesystem::path& p)
{
    std::error_code ec;
    std::filesystem::remove_all(p, ec);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// 3.3.5.1 — persistent_json is NOT trivially copyable; document the
//            alternative persistence approach
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.3.5.1: persistent_json is not trivially copyable (expected)",
          "[task3.3][persist_json]")
{
    // nlohmann::basic_json stores a tagged union with non-trivial members.
    // This is expected — direct persist<T> is not applicable.
    CHECK_FALSE(std::is_trivially_copyable<json>::value);

    // But the StringType (persistent_string) IS trivially copyable.
    CHECK(std::is_trivially_copyable<jgit::persistent_string>::value);
}

// ---------------------------------------------------------------------------
// 3.3.5.2 — Construct persistent_json from JSON literal
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.3.5.2: create persistent_json object from JSON literal",
          "[task3.3][persist_json]")
{
    json j = {{"name", "Alice"}, {"age", 30}, {"active", true}};

    REQUIRE(j.is_object());
    REQUIRE(j["name"].is_string());
    REQUIRE(j["name"].get<std::string>() == "Alice");
    REQUIRE(j["age"].get<int>() == 30);
    REQUIRE(j["active"].get<bool>() == true);
}

// ---------------------------------------------------------------------------
// 3.3.5.3 — Save and load persistent_json via dump()/parse()
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.3.5.3: persistent_json dump/parse round-trip preserves all types",
          "[task3.3][persist_json]")
{
    json orig;
    orig["str"]   = "hello";
    orig["int"]   = -42;
    orig["uint"]  = 100u;
    orig["float"] = 3.14;
    orig["bool"]  = false;
    orig["null"]  = nullptr;
    orig["array"] = json::array({1, 2, 3});
    orig["nested"]["key"] = "value";

    std::string serialised = std::string(orig.dump().c_str());
    json loaded = json::parse(serialised);

    REQUIRE(loaded["str"].get<std::string>() == "hello");
    REQUIRE(loaded["int"].get<int>() == -42);
    REQUIRE(loaded["uint"].get<unsigned>() == 100u);
    REQUIRE(std::abs(loaded["float"].get<double>() - 3.14) < 1e-9);
    REQUIRE(loaded["bool"].get<bool>() == false);
    REQUIRE(loaded["null"].is_null());
    REQUIRE(loaded["array"].size() == 3u);
    REQUIRE(loaded["nested"]["key"].get<std::string>() == "value");
}

// ---------------------------------------------------------------------------
// 3.3.5.4 — persistent_json array round-trip
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.3.5.4: persistent_json array dump/parse round-trip",
          "[task3.3][persist_json]")
{
    json arr = json::array({"alpha", "beta", "gamma"});
    REQUIRE(arr.is_array());
    REQUIRE(arr.size() == 3u);

    std::string s = std::string(arr.dump().c_str());
    json loaded = json::parse(s);

    REQUIRE(loaded.is_array());
    REQUIRE(loaded.size() == 3u);
    REQUIRE(loaded[0].get<std::string>() == "alpha");
    REQUIRE(loaded[1].get<std::string>() == "beta");
    REQUIRE(loaded[2].get<std::string>() == "gamma");
}

// ---------------------------------------------------------------------------
// 3.3.5.5 — nlohmann::json merge_patch with persistent_json
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.3.5.5: nlohmann::json merge_patch algorithm works with persistent_json",
          "[task3.3][persist_json]")
{
    json base = {{"a", 1}, {"b", 2}, {"c", 3}};
    json patch = {{"b", 20}, {"d", 4}};

    base.merge_patch(patch);

    REQUIRE(base["a"].get<int>() == 1);
    REQUIRE(base["b"].get<int>() == 20);   // updated
    REQUIRE(base["c"].get<int>() == 3);
    REQUIRE(base["d"].get<int>() == 4);    // added
}

// ---------------------------------------------------------------------------
// 3.3.5.6 — nlohmann::json diff with persistent_json
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.3.5.6: nlohmann::json diff algorithm works with persistent_json",
          "[task3.3][persist_json]")
{
    json source = {{"a", 1}, {"b", 2}};
    json target = {{"a", 1}, {"b", 3}, {"c", 4}};

    json patch = json::diff(source, target);
    REQUIRE(patch.is_array());
    REQUIRE(!patch.empty());

    // Apply the patch to source and verify it becomes target
    json result = source;
    result = result.patch(patch);
    REQUIRE(result["a"].get<int>() == 1);
    REQUIRE(result["b"].get<int>() == 3);
    REQUIRE(result["c"].get<int>() == 4);
}

// ---------------------------------------------------------------------------
// 3.3.5.7 — alias pattern: using json = jgit::persistent_json; works
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.3.5.7: alias 'using json = jgit::persistent_json' works as expected",
          "[task3.3][persist_json]")
{
    // using json = jgit::persistent_json; is defined at file scope above.
    json j = {{"key", "val"}, {"num", 7}};

    REQUIRE(j.is_object());
    REQUIRE(j["key"].get<std::string>() == "val");
    REQUIRE(j["num"].get<int>() == 7);

    // JSON Pointer syntax with persistent_json's own pointer type.
    // json_pointer<string_t> requires a persistent_string argument.
    using json_ptr = json::json_pointer;
    REQUIRE(j.at(json_ptr(jgit::persistent_string("/key"))).get<std::string>() == "val");
}

// ---------------------------------------------------------------------------
// 3.3.5.8 — import/export via PersistentJsonStore with persistent_json data
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.3.5.8: persistent_json data survives round-trip via PersistentJsonStore",
          "[task3.3][persist_json]")
{
    auto dir = tmp_dir("pjs_pjson");
    rm_dir(dir);

    // Build a persistent_json document
    json orig = {{"version", 2}, {"data", json::array({10, 20, 30})}, {"label", "test"}};

    // Convert to nlohmann::json (standard) for import into PersistentJsonStore
    nlohmann::json nlohmann_doc = nlohmann::json::parse(std::string(orig.dump().c_str()));

    uint32_t root_id{};
    {
        jgit::PersistentJsonStore store(dir);
        root_id = store.import_json(nlohmann_doc);
        REQUIRE(root_id != 0u);
    }  // store destructor saves to disk

    // Reload and export
    {
        jgit::PersistentJsonStore store(dir);
        nlohmann::json restored = store.export_json(root_id);

        // Re-wrap in persistent_json for verification
        json result = json::parse(restored.dump());

        REQUIRE(result["version"].get<int>() == 2);
        REQUIRE(result["label"].get<std::string>() == "test");
        REQUIRE(result["data"].size() == 3u);
        REQUIRE(result["data"][0].get<int>() == 10);
        REQUIRE(result["data"][2].get<int>() == 30);
    }

    rm_dir(dir);
}
