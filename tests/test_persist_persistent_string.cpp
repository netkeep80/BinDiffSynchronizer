#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <type_traits>
#include <string>

#include "persist.h"
#include "jgit/persistent_string.h"

// =============================================================================
// Task 3.2.1 — Tests for persist<jgit::persistent_string>
// =============================================================================
//
// Scope: verify that persist<persistent_string> compiles and correctly
// saves/loads a jgit::persistent_string across construction/destruction cycles.
//
// persistent_string is already trivially copyable (verified by static_assert
// in persistent_string.h), so no changes to the header are needed.
// =============================================================================

namespace {
    std::string tmp_name_ps(const char* tag)
    {
        return std::string("./test_persist_ps_") + tag + ".tmp";
    }

    void rm_ps(const std::string& path)
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
} // anonymous namespace

// ---------------------------------------------------------------------------
// 3.2.1.1 — create, assign, destroy, reload from named file (round-trip)
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.2.1.1: persist<persistent_string> saves on destruction, loads on construction",
          "[task3.2][persist_persistent_string]")
{
    using jgit::persistent_string;

    std::string fname = tmp_name_ps("roundtrip");
    rm_ps(fname);

    // Phase 1: create, write value, destroy → file written
    {
        persist<persistent_string> p(fname);
        persistent_string ps("hello");
        p = ps;
    }

    // Phase 2: recreate with same name, read back
    {
        persist<persistent_string> p(fname);
        persistent_string loaded = static_cast<persistent_string>(p);
        REQUIRE(loaded == "hello");
    }

    rm_ps(fname);
}

// ---------------------------------------------------------------------------
// 3.2.1.2 — short strings (SSO path, <= 23 chars) persist correctly
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.2.1.2: persist<persistent_string> correctly persists short strings (SSO path)",
          "[task3.2][persist_persistent_string]")
{
    using jgit::persistent_string;

    std::string fname = tmp_name_ps("sso");
    rm_ps(fname);

    const char* short_str = "hi";  // 2 chars, well within SSO_SIZE (23)

    {
        persist<persistent_string> p(fname);
        persistent_string ps(short_str);
        REQUIRE_FALSE(ps.is_long);  // verify it is in SSO path
        p = ps;
    }

    {
        persist<persistent_string> p(fname);
        persistent_string loaded = static_cast<persistent_string>(p);
        REQUIRE_FALSE(loaded.is_long);
        REQUIRE(loaded == short_str);
        REQUIRE(loaded.size() == std::strlen(short_str));
    }

    rm_ps(fname);
}

// ---------------------------------------------------------------------------
// 3.2.1.3 — long strings (LONG_BUF path, > 23 chars) persist correctly
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.2.1.3: persist<persistent_string> correctly persists long strings (LONG_BUF path)",
          "[task3.2][persist_persistent_string]")
{
    using jgit::persistent_string;

    std::string fname = tmp_name_ps("longbuf");
    rm_ps(fname);

    // A string longer than SSO_SIZE (23 chars)
    const std::string long_str(80, 'x');  // 80 'x' characters

    {
        persist<persistent_string> p(fname);
        persistent_string ps(long_str);
        REQUIRE(ps.is_long);  // verify it is in LONG_BUF path
        p = ps;
    }

    {
        persist<persistent_string> p(fname);
        persistent_string loaded = static_cast<persistent_string>(p);
        REQUIRE(loaded.is_long);
        REQUIRE(loaded == long_str);
        REQUIRE(loaded.size() == long_str.size());
    }

    rm_ps(fname);
}

// ---------------------------------------------------------------------------
// 3.2.1.4 — loaded string compares equal to original via operator==
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.2.1.4: loaded persistent_string compares equal to original",
          "[task3.2][persist_persistent_string]")
{
    using jgit::persistent_string;

    std::string fname = tmp_name_ps("compare");
    rm_ps(fname);

    const char* original_cstr = "compare_me";

    {
        persist<persistent_string> p(fname);
        persistent_string orig(original_cstr);
        p = orig;
    }

    {
        persist<persistent_string> p(fname);
        persistent_string loaded = static_cast<persistent_string>(p);

        // Test operator==(const persistent_string&)
        persistent_string expected(original_cstr);
        REQUIRE(loaded == expected);

        // Test operator==(const char*)
        REQUIRE(loaded == original_cstr);

        // Test operator==(const std::string&)
        REQUIRE(loaded == std::string(original_cstr));
    }

    rm_ps(fname);
}

// ---------------------------------------------------------------------------
// 3.2.1.5 — empty string saves and loads correctly
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.2.1.5: persist<persistent_string> correctly handles empty string",
          "[task3.2][persist_persistent_string]")
{
    using jgit::persistent_string;

    std::string fname = tmp_name_ps("empty");
    rm_ps(fname);

    {
        persist<persistent_string> p(fname);
        persistent_string empty_ps{};
        REQUIRE(empty_ps.empty());
        p = empty_ps;
    }

    {
        persist<persistent_string> p(fname);
        persistent_string loaded = static_cast<persistent_string>(p);
        REQUIRE(loaded.empty());
        REQUIRE(loaded.size() == 0u);
        REQUIRE(loaded == "");
    }

    rm_ps(fname);
}
