#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <nlohmann/json.hpp>
#include "jgit/persistent_json_store.h"

// =============================================================================
// Task 2.4 - jgit::PersistentJsonStore unit tests
//
// Tests cover:
//   - import_json / export_json round-trip for all 7 JSON types
//   - get_node / set_node direct access
//   - get_field / get_index navigation helpers
//   - Nested structures (depth >= 3)
//   - Multi-slab chaining (> 16 array elements, > 16 object keys)
//   - Pool counters
// =============================================================================

using namespace jgit;
using json = nlohmann::json;

// Helper: import a document and re-export it, checking equality.
static bool round_trip(const json& doc) {
    PersistentJsonStore store;
    uint32_t id = store.import_json(doc);
    json out = store.export_json(id);
    return out == doc;
}

TEST_CASE("Task 2.4.1: PersistentJsonStore round-trip: null",
          "[persistent_json_store][null]")
{
    REQUIRE(round_trip(json(nullptr)));
}

TEST_CASE("Task 2.4.2: PersistentJsonStore round-trip: boolean true/false",
          "[persistent_json_store][boolean]")
{
    REQUIRE(round_trip(json(true)));
    REQUIRE(round_trip(json(false)));
}

TEST_CASE("Task 2.4.3: PersistentJsonStore round-trip: integer",
          "[persistent_json_store][number_int]")
{
    REQUIRE(round_trip(json(0)));
    REQUIRE(round_trip(json(42)));
    REQUIRE(round_trip(json(-1000)));
    REQUIRE(round_trip(json(int64_t(9007199254740992LL))));  // 2^53
}

TEST_CASE("Task 2.4.4: PersistentJsonStore round-trip: float",
          "[persistent_json_store][number_float]")
{
    REQUIRE(round_trip(json(3.14)));
    REQUIRE(round_trip(json(-2.71828)));
    REQUIRE(round_trip(json(0.0)));
}

TEST_CASE("Task 2.4.5: PersistentJsonStore round-trip: string",
          "[persistent_json_store][string]")
{
    REQUIRE(round_trip(json("hello")));
    REQUIRE(round_trip(json("")));
    // Long string (> SSO_SIZE = 23)
    REQUIRE(round_trip(json(std::string(100, 'A'))));
}

TEST_CASE("Task 2.4.6: PersistentJsonStore round-trip: empty array",
          "[persistent_json_store][array]")
{
    REQUIRE(round_trip(json::array()));
}

TEST_CASE("Task 2.4.7: PersistentJsonStore round-trip: simple array",
          "[persistent_json_store][array]")
{
    json doc = json::array({1, 2, 3, "four", true, nullptr});
    REQUIRE(round_trip(doc));
}

TEST_CASE("Task 2.4.8: PersistentJsonStore round-trip: empty object",
          "[persistent_json_store][object]")
{
    REQUIRE(round_trip(json::object()));
}

TEST_CASE("Task 2.4.9: PersistentJsonStore round-trip: simple object",
          "[persistent_json_store][object]")
{
    json doc = {{"name", "Alice"}, {"age", 30}, {"active", true}};
    REQUIRE(round_trip(doc));
}

TEST_CASE("Task 2.4.10: PersistentJsonStore round-trip: nested structure (depth >= 3)",
          "[persistent_json_store][nested]")
{
    json doc = {
        {"user", {
            {"name", "Bob"},
            {"address", {
                {"city", "Moscow"},
                {"zip", "101000"}
            }},
            {"scores", json::array({95, 87, 92})}
        }},
        {"version", 3}
    };
    REQUIRE(round_trip(doc));
}

TEST_CASE("Task 2.4.11: PersistentJsonStore get_node direct access",
          "[persistent_json_store][get_node]")
{
    PersistentJsonStore store;
    uint32_t id = store.import_json(json(42));

    const persistent_json_value& node = store.get_node(id);
    REQUIRE(node.is_int());
    REQUIRE(node.get_int() == 42);
}

TEST_CASE("Task 2.4.12: PersistentJsonStore set_node modifies a node",
          "[persistent_json_store][set_node]")
{
    PersistentJsonStore store;
    uint32_t id = store.import_json(json(10));

    store.set_node(id, persistent_json_value::make_int(99));
    REQUIRE(store.get_node(id).get_int() == 99);
}

TEST_CASE("Task 2.4.13: PersistentJsonStore get_node throws for invalid id",
          "[persistent_json_store][get_node]")
{
    PersistentJsonStore store;
    REQUIRE_THROWS_AS(store.get_node(0), std::out_of_range);
    REQUIRE_THROWS_AS(store.get_node(9999), std::out_of_range);
}

TEST_CASE("Task 2.4.14: PersistentJsonStore get_field navigation",
          "[persistent_json_store][get_field]")
{
    PersistentJsonStore store;
    json doc = {{"x", 10}, {"y", 20}};
    uint32_t root_id = store.import_json(doc);

    uint32_t x_id = store.get_field(root_id, "x");
    uint32_t y_id = store.get_field(root_id, "y");
    uint32_t z_id = store.get_field(root_id, "z");  // missing key

    REQUIRE(x_id != 0);
    REQUIRE(store.get_node(x_id).get_int() == 10);

    REQUIRE(y_id != 0);
    REQUIRE(store.get_node(y_id).get_int() == 20);

    REQUIRE(z_id == 0);  // missing → returns 0
}

TEST_CASE("Task 2.4.15: PersistentJsonStore get_index navigation",
          "[persistent_json_store][get_index]")
{
    PersistentJsonStore store;
    json doc = json::array({"alpha", "beta", "gamma"});
    uint32_t root_id = store.import_json(doc);

    uint32_t i0 = store.get_index(root_id, 0);
    uint32_t i1 = store.get_index(root_id, 1);
    uint32_t i2 = store.get_index(root_id, 2);
    uint32_t i3 = store.get_index(root_id, 3);  // out of bounds

    REQUIRE(i0 != 0);
    REQUIRE(store.get_node(i0).get_string().to_std_string() == "alpha");
    REQUIRE(store.get_node(i1).get_string().to_std_string() == "beta");
    REQUIRE(store.get_node(i2).get_string().to_std_string() == "gamma");
    REQUIRE(i3 == 0);
}

TEST_CASE("Task 2.4.16: PersistentJsonStore multi-slab array (> 16 elements)",
          "[persistent_json_store][array][multi_slab]")
{
    // json_array_slab capacity is 16 — an array of 32 elements triggers chaining.
    PersistentJsonStore store;
    json doc = json::array();
    for (int i = 0; i < 32; ++i) doc.push_back(i);

    uint32_t id = store.import_json(doc);
    json out = store.export_json(id);
    REQUIRE(out == doc);

    // Check a few individual elements via get_index.
    uint32_t elem0  = store.get_index(id, 0);
    uint32_t elem16 = store.get_index(id, 16);
    uint32_t elem31 = store.get_index(id, 31);
    REQUIRE(store.get_node(elem0).get_int()  == 0);
    REQUIRE(store.get_node(elem16).get_int() == 16);
    REQUIRE(store.get_node(elem31).get_int() == 31);
}

TEST_CASE("Task 2.4.17: PersistentJsonStore multi-slab object (> 16 keys)",
          "[persistent_json_store][object][multi_slab]")
{
    // json_object_slab capacity is 16 — an object with 32 keys triggers chaining.
    PersistentJsonStore store;
    json doc = json::object();
    for (int i = 0; i < 32; ++i) {
        doc[std::string("key") + std::to_string(i)] = i * 10;
    }

    uint32_t id = store.import_json(doc);
    json out = store.export_json(id);
    REQUIRE(out == doc);

    // Spot-check via get_field.
    uint32_t v0  = store.get_field(id, "key0");
    uint32_t v15 = store.get_field(id, "key15");
    uint32_t v31 = store.get_field(id, "key31");
    REQUIRE(store.get_node(v0).get_int()  == 0);
    REQUIRE(store.get_node(v15).get_int() == 150);
    REQUIRE(store.get_node(v31).get_int() == 310);
}

TEST_CASE("Task 2.4.18: PersistentJsonStore pool counters",
          "[persistent_json_store][counters]")
{
    PersistentJsonStore store;
    REQUIRE(store.node_count()   == 0);
    REQUIRE(store.array_count()  == 0);
    REQUIRE(store.object_count() == 0);

    store.import_json(json(42));
    REQUIRE(store.node_count() == 1);

    store.import_json(json::array({1, 2, 3}));
    // 1 array node + 3 int nodes = 4 new nodes; 1 new array slab.
    REQUIRE(store.node_count()  == 5);
    REQUIRE(store.array_count() == 1);
}

TEST_CASE("Task 2.4.19: PersistentJsonStore multiple independent documents",
          "[persistent_json_store][multi_doc]")
{
    PersistentJsonStore store;
    json doc1 = {{"a", 1}};
    json doc2 = {{"b", 2}};

    uint32_t id1 = store.import_json(doc1);
    uint32_t id2 = store.import_json(doc2);

    REQUIRE(id1 != id2);
    REQUIRE(store.export_json(id1) == doc1);
    REQUIRE(store.export_json(id2) == doc2);
}

TEST_CASE("Task 2.4.20: PersistentJsonStore export_json with invalid id returns null",
          "[persistent_json_store][export_json]")
{
    PersistentJsonStore store;
    json out = store.export_json(0);
    REQUIRE(out.is_null());

    json out2 = store.export_json(9999);
    REQUIRE(out2.is_null());
}
