#pragma once

#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <vector>
#include <cstdint>

#include <nlohmann/json.hpp>
#include "hash.h"
#include "serialization.h"

namespace jgit {

/**
 * Content-addressed object store for JSON documents.
 *
 * Objects are serialized to CBOR format, hashed with SHA-256, and stored under
 * a Git-like directory layout:
 *
 *   <root>/.jgit/objects/<2-char-dir>/<62-char-filename>
 *
 * The store is immutable and idempotent: storing the same content twice produces
 * the same ObjectId and does not overwrite the existing file.
 */
class ObjectStore {
public:
    /**
     * Open an existing jgit object store rooted at `root_path`.
     * The `.jgit/` subdirectory must already exist (call `init()` to create it).
     */
    explicit ObjectStore(const std::filesystem::path& root_path)
        : root_(root_path / ".jgit")
    {
    }

    /**
     * Serialize `object` to CBOR, compute its SHA-256, write to disk (if not
     * already present), and return the resulting ObjectId.
     */
    ObjectId put(const nlohmann::json& object) {
        auto bytes = to_bytes(object);
        ObjectId id = hash_object(bytes);

        std::filesystem::path obj_path = object_path(id);
        if (!std::filesystem::exists(obj_path)) {
            std::filesystem::create_directories(obj_path.parent_path());
            std::ofstream out(obj_path, std::ios::binary);
            if (!out) {
                throw std::runtime_error("ObjectStore::put: cannot open file for writing: " + obj_path.string());
            }
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        }

        return id;
    }

    /**
     * Retrieve a JSON object by its ObjectId.
     * Returns std::nullopt if the object does not exist in the store.
     */
    std::optional<nlohmann::json> get(const ObjectId& id) const {
        std::filesystem::path obj_path = object_path(id);
        if (!std::filesystem::exists(obj_path)) {
            return std::nullopt;
        }

        std::ifstream in(obj_path, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }

        std::vector<uint8_t> bytes(
            (std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>()
        );

        return from_bytes(bytes);
    }

    /**
     * Check whether an object with the given ObjectId exists in the store.
     */
    bool exists(const ObjectId& id) const {
        return std::filesystem::exists(object_path(id));
    }

    /**
     * Initialize a new empty jgit repository at `path`.
     * Creates the following structure:
     *
     *   path/.jgit/
     *   path/.jgit/objects/
     *   path/.jgit/refs/heads/
     *   path/.jgit/HEAD
     *
     * Returns an ObjectStore opened at `path`.
     */
    static ObjectStore init(const std::filesystem::path& path) {
        std::filesystem::path jgit_dir = path / ".jgit";
        std::filesystem::create_directories(jgit_dir / "objects");
        std::filesystem::create_directories(jgit_dir / "refs" / "heads");

        std::filesystem::path head_path = jgit_dir / "HEAD";
        if (!std::filesystem::exists(head_path)) {
            std::ofstream head(head_path);
            if (!head) {
                throw std::runtime_error("ObjectStore::init: cannot create HEAD file");
            }
            head << "ref: refs/heads/main\n";
        }

        return ObjectStore(path);
    }

private:
    std::filesystem::path root_;

    std::filesystem::path object_path(const ObjectId& id) const {
        return root_ / "objects" / id.dir() / id.file();
    }
};

} // namespace jgit
