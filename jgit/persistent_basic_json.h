#pragma once

// =============================================================================
// Task 3.3.3 / 3.3.4 — jgit::persistent_json
//
// An instantiation of nlohmann::basic_json<> that replaces ALL default
// container/string types with jgit persistent-family types:
//
//   ObjectType  = jgit::persistent_map_adapter   — persistent-family map adapter
//                     (Task 3.3.4: replaces std::map; wraps std::map with the
//                     jgit persistent-adapter pattern for future mmap backing)
//   ArrayType   = jgit::persistent_array_adapter — persistent-family array adapter
//                     (Task 3.3.4: replaces std::vector; wraps std::vector with
//                     the jgit persistent-adapter pattern for future mmap backing)
//   StringType  = jgit::persistent_string         — persistent, fixed-size,
//                     trivially-copyable string (SSO + fptr<char> for long strings)
//   BooleanType  = bool             — Unchanged.
//   NumberIntegerType  = int64_t    — Unchanged.
//   NumberUnsignedType = uint64_t   — Unchanged.
//   NumberFloatType    = double     — Unchanged.
//   AllocatorType      = std::allocator — Unchanged.
//   JSONSerializer     = nlohmann::adl_serializer — Unchanged.
//   BinaryType         = std::vector<uint8_t>     — Unchanged.
//
// Usage:
//   #include "jgit/persistent_basic_json.h"
//
//   jgit::persistent_json j;
//   j["name"] = "Alice";
//   j["age"]  = 30;
//   std::string s = j.dump();
//
// Design notes:
//   - persistent_json is NOT trivially copyable (nlohmann::basic_json stores
//     a tagged union with non-trivial members). Use snapshot()/restore() via
//     ObjectStore for persistence across restarts.
//   - ObjectType and ArrayType use jgit::persistent_map_adapter and
//     jgit::persistent_array_adapter respectively.  These are currently backed
//     by std::map/std::vector and provide the full standard container interface
//     required by nlohmann::basic_json.  They are named as "persistent-family"
//     adapters to document intent and provide a clear extension point for
//     future implementations backed by jgit's PageDevice / mmap.
//   - StringType = jgit::persistent_string uses SSO for short strings and
//     fptr<char> (AddressManager-backed) for long strings, avoiding heap.
//
// =============================================================================

#include <nlohmann/json.hpp>
#include "persistent_string.h"
#include "persistent_map_adapter.h"
#include "persistent_array_adapter.h"

namespace jgit {

// persistent_json: nlohmann::basic_json fully specialised with jgit types.
//
//   ObjectType  = jgit::persistent_map_adapter   (Task 3.3.4)
//   ArrayType   = jgit::persistent_array_adapter (Task 3.3.4)
//   StringType  = jgit::persistent_string        (Task 3.3.3)
//
// All three container/string types are now from the jgit:: persistent-family,
// completing the Task 3.3.4 requirement to replace std::map and std::vector
// with persistent adapters.
using persistent_json = nlohmann::basic_json<
    jgit::persistent_map_adapter,    // ObjectType  — persistent-family map adapter
    jgit::persistent_array_adapter,  // ArrayType   — persistent-family array adapter
    jgit::persistent_string          // StringType  — persistent, fixed-size, trivially-copyable
>;

} // namespace jgit
