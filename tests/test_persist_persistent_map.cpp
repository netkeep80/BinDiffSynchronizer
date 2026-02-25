#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <type_traits>
#include <string>
#include <vector>

#include "persist.h"
#include "jgit/persistent_map.h"

// =============================================================================
// Task 3.2.2 — Tests for persist<jgit::persistent_map<V,C>> and the new
//              fptr<persistent_map<V,C>> next_node overflow chain
// =============================================================================
//
// Scope:
//   - Verify persist<persistent_map<int32_t>> compiles and round-trips.
//   - Verify the overflow chain works: multiple slabs linked via next_node.
//   - Verify persistence survives destroy/recreate cycles.
//
// NOTE ON STACK SIZE: persistent_map<V, C> contains C entries of persistent_string
// (~65 KB each), so sizeof(persistent_map<int32_t, 8>) ≈ 512 KB.
// persist<T> stores _data[sizeof(T)] inline.
// To avoid stack overflow on Windows (1 MB stack), persist<Map> objects that
// hold large maps are heap-allocated via std::unique_ptr.
// Capacity-2 maps (~130 KB) are small enough for stack use.
// =============================================================================

namespace {
    std::string tmp_name_pm(const char* tag)
    {
        return std::string("./test_persist_pm_") + tag + ".tmp";
    }

    void rm_pm(const std::string& path)
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
} // anonymous namespace

// ---------------------------------------------------------------------------
// 3.2.2.1 — persist<persistent_map<int32_t>> save and load
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.2.2.1: persist<persistent_map<int32_t>> saves on destruction, loads on construction",
          "[task3.2][persist_persistent_map]")
{
    // Use Capacity=2 so the persist object is ~130 KB (fits on Windows stack).
    using Map = jgit::persistent_map<int32_t, 2>;

    std::string fname = tmp_name_pm("int_roundtrip");
    rm_pm(fname);

    // Phase 1: create, insert, destroy → file written
    {
        persist<Map> p(fname);
        Map m = static_cast<Map>(p);
        m.insert_or_assign("alpha", 1);
        m.insert_or_assign("beta",  2);
        p = m;
    }

    // Phase 2: recreate with same name, read back
    {
        persist<Map> p(fname);
        Map m = static_cast<Map>(p);
        REQUIRE(m.size == 2u);

        int32_t* va = m.find("alpha");
        int32_t* vb = m.find("beta");

        REQUIRE(va != nullptr);
        REQUIRE(*va == 1);
        REQUIRE(vb != nullptr);
        REQUIRE(*vb == 2);
    }

    rm_pm(fname);
}

// ---------------------------------------------------------------------------
// 3.2.2.2 — Insert elements, destroy, reload — all elements present
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.2.2.2: persist<persistent_map> elements survive destroy/reload cycle",
          "[task3.2][persist_persistent_map]")
{
    using Map = jgit::persistent_map<int32_t, 4>;

    std::string fname = tmp_name_pm("reload");
    rm_pm(fname);

    // Heap-allocate the persist<Map> to avoid ~262 KB stack frame on Windows.
    auto p = std::make_unique<persist<Map>>(fname);
    Map m = static_cast<Map>(*p);
    m.insert_or_assign("w", 10);
    m.insert_or_assign("x", 20);
    m.insert_or_assign("y", 30);
    m.insert_or_assign("z", 40);
    REQUIRE(m.full());
    *p = m;
    p.reset();  // triggers destructor → writes to file

    // Reload and verify all 4 entries
    p = std::make_unique<persist<Map>>(fname);
    Map loaded = static_cast<Map>(*p);
    p.reset();

    REQUIRE(loaded.size == 4u);
    REQUIRE(loaded.contains("w"));
    REQUIRE(loaded.contains("x"));
    REQUIRE(loaded.contains("y"));
    REQUIRE(loaded.contains("z"));
    REQUIRE(*loaded.find("w") == 10);
    REQUIRE(*loaded.find("x") == 20);
    REQUIRE(*loaded.find("y") == 30);
    REQUIRE(*loaded.find("z") == 40);

    rm_pm(fname);
}

// ---------------------------------------------------------------------------
// 3.2.2.3 — Overflow chain via fptr: insert > Capacity elements across two
//            slabs; persist each slab and load the full chain
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.2.2.3: persist<persistent_map> overflow chain via fptr next_node",
          "[task3.2][persist_persistent_map]")
{
    // Use Capacity=3: each slab is ~196 KB, fits on stack individually.
    using Map = jgit::persistent_map<int32_t, 3>;

    std::string fname1 = tmp_name_pm("chain_slab1");
    std::string fname2 = tmp_name_pm("chain_slab2");
    rm_pm(fname1);
    rm_pm(fname2);

    // Phase 1: fill first slab (capacity 3), chain to second slab for overflow.
    // Persist both slabs to files.
    {
        persist<Map> p1(fname1);
        persist<Map> p2(fname2);

        Map slab1 = static_cast<Map>(p1);
        Map slab2 = static_cast<Map>(p2);

        // Insert 3 items into slab1 (fills it)
        slab1.insert_or_assign("aa", 1);
        slab1.insert_or_assign("bb", 2);
        slab1.insert_or_assign("cc", 3);
        REQUIRE(slab1.full());

        // Insert 2 more items into slab2 (overflow)
        slab2.insert_or_assign("dd", 4);
        slab2.insert_or_assign("ee", 5);
        REQUIRE(slab2.size == 2u);

        // Chain slab1 → slab2 using fptr::set_addr with a logical slot index.
        // Here we use 1 as the slab2 pool index (external pool assigns indices).
        slab1.next_node.set_addr(1u);
        REQUIRE(slab1.next_node.addr() == 1u);

        p1 = slab1;
        p2 = slab2;
    }

    // Phase 2: reload both slabs from files, verify chain integrity.
    {
        persist<Map> p1(fname1);
        persist<Map> p2(fname2);

        Map slab1 = static_cast<Map>(p1);
        Map slab2 = static_cast<Map>(p2);

        // Verify slab1 contents
        REQUIRE(slab1.size == 3u);
        REQUIRE(slab1.full());
        REQUIRE(*slab1.find("aa") == 1);
        REQUIRE(*slab1.find("bb") == 2);
        REQUIRE(*slab1.find("cc") == 3);

        // Verify next_node pointer was persisted
        REQUIRE(slab1.next_node.addr() == 1u);

        // Verify slab2 contents (the overflow slab)
        REQUIRE(slab2.size == 2u);
        REQUIRE(*slab2.find("dd") == 4);
        REQUIRE(*slab2.find("ee") == 5);

        // Verify slab2 has no further overflow
        REQUIRE(slab2.next_node.addr() == 0u);
    }

    rm_pm(fname1);
    rm_pm(fname2);
}

// ---------------------------------------------------------------------------
// 3.2.2.4 — erase element, persistence after erase
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.2.2.4: persist<persistent_map> erase survives persist/reload",
          "[task3.2][persist_persistent_map]")
{
    using Map = jgit::persistent_map<int32_t, 4>;

    std::string fname = tmp_name_pm("erase");
    rm_pm(fname);

    // Phase 1: insert 3 elements (heap-alloc to avoid ~262 KB stack frame)
    {
        auto p = std::make_unique<persist<Map>>(fname);
        Map m = static_cast<Map>(*p);
        m.insert_or_assign("keep1", 100);
        m.insert_or_assign("remove", 200);
        m.insert_or_assign("keep2", 300);
        *p = m;
    }

    // Phase 2: reload, erase one element, save again
    {
        auto p = std::make_unique<persist<Map>>(fname);
        Map m = static_cast<Map>(*p);
        REQUIRE(m.size == 3u);

        bool erased = m.erase("remove");
        REQUIRE(erased);
        REQUIRE(m.size == 2u);
        REQUIRE(m.find("remove") == nullptr);

        *p = m;
    }

    // Phase 3: reload again, verify erased element is gone
    {
        auto p = std::make_unique<persist<Map>>(fname);
        Map m = static_cast<Map>(*p);
        REQUIRE(m.size == 2u);
        REQUIRE(m.find("remove") == nullptr);
        REQUIRE(m.contains("keep1"));
        REQUIRE(m.contains("keep2"));
        REQUIRE(*m.find("keep1") == 100);
        REQUIRE(*m.find("keep2") == 300);
    }

    rm_pm(fname);
}

// ---------------------------------------------------------------------------
// 3.2.2.5 — Iterate over elements of a saved/loaded slab
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.2.2.5: iterate over entries of a loaded persist<persistent_map>",
          "[task3.2][persist_persistent_map]")
{
    // Use Capacity=4: ~262 KB on heap — heap-alloc to avoid stack overflow.
    using Map = jgit::persistent_map<int32_t, 4>;

    std::string fname = tmp_name_pm("iterate");
    rm_pm(fname);

    // Phase 1: insert sorted keys with known values
    {
        auto p = std::make_unique<persist<Map>>(fname);
        Map m = static_cast<Map>(*p);
        m.insert_or_assign("c", 30);
        m.insert_or_assign("a", 10);
        m.insert_or_assign("b", 20);
        *p = m;
    }

    // Phase 2: reload and iterate; verify sorted order and all values
    {
        auto p = std::make_unique<persist<Map>>(fname);
        Map m = static_cast<Map>(*p);

        REQUIRE(m.size == 3u);

        std::vector<std::pair<std::string, int32_t>> items;
        for (const auto& entry : m) {
            items.push_back({ entry.key.to_std_string(), entry.value });
        }

        // persistent_map keeps entries sorted by key
        REQUIRE(items.size() == 3u);
        REQUIRE(items[0].first == "a");
        REQUIRE(items[0].second == 10);
        REQUIRE(items[1].first == "b");
        REQUIRE(items[1].second == 20);
        REQUIRE(items[2].first == "c");
        REQUIRE(items[2].second == 30);
    }

    rm_pm(fname);
}
