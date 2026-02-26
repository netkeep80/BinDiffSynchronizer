#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <type_traits>

#include "persist.h"

// =============================================================================
// Tests for persist<T>, AddressManager<T>, and fptr<T>
// =============================================================================

namespace {
    // Generate a unique filename per test to avoid cross-test interference.
    std::string tmp_name(const char* tag)
    {
        return std::string("./test_persist_") + tag + ".tmp";
    }

    void rm_file(const std::string& path)
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
} // anonymous namespace

// ---------------------------------------------------------------------------
// persist<int> — save/load roundtrip
// ---------------------------------------------------------------------------
TEST_CASE("persist<int>: saves on destruction, loads on construction",
          "[persist]")
{
    std::string fname = tmp_name("int_roundtrip");
    rm_file(fname);

    {
        persist<int> p(fname);
        p = 12345;
    }
    {
        persist<int> p(fname);
        int val = static_cast<int>(p);
        REQUIRE(val == 12345);
    }

    rm_file(fname);
}

// ---------------------------------------------------------------------------
// persist<double> — save/load roundtrip
// ---------------------------------------------------------------------------
TEST_CASE("persist<double>: saves and loads correctly",
          "[persist]")
{
    std::string fname = tmp_name("double_roundtrip");
    rm_file(fname);

    {
        persist<double> p(fname);
        p = 3.14159265358979;
    }
    {
        persist<double> p(fname);
        double val = static_cast<double>(p);
        REQUIRE(val == 3.14159265358979);
    }

    rm_file(fname);
}

// ---------------------------------------------------------------------------
// persist<int> — value constructor does not load from file
// ---------------------------------------------------------------------------
TEST_CASE("persist<int>: value constructor initialises without file I/O",
          "[persist]")
{
    // The value constructor does not load from file; it initialises from the
    // provided value directly.
    persist<int> p(42);
    int val = static_cast<int>(p);
    REQUIRE(val == 42);
}

// ---------------------------------------------------------------------------
// persist<int> — operator= and operator _Tref
// ---------------------------------------------------------------------------
TEST_CASE("persist<int>: operator= and implicit conversion work correctly",
          "[persist]")
{
    std::string fname = tmp_name("int_ops");
    rm_file(fname);

    persist<int> p(fname);
    p = 100;
    int v1 = static_cast<int>(p);
    REQUIRE(v1 == 100);

    p = 200;
    int v2 = static_cast<int>(p);
    REQUIRE(v2 == 200);

    rm_file(fname);
}

// ---------------------------------------------------------------------------
// persist<T> — file size equals sizeof(T)
// ---------------------------------------------------------------------------
TEST_CASE("persist<int>: saved file size equals sizeof(int)",
          "[persist]")
{
    std::string fname = tmp_name("int_size");
    rm_file(fname);

    {
        persist<int> p(fname);
        p = 999;
    }

    std::error_code ec;
    auto fsize = std::filesystem::file_size(fname, ec);
    REQUIRE(!ec);
    REQUIRE(fsize == sizeof(int));

    rm_file(fname);
}

// ---------------------------------------------------------------------------
// AddressManager — Create, access, Delete
// ---------------------------------------------------------------------------
TEST_CASE("AddressManager<double>: Create / access / Delete",
          "[address_manager]")
{
    // Use a unique name to avoid collision with other tests.
    char name[] = "test_am_create_delete";
    unsigned slot = AddressManager<double>::Create(name);
    REQUIRE(slot != 0u);

    // Write a value through the public fptr interface (operator[] on AddressManager is private).
    fptr<double> dp;
    dp.set_addr(slot);
    *dp = 3.14;
    double val = *dp;
    REQUIRE(val == 3.14);

    // Find by name.
    unsigned found = AddressManager<double>::Find(name);
    REQUIRE(found == slot);

    // Delete and verify slot is freed.
    AddressManager<double>::Delete(slot);
    unsigned found2 = AddressManager<double>::Find(name);
    REQUIRE(found2 == 0u);
}

// ---------------------------------------------------------------------------
// AddressManager — CreateArray / GetCount / GetArrayElement / DeleteArray
// ---------------------------------------------------------------------------
TEST_CASE("AddressManager<int>: CreateArray / GetArrayElement / DeleteArray",
          "[address_manager]")
{
    char name[] = "test_am_array";
    unsigned slot = AddressManager<int>::CreateArray(5, name);
    REQUIRE(slot != 0u);
    REQUIRE(AddressManager<int>::GetCount(slot) == 5u);

    // Write elements.
    for( unsigned i = 0; i < 5; i++ )
        AddressManager<int>::GetArrayElement(slot, i) = static_cast<int>(i * 10);

    // Read back.
    for( unsigned i = 0; i < 5; i++ )
        REQUIRE(AddressManager<int>::GetArrayElement(slot, i) == static_cast<int>(i * 10));

    AddressManager<int>::DeleteArray(slot);
    REQUIRE(AddressManager<int>::Find(name) == 0u);
}

// ---------------------------------------------------------------------------
// fptr<double> — New / dereference / Delete
// ---------------------------------------------------------------------------
TEST_CASE("fptr<double>: New / dereference / Delete",
          "[fptr]")
{
    char name[] = "test_fptr_double_new";
    fptr<double> p;
    REQUIRE(p.addr() == 0u);

    p.New(name);
    REQUIRE(p.addr() != 0u);

    *p = 2.718;
    REQUIRE(*p == 2.718);

    p.Delete();
    REQUIRE(p.addr() == 0u);
}

// ---------------------------------------------------------------------------
// fptr<int> — NewArray / operator[] / count / DeleteArray
// ---------------------------------------------------------------------------
TEST_CASE("fptr<int>: NewArray / operator[] / count / DeleteArray",
          "[fptr]")
{
    fptr<int> arr;
    arr.NewArray(6);
    REQUIRE(arr.addr() != 0u);
    REQUIRE(arr.count() == 6u);

    for( unsigned i = 0; i < 6; i++ )
        arr[i] = static_cast<int>(i + 1);

    for( unsigned i = 0; i < 6; i++ )
        REQUIRE(arr[i] == static_cast<int>(i + 1));

    arr.DeleteArray();
    REQUIRE(arr.addr() == 0u);
    REQUIRE(arr.count() == 0u);
}

// ---------------------------------------------------------------------------
// fptr<T> — trivially copyable (can be embedded in persist<> structs)
// ---------------------------------------------------------------------------
TEST_CASE("fptr<int>: is trivially copyable",
          "[fptr]")
{
    REQUIRE(std::is_trivially_copyable<fptr<int>>::value);
    REQUIRE(std::is_trivially_copyable<fptr<double>>::value);
    REQUIRE(std::is_trivially_copyable<fptr<char>>::value);
}

// ---------------------------------------------------------------------------
// fptr<T> — set_addr / addr roundtrip
// ---------------------------------------------------------------------------
TEST_CASE("fptr<int>: set_addr and addr roundtrip",
          "[fptr]")
{
    fptr<int> p;
    p.set_addr(7u);
    REQUIRE(p.addr() == 7u);
    p.set_addr(0u);
    REQUIRE(p.addr() == 0u);
}
