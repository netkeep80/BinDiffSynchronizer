#pragma once

// =============================================================================
// Task 3.3.4 — jgit::persistent_map_adapter<K, V, Compare, Allocator>
//
// A drop-in replacement for std::map that satisfies the nlohmann::basic_json
// ObjectType template parameter interface.
//
// Purpose:
//   nlohmann::basic_json<ObjectType, ...> requires ObjectType to be an
//   AssociativeContainer template with signature:
//       template<Key, Value, Compare, Allocator> class ObjectType
//
//   This adapter wraps std::map<K, V, Compare, Allocator> to provide the
//   required interface under the jgit:: namespace, making the type clearly
//   identifiable as a "persistent-adapter" type for JSON object storage.
//
// Design rationale:
//   The values stored in this map are nlohmann::basic_json nodes, which are
//   NOT trivially copyable.  Therefore the raw jgit::persistent_map<V, N>
//   slab struct (which requires trivially copyable V) cannot be used directly
//   as the ObjectType for basic_json — it would violate the slab's layout
//   guarantees.
//
//   This adapter uses std::map internally and is intended as:
//     (a) a clearly-named "persistent family" wrapper establishing the adapter
//         pattern from the phase3-plan.md Task 3.3.4;
//     (b) a bridge so that persistent_json (nlohmann::basic_json with all
//         jgit:: types) compiles and passes all tests;
//     (c) the foundation for a future version that memory-maps the map entries
//         to a flat binary file using jgit's PageDevice / AddressManager.
//
// Usage:
//   #include "jgit/persistent_map_adapter.h"
//
//   // The ObjectType template parameter for basic_json:
//   using persistent_json = nlohmann::basic_json<
//       jgit::persistent_map_adapter,   // ObjectType
//       jgit::persistent_array_adapter, // ArrayType
//       jgit::persistent_string         // StringType
//   >;
//
// Full std::map-compatible interface is provided by inheriting from std::map.
// =============================================================================

#include <map>
#include <functional>
#include <memory>

namespace jgit {

// persistent_map_adapter<K, V, Compare, Allocator>
//
// Template parameters match the signature expected by nlohmann::basic_json
// for its ObjectType parameter:
//   template<typename K, typename V, typename... Args> class ObjectType
//
// We derive from std::map<K, V, Compare, Allocator> to inherit the complete
// AssociativeContainer interface without duplicating it.
//
// The only addition is a "persistent_adapter" type tag so that external code
// can identify this as a jgit persistent-family type.
template<
    typename K,
    typename V,
    typename Compare   = std::less<K>,
    typename Allocator = std::allocator<std::pair<const K, V>>
>
class persistent_map_adapter
    : public std::map<K, V, Compare, Allocator>
{
public:
    // ---- type tag ----
    static constexpr bool is_persistent_adapter = true;

    // ---- inherit all constructors from std::map ----
    using base_type = std::map<K, V, Compare, Allocator>;
    using base_type::base_type;          // inheriting constructors (C++11)
    using base_type::operator=;
};

} // namespace jgit
