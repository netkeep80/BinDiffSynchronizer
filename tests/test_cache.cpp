#include <catch2/catch_test_macros.hpp>

#include <vector>
#include <map>
#include <cstring>

#include "PageDevice.h"

// =============================================================================
// Task 3.1.4 — Tests for Cache<T, CacheSize, SpaceSize>
// =============================================================================
//
// Cache is an abstract base class.  We test it through a minimal concrete
// subclass (MockCache) that tracks Load/Save call counts and provides a simple
// backing store (map of index -> data).
// =============================================================================

namespace {

// A simple 4-byte page type for testing.
struct TestPage { int value = 0; };

// Concrete cache with CacheSize=4, SpaceSize=4 (address space of 2^4=16 slots).
class MockCache : public Cache<TestPage, 4, 4>
{
public:
    int load_count = 0;
    int save_count = 0;
    std::map<unsigned, TestPage> backing;

    // Destructor: call Flush() while our vtable (Load/Save) is still valid.
    // Must be done here (not in Cache::~Cache) to avoid pure-virtual calls.
    virtual ~MockCache()
    {
        this->Flush();
    }

protected:
    bool Load(unsigned Index, TestPage& Ref) override
    {
        ++load_count;
        auto it = backing.find(Index);
        if (it != backing.end()) {
            Ref = it->second;
            return true;
        }
        Ref.value = 0;
        return true;  // success (zero-filled page)
    }

    bool Save(unsigned Index, const TestPage& Ref)
    {
        ++save_count;
        backing[Index] = Ref;
        return true;
    }

    // Satisfy the pure virtual signature (non-const ref)
    bool Save(unsigned Index, TestPage& Ref) override
    {
        return Save(Index, static_cast<const TestPage&>(Ref));
    }
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// 3.1.4.1 — GetData for new index calls Load
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.1.4.1: GetData for new index calls Load",
          "[task3.1][cache]")
{
    MockCache c;
    c.load_count = 0;

    TestPage* p = c.GetData(0, false);
    REQUIRE(p != nullptr);
    REQUIRE(c.load_count == 1);
}

// ---------------------------------------------------------------------------
// 3.1.4.2 — GetData for already-cached index does NOT call Load again
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.1.4.2: GetData for cached index does not call Load again",
          "[task3.1][cache]")
{
    MockCache c;
    c.GetData(0, false);
    int count_after_first = c.load_count;

    c.GetData(0, false);  // same index — should be a cache hit
    REQUIRE(c.load_count == count_after_first);  // no extra Load
}

// ---------------------------------------------------------------------------
// 3.1.4.3 — ForWrite=true marks the page dirty
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.1.4.3: GetData with ForWrite=true marks page dirty",
          "[task3.1][cache]")
{
    MockCache c;
    TestPage* p = c.GetData(0, true);  // ForWrite = true
    REQUIRE(p != nullptr);

    // Force eviction: fill remaining slots and one more to evict slot 0
    // CacheSize=4, so we need slots 1,2,3,4 to evict slot 0 (FIFO).
    c.GetData(1, false);
    c.GetData(2, false);
    c.GetData(3, false);
    // Accessing slot 4 will evict slot 0 (LFI/FIFO: PoolPos was 0 for first load)
    c.save_count = 0;
    c.GetData(4, false);

    // Slot 0 was dirty, so it should have been saved during eviction
    REQUIRE(c.save_count >= 1);
}

// ---------------------------------------------------------------------------
// 3.1.4.4 — Eviction of dirty page calls Save
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.1.4.4: Eviction of dirty page calls Save",
          "[task3.1][cache]")
{
    MockCache c;
    // Write to slot 0
    TestPage* p = c.GetData(0, true);
    REQUIRE(p != nullptr);
    p->value = 42;

    // Fill the rest of the cache to force eviction of slot 0
    c.GetData(1, false);
    c.GetData(2, false);
    c.GetData(3, false);

    c.save_count = 0;
    c.GetData(4, false);  // evicts slot 0 (dirty)
    REQUIRE(c.save_count >= 1);
    REQUIRE(c.backing[0].value == 42);
}

// ---------------------------------------------------------------------------
// 3.1.4.5 — Eviction of clean page does NOT call Save
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.1.4.5: Eviction of clean page does not call Save",
          "[task3.1][cache]")
{
    MockCache c;
    // Read (not write) slot 0 — clean
    c.GetData(0, false);

    c.GetData(1, false);
    c.GetData(2, false);
    c.GetData(3, false);

    c.save_count = 0;
    c.GetData(4, false);  // evicts slot 0 (clean)
    REQUIRE(c.save_count == 0);
}

// ---------------------------------------------------------------------------
// 3.1.4.6 — Flush() saves all dirty pages
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.1.4.6: Flush saves all dirty pages",
          "[task3.1][cache]")
{
    MockCache c;
    TestPage* p0 = c.GetData(0, true);
    if (p0) p0->value = 10;
    TestPage* p1 = c.GetData(1, true);
    if (p1) p1->value = 20;
    c.GetData(2, false);  // clean

    c.save_count = 0;
    c.Flush();

    REQUIRE(c.save_count == 2);  // only 2 dirty pages saved
}

// ---------------------------------------------------------------------------
// 3.1.4.7 — Destructor saves dirty pages (via Flush in concrete destructor)
// ---------------------------------------------------------------------------
// We use an external variable to track the save_count at destruction time.
namespace t3147 {
    int saves_at_destruction = 0;

    struct TrackedPage { int value = 0; };

    class TrackedCache : public Cache<TrackedPage, 4, 4>
    {
    public:
        virtual ~TrackedCache()
        {
            this->Flush();
            saves_at_destruction = save_count;
        }
        int save_count = 0;

    protected:
        bool Load(unsigned /*Index*/, TrackedPage& Ref) override
        {
            Ref.value = 0;
            return true;
        }
        bool Save(unsigned /*Index*/, TrackedPage& /*Ref*/) override
        {
            ++save_count;
            return true;
        }
    };
} // namespace t3147

TEST_CASE("Task 3.1.4.7: Destructor saves dirty pages via Flush",
          "[task3.1][cache]")
{
    using namespace t3147;
    saves_at_destruction = 0;
    {
        TrackedCache c;
        TrackedPage* p = c.GetData(0, true);
        if (p) p->value = 99;
        // c goes out of scope — destructor calls Flush
    }
    // Destructor should have called Save for the one dirty page
    REQUIRE(saves_at_destruction >= 1);
}

// ---------------------------------------------------------------------------
// 3.1.4.8 — Filling cache up to CacheSize pages: all cache hits after first load
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.1.4.8: Filling cache to CacheSize, no evictions",
          "[task3.1][cache]")
{
    MockCache c;
    // CacheSize = 4; fill all 4 slots
    c.GetData(0, false);
    c.GetData(1, false);
    c.GetData(2, false);
    c.GetData(3, false);

    // All 4 pages are cached — re-accessing should not call Load again
    int loads_before = c.load_count;
    c.GetData(0, false);
    c.GetData(1, false);
    c.GetData(2, false);
    c.GetData(3, false);
    REQUIRE(c.load_count == loads_before);  // no additional loads
}

// ---------------------------------------------------------------------------
// 3.1.4.9 — Eviction on overflow: CacheSize + 1 pages causes one eviction
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.1.4.9: CacheSize+1 pages causes eviction",
          "[task3.1][cache]")
{
    MockCache c;
    // Fill the cache (4 slots)
    c.GetData(0, false);
    c.GetData(1, false);
    c.GetData(2, false);
    c.GetData(3, false);

    int loads_before = c.load_count;
    // Access slot 4 — must evict one existing slot and Load slot 4
    c.GetData(4, false);
    REQUIRE(c.load_count == loads_before + 1);  // one extra load for the new slot
}
