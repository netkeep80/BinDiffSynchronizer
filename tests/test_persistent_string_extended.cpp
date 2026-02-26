#include <catch2/catch_test_macros.hpp>

#include <string>
#include <type_traits>

#include "jgit/persistent_string.h"
#include "jgit/persistent_basic_json.h"

// =============================================================================
// Task 3.3.2 — Tests for jgit::persistent_string as StringType in basic_json<>
// =============================================================================
//
// Scope:
//   - Verify persistent_string compiles as the StringType template parameter
//     for nlohmann::basic_json.
//   - Verify basic JSON operations (create, assign, read, iterate) work with
//     jgit::persistent_json (= basic_json<..., persistent_string>).
//   - Verify std::is_trivially_copyable is preserved after the extension.
// =============================================================================

// ---------------------------------------------------------------------------
// 3.3.2.0 — Compile-time check: persistent_string remains trivially copyable
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.3.2.0: persistent_string remains trivially copyable after StringType extension",
          "[task3.3][persistent_string_extended]")
{
    // Adding ordinary member functions must not break trivial copyability.
    CHECK(std::is_trivially_copyable<jgit::persistent_string>::value);
}

// ---------------------------------------------------------------------------
// 3.3.2.1 — persistent_string as StringType: create json object
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.3.2.1: persistent_string as StringType: create persistent_json object",
          "[task3.3][persistent_string_extended]")
{
    jgit::persistent_json j;
    j["key"] = "value";
    // Use count() instead of contains() because contains() requires the key
    // argument to be constructible from the string_t (persistent_string).
    REQUIRE(j.count(jgit::persistent_string("key")) > 0);
    REQUIRE(j["key"].is_string());
}

// ---------------------------------------------------------------------------
// 3.3.2.2 — persistent_string as StringType: assign string value
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.3.2.2: persistent_string as StringType: assign json[\"key\"] = \"value\"",
          "[task3.3][persistent_string_extended]")
{
    jgit::persistent_json j;
    j["name"] = "Alice";
    j["city"] = "Wonderland";

    // Retrieve values back as std::string
    std::string name = j["name"].get<std::string>();
    std::string city = j["city"].get<std::string>();

    REQUIRE(name == "Alice");
    REQUIRE(city == "Wonderland");
}

// ---------------------------------------------------------------------------
// 3.3.2.3 — persistent_string as StringType: read via get<std::string>()
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.3.2.3: persistent_string as StringType: read via get<std::string>()",
          "[task3.3][persistent_string_extended]")
{
    jgit::persistent_json j;
    j["x"] = "hello";
    j["y"] = 42;
    j["z"] = true;

    REQUIRE(j["x"].get<std::string>() == "hello");
    REQUIRE(j["y"].get<int>() == 42);
    REQUIRE(j["z"].get<bool>() == true);
}

// ---------------------------------------------------------------------------
// 3.3.2.4 — persistent_string as StringType: iterate over keys of object
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.3.2.4: persistent_string as StringType: iterate over object keys",
          "[task3.3][persistent_string_extended]")
{
    jgit::persistent_json j;
    j["b"] = 2;
    j["a"] = 1;
    j["c"] = 3;

    // Iterate via the object items using the underlying map iterator.
    // j.items() uses persistent_string as key_type; convert to std::string.
    std::vector<std::string> keys;
    for (auto it = j.begin(); it != j.end(); ++it) {
        // key() returns const string_t& (= const jgit::persistent_string&)
        keys.push_back(std::string(it.key().c_str()));
    }

    // std::map keeps keys sorted
    REQUIRE(keys.size() == 3u);
    REQUIRE(keys[0] == "a");
    REQUIRE(keys[1] == "b");
    REQUIRE(keys[2] == "c");
}

// ---------------------------------------------------------------------------
// 3.3.2.5 — persistent_string as StringType: dump() round-trip
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.3.2.5: persistent_string as StringType: dump() and parse() round-trip",
          "[task3.3][persistent_string_extended]")
{
    jgit::persistent_json orig;
    orig["name"]    = "Bob";
    orig["version"] = 1;
    orig["tags"]    = jgit::persistent_json::array({"alpha", "beta"});

    // dump() returns a persistent_string — convert to std::string for comparison
    std::string serialised = std::string(orig.dump().c_str());
    REQUIRE(!serialised.empty());

    // Parse back
    jgit::persistent_json loaded = jgit::persistent_json::parse(serialised);
    REQUIRE(loaded["name"].get<std::string>() == "Bob");
    REQUIRE(loaded["version"].get<int>() == 1);
    REQUIRE(loaded["tags"].size() == 2u);
    REQUIRE(loaded["tags"][0].get<std::string>() == "alpha");
    REQUIRE(loaded["tags"][1].get<std::string>() == "beta");
}

// ---------------------------------------------------------------------------
// 3.3.2.6 — persistent_string as StringType: nested object
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.3.2.6: persistent_string as StringType: nested JSON object",
          "[task3.3][persistent_string_extended]")
{
    jgit::persistent_json j;
    j["user"]["name"] = "Carol";
    j["user"]["age"]  = 25;
    j["scores"]       = jgit::persistent_json::array({10, 20, 30});

    REQUIRE(j["user"]["name"].get<std::string>() == "Carol");
    REQUIRE(j["user"]["age"].get<int>() == 25);
    REQUIRE(j["scores"][1].get<int>() == 20);
}
