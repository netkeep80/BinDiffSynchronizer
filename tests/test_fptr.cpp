#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <type_traits>

#include "persist.h"

// =============================================================================
// Task 3.1.2 — Tests for fptr<_T>
// =============================================================================
//
// fptr<T> is a persistent pointer: it stores an unsigned address index into
// AddressManager<T>. Tests verify creation, copy, dereference, Delete(), and
// reference-count semantics.
//
// NOTE: AddressManager<T> is a process-wide singleton per type T.  To avoid
// cross-test interference we use distinct tag types (unique struct definitions)
// for each test, so each test gets its own independent AddressManager instance.
// =============================================================================

// ---------------------------------------------------------------------------
// 3.1.2.1 — fptr::New() creates an object, operator->() returns valid pointer
// ---------------------------------------------------------------------------
namespace t312_1 {
    struct Val { int x = 0; };
}
TEST_CASE("Task 3.1.2.1: fptr::New creates object, operator-> returns valid pointer",
          "[task3.1][fptr]")
{
    using namespace t312_1;
    fptr<Val> fp;
    fp.New(const_cast<char*>("test_fptr_new"));

    REQUIRE(fp.addr() != 0u);
    fp->x = 99;
    REQUIRE(fp->x == 99);

    fp.Delete();
    REQUIRE(fp.addr() == 0u);
}

// ---------------------------------------------------------------------------
// 3.1.2.2 — copy constructor copies address correctly (was bug: ptr->__addr)
// ---------------------------------------------------------------------------
namespace t312_2 {
    struct Val { int x = 0; };
}
TEST_CASE("Task 3.1.2.2: fptr copy constructor copies address (not __addr via operator->)",
          "[task3.1][fptr]")
{
    using namespace t312_2;
    fptr<Val> original;
    original.New(const_cast<char*>("test_copy_ctor"));
    original->x = 7;

    // Copy: fptr(fptr<_T>& ptr) — was buggy, now fixed to `ptr.__addr`
    fptr<Val> copy(original);
    REQUIRE(copy.addr() == original.addr());
    REQUIRE(copy->x == 7);

    original.Delete();
}

// ---------------------------------------------------------------------------
// 3.1.2.3 — operator*() returns reference to the object
// ---------------------------------------------------------------------------
namespace t312_3 {
    struct Val { int x = 0; };
}
TEST_CASE("Task 3.1.2.3: fptr operator*() returns reference to the object",
          "[task3.1][fptr]")
{
    using namespace t312_3;
    fptr<Val> fp;
    fp.New(const_cast<char*>("test_deref"));

    (*fp).x = 55;
    REQUIRE((*fp).x == 55);

    fp.Delete();
}

// ---------------------------------------------------------------------------
// 3.1.2.4 — fptr(char*) finds existing object by name
// ---------------------------------------------------------------------------
namespace t312_4 {
    struct Val { int x = 0; };
}
TEST_CASE("Task 3.1.2.4: fptr(char*) constructor finds existing object by name",
          "[task3.1][fptr]")
{
    using namespace t312_4;
    fptr<Val> fp;
    fp.New(const_cast<char*>("test_find_by_name"));
    fp->x = 123;

    // Construct another fptr pointing to the same named slot
    fptr<Val> fp2(const_cast<char*>("test_find_by_name"));
    REQUIRE(fp2.addr() == fp.addr());
    REQUIRE(fp2->x == 123);

    fp.Delete();
}

// ---------------------------------------------------------------------------
// 3.1.2.5 — Delete() makes the slot inaccessible
// ---------------------------------------------------------------------------
namespace t312_5 {
    struct Val { int x = 0; };
}
TEST_CASE("Task 3.1.2.5: fptr::Delete() makes slot inaccessible",
          "[task3.1][fptr]")
{
    using namespace t312_5;
    fptr<Val> fp;
    fp.New(const_cast<char*>("test_delete"));
    fp->x = 77;

    fp.Delete();
    REQUIRE(fp.addr() == 0u);

    // A new fptr searching for the same name should NOT find it
    fptr<Val> fp2(const_cast<char*>("test_delete"));
    REQUIRE(fp2.addr() == 0u);
}

// ---------------------------------------------------------------------------
// 3.1.2.6 — operator= (char*) reassigns address
// ---------------------------------------------------------------------------
namespace t312_6 {
    struct Val { int x = 0; };
}
TEST_CASE("Task 3.1.2.6: fptr operator=(char*) reassigns to named slot",
          "[task3.1][fptr]")
{
    using namespace t312_6;
    fptr<Val> fp;
    fp.New(const_cast<char*>("test_assign_name"));
    fp->x = 88;
    unsigned first_addr = fp.addr();

    fptr<Val> fp2;
    fp2 = const_cast<char*>("test_assign_name");
    REQUIRE(fp2.addr() == first_addr);
    REQUIRE(fp2->x == 88);

    fp.Delete();
}
