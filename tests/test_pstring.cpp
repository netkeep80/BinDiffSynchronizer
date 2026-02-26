#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <type_traits>

#include "pstring.h"

// =============================================================================
// Tests for pstring (persistent string)
// =============================================================================

// ---------------------------------------------------------------------------
// pstring_data — trivially copyable layout
// ---------------------------------------------------------------------------
TEST_CASE("pstring_data: is trivially copyable",
          "[pstring][layout]")
{
    REQUIRE(std::is_trivially_copyable<pstring_data>::value);
    // Phase 3: pstring_data должен занимать 2 * sizeof(void*) байт.
    REQUIRE(sizeof(pstring_data) == 2 * sizeof(void*));
}

// ---------------------------------------------------------------------------
// pstring_data — Phase 3: поля имеют тип uintptr_t
// ---------------------------------------------------------------------------
TEST_CASE("pstring_data: length is uintptr_t (Phase 3)",
          "[pstring][layout][phase3]")
{
    // sizeof(uintptr_t) == sizeof(void*) на любой платформе.
    REQUIRE(sizeof(pstring_data::length) == sizeof(void*));
    // Поле chars (fptr<char>) также хранит uintptr_t.
    REQUIRE(sizeof(pstring_data::chars) == sizeof(void*));
}

// ---------------------------------------------------------------------------
// pstring — default data (null/empty)
// ---------------------------------------------------------------------------
TEST_CASE("pstring: zero-initialised pstring_data gives empty string",
          "[pstring][construct]")
{
    pstring_data sd{};
    pstring ps(sd);

    REQUIRE(ps.empty());
    REQUIRE(ps.size() == 0u);
    REQUIRE(std::strcmp(ps.c_str(), "") == 0);
}

// ---------------------------------------------------------------------------
// pstring — assign short string
// ---------------------------------------------------------------------------
TEST_CASE("pstring: assign short string stores correct content",
          "[pstring][assign]")
{
    pstring_data sd{};
    pstring ps(sd);

    ps.assign("hello");
    REQUIRE(!ps.empty());
    REQUIRE(ps.size() == 5u);
    REQUIRE(std::strcmp(ps.c_str(), "hello") == 0);
}

// ---------------------------------------------------------------------------
// pstring — assign longer string
// ---------------------------------------------------------------------------
TEST_CASE("pstring: assign longer string stores correct content",
          "[pstring][assign]")
{
    pstring_data sd{};
    pstring ps(sd);

    const char* long_str = "The quick brown fox jumps over the lazy dog";
    ps.assign(long_str);
    REQUIRE(ps.size() == std::strlen(long_str));
    REQUIRE(std::strcmp(ps.c_str(), long_str) == 0);
}

// ---------------------------------------------------------------------------
// pstring — reassign to different string
// ---------------------------------------------------------------------------
TEST_CASE("pstring: reassigning frees old allocation and stores new content",
          "[pstring][reassign]")
{
    pstring_data sd{};
    pstring ps(sd);

    ps.assign("first");
    REQUIRE(ps.size() == 5u);

    ps.assign("second value");
    REQUIRE(ps.size() == 12u);
    REQUIRE(std::strcmp(ps.c_str(), "second value") == 0);
}

// ---------------------------------------------------------------------------
// pstring — assign empty string
// ---------------------------------------------------------------------------
TEST_CASE("pstring: assign empty string clears content",
          "[pstring][assign]")
{
    pstring_data sd{};
    pstring ps(sd);

    ps.assign("nonempty");
    REQUIRE(!ps.empty());

    ps.assign("");
    REQUIRE(ps.empty());
    REQUIRE(ps.size() == 0u);
}

// ---------------------------------------------------------------------------
// pstring — assign nullptr
// ---------------------------------------------------------------------------
TEST_CASE("pstring: assign nullptr gives empty string",
          "[pstring][assign]")
{
    pstring_data sd{};
    pstring ps(sd);

    ps.assign(nullptr);
    REQUIRE(ps.empty());
    REQUIRE(ps.size() == 0u);
    REQUIRE(std::strcmp(ps.c_str(), "") == 0);
}

// ---------------------------------------------------------------------------
// pstring — clear
// ---------------------------------------------------------------------------
TEST_CASE("pstring: clear resets to empty",
          "[pstring][clear]")
{
    pstring_data sd{};
    pstring ps(sd);

    ps.assign("hello world");
    REQUIRE(!ps.empty());

    ps.clear();
    REQUIRE(ps.empty());
    REQUIRE(ps.size() == 0u);
    REQUIRE(sd.chars.addr() == 0u);
}

// ---------------------------------------------------------------------------
// pstring — operator==
// ---------------------------------------------------------------------------
TEST_CASE("pstring: operator== compares correctly",
          "[pstring][compare]")
{
    pstring_data sd1{};
    pstring ps1(sd1);
    ps1.assign("hello");

    pstring_data sd2{};
    pstring ps2(sd2);
    ps2.assign("hello");

    pstring_data sd3{};
    pstring ps3(sd3);
    ps3.assign("world");

    REQUIRE(ps1 == ps2);
    REQUIRE(!(ps1 == ps3));
    REQUIRE(ps1 == "hello");
    REQUIRE(!(ps1 == "world"));
}

// ---------------------------------------------------------------------------
// pstring — operator[] character access
// ---------------------------------------------------------------------------
TEST_CASE("pstring: operator[] accesses individual characters",
          "[pstring][index]")
{
    pstring_data sd{};
    pstring ps(sd);
    ps.assign("abc");

    REQUIRE(ps[0] == 'a');
    REQUIRE(ps[1] == 'b');
    REQUIRE(ps[2] == 'c');
}

// ---------------------------------------------------------------------------
// pstring — operator< (lexicographic order)
// ---------------------------------------------------------------------------
TEST_CASE("pstring: operator< gives lexicographic order",
          "[pstring][compare]")
{
    pstring_data sd1{};
    pstring pa(sd1);
    pa.assign("apple");

    pstring_data sd2{};
    pstring pb(sd2);
    pb.assign("banana");

    REQUIRE(pa < pb);
    REQUIRE(!(pb < pa));
}

// ---------------------------------------------------------------------------
// pstring — slot index recorded in pstring_data
// ---------------------------------------------------------------------------
TEST_CASE("pstring: pstring_data records non-zero slot index after assign",
          "[pstring][data]")
{
    pstring_data sd{};
    REQUIRE(sd.chars.addr() == 0u);

    pstring ps(sd);
    ps.assign("test");
    REQUIRE(sd.chars.addr() != 0u);
    REQUIRE(sd.length == 4u);

    ps.clear();
    REQUIRE(sd.chars.addr() == 0u);
    REQUIRE(sd.length == 0u);
}
