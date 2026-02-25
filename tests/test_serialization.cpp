#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "jgit/serialization.h"

using nlohmann::json;

// -----------------------------------------------------------------------
// Tests for JSON <-> CBOR round-trip serialization (jgit/serialization.h)
// -----------------------------------------------------------------------

TEST_CASE("Serialization: null round-trip", "[serialization]")
{
    json original = nullptr;
    auto bytes = jgit::to_bytes(original);
    json recovered = jgit::from_bytes(bytes);
    REQUIRE(recovered == original);
}

TEST_CASE("Serialization: boolean round-trip", "[serialization]")
{
    SECTION("true") {
        json original = true;
        REQUIRE(jgit::from_bytes(jgit::to_bytes(original)) == original);
    }
    SECTION("false") {
        json original = false;
        REQUIRE(jgit::from_bytes(jgit::to_bytes(original)) == original);
    }
}

TEST_CASE("Serialization: integer round-trip", "[serialization]")
{
    SECTION("zero") {
        json original = 0;
        REQUIRE(jgit::from_bytes(jgit::to_bytes(original)) == original);
    }
    SECTION("positive") {
        json original = 12345;
        REQUIRE(jgit::from_bytes(jgit::to_bytes(original)) == original);
    }
    SECTION("negative") {
        json original = -9999;
        REQUIRE(jgit::from_bytes(jgit::to_bytes(original)) == original);
    }
    SECTION("large") {
        json original = 1000000000LL;
        REQUIRE(jgit::from_bytes(jgit::to_bytes(original)) == original);
    }
}

TEST_CASE("Serialization: float round-trip", "[serialization]")
{
    json original = 3.14159;
    auto bytes = jgit::to_bytes(original);
    json recovered = jgit::from_bytes(bytes);
    REQUIRE(recovered.is_number_float());
    REQUIRE(recovered.get<double>() == Catch::Approx(3.14159));
}

TEST_CASE("Serialization: string round-trip", "[serialization]")
{
    SECTION("empty string") {
        json original = "";
        REQUIRE(jgit::from_bytes(jgit::to_bytes(original)) == original);
    }
    SECTION("ASCII string") {
        json original = "hello, world";
        REQUIRE(jgit::from_bytes(jgit::to_bytes(original)) == original);
    }
    SECTION("Unicode string") {
        json original = u8"Привет, мир!";
        REQUIRE(jgit::from_bytes(jgit::to_bytes(original)) == original);
    }
}

TEST_CASE("Serialization: array round-trip", "[serialization]")
{
    SECTION("empty array") {
        json original = json::array();
        REQUIRE(jgit::from_bytes(jgit::to_bytes(original)) == original);
    }
    SECTION("mixed array") {
        json original = {1, "two", 3.0, true, nullptr};
        REQUIRE(jgit::from_bytes(jgit::to_bytes(original)) == original);
    }
}

TEST_CASE("Serialization: object round-trip", "[serialization]")
{
    SECTION("empty object") {
        json original = json::object();
        REQUIRE(jgit::from_bytes(jgit::to_bytes(original)) == original);
    }
    SECTION("flat object") {
        json original = {{"key", "value"}, {"number", 42}, {"flag", false}};
        REQUIRE(jgit::from_bytes(jgit::to_bytes(original)) == original);
    }
}

TEST_CASE("Serialization: nested structure round-trip (depth >= 3)", "[serialization]")
{
    json original = {
        {"level1", {
            {"level2", {
                {"level3", {
                    {"value", 123},
                    {"list", {1, 2, 3}}
                }}
            }}
        }},
        {"metadata", {
            {"created", "2026-02-25"},
            {"author", "jgit"}
        }}
    };

    auto bytes = jgit::to_bytes(original);
    json recovered = jgit::from_bytes(bytes);
    REQUIRE(recovered == original);
}

TEST_CASE("Serialization: CBOR output is non-empty", "[serialization]")
{
    json doc = {{"x", 1}};
    auto bytes = jgit::to_bytes(doc);
    REQUIRE(!bytes.empty());
}
