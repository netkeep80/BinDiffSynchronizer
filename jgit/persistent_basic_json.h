#pragma once

// =============================================================================
// Task 3.3.3 — jgit::persistent_json
//
// An instantiation of nlohmann::basic_json<> that uses jgit::persistent_string
// as its StringType.  This replaces the default std::string key/value storage
// with jgit's fixed-size, trivially-copyable string, enabling direct binary
// serialisation of string values without heap allocation.
//
// Template parameter choices (Phase 3.3.3 — first stage):
//
//   ObjectType  = std::map          — Standard ordered map.  Replaced by a
//                                     persistent adapter in Task 3.3.4.
//   ArrayType   = std::vector       — Standard resizable array.  Replaced by
//                                     a persistent adapter in Task 3.3.4.
//   StringType  = jgit::persistent_string — Fixed-size, trivially-copyable
//                                     string; replaces std::string.
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
// Limitations in this first stage:
//   - persistent_json is NOT trivially copyable (nlohmann::basic_json stores
//     a tagged union with non-trivial members). Use snapshot()/restore() via
//     ObjectStore for persistence across restarts.
//   - ObjectType and ArrayType remain std::map/std::vector (heap-allocated).
//     Task 3.3.4 replaces these with persistent adapters.
//
// =============================================================================

#include <map>
#include <vector>
#include <nlohmann/json.hpp>
#include "persistent_string.h"

namespace jgit {

// persistent_json: nlohmann::basic_json specialised with jgit::persistent_string
// as the string type.  The persistent_string replaces std::string for all
// JSON string values (keys and string-typed leaf values).
//
// ObjectType and ArrayType remain std::map/std::vector in this first stage
// (Task 3.3.3).  They will be replaced with persistent adapters in Task 3.3.4.
using persistent_json = nlohmann::basic_json<
    std::map,                    // ObjectType  (Task 3.3.4: replace with persistent adapter)
    std::vector,                 // ArrayType   (Task 3.3.4: replace with persistent adapter)
    jgit::persistent_string      // StringType  — persistent, fixed-size, trivially-copyable
>;

} // namespace jgit
