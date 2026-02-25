#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include "persist.h"

// =============================================================================
// Task 3.1.3 — Tests for AddressManager<_T>
// =============================================================================
//
// AddressManager manages a persistent address space of up to AddressSpace
// slots of type T.  Slot 0 is reserved (null/invalid).
//
// NOTE: AddressManager<T> is a per-type process-wide singleton.  We use unique
// tag types per test to get independent managers and avoid cross-test state.
// =============================================================================

// ---------------------------------------------------------------------------
// 3.1.3.1 — Create() returns an index >= 1
// ---------------------------------------------------------------------------
namespace t313_1 {
    struct Obj { int val = 0; };
}
TEST_CASE("Task 3.1.3.1: AddressManager::Create returns index >= 1",
          "[task3.1][address_manager]")
{
    using namespace t313_1;
    char name[] = "obj_313_1";
    unsigned idx = AddressManager<Obj>::Create(name);
    REQUIRE(idx >= 1u);
    AddressManager<Obj>::Delete(idx);
}

// ---------------------------------------------------------------------------
// 3.1.3.2 — Find() by name returns the index of a previously created object
// ---------------------------------------------------------------------------
namespace t313_2 {
    struct Obj { int val = 0; };
}
TEST_CASE("Task 3.1.3.2: AddressManager::Find locates created object by name",
          "[task3.1][address_manager]")
{
    using namespace t313_2;
    char name[] = "obj_313_2";
    unsigned created = AddressManager<Obj>::Create(name);
    REQUIRE(created >= 1u);

    unsigned found = AddressManager<Obj>::Find(name);
    REQUIRE(found == created);

    AddressManager<Obj>::Delete(created);
}

// ---------------------------------------------------------------------------
// 3.1.3.3 — Create() with same name returns existing index (no duplicate)
//            (The fixed Create() now calls Find() and returns the existing index)
// ---------------------------------------------------------------------------
namespace t313_3 {
    struct Obj { int val = 0; };
}
TEST_CASE("Task 3.1.3.3: AddressManager::Create with duplicate name returns existing index",
          "[task3.1][address_manager]")
{
    using namespace t313_3;
    char name[] = "obj_313_3_dup";
    unsigned first  = AddressManager<Obj>::Create(name);
    unsigned second = AddressManager<Obj>::Create(name);
    REQUIRE(first == second);
    AddressManager<Obj>::Delete(first);
}

// ---------------------------------------------------------------------------
// 3.1.3.4 — Address space limit: creating more than ADDRESS_SPACE-1 unique objects fails
// ---------------------------------------------------------------------------
// This test uses a small address space via the AddressSpace template parameter.
namespace t313_4 {
    struct Obj { int val = 0; };
}
TEST_CASE("Task 3.1.3.4: AddressManager with small AddressSpace limits creation",
          "[task3.1][address_manager]")
{
    using namespace t313_4;
    // Use AddressSpace=4: slots 1,2,3 valid; slot 0 reserved.
    // We create 3 objects; the 4th attempt should return 0.
    char n1[] = "small_1", n2[] = "small_2", n3[] = "small_3", n4[] = "small_4";
    unsigned a = AddressManager<Obj, 4>::Create(n1);
    unsigned b = AddressManager<Obj, 4>::Create(n2);
    unsigned c = AddressManager<Obj, 4>::Create(n3);
    unsigned d = AddressManager<Obj, 4>::Create(n4);  // should fail (space full)

    REQUIRE(a >= 1u);
    REQUIRE(b >= 1u);
    REQUIRE(c >= 1u);
    REQUIRE(d == 0u);  // address space exhausted

    AddressManager<Obj, 4>::Delete(a);
    AddressManager<Obj, 4>::Delete(b);
    AddressManager<Obj, 4>::Delete(c);
}

// ---------------------------------------------------------------------------
// 3.1.3.5 — After Delete(), the slot is no longer found by Find()
// ---------------------------------------------------------------------------
namespace t313_5 {
    struct Obj { int val = 0; };
}
TEST_CASE("Task 3.1.3.5: After Delete, Find returns 0 for that name",
          "[task3.1][address_manager]")
{
    using namespace t313_5;
    char name[] = "obj_313_5";
    unsigned idx = AddressManager<Obj>::Create(name);
    REQUIRE(idx >= 1u);

    AddressManager<Obj>::Delete(idx);

    unsigned found = AddressManager<Obj>::Find(name);
    REQUIRE(found == 0u);
}

// ---------------------------------------------------------------------------
// 3.1.3.6 — AddressSpace is a template parameter (verify compile-time flexibility)
// ---------------------------------------------------------------------------
namespace t313_6 {
    struct Obj { int val = 0; };
}
TEST_CASE("Task 3.1.3.6: AddressManager<T, AddressSpace> template param compiles",
          "[task3.1][address_manager]")
{
    using namespace t313_6;
    // Verify different AddressSpace instantiations compile and work independently
    char n8[] = "obj_8slot";
    char n16[] = "obj_16slot";
    unsigned idx8  = AddressManager<Obj, 8>::Create(n8);
    unsigned idx16 = AddressManager<Obj, 16>::Create(n16);

    REQUIRE(idx8  >= 1u);
    REQUIRE(idx16 >= 1u);

    AddressManager<Obj, 8>::Delete(idx8);
    AddressManager<Obj, 16>::Delete(idx16);
}
