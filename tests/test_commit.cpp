#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include "jgit/commit.h"
#include "jgit/hash.h"

namespace fs = std::filesystem;
using nlohmann::json;

// -----------------------------------------------------------------------
// Tests for jgit::Commit (jgit/commit.h)
// -----------------------------------------------------------------------

TEST_CASE("Commit: to_json produces expected keys", "[commit]")
{
    jgit::Commit c;
    c.root      = jgit::ObjectId{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};
    c.parent    = std::nullopt;
    c.author    = "alice";
    c.timestamp = 1000000000LL;
    c.message   = "Initial commit";

    json j = c.to_json();
    REQUIRE(j["type"]      == "commit");
    REQUIRE(j["root"]      == "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    REQUIRE(j["parent"]    == nullptr);
    REQUIRE(j["author"]    == "alice");
    REQUIRE(j["timestamp"] == 1000000000LL);
    REQUIRE(j["message"]   == "Initial commit");
}

TEST_CASE("Commit: to_json with parent ObjectId", "[commit]")
{
    jgit::Commit c;
    c.root      = jgit::ObjectId{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};
    c.parent    = jgit::ObjectId{"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"};
    c.author    = "bob";
    c.timestamp = 1000000001LL;
    c.message   = "Second commit";

    json j = c.to_json();
    REQUIRE(j["parent"] == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
}

TEST_CASE("Commit: from_json round-trip without parent", "[commit]")
{
    jgit::Commit original;
    original.root      = jgit::ObjectId{"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"};
    original.parent    = std::nullopt;
    original.author    = "charlie";
    original.timestamp = 1234567890LL;
    original.message   = "Hello world";

    json j = original.to_json();
    jgit::Commit restored = jgit::Commit::from_json(j);

    REQUIRE(restored.root.hex    == original.root.hex);
    REQUIRE(!restored.parent.has_value());
    REQUIRE(restored.author      == original.author);
    REQUIRE(restored.timestamp   == original.timestamp);
    REQUIRE(restored.message     == original.message);
}

TEST_CASE("Commit: from_json round-trip with parent", "[commit]")
{
    jgit::Commit original;
    original.root      = jgit::ObjectId{"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"};
    original.parent    = jgit::ObjectId{"eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"};
    original.author    = "dave";
    original.timestamp = 9999999999LL;
    original.message   = "With parent";

    json j = original.to_json();
    jgit::Commit restored = jgit::Commit::from_json(j);

    REQUIRE(restored.root.hex           == original.root.hex);
    REQUIRE(restored.parent.has_value());
    REQUIRE(restored.parent->hex        == original.parent->hex);
    REQUIRE(restored.author             == original.author);
    REQUIRE(restored.timestamp          == original.timestamp);
    REQUIRE(restored.message            == original.message);
}

TEST_CASE("Commit: from_json throws on non-commit object", "[commit]")
{
    json j = {{"type", "blob"}, {"data", "something"}};
    REQUIRE_THROWS_AS(jgit::Commit::from_json(j), std::invalid_argument);
}
