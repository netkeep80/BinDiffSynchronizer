#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include "jgit/object_store.h"

namespace fs = std::filesystem;
using nlohmann::json;

// Helper: create a unique temporary directory for each test case
static fs::path make_temp_dir(const std::string& suffix) {
    fs::path tmp = fs::temp_directory_path() / ("jgit_test_" + suffix);
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    return tmp;
}

// -----------------------------------------------------------------------
// Tests for ObjectStore (jgit/object_store.h)
// -----------------------------------------------------------------------

TEST_CASE("ObjectStore: init creates expected directory structure", "[object_store]")
{
    auto tmp = make_temp_dir("init");

    auto store = jgit::ObjectStore::init(tmp);

    REQUIRE(fs::exists(tmp / ".jgit"));
    REQUIRE(fs::is_directory(tmp / ".jgit" / "objects"));
    REQUIRE(fs::is_directory(tmp / ".jgit" / "refs" / "heads"));
    REQUIRE(fs::exists(tmp / ".jgit" / "HEAD"));

    fs::remove_all(tmp);
}

TEST_CASE("ObjectStore: init creates HEAD with correct content", "[object_store]")
{
    auto tmp = make_temp_dir("head");
    jgit::ObjectStore::init(tmp);

    std::string content;
    {
        std::ifstream head(tmp / ".jgit" / "HEAD");
        content.assign((std::istreambuf_iterator<char>(head)),
                        std::istreambuf_iterator<char>());
    }  // head closed here — required on Windows before remove_all
    REQUIRE(content == "ref: refs/heads/main\n");

    fs::remove_all(tmp);
}

TEST_CASE("ObjectStore: put and get round-trip", "[object_store]")
{
    auto tmp = make_temp_dir("roundtrip");
    auto store = jgit::ObjectStore::init(tmp);

    json original = {{"name", "Alice"}, {"age", 30}, {"active", true}};

    jgit::ObjectId id = store.put(original);
    auto retrieved = store.get(id);

    REQUIRE(retrieved.has_value());
    REQUIRE(retrieved.value() == original);

    fs::remove_all(tmp);
}

TEST_CASE("ObjectStore: put is idempotent", "[object_store]")
{
    auto tmp = make_temp_dir("idempotent");
    auto store = jgit::ObjectStore::init(tmp);

    json obj = {{"x", 42}};

    jgit::ObjectId id1 = store.put(obj);
    jgit::ObjectId id2 = store.put(obj);

    REQUIRE(id1 == id2);

    fs::remove_all(tmp);
}

TEST_CASE("ObjectStore: get missing object returns nullopt", "[object_store]")
{
    auto tmp = make_temp_dir("missing");
    auto store = jgit::ObjectStore::init(tmp);

    jgit::ObjectId fake_id{"0000000000000000000000000000000000000000000000000000000000000000"};
    auto result = store.get(fake_id);

    REQUIRE_FALSE(result.has_value());

    fs::remove_all(tmp);
}

TEST_CASE("ObjectStore: exists returns false before put, true after", "[object_store]")
{
    auto tmp = make_temp_dir("exists");
    auto store = jgit::ObjectStore::init(tmp);

    json obj = {{"test", "exists"}};

    // Compute what the id would be without storing
    auto bytes = jgit::to_bytes(obj);
    jgit::ObjectId id = jgit::hash_object(bytes);

    REQUIRE_FALSE(store.exists(id));

    store.put(obj);

    REQUIRE(store.exists(id));

    fs::remove_all(tmp);
}

TEST_CASE("ObjectStore: different objects produce different hashes", "[object_store]")
{
    auto tmp = make_temp_dir("different");
    auto store = jgit::ObjectStore::init(tmp);

    jgit::ObjectId id1 = store.put({{"key", "value1"}});
    jgit::ObjectId id2 = store.put({{"key", "value2"}});

    REQUIRE(id1 != id2);

    fs::remove_all(tmp);
}

TEST_CASE("ObjectStore: stores multiple distinct objects", "[object_store]")
{
    auto tmp = make_temp_dir("multiple");
    auto store = jgit::ObjectStore::init(tmp);

    json docs[] = {
        {{"type", "user"}, {"id", 1}},
        {{"type", "post"}, {"id", 2}, {"title", "Hello"}},
        nullptr,
        json::array({1, 2, 3}),
        true
    };

    std::vector<jgit::ObjectId> ids;
    for (auto& doc : docs) {
        ids.push_back(store.put(doc));
    }

    // All hashes are distinct
    for (std::size_t i = 0; i < ids.size(); ++i) {
        for (std::size_t j = i + 1; j < ids.size(); ++j) {
            REQUIRE(ids[i] != ids[j]);
        }
    }

    // Each retrieves correctly
    for (std::size_t i = 0; i < ids.size(); ++i) {
        auto retrieved = store.get(ids[i]);
        REQUIRE(retrieved.has_value());
        REQUIRE(retrieved.value() == docs[i]);
    }

    fs::remove_all(tmp);
}

TEST_CASE("ObjectStore: persists across store instances", "[object_store]")
{
    auto tmp = make_temp_dir("persist");

    jgit::ObjectId stored_id;
    json original = {{"persistent", true}, {"data", {1, 2, 3}}};

    {
        auto store = jgit::ObjectStore::init(tmp);
        stored_id = store.put(original);
    }

    // Re-open the same directory
    {
        jgit::ObjectStore store2(tmp);
        auto retrieved = store2.get(stored_id);
        REQUIRE(retrieved.has_value());
        REQUIRE(retrieved.value() == original);
    }

    fs::remove_all(tmp);
}
