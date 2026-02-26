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
TEST_CASE("Task 2.2.1.1: persistent_string is fixed-size (no regular heap members)",
          "[task2.2][persistent_string][layout]")
{
    // Task 3.4 redesign: persistent_string now uses fptr<char> for long strings
    // instead of a static 65 KB buffer.  It is no longer trivially copyable
    // (the copy constructor/copy assignment allocate new persistent memory for
    // long strings), but the struct is still fixed-size with no regular heap
    // pointers — all dynamic storage goes through AddressManager<char>.

    // The struct should be significantly smaller than the old 65 KB layout.
    // New layout: sso_buf[24] + bool(1) + padding + fptr<char>(4) + unsigned(4).
    constexpr size_t new_size = sizeof(jgit::persistent_string);
    // Must be much smaller than the old 65561-byte layout.
    REQUIRE(new_size < 1024u);
    // Must be at least SSO_SIZE+1 (sso_buf) + 1 (is_long) + 4 (fptr) + 4 (long_len).
    REQUIRE(new_size >= jgit::persistent_string::SSO_SIZE + 1 + 1 + 4 + 4);
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
// 2.2.1.8 — SSO short string raw-byte copy equivalence (persist<T> compat.)
// ---------------------------------------------------------------------------
// Task 3.4 note: persistent_string is no longer trivially copyable because the
// copy constructor allocates new persistent memory for long strings.  For
// short strings (SSO path, ≤ SSO_SIZE chars), the entire state is in sso_buf[]
// and raw-byte copy still works correctly.  Long strings store a fptr<char>
// address index; raw-byte copy of a long string shares the AddressManager<char>
// slot (same __addr), which is correct in a single process (both the original
// and the copy point to the same slot).  Cross-process persistence of long
// strings works via AddressManager<char>'s itable save/restore mechanism.
TEST_CASE("Task 2.2.1.8: persistent_string SSO short string survives raw-byte copy",
          "[task2.2][persistent_string][persist-compat]")
{
    // Short string — SSO path, safe to raw-copy.
    jgit::persistent_string original("test-key");
    REQUIRE(!original.is_long);  // "test-key" is 8 chars, well within SSO_SIZE

    alignas(jgit::persistent_string) unsigned char buf[sizeof(jgit::persistent_string)];
    std::memcpy(buf, &original, sizeof(jgit::persistent_string));

    jgit::persistent_string restored;
    std::memcpy(&restored, buf, sizeof(jgit::persistent_string));

    REQUIRE(restored == original);
    REQUIRE(restored.size() == original.size());

    // Long string raw-byte copy: both original and copy share the same
    // AddressManager<char> slot (__addr).  Within a single process this is
    // valid (both point to the same underlying char array).
    std::string long_val(500, 'Q');
    jgit::persistent_string orig_long(long_val);
    REQUIRE(orig_long.is_long);
    std::memcpy(buf, &orig_long, sizeof(jgit::persistent_string));
    jgit::persistent_string rest_long;
    std::memcpy(&rest_long, buf, sizeof(jgit::persistent_string));
    // Both share the same slot index — content is identical.
    REQUIRE(rest_long == orig_long);
    // Avoid double-free: do not call free_long() on the raw-byte copy.
    // Only free the original (which owns the slot).
    orig_long.free_long();
}
