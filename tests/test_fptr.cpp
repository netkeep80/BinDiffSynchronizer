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

// =============================================================================
// Task 3.4 — fptr<T> array support: NewArray / DeleteArray / operator[]
//
// fptr<T>::NewArray(count) allocates a contiguous array of `count` elements
// in persistent memory via AddressManager<T>::CreateArray.  The count is
// stored in the AddressManager slot metadata so that fptr<T> itself stays
// trivially copyable (stores only __addr).
//
// fptr<T>::operator[](idx) accesses element idx of the persistent array.
// fptr<T>::count() returns the element count stored in the slot.
// fptr<T>::DeleteArray() frees the array and resets __addr to 0.
// =============================================================================

// ---------------------------------------------------------------------------
// 3.4.1 — NewArray() allocates a zero-initialised array; operator[] works
// ---------------------------------------------------------------------------
namespace t341 {
    struct Elem { int x = 0; };
}
TEST_CASE("Task 3.4.1: fptr::NewArray allocates array; operator[] reads/writes elements",
          "[task3.4][fptr][array]")
{
    using namespace t341;
    fptr<Elem> fp;
    fp.NewArray(5);  // allocate 5 elements

    REQUIRE(fp.addr() != 0u);
    REQUIRE(fp.count() == 5u);

    // Elements should be zero-initialised.
    for (unsigned i = 0; i < 5; ++i) {
        REQUIRE(fp[i].x == 0);
    }

    // Write and read back via operator[].
    for (unsigned i = 0; i < 5; ++i) {
        fp[i].x = static_cast<int>(i * 10);
    }
    for (unsigned i = 0; i < 5; ++i) {
        REQUIRE(fp[i].x == static_cast<int>(i * 10));
    }

    fp.DeleteArray();
    REQUIRE(fp.addr() == 0u);
}

// ---------------------------------------------------------------------------
// 3.4.2 — count() returns 0 for single-object slots and correct value for arrays
// ---------------------------------------------------------------------------
namespace t342 {
    struct Elem { int x = 0; };
}
TEST_CASE("Task 3.4.2: fptr::count() returns 0 for single objects, count for arrays",
          "[task3.4][fptr][array]")
{
    using namespace t342;
    // Single object: count should be 0.
    fptr<Elem> fp_single;
    fp_single.New(const_cast<char*>("single_342"));
    REQUIRE(fp_single.count() == 0u);
    fp_single.Delete();

    // Array: count should match allocation count.
    fptr<Elem> fp_arr;
    fp_arr.NewArray(7);
    REQUIRE(fp_arr.count() == 7u);
    fp_arr.DeleteArray();
}

// ---------------------------------------------------------------------------
// 3.4.3 — DeleteArray() frees the slot; addr() becomes 0
// ---------------------------------------------------------------------------
namespace t343 {
    struct Elem { int x = 0; };
}
TEST_CASE("Task 3.4.3: fptr::DeleteArray frees the slot and sets addr to 0",
          "[task3.4][fptr][array]")
{
    using namespace t343;
    fptr<Elem> fp;
    fp.NewArray(3);
    REQUIRE(fp.addr() != 0u);

    fp.DeleteArray();
    REQUIRE(fp.addr() == 0u);
    REQUIRE(fp.count() == 0u);
}

// ---------------------------------------------------------------------------
// 3.4.4 — fptr<char> array: NewArray stores and retrieves char data
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.4.4: fptr<char>::NewArray stores and retrieves char array data",
          "[task3.4][fptr][array][char]")
{
    fptr<char> fp;
    const char* src = "hello persistent array";
    unsigned len = static_cast<unsigned>(std::strlen(src));
    fp.NewArray(len + 1);  // +1 for NUL

    REQUIRE(fp.addr() != 0u);
    REQUIRE(fp.count() == len + 1u);

    // Write string into the array.
    for (unsigned i = 0; i <= len; ++i) fp[i] = src[i];

    // Read back and compare.
    for (unsigned i = 0; i <= len; ++i) {
        REQUIRE(fp[i] == src[i]);
    }

    fp.DeleteArray();
    REQUIRE(fp.addr() == 0u);
}

// ---------------------------------------------------------------------------
// 3.4.5 — Multiple independent fptr arrays don't interfere
// ---------------------------------------------------------------------------
namespace t345 {
    struct Elem { int x = 0; };
}
TEST_CASE("Task 3.4.5: Multiple independent fptr arrays don't interfere",
          "[task3.4][fptr][array]")
{
    using namespace t345;
    fptr<Elem> fp1, fp2;
    fp1.NewArray(4);
    fp2.NewArray(4);

    REQUIRE(fp1.addr() != fp2.addr());  // different slots

    for (unsigned i = 0; i < 4; ++i) {
        fp1[i].x = static_cast<int>(i + 1);
        fp2[i].x = static_cast<int>((i + 1) * 100);
    }
    for (unsigned i = 0; i < 4; ++i) {
        REQUIRE(fp1[i].x == static_cast<int>(i + 1));
        REQUIRE(fp2[i].x == static_cast<int>((i + 1) * 100));
    }

    fp1.DeleteArray();
    fp2.DeleteArray();
    REQUIRE(fp1.addr() == 0u);
    REQUIRE(fp2.addr() == 0u);
}

// ---------------------------------------------------------------------------
// 3.4.6 — fptr is trivially copyable (stores only unsigned __addr)
// ---------------------------------------------------------------------------
namespace t346 {
    struct Elem { int x = 0; };
}
TEST_CASE("Task 3.4.6: fptr<T> remains trivially copyable after array-support additions",
          "[task3.4][fptr][array]")
{
    using namespace t346;
    // fptr<T> must remain trivially copyable so that it can be embedded in
    // trivially-copyable structs (e.g. persistent_map, persistent_array).
    REQUIRE(std::is_trivially_copyable<fptr<Elem>>::value);
    REQUIRE(sizeof(fptr<Elem>) == sizeof(unsigned));

    // Also check for char (used in persistent_string).
    REQUIRE(std::is_trivially_copyable<fptr<char>>::value);
    REQUIRE(sizeof(fptr<char>) == sizeof(unsigned));
}

// ---------------------------------------------------------------------------
// 3.4.7 — persistent_string uses fptr<char> for long strings
// ---------------------------------------------------------------------------
#include "jgit/persistent_string.h"
TEST_CASE("Task 3.4.7: persistent_string long path uses fptr<char> (not static buffer)",
          "[task3.4][fptr][array][persistent_string]")
{
    using jgit::persistent_string;

    // Short string: SSO path, no fptr allocation.
    persistent_string s_short("hello");
    REQUIRE(!s_short.is_long);
    REQUIRE(s_short.long_ptr.addr() == 0u);

    // Long string: fptr<char> allocation.
    std::string long_val(100, 'A');
    persistent_string s_long(long_val.c_str());
    REQUIRE(s_long.is_long);
    REQUIRE(s_long.long_ptr.addr() != 0u);
    REQUIRE(s_long.long_ptr.count() >= 101u);  // char array has >= 101 elements

    // Content is correct.
    REQUIRE(std::strcmp(s_long.c_str(), long_val.c_str()) == 0);

    // Explicit deallocation.
    s_long.free_long();
    REQUIRE(!s_long.is_long);
    REQUIRE(s_long.long_ptr.addr() == 0u);
}
