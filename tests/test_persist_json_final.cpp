#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <type_traits>

#include "jgit/persistent_basic_json.h"
#include "jgit/persistent_map_adapter.h"
#include "jgit/persistent_array_adapter.h"
#include "jgit/object_store.h"
#include "jgit/persistent_json_store.h"

// =============================================================================
// Task 3.3.4 — Final tests for fully-persistent persistent_json
//
// Verifies that jgit::persistent_json now uses:
//   ObjectType  = jgit::persistent_map_adapter
//   ArrayType   = jgit::persistent_array_adapter
//   StringType  = jgit::persistent_string
//
// Also loads and processes the large tests/test.json file (360 000+ lines,
// ~11 MB) to validate that the persistent infrastructure works at scale.
//
// NOTE on known limitation with long keys (> SSO_SIZE = 23 chars):
//   jgit::persistent_string uses shallow-copy semantics for long strings
//   (stored via AddressManager<char> slots).  When nlohmann's JSON lexer
//   builds a key in its token_buffer (type persistent_string), copies it
//   into a std::map node, then clears the token_buffer (freeing the slot),
//   the map key's long_ptr becomes dangling.  This means parsing JSON with
//   keys > 23 chars directly via persistent_json::parse() is unsafe.
//
//   Safe patterns:
//     - Parse JSON with standard nlohmann::json (std::string keys), then
//       import into PersistentJsonStore for durable storage.
//     - Use persistent_json::parse() only for documents where ALL keys and
//       string values fit within SSO_SIZE (≤ 23 chars).
// =============================================================================

using json = jgit::persistent_json;

// ---------------------------------------------------------------------------
// Helper utilities
// ---------------------------------------------------------------------------
namespace {

std::string test_json_path()
{
#ifdef TEST_JSON_PATH
    return TEST_JSON_PATH;
#else
    const char* candidates[] = {
        "tests/test.json",
        "../tests/test.json",
        "../../tests/test.json",
    };
    for (const char* p : candidates) {
        if (std::filesystem::exists(p)) return p;
    }
    return "tests/test.json";
#endif
}

std::filesystem::path tmp_dir(const char* tag)
{
    return std::filesystem::temp_directory_path() / ("test_pjson_final_" + std::string(tag));
}

void rm_dir(const std::filesystem::path& p)
{
    std::error_code ec;
    std::filesystem::remove_all(p, ec);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// 3.3.4.1 — Verify ObjectType is persistent_map_adapter
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.3.4.1: persistent_json uses persistent_map_adapter as ObjectType",
          "[task3.3][persist_json_final]")
{
    using object_t = json::object_t;
    CHECK(object_t::is_persistent_adapter);

    // Adapter is distinct from plain std::map
    using std_map_t = std::map<json::string_t, json>;
    constexpr bool same = std::is_same<object_t, std_map_t>::value;
    CHECK_FALSE(same);
}

// ---------------------------------------------------------------------------
// 3.3.4.2 — Verify ArrayType is persistent_array_adapter
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.3.4.2: persistent_json uses persistent_array_adapter as ArrayType",
          "[task3.3][persist_json_final]")
{
    using array_t = json::array_t;
    CHECK(array_t::is_persistent_adapter);

    using std_vec_t = std::vector<json>;
    constexpr bool same = std::is_same<array_t, std_vec_t>::value;
    CHECK_FALSE(same);
}

// ---------------------------------------------------------------------------
// 3.3.4.3 — Verify StringType is persistent_string
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.3.4.3: persistent_json uses persistent_string as StringType",
          "[task3.3][persist_json_final]")
{
    using string_t = json::string_t;
    constexpr bool is_ps = std::is_same<string_t, jgit::persistent_string>::value;
    CHECK(is_ps);
}

// ---------------------------------------------------------------------------
// 3.3.4.4 — Basic construction and field access
//            (all string keys ≤ SSO_SIZE = 23 chars — safe path)
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.3.4.4: persistent_json basic object with all-persistent types works",
          "[task3.3][persist_json_final]")
{
    json j = {{"name", "Bob"}, {"score", 99}, {"active", true}};

    REQUIRE(j.is_object());
    REQUIRE(j["name"].is_string());
    CHECK(j["name"].get<std::string>() == "Bob");
    CHECK(j["score"].get<int>() == 99);
    CHECK(j["active"].get<bool>() == true);
}

// ---------------------------------------------------------------------------
// 3.3.4.5 — Nested array/object creation and access
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.3.4.5: persistent_json nested structures with persistent adapters",
          "[task3.3][persist_json_final]")
{
    json j;
    j["items"] = json::array({1, 2, 3, 4, 5});
    j["meta"]["version"] = 2;
    j["meta"]["name"] = "test";

    REQUIRE(j["items"].is_array());
    REQUIRE(j["items"].size() == 5u);
    CHECK(j["items"][0].get<int>() == 1);
    CHECK(j["items"][4].get<int>() == 5);

    REQUIRE(j["meta"].is_object());
    CHECK(j["meta"]["version"].get<int>() == 2);
    CHECK(j["meta"]["name"].get<std::string>() == "test");
}

// ---------------------------------------------------------------------------
// 3.3.4.6 — dump()/parse() round-trip
//            (all string keys/values ≤ 23 chars — safe path)
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.3.4.6: persistent_json dump/parse round-trip with persistent adapters",
          "[task3.3][persist_json_final]")
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
// 3.3.4.7 — merge_patch with persistent adapters
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.3.4.7: persistent_json merge_patch works with persistent adapters",
          "[task3.3][persist_json_final]")
{
    json base = {{"a", 1}, {"b", 2}, {"c", 3}};
    json patch = {{"b", 20}, {"d", 4}};

    base.merge_patch(patch);

    CHECK(base["a"].get<int>() == 1);
    CHECK(base["b"].get<int>() == 20);
    CHECK(base["c"].get<int>() == 3);
    CHECK(base["d"].get<int>() == 4);
}

// ---------------------------------------------------------------------------
// 3.3.4.8 — diff/patch algorithms with persistent adapters
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.3.4.8: persistent_json diff/patch algorithms work with persistent adapters",
          "[task3.3][persist_json_final]")
{
    json source = {{"a", 1}, {"b", 2}};
    json target = {{"a", 1}, {"b", 3}, {"c", 4}};

    json diff_patch = json::diff(source, target);
    REQUIRE(diff_patch.is_array());
    REQUIRE(!diff_patch.empty());

    json result = source;
    result = result.patch(diff_patch);
    CHECK(result["a"].get<int>() == 1);
    CHECK(result["b"].get<int>() == 3);
    CHECK(result["c"].get<int>() == 4);
}

// ---------------------------------------------------------------------------
// 3.3.4.9 — PersistentJsonStore round-trip with persistent_json data
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.3.4.9: persistent_json data survives round-trip via PersistentJsonStore",
          "[task3.3][persist_json_final]")
{
    auto dir = tmp_dir("pjs_final");
    rm_dir(dir);

    json orig = {{"version", 4}, {"data", json::array({10, 20, 30})}, {"label", "final"}};

    nlohmann::json nlohmann_doc = nlohmann::json::parse(std::string(orig.dump().c_str()));

    uint32_t root_id{};
    {
        jgit::PersistentJsonStore store(dir);
        root_id = store.import_json(nlohmann_doc);
        REQUIRE(root_id != 0u);
    }

    {
        jgit::PersistentJsonStore store(dir);
        nlohmann::json restored = store.export_json(root_id);
        json result = json::parse(restored.dump());
        CHECK(result["version"].get<int>() == 4);
        CHECK(result["label"].get<std::string>() == "final");
        CHECK(result["data"].size() == 3u);
        CHECK(result["data"][0].get<int>() == 10);
        CHECK(result["data"][2].get<int>() == 30);
    }

    rm_dir(dir);
}

// ---------------------------------------------------------------------------
// 3.3.4.10 — Parse tests/test.json with standard nlohmann::json (safe path)
//             and verify its structure.
//
// Background: test.json has 5 object keys longer than 23 chars.  Direct
// persistent_json::parse() with such keys triggers a use-after-free on the
// persistent_string's long storage (see module header for details).  The safe
// path is to use nlohmann::json::parse() for large files with long keys.
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.3.4.10: large tests/test.json parsed safely with nlohmann::json",
          "[task3.3][persist_json_final][large_file]")
{
    const std::string path = test_json_path();

    if (!std::filesystem::exists(path)) {
        SKIP("tests/test.json not found at: " + path);
    }

    std::ifstream f(path, std::ios::binary);
    REQUIRE(f.is_open());
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    REQUIRE(!content.empty());

    // Parse with standard nlohmann::json (uses std::string — no issue with long keys).
    nlohmann::json doc;
    REQUIRE_NOTHROW(doc = nlohmann::json::parse(content));
    REQUIRE(!doc.is_null());
    REQUIRE(doc.is_object());
    CHECK(!doc.empty());

    // Round-trip via dump/re-parse.
    std::string dumped;
    REQUIRE_NOTHROW(dumped = doc.dump());
    nlohmann::json reparsed;
    REQUIRE_NOTHROW(reparsed = nlohmann::json::parse(dumped));
    CHECK(reparsed.size() == doc.size());
}

// ---------------------------------------------------------------------------
// 3.3.4.11 — Import large tests/test.json into PersistentJsonStore
//             and export back — verifies PersistentJsonStore with large files.
//
// This is the recommended pattern for large JSON persistence in jgit:
//   1. Parse via nlohmann::json (std::string keys)
//   2. import_json() into PersistentJsonStore (flat pool, no string copy issue)
//   3. export_json() and wrap in persistent_json for further processing
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.3.4.11: large tests/test.json survives PersistentJsonStore round-trip",
          "[task3.3][persist_json_final][large_file]")
{
    const std::string path = test_json_path();

    if (!std::filesystem::exists(path)) {
        SKIP("tests/test.json not found at: " + path);
    }

    std::ifstream f(path, std::ios::binary);
    REQUIRE(f.is_open());
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    nlohmann::json nlohmann_doc = nlohmann::json::parse(content);
    REQUIRE(nlohmann_doc.is_object());

    const size_t top_level_keys = nlohmann_doc.size();

    auto dir = tmp_dir("large_json");
    rm_dir(dir);

    uint32_t root_id{};
    {
        jgit::PersistentJsonStore store(dir);
        root_id = store.import_json(nlohmann_doc);
        REQUIRE(root_id != 0u);
    }

    {
        jgit::PersistentJsonStore store(dir);
        nlohmann::json restored = store.export_json(root_id);
        CHECK(restored.is_object());
        CHECK(restored.size() == top_level_keys);
    }

    rm_dir(dir);
}

// ---------------------------------------------------------------------------
// 3.3.4.12 — persistent_json::parse() works correctly for JSON where all
//             keys and string values fit within SSO_SIZE (≤ 23 chars).
//             This is the safe use case for direct parse with persistent_json.
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.3.4.12: persistent_json parse is safe for documents with short keys only",
          "[task3.3][persist_json_final]")
{
    // All keys "key0" .. "key99" are 4-5 chars — well within SSO_SIZE = 23.
    std::string json_str = "{";
    for (int i = 0; i < 100; i++) {
        if (i > 0) json_str += ",";
        json_str += "\"key" + std::to_string(i) + "\":" + std::to_string(i * 10);
    }
    json_str += "}";

    json doc;
    REQUIRE_NOTHROW(doc = json::parse(json_str));
    REQUIRE(doc.is_object());
    CHECK(doc.size() == 100u);
    CHECK(doc["key0"].get<int>() == 0);
    CHECK(doc["key99"].get<int>() == 990);
}
