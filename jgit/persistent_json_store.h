#pragma once

#include <vector>
#include <string>
#include <stdexcept>
#include <cstdint>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "persistent_json_value.h"
#include "persistent_array.h"
#include "persistent_map.h"
#include "object_store.h"

// =============================================================================
// Task 2.4 — jgit::PersistentJsonStore
//
// Persistent JSON document store that manages a tree of persistent_json_value
// nodes.  It replaces the heap-allocated, pointer-based nlohmann::json tree
// with a flat array of fixed-size nodes addressed by integer IDs.
//
// Architecture:
//   - Three flat pools, each a std::vector<T> (can be backed by mmap / file):
//       value_pool  — stores persistent_json_value nodes
//       array_pool  — stores persistent_array slabs (elements are value IDs)
//       object_pool — stores persistent_map slabs  (keys are strings, values
//                     are value IDs)
//   - Every node is identified by its uint32_t index in value_pool.
//   - Array slabs store uint32_t value IDs (indices into value_pool).
//   - Object slabs map persistent_string keys to uint32_t value IDs.
//   - ID 0 is reserved as "null / empty"; the pool is 1-indexed.
//
// This implementation stores the pools in process memory (std::vector).
// A future version will memory-map the vectors from disk to achieve
// zero-parse reload — the pools are already laid out as flat arrays of
// fixed-size structs, so mmap is straightforward.
//
// API mirrors the plan in phase2-plan.md, Tasks 2.4 and 2.5.
//
// Task 3.2.4 — Added path-based constructor:
//   explicit PersistentJsonStore(const std::filesystem::path& base_dir)
//   Loads pool state from base_dir/{values,arrays,objects}.bin on construction.
//   Saves pool state on destruction.
//   The no-arg constructor retains in-memory-only behavior for backward compat.
// =============================================================================

namespace jgit {

// Slab types used inside the store.
// Array elements: IDs into value_pool.
using json_array_slab  = persistent_array<uint32_t, 16>;
// Object entries:  persistent_string key  →  uint32_t value ID.
using json_object_slab = persistent_map<uint32_t, 16>;

class PersistentJsonStore {
public:
    // ---- construction ----

    PersistentJsonStore() {
        // Reserve slot 0 as "null / not found" sentinel.
        // Use emplace_back() to construct large slab objects directly in heap
        // memory, avoiding a large (~1 MB) stack-allocated temporary that would
        // overflow the default Windows/MSVC stack (1 MB).
        value_pool_.emplace_back();  // persistent_json_value default = null
        array_pool_.emplace_back();  // json_array_slab default = empty slab
        object_pool_.emplace_back(); // json_object_slab default = empty slab (~1 MB)
    }

    // Task 3.2.4 — Path-based constructor: open or create a persistent store
    // rooted at base_dir.  If pool files already exist, they are loaded;
    // otherwise the store is initialised empty (slot 0 reserved as sentinel).
    // On destruction the pools are automatically saved back to disk.
    explicit PersistentJsonStore(const std::filesystem::path& base_dir)
        : base_dir_(base_dir)
    {
        std::filesystem::create_directories(base_dir_);
        if (!load_pools()) {
            // No existing pool files — initialise empty with sentinel slot 0.
            value_pool_.emplace_back();
            array_pool_.emplace_back();
            object_pool_.emplace_back();
        }
    }

    // Destructor: if a base_dir was provided, save pools to disk automatically.
    ~PersistentJsonStore() {
        if (!base_dir_.empty()) {
            save_pools();
        }
    }

    // ---- import from nlohmann::json (one-time conversion) ----

    /**
     * Recursively convert a nlohmann::json document into the persistent store.
     * Returns the uint32_t ID of the root node in value_pool_.
     * ID 0 is the reserved null sentinel; all real nodes have ID >= 1.
     */
    uint32_t import_json(const nlohmann::json& doc) {
        return import_node(doc);
    }

    // ---- export to nlohmann::json (compatibility / round-trip testing) ----

    /**
     * Recursively reconstruct a nlohmann::json document from the persistent
     * store starting at the node with the given ID.
     * Throws std::out_of_range if root_id is out of bounds.
     */
    nlohmann::json export_json(uint32_t root_id) const {
        return export_node(root_id);
    }

    // ---- direct node access ----

    /**
     * Return a reference to the node with the given ID.
     * Throws std::out_of_range if id is 0 or out of range.
     */
    persistent_json_value& get_node(uint32_t id) {
        if (id == 0 || id >= value_pool_.size()) {
            throw std::out_of_range("PersistentJsonStore::get_node: id out of range");
        }
        return value_pool_[id];
    }

    const persistent_json_value& get_node(uint32_t id) const {
        if (id == 0 || id >= value_pool_.size()) {
            throw std::out_of_range("PersistentJsonStore::get_node: id out of range");
        }
        return value_pool_[id];
    }

    /**
     * Write a new value to an existing node slot.
     * Returns id (for convenience).
     */
    uint32_t set_node(uint32_t id, const persistent_json_value& val) {
        if (id == 0 || id >= value_pool_.size()) {
            throw std::out_of_range("PersistentJsonStore::set_node: id out of range");
        }
        value_pool_[id] = val;
        return id;
    }

    // ---- navigation helpers ----

    /**
     * Look up a field by key inside a JSON object node.
     * Returns the value ID of the field, or 0 if not found.
     */
    uint32_t get_field(uint32_t obj_node_id, const char* key) const {
        if (obj_node_id == 0 || obj_node_id >= value_pool_.size()) return 0;
        const persistent_json_value& node = value_pool_[obj_node_id];
        if (!node.is_object()) return 0;

        uint32_t slab_id = node.get_object_id();
        while (slab_id != 0 && slab_id < object_pool_.size()) {
            const json_object_slab& slab = object_pool_[slab_id];
            const uint32_t* val_id = slab.find(key);
            if (val_id) return *val_id;
            slab_id = slab.next_node.addr();  // Task 3.2.2: was next_node_id
        }
        return 0;
    }

    /**
     * Get the value ID at a given index inside a JSON array node.
     * Returns 0 if the index is out of bounds.
     */
    uint32_t get_index(uint32_t arr_node_id, size_t index) const {
        if (arr_node_id == 0 || arr_node_id >= value_pool_.size()) return 0;
        const persistent_json_value& node = value_pool_[arr_node_id];
        if (!node.is_array()) return 0;

        uint32_t slab_id = node.get_array_id();
        size_t   pos     = index;
        while (slab_id != 0 && slab_id < array_pool_.size()) {
            const json_array_slab& slab = array_pool_[slab_id];
            if (pos < slab.size) {
                return slab[static_cast<uint32_t>(pos)];
            }
            pos     -= slab.size;
            slab_id  = slab.next_slab_id;
        }
        return 0;
    }

    // ---- Task 2.5: integration with Phase 1 ObjectStore ----

    /**
     * Snapshot: export the subtree rooted at root_id as nlohmann::json, then
     * serialize it to CBOR and store it as an immutable blob in the given
     * ObjectStore.  Returns the content-addressed ObjectId of the snapshot.
     *
     * This is analogous to "git commit": the live working tree is serialised
     * into an immutable, content-addressed object in the commit history store.
     *
     * Throws std::out_of_range if root_id is invalid.
     */
    ObjectId snapshot(uint32_t root_id, ObjectStore& store) const {
        nlohmann::json doc = export_json(root_id);
        return store.put(doc);
    }

    /**
     * Restore: load the blob identified by id from the ObjectStore, parse it
     * back as nlohmann::json, and import it into this PersistentJsonStore.
     * Returns the uint32_t node ID of the freshly-imported root in value_pool_.
     *
     * This is analogous to "git checkout": an immutable historical snapshot is
     * deserialised and loaded into the live working tree for further editing.
     *
     * Throws std::runtime_error if id does not exist in the store.
     */
    uint32_t restore(const ObjectId& id, const ObjectStore& store) {
        std::optional<nlohmann::json> doc = store.get(id);
        if (!doc) {
            throw std::runtime_error(
                "PersistentJsonStore::restore: ObjectId not found in store: " + id.hex);
        }
        return import_json(*doc);
    }

    // ---- pool size queries (for testing / debugging) ----

    size_t node_count()   const noexcept { return value_pool_.size() - 1; }  // excludes slot 0
    size_t array_count()  const noexcept { return array_pool_.size() - 1; }
    size_t object_count() const noexcept { return object_pool_.size() - 1; }

private:
    // ---- flat pools ----
    std::vector<persistent_json_value> value_pool_;
    std::vector<json_array_slab>       array_pool_;
    std::vector<json_object_slab>      object_pool_;

    // Task 3.2.4: base directory for pool persistence (empty = in-memory only)
    std::filesystem::path base_dir_;

    // ---- pool persistence helpers ----

    // Write a vector of trivially-copyable T to a binary file.
    template<typename T>
    static void write_pool(const std::filesystem::path& path, const std::vector<T>& pool) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        uint64_t count = static_cast<uint64_t>(pool.size());
        out.write(reinterpret_cast<const char*>(&count), sizeof(count));
        if (count > 0) {
            out.write(reinterpret_cast<const char*>(pool.data()),
                      static_cast<std::streamsize>(count * sizeof(T)));
        }
    }

    // Read a vector of trivially-copyable T from a binary file.
    // Returns true if the file existed and was read successfully.
    template<typename T>
    static bool read_pool(const std::filesystem::path& path, std::vector<T>& pool) {
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) return false;
        uint64_t count = 0;
        in.read(reinterpret_cast<char*>(&count), sizeof(count));
        if (!in) return false;
        pool.resize(static_cast<size_t>(count));
        if (count > 0) {
            in.read(reinterpret_cast<char*>(pool.data()),
                    static_cast<std::streamsize>(count * sizeof(T)));
        }
        return in.good() || in.eof();
    }

    // Load all three pools from base_dir_.  Returns true if all three files
    // were found and loaded; returns false if any file is missing (caller
    // should re-initialise with sentinel slot 0 in that case).
    bool load_pools() {
        std::vector<persistent_json_value> vp;
        std::vector<json_array_slab>       ap;
        std::vector<json_object_slab>      op;
        if (!read_pool(base_dir_ / "values.bin",  vp)) return false;
        if (!read_pool(base_dir_ / "arrays.bin",  ap)) return false;
        if (!read_pool(base_dir_ / "objects.bin", op)) return false;
        value_pool_  = std::move(vp);
        array_pool_  = std::move(ap);
        object_pool_ = std::move(op);
        return true;
    }

    // Save all three pools to base_dir_.
    void save_pools() const {
        write_pool(base_dir_ / "values.bin",  value_pool_);
        write_pool(base_dir_ / "arrays.bin",  array_pool_);
        write_pool(base_dir_ / "objects.bin", object_pool_);
    }

    // ---- internal allocation helpers ----

    uint32_t alloc_node(const persistent_json_value& val) {
        uint32_t id = static_cast<uint32_t>(value_pool_.size());
        value_pool_.push_back(val);  // ~65 KB copy — val already lives on heap or caller's frame
        return id;
    }

    uint32_t alloc_array_slab() {
        uint32_t id = static_cast<uint32_t>(array_pool_.size());
        array_pool_.emplace_back();  // construct json_array_slab directly in heap (avoids stack temp)
        return id;
    }

    uint32_t alloc_object_slab() {
        uint32_t id = static_cast<uint32_t>(object_pool_.size());
        object_pool_.emplace_back();  // construct json_object_slab (~1 MB) directly in heap
        return id;
    }

    // ---- recursive import ----

    uint32_t import_node(const nlohmann::json& doc) {
        switch (doc.type()) {
            case nlohmann::json::value_t::null:
                return alloc_node(persistent_json_value::make_null());

            case nlohmann::json::value_t::boolean:
                return alloc_node(persistent_json_value::make_bool(doc.get<bool>()));

            case nlohmann::json::value_t::number_integer:
            case nlohmann::json::value_t::number_unsigned:
                return alloc_node(persistent_json_value::make_int(doc.get<int64_t>()));

            case nlohmann::json::value_t::number_float:
                return alloc_node(persistent_json_value::make_float(doc.get<double>()));

            case nlohmann::json::value_t::string:
                return alloc_node(persistent_json_value::make_string(doc.get<std::string>()));

            case nlohmann::json::value_t::array: {
                uint32_t slab_id  = alloc_array_slab();
                uint32_t node_id  = alloc_node(persistent_json_value::make_array(slab_id));
                uint32_t cur_slab = slab_id;

                for (const auto& elem : doc) {
                    uint32_t elem_id = import_node(elem);
                    if (!array_pool_[cur_slab].push_back(elem_id)) {
                        // Current slab is full — allocate a new one and chain it.
                        uint32_t next_slab = alloc_array_slab();
                        array_pool_[cur_slab].next_slab_id = next_slab;
                        cur_slab = next_slab;
                        array_pool_[cur_slab].push_back(elem_id);
                    }
                }
                return node_id;
            }

            case nlohmann::json::value_t::object: {
                uint32_t slab_id  = alloc_object_slab();
                uint32_t node_id  = alloc_node(persistent_json_value::make_object(slab_id));
                uint32_t cur_slab = slab_id;

                for (auto it = doc.begin(); it != doc.end(); ++it) {
                    uint32_t val_id = import_node(it.value());
                    if (!object_pool_[cur_slab].insert_or_assign(it.key(), val_id)) {
                        // Current slab is full — allocate a new one and chain it.
                        uint32_t next_slab = alloc_object_slab();
                        object_pool_[cur_slab].next_node.set_addr(next_slab);  // Task 3.2.2: was next_node_id
                        cur_slab = next_slab;
                        object_pool_[cur_slab].insert_or_assign(it.key(), val_id);
                    }
                }
                return node_id;
            }

            default:
                // Unknown type — store as null.
                return alloc_node(persistent_json_value::make_null());
        }
    }

    // ---- recursive export ----

    nlohmann::json export_node(uint32_t id) const {
        if (id == 0 || id >= value_pool_.size()) {
            return nlohmann::json(nullptr);
        }
        const persistent_json_value& node = value_pool_[id];

        switch (node.type) {
            case json_type::null:
                return nlohmann::json(nullptr);

            case json_type::boolean:
                return nlohmann::json(node.get_bool());

            case json_type::number_int:
                return nlohmann::json(node.get_int());

            case json_type::number_float:
                return nlohmann::json(node.get_float());

            case json_type::string:
                return nlohmann::json(node.get_string().to_std_string());

            case json_type::array: {
                nlohmann::json arr = nlohmann::json::array();
                uint32_t slab_id = node.get_array_id();
                while (slab_id != 0 && slab_id < array_pool_.size()) {
                    const json_array_slab& slab = array_pool_[slab_id];
                    for (uint32_t i = 0; i < slab.size; ++i) {
                        arr.push_back(export_node(slab[i]));
                    }
                    slab_id = slab.next_slab_id;
                }
                return arr;
            }

            case json_type::object: {
                nlohmann::json obj = nlohmann::json::object();
                uint32_t slab_id = node.get_object_id();
                while (slab_id != 0 && slab_id < object_pool_.size()) {
                    const json_object_slab& slab = object_pool_[slab_id];
                    for (uint32_t i = 0; i < slab.size; ++i) {
                        const auto& entry = slab[i];
                        obj[entry.key.to_std_string()] = export_node(entry.value);
                    }
                    slab_id = slab.next_node.addr();  // Task 3.2.2: was next_node_id
                }
                return obj;
            }

            default:
                return nlohmann::json(nullptr);
        }
    }
};

} // namespace jgit
