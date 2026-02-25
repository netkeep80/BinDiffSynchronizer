#pragma once

#include <cstdint>
#include <cassert>
#include <type_traits>

#include "persistent_string.h"

// =============================================================================
// Task 2.3 — jgit::persistent_json_value
//
// Core persistent node type that replaces nlohmann::json::json_value.
//
// Design:
//   - Fixed-size struct with a type tag and a union of all possible JSON value
//     types.  No heap allocation — fully compatible with persist<T>.
//   - Object and array values are referenced by integer slab IDs (indices into
//     an AddressManager or PageDevice pool), not raw pointers.  The IDs are
//     resolved by PersistentJsonStore (Task 2.4).
//   - String values are stored inline using persistent_string (SSO + fixed
//     buffer), so even strings fit without heap allocation.
//   - Boolean, integer, and float values are stored directly in the union.
//
// Key property: sizeof(persistent_json_value) is a compile-time constant with
// NO heap-allocated members, making it fully compatible with persist<T> and
// AddressManager<persistent_json_value>.
// =============================================================================

namespace jgit {

// JSON type discriminator — stored in a single byte to minimise padding.
enum class json_type : uint8_t {
    null        = 0,
    boolean     = 1,
    number_int  = 2,
    number_float= 3,
    string      = 4,
    array       = 5,   // payload is array_id (slab index in the array pool)
    object      = 6    // payload is object_id (slab index in the map pool)
};

struct persistent_json_value {
    // ---- type discriminator ----
    json_type type;

    // ---- padding to align the union on an 8-byte boundary ----
    // (without explicit padding the compiler would insert it anyway; making it
    // explicit documents intent and helps static_assert checks)
    uint8_t _pad[7];

    // ---- value storage ----
    // The union holds exactly one active member as indicated by `type`.
    // Invariant: all union members that are NOT active must be treated as
    // uninitialised — callers must only read the member matching `type`.
    union {
        bool         boolean_val;   // json_type::boolean
        int64_t      int_val;       // json_type::number_int
        double       float_val;     // json_type::number_float
        persistent_string string_val;  // json_type::string (inline, no heap)
        uint32_t     array_id;      // json_type::array  — slab index in array pool
        uint32_t     object_id;     // json_type::object — slab index in map pool
    };

    // ---- constructors ----

    // Default: null value
    persistent_json_value() noexcept
        : type(json_type::null), _pad{}, int_val(0)
    {}

    // Null
    static persistent_json_value make_null() noexcept {
        persistent_json_value v;
        v.type    = json_type::null;
        v.int_val = 0;
        return v;
    }

    // Boolean
    static persistent_json_value make_bool(bool b) noexcept {
        persistent_json_value v;
        v.type        = json_type::boolean;
        v.boolean_val = b;
        return v;
    }

    // Integer
    static persistent_json_value make_int(int64_t n) noexcept {
        persistent_json_value v;
        v.type    = json_type::number_int;
        v.int_val = n;
        return v;
    }

    // Float
    static persistent_json_value make_float(double d) noexcept {
        persistent_json_value v;
        v.type      = json_type::number_float;
        v.float_val = d;
        return v;
    }

    // String (from C string)
    static persistent_json_value make_string(const char* s) noexcept {
        persistent_json_value v;
        v.type       = json_type::string;
        v.string_val = persistent_string{};
        v.string_val.assign(s);
        return v;
    }

    // String (from std::string)
    static persistent_json_value make_string(const std::string& s) noexcept {
        return make_string(s.c_str());
    }

    // Array (id is a slab index managed by PersistentJsonStore)
    static persistent_json_value make_array(uint32_t id) noexcept {
        persistent_json_value v;
        v.type     = json_type::array;
        v.array_id = id;
        return v;
    }

    // Object (id is a slab index managed by PersistentJsonStore)
    static persistent_json_value make_object(uint32_t object_id_val) noexcept {
        persistent_json_value v;
        v.type      = json_type::object;
        v.object_id = object_id_val;
        return v;
    }

    // ---- type queries ----

    bool is_null()   const noexcept { return type == json_type::null; }
    bool is_bool()   const noexcept { return type == json_type::boolean; }
    bool is_int()    const noexcept { return type == json_type::number_int; }
    bool is_float()  const noexcept { return type == json_type::number_float; }
    bool is_string() const noexcept { return type == json_type::string; }
    bool is_array()  const noexcept { return type == json_type::array; }
    bool is_object() const noexcept { return type == json_type::object; }

    // ---- value accessors (caller must check type first) ----

    bool                    get_bool()   const noexcept { assert(is_bool());   return boolean_val; }
    int64_t                 get_int()    const noexcept { assert(is_int());    return int_val; }
    double                  get_float()  const noexcept { assert(is_float());  return float_val; }
    const persistent_string& get_string() const noexcept { assert(is_string()); return string_val; }
    persistent_string&       get_string()       noexcept { assert(is_string()); return string_val; }
    uint32_t                get_array_id()  const noexcept { assert(is_array());  return array_id; }
    uint32_t                get_object_id() const noexcept { assert(is_object()); return object_id; }
};

// ---- static layout assertions ----

// The struct must be trivially copyable for use with persist<T>.
static_assert(std::is_trivially_copyable<persistent_json_value>::value,
              "jgit::persistent_json_value must be trivially copyable for use with persist<T>");

// The union largest member is persistent_string — verify the struct size is
// predictable.  We allow the compiler to choose the exact size (due to union
// alignment), but document it via a compile-time check.
static_assert(sizeof(persistent_json_value) >= sizeof(persistent_string) + 8,
              "jgit::persistent_json_value: unexpected size — struct too small");

} // namespace jgit
