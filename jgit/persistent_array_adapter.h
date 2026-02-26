#pragma once

// =============================================================================
// Task 3.3.4 — jgit::persistent_array_adapter<T, Allocator>
//
// A drop-in replacement for std::vector that satisfies the nlohmann::basic_json
// ArrayType template parameter interface.
//
// Purpose:
//   nlohmann::basic_json<ObjectType, ArrayType, ...> requires ArrayType to be
//   a SequenceContainer template with signature:
//       template<typename T, typename... Args> class ArrayType
//
//   This adapter wraps std::vector<T, Allocator> to provide the required
//   interface under the jgit:: namespace, making the type clearly identifiable
//   as a "persistent-adapter" type for JSON array storage.
//
// Design rationale:
//   The elements stored in this vector are nlohmann::basic_json nodes, which
//   are NOT trivially copyable.  Therefore the raw jgit::persistent_array<T,N>
//   slab struct (which requires trivially copyable T) cannot be used directly
//   as the ArrayType for basic_json — it would violate the slab's layout
//   guarantees.
//
//   This adapter uses std::vector internally and is intended as:
//     (a) a clearly-named "persistent family" wrapper establishing the adapter
//         pattern from the phase3-plan.md Task 3.3.4;
//     (b) a bridge so that persistent_json (nlohmann::basic_json with all
//         jgit:: types) compiles and passes all tests;
//     (c) the foundation for a future version that memory-maps the array
//         elements to a flat binary file using jgit's PageDevice.
//
// Usage:
//   #include "jgit/persistent_array_adapter.h"
//
//   // The ArrayType template parameter for basic_json:
//   using persistent_json = nlohmann::basic_json<
//       jgit::persistent_map_adapter,   // ObjectType
//       jgit::persistent_array_adapter, // ArrayType
//       jgit::persistent_string         // StringType
//   >;
//
// Full std::vector-compatible interface is provided by inheriting from
// std::vector.  No additional members are required by nlohmann::basic_json.
// =============================================================================

#include <vector>
#include <memory>

namespace jgit {

// persistent_array_adapter<T, Allocator>
//
// Template parameters match the signature expected by nlohmann::basic_json
// for its ArrayType parameter:
//   template<typename T, typename... Args> class ArrayType
//
// We derive from std::vector<T, Allocator> to inherit the complete
// SequenceContainer interface without duplicating it.
//
// The only addition is a "persistent_adapter" type tag so that external code
// can identify this as a jgit persistent-family type.
template<
    typename T,
    typename Allocator = std::allocator<T>
>
class persistent_array_adapter
    : public std::vector<T, Allocator>
{
public:
    // ---- type tag ----
    static constexpr bool is_persistent_adapter = true;

    // ---- inherit all constructors from std::vector ----
    using base_type = std::vector<T, Allocator>;
    using base_type::base_type;      // inheriting constructors (C++11)
    using base_type::operator=;
};

} // namespace jgit
