#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <cstdint>

#include "PageDevice.h"
#include "StaticPageDevice.h"

// =============================================================================
// Task 3.1.5 — Tests for MemoryDevice
// =============================================================================
//
// MemoryDevice wraps a PageDevice and provides byte-level Read/Write access
// across pages.  We use StaticPageDevice as the backing concrete implementation.
//
// MemoryDevice<MemorySize=12, PageSize=8, PoolSize=4, Cache, StaticPageDevice>:
//   - PageSize = 8  → each page is 2^8 = 256 bytes
//   - MemorySize=12 → total address space = 2^12 = 4096 bytes
//   - SpaceSize = 12-8 = 4  → 2^4 = 16 pages
// =============================================================================

// Small MemoryDevice backed by StaticPageDevice for testing.
// MemorySize=12, PageSize=8 → 4096 bytes, 16 pages of 256 bytes each.
using TestMemory = MemoryDevice<12, 8, 4, Cache, StaticPageDevice>;

// ---------------------------------------------------------------------------
// 3.1.5.1 — Write single byte at address 0, read it back
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.1.5.1: Write/Read single byte at address 0",
          "[task3.1][memory_device]")
{
    TestMemory mem;
    unsigned char w = 0xAB;
    bool ok = mem.Write(0, &w, 1);
    REQUIRE(ok);

    unsigned char r = 0x00;
    ok = mem.Read(0, &r, 1);
    REQUIRE(ok);
    REQUIRE(r == 0xAB);
}

// ---------------------------------------------------------------------------
// 3.1.5.2 — Write block crossing page boundary, read it back
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.1.5.2: Write/Read block crossing page boundary",
          "[task3.1][memory_device]")
{
    TestMemory mem;

    // Page boundary is at address 256 (2^8 = 0x100).
    // Write 8 bytes starting at address 252 → spans pages 0 and 1.
    constexpr unsigned start = 252;
    constexpr unsigned len   = 8;
    unsigned char write_buf[len];
    for (unsigned i = 0; i < len; i++) write_buf[i] = (unsigned char)(i + 1);

    bool ok = mem.Write(start, write_buf, len);
    REQUIRE(ok);

    unsigned char read_buf[len] = {};
    ok = mem.Read(start, read_buf, len);
    REQUIRE(ok);
    REQUIRE(std::memcmp(write_buf, read_buf, len) == 0);
}

// ---------------------------------------------------------------------------
// 3.1.5.3 — WriteObject<T> / ReadObject<T> for a POD type
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.1.5.3: WriteObject/ReadObject round-trip for POD type",
          "[task3.1][memory_device]")
{
    TestMemory mem;

    struct Vec3 { float x, y, z; };
    Vec3 src = {1.0f, 2.5f, -3.14f};

    bool ok = mem.WriteObject(0, src);
    REQUIRE(ok);

    Vec3 dst = {};
    ok = mem.ReadObject(0, dst);
    REQUIRE(ok);
    REQUIRE(dst.x == src.x);
    REQUIRE(dst.y == src.y);
    REQUIRE(dst.z == src.z);
}

// ---------------------------------------------------------------------------
// 3.1.5.4 — MemoryDevice address space: page index is bounded by mask
// ---------------------------------------------------------------------------
// The MemoryDevice's address space uses a bitmask to extract the page index.
// Addresses outside the MemorySize range wrap (the high bits are masked off).
// The underlying StaticPageDevice bounds-check returns false for any page index
// >= 2^SpaceSize, and GetData() returns NULL for such cases.
//
// We test that addresses that map to valid page indices (within the mask range)
// are accessible, covering the full range 0..2^SpaceSize-1.
TEST_CASE("Task 3.1.5.4: MemoryDevice accesses all pages within address space",
          "[task3.1][memory_device]")
{
    TestMemory mem;

    // TestMemory: MemorySize=12, PageSize=8 → 16 pages of 256 bytes.
    // Write to the first byte of each of the 16 pages and read them back.
    for (unsigned page = 0; page < 16; page++) {
        unsigned address = page << 8;  // page * 256
        unsigned char w = (unsigned char)(page + 1);
        REQUIRE(mem.Write(address, &w, 1));
        unsigned char r = 0;
        REQUIRE(mem.Read(address, &r, 1));
        REQUIRE(r == w);
    }
}

// ---------------------------------------------------------------------------
// 3.1.5.5 — Sequential writes to different pages all persist
// ---------------------------------------------------------------------------
TEST_CASE("Task 3.1.5.5: Sequential writes to different pages are all readable",
          "[task3.1][memory_device]")
{
    TestMemory mem;

    // Write a unique byte to the start of each of 3 different pages.
    // Page 0: 0x000, Page 1: 0x100, Page 2: 0x200
    unsigned char v0 = 0x11, v1 = 0x22, v2 = 0x33;
    mem.Write(0x000, &v0, 1);
    mem.Write(0x100, &v1, 1);
    mem.Write(0x200, &v2, 1);

    unsigned char r0 = 0, r1 = 0, r2 = 0;
    mem.Read(0x000, &r0, 1);
    mem.Read(0x100, &r1, 1);
    mem.Read(0x200, &r2, 1);

    REQUIRE(r0 == v0);
    REQUIRE(r1 == v1);
    REQUIRE(r2 == v2);
}
