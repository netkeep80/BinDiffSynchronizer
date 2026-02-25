#include <catch2/catch_test_macros.hpp>
#include "jgit/hash.h"

// -----------------------------------------------------------------------
// Tests for SHA-256 content addressing (jgit/hash.h + third_party/sha256.hpp)
// -----------------------------------------------------------------------

TEST_CASE("Hash: SHA-256 of empty input produces known value", "[hash]")
{
    // SHA-256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
    std::vector<uint8_t> empty_data;
    std::string hex = sha256_hex(empty_data);
    REQUIRE(hex == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("Hash: SHA-256 of known string produces correct result", "[hash]")
{
    // SHA-256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
    // Verified with: echo -n "abc" | sha256sum
    std::string input = "abc";
    std::vector<uint8_t> data(input.begin(), input.end());
    std::string hex = sha256_hex(data);
    REQUIRE(hex == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("Hash: ObjectId has correct structure", "[hash]")
{
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    jgit::ObjectId id = jgit::hash_object(data);

    REQUIRE(id.hex.size() == 64u);
    REQUIRE(id.dir().size() == 2u);
    REQUIRE(id.file().size() == 62u);
    REQUIRE(id.hex == id.dir() + id.file());
}

TEST_CASE("Hash: hex contains only lowercase hex characters", "[hash]")
{
    std::vector<uint8_t> data = {0xDE, 0xAD, 0xBE, 0xEF};
    jgit::ObjectId id = jgit::hash_object(data);

    for (char c : id.hex) {
        bool is_hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        REQUIRE(is_hex);
    }
}

TEST_CASE("Hash: same data produces same ObjectId (deterministic)", "[hash]")
{
    std::vector<uint8_t> data = {10, 20, 30, 40};
    jgit::ObjectId id1 = jgit::hash_object(data);
    jgit::ObjectId id2 = jgit::hash_object(data);
    REQUIRE(id1 == id2);
}

TEST_CASE("Hash: different data produces different ObjectId", "[hash]")
{
    std::vector<uint8_t> data1 = {1, 2, 3};
    std::vector<uint8_t> data2 = {1, 2, 4};
    jgit::ObjectId id1 = jgit::hash_object(data1);
    jgit::ObjectId id2 = jgit::hash_object(data2);
    REQUIRE(id1 != id2);
}

TEST_CASE("Hash: ObjectId equality operators", "[hash]")
{
    std::vector<uint8_t> data = {99};
    jgit::ObjectId a = jgit::hash_object(data);
    jgit::ObjectId b = jgit::hash_object(data);
    jgit::ObjectId c = jgit::hash_object({100});

    REQUIRE(a == b);
    REQUIRE_FALSE(a == c);
    REQUIRE(a != c);
    REQUIRE_FALSE(a != b);
}
