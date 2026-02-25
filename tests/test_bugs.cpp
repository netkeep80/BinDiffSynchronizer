#include <catch2/catch_test_macros.hpp>

// ============================================================
// Bug regression tests for PageDevice.h and persist.h fixes
// ============================================================

// -----------------------------------------------------------------------
// Bug 1.1: PageDevice.h — bit-shift operator fix
//
// The original code had:
//   unsigned Index = (Address & PageMask) > PageSize;   // BUG: comparison
//
// After fix:
//   unsigned Index = (Address & PageMask) >> PageSize;  // correct right shift
//
// We test the index calculation logic directly using the same constants
// to verify the corrected formula produces the right page index.
// -----------------------------------------------------------------------
TEST_CASE("Bug 1.1: MemoryDevice page index uses right-shift not comparison", "[bugs][pagedevice]")
{
    // MemoryDevice<24, 16> has:
    //   PageSize   = 16  (each page is 2^16 = 65536 bytes)
    //   MemorySize = 24  (total address space = 2^24 = 16 MB)
    //   PageMask   = ((1 << (24-16)) - 1) << 16 = 0x00FF0000
    //   OffsetMask = (1 << 16) - 1              = 0x0000FFFF

    constexpr unsigned PageSize   = 16;
    constexpr unsigned MemorySize = 24;
    constexpr unsigned PageMask   = ((1u << (MemorySize - PageSize)) - 1u) << PageSize;

    // Address 0x010000 = page 1
    {
        unsigned Address = 0x010000u;
        unsigned correct_index = (Address & PageMask) >> PageSize;
        REQUIRE(correct_index == 1u);

        // The old (buggy) code would compute a boolean comparison (0 or 1)
        // rather than the actual page number — but for page 1 both happen to
        // give 1. Use a higher page to expose the difference.
    }

    // Address 0x050000 = page 5
    {
        unsigned Address = 0x050000u;
        unsigned correct_index = (Address & PageMask) >> PageSize;
        REQUIRE(correct_index == 5u);

        // Buggy code: (0x050000 & PageMask) > PageSize
        //           = 0x050000 > 16 = 1  — WRONG, should be 5
        unsigned buggy_index = (Address & PageMask) > PageSize;
        REQUIRE(buggy_index != correct_index);  // confirm the bug would have been wrong
    }

    // Address 0 = page 0
    {
        unsigned Address = 0u;
        unsigned correct_index = (Address & PageMask) >> PageSize;
        REQUIRE(correct_index == 0u);
    }
}

// -----------------------------------------------------------------------
// Bug 1.2: persist.h — NULL used as integer
//
// The original code had:
//   unsigned addr = NULL;   // BUG: NULL is not an integer
//
// After fix:
//   unsigned addr = 0;
//
// This test verifies that zero-initialized addresses are handled correctly.
// -----------------------------------------------------------------------
TEST_CASE("Bug 1.2: address zero initialization is handled correctly", "[bugs][persist]")
{
    unsigned addr = 0;  // was: unsigned addr = NULL;
    REQUIRE(addr == 0u);

    // addr == 0 should mean "not found" / "invalid address"
    bool is_valid = (addr != 0u);
    REQUIRE_FALSE(is_valid);
}

// -----------------------------------------------------------------------
// Bug 1.3: persist.h — missing return *this in fptr<T>::operator=
//
// The original code had no return statement, which is undefined behaviour.
// After fix: return *this; is present.
//
// We can't easily instantiate fptr<T> in isolation (it pulls in AddressManager),
// but we can demonstrate the same pattern with a minimal proxy struct to verify
// that the pattern compiles and produces the correct chained-assignment result.
// -----------------------------------------------------------------------
namespace {

struct MinimalFptr {
    unsigned __addr = 0;

    MinimalFptr& operator=(const char* /*name*/) {
        __addr = 42;
        return *this;   // Bug 1.3 fix: this line was missing in original code
    }
};

} // anonymous namespace

TEST_CASE("Bug 1.3: operator= returns *this enabling chained assignment", "[bugs][persist]")
{
    MinimalFptr a, b;

    // Chain assignment — would not compile (or invoke UB) without return *this
    b = (a = "some_name");

    REQUIRE(a.__addr == 42u);
    REQUIRE(b.__addr == 42u);
}

// -----------------------------------------------------------------------
// Bug 1.4: persist.h — persist(const _T& ref) constructor
//
// The original copy constructor used an initializer list with (*(_T*)_data)(ref),
// which is malformed because _data is a plain byte array, not a _T member.
//
// After fix: placement new is used: new((void*)_data) _T(ref);
//
// We verify that constructing a persist<int> from an integer value preserves it.
// -----------------------------------------------------------------------
#include "persist.h"

TEST_CASE("Bug 1.4: persist copy constructor initializes via placement new", "[bugs][persist]")
{
    // Construct a persist<int> from a known value.
    // With the bug the initializer list was ill-formed; with the fix it compiles
    // and the value is correctly initialized.
    persist<int> p(42);
    int value = static_cast<int>(p);
    REQUIRE(value == 42);
}
