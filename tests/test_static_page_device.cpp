#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include "PageDevice.h"
#include "StaticPageDevice.h"

// =============================================================================
// Task 3.1.6 — Tests for StaticPageDevice
// =============================================================================
//
// StaticPageDevice is an in-memory PageDevice backed by a static array of pages.
// It fixes the __PageCount dependent-name issue from the original code.
//
// We test Load/Save through the Cache::GetData mechanism inherited from
// PageDevice/Cache — writing data via Save and reading it back via Load.
// We access Load/Save indirectly through a concrete subclass that exposes them.
// =============================================================================

namespace {

// A thin wrapper that exposes protected Load/Save for testing.
template<unsigned PageSize = 8, unsigned PoolSize = 4, unsigned SpaceSize = 3>
class TestStaticPageDevice : public StaticPageDevice<PageSize, PoolSize, SpaceSize>
{
public:
    bool pub_load(unsigned Index, Page<PageSize>& Ref)
    {
        return this->Load(Index, Ref);
    }
    bool pub_save(unsigned Index, Page<PageSize>& Ref)
    {
        return this->Save(Index, Ref);
    }
    unsigned page_count() const
    {
        return 1u << SpaceSize;  // 2^SpaceSize pages
    }
};

using Dev = TestStaticPageDevice<8, 4, 3>;  // 8-byte pages, 4 pool, 8 pages

} // anonymous namespace

// ---------------------------------------------------------------------------
// 3.1.6.1 — Save a page, then Load it back: data survives round-trip
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.1.6.1: StaticPageDevice Save/Load round-trip for one page",
          "[task3.1][static_page_device]")
{
    Dev dev;
    Page<8> write_page, read_page;
    std::memset(write_page.Data, 0xAB, sizeof(write_page.Data));
    std::memset(read_page.Data, 0,     sizeof(read_page.Data));

    bool ok = dev.pub_save(0, write_page);
    REQUIRE(ok);

    ok = dev.pub_load(0, read_page);
    REQUIRE(ok);
    REQUIRE(std::memcmp(write_page.Data, read_page.Data, sizeof(write_page.Data)) == 0);
}

// ---------------------------------------------------------------------------
// 3.1.6.2 — Save multiple pages, load in reverse order
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.1.6.2: StaticPageDevice Save multiple pages, Load in reverse order",
          "[task3.1][static_page_device]")
{
    Dev dev;
    unsigned count = dev.page_count();  // 8 pages

    // Save: fill each page with its index value
    for (unsigned i = 0; i < count; i++) {
        Page<8> p;
        std::memset(p.Data, (int)i, sizeof(p.Data));
        REQUIRE(dev.pub_save(i, p));
    }

    // Load in reverse, verify
    for (unsigned i = count; i-- > 0; ) {
        Page<8> p;
        REQUIRE(dev.pub_load(i, p));
        for (unsigned b = 0; b < sizeof(p.Data); b++) {
            REQUIRE(p.Data[b] == (unsigned char)i);
        }
    }
}

// ---------------------------------------------------------------------------
// 3.1.6.3 — Load/Save out of range returns false
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.1.6.3: StaticPageDevice Load/Save out of range returns false",
          "[task3.1][static_page_device]")
{
    Dev dev;
    unsigned out_of_range = dev.page_count();  // one beyond last valid index

    Page<8> p;
    REQUIRE_FALSE(dev.pub_load(out_of_range, p));
    REQUIRE_FALSE(dev.pub_save(out_of_range, p));
}

// ---------------------------------------------------------------------------
// 3.1.6.4 — After construction, all pages are zeroed
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.1.6.4: StaticPageDevice initializes all pages to zero",
          "[task3.1][static_page_device]")
{
    Dev dev;
    unsigned count = dev.page_count();

    for (unsigned i = 0; i < count; i++) {
        Page<8> p;
        // Force-fill with non-zero so we can detect if Load overwrites it
        std::memset(p.Data, 0xFF, sizeof(p.Data));
        REQUIRE(dev.pub_load(i, p));

        // After loading a freshly constructed page, all bytes should be 0
        for (unsigned b = 0; b < sizeof(p.Data); b++) {
            REQUIRE(p.Data[b] == 0u);
        }
    }
}
