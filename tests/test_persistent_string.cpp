#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>
#include <type_traits>

#include "jgit/persistent_string.h"

// =============================================================================
// Task 2.2.1 — Unit tests for jgit::persistent_string
// =============================================================================

// ---------------------------------------------------------------------------
// 2.2.1.1 — Layout: fixed-size, trivially copyable
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.2.1.1: persistent_string is trivially copyable and fixed-size",
          "[task2.2][persistent_string][layout]")
{
    // Must be trivially copyable so that persist<persistent_string> can
    // save/load the raw bytes safely.
    REQUIRE(std::is_trivially_copyable<jgit::persistent_string>::value);

    // Size must be the compile-time constant — no hidden heap allocation.
    constexpr size_t expected_size =
        jgit::persistent_string::SSO_SIZE + 1    // sso_buf
        + 1                                      // is_long
        + jgit::persistent_string::LONG_BUF_SIZE + 1; // long_buf
    REQUIRE(sizeof(jgit::persistent_string) == expected_size);
}

// ---------------------------------------------------------------------------
// 2.2.1.2 — Default constructor: empty string
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.2.1.2: persistent_string default constructor gives empty string",
          "[task2.2][persistent_string][construct]")
{
    jgit::persistent_string s;
    REQUIRE(s.empty());
    REQUIRE(s.size() == 0u);
    REQUIRE(std::strcmp(s.c_str(), "") == 0);
    REQUIRE(!s.is_long);
}

// ---------------------------------------------------------------------------
// 2.2.1.3 — SSO path: short strings (≤ SSO_SIZE chars)
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.2.1.3: persistent_string SSO path for short strings",
          "[task2.2][persistent_string][sso]")
{
    // Single character
    jgit::persistent_string a("x");
    REQUIRE(!a.is_long);
    REQUIRE(a.size() == 1u);
    REQUIRE(std::strcmp(a.c_str(), "x") == 0);

    // Exactly SSO_SIZE characters (maximum inline length)
    std::string max_sso(jgit::persistent_string::SSO_SIZE, 'A');
    jgit::persistent_string b(max_sso.c_str());
    REQUIRE(!b.is_long);
    REQUIRE(b.size() == jgit::persistent_string::SSO_SIZE);
    REQUIRE(b == max_sso);
}

// ---------------------------------------------------------------------------
// 2.2.1.4 — Long string path: strings longer than SSO_SIZE
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.2.1.4: persistent_string long path for strings > SSO_SIZE",
          "[task2.2][persistent_string][long]")
{
    // SSO_SIZE + 1 characters — should switch to long path
    std::string long_str(jgit::persistent_string::SSO_SIZE + 1, 'Z');
    jgit::persistent_string s(long_str.c_str());

    REQUIRE(s.is_long);
    REQUIRE(s.size() == long_str.size());
    REQUIRE(s == long_str);

    // 64-character string (typical ObjectId length)
    std::string id_str(64, 'f');
    jgit::persistent_string id(id_str.c_str());
    REQUIRE(id.is_long);
    REQUIRE(id.size() == 64u);
    REQUIRE(id == id_str);
}

// ---------------------------------------------------------------------------
// 2.2.1.5 — Assignment from C string and std::string
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.2.1.5: persistent_string assignment operators",
          "[task2.2][persistent_string][assign]")
{
    jgit::persistent_string s;

    // Assign short string
    s = "hello";
    REQUIRE(!s.is_long);
    REQUIRE(s.size() == 5u);
    REQUIRE(s == "hello");

    // Re-assign with a long string
    std::string long_val(100, 'X');
    s = long_val;
    REQUIRE(s.is_long);
    REQUIRE(s.size() == 100u);
    REQUIRE(s == long_val);

    // Re-assign back to short string
    s = "short";
    REQUIRE(!s.is_long);
    REQUIRE(s == "short");
}

// ---------------------------------------------------------------------------
// 2.2.1.6 — Comparison operators
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.2.1.6: persistent_string comparison operators",
          "[task2.2][persistent_string][compare]")
{
    jgit::persistent_string a("apple");
    jgit::persistent_string b("banana");
    jgit::persistent_string a2("apple");

    REQUIRE(a == a2);
    REQUIRE(a != b);
    REQUIRE(a < b);   // "apple" < "banana" lexicographically
    REQUIRE(a == "apple");
    REQUIRE(a == std::string("apple"));
}

// ---------------------------------------------------------------------------
// 2.2.1.7 — Conversion to std::string
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.2.1.7: persistent_string to_std_string round-trip",
          "[task2.2][persistent_string][convert]")
{
    // Short string round-trip
    std::string orig_short = "hello";
    jgit::persistent_string ps(orig_short);
    REQUIRE(ps.to_std_string() == orig_short);

    // Long string round-trip
    std::string orig_long(200, 'W');
    jgit::persistent_string pl(orig_long);
    REQUIRE(pl.to_std_string() == orig_long);
}

// ---------------------------------------------------------------------------
// 2.2.1.8 — Raw-byte copy equivalence (persist<T> compatibility)
// ---------------------------------------------------------------------------
TEST_CASE("Task 2.2.1.8: persistent_string survives raw-byte copy (persist<T> pattern)",
          "[task2.2][persistent_string][persist-compat]")
{
    jgit::persistent_string original("test-key");

    // Simulate what persist<T> does: save raw bytes, then load them back.
    alignas(jgit::persistent_string) unsigned char buf[sizeof(jgit::persistent_string)];
    std::memcpy(buf, &original, sizeof(jgit::persistent_string));

    jgit::persistent_string restored;
    std::memcpy(&restored, buf, sizeof(jgit::persistent_string));

    REQUIRE(restored == original);
    REQUIRE(restored.size() == original.size());

    // Same for a long string
    std::string long_val(500, 'Q');
    jgit::persistent_string orig_long(long_val);
    std::memcpy(buf, &orig_long, sizeof(jgit::persistent_string));
    jgit::persistent_string rest_long;
    std::memcpy(&rest_long, buf, sizeof(jgit::persistent_string));
    REQUIRE(rest_long == orig_long);
}
