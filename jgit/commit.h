#pragma once

#include <string>
#include <cstdint>
#include <stdexcept>

#include <nlohmann/json.hpp>
#include "hash.h"
#include "serialization.h"

namespace jgit {

/**
 * A jgit commit — the immutable record of one version of a JSON document.
 *
 * Fields mirror a Git commit:
 *   root_snapshot  ← tree      (content-addressed CBOR blob of the JSON document)
 *   parent_id      ← parent    (ObjectId of parent commit; empty hex = root commit)
 *   author         ← author    (free-form string)
 *   timestamp      ← author date (Unix seconds)
 *   message        ← commit message
 *
 * The commit itself is serialised to JSON and stored in the ObjectStore as a
 * content-addressed blob.  Its ObjectId (SHA-256 of the serialised form) is
 * stored in the `id` field after deserialisation, but is NOT part of the
 * serialised JSON (to avoid a circular hash dependency).
 */
struct Commit {
    ObjectId    id;              // SHA-256 of serialised commit JSON (set after store/load)
    ObjectId    root_snapshot;   // ObjectId of the CBOR blob of the JSON document content
    ObjectId    parent_id;       // Parent commit ObjectId; hex == "" means root commit
    std::string author;          // Free-form author string
    int64_t     timestamp;       // Unix timestamp (seconds since epoch)
    std::string message;         // Commit message

    /** True if this is the first commit (no parent). */
    bool is_root() const noexcept { return parent_id.hex.empty(); }

    /**
     * Serialise this commit to a nlohmann::json object suitable for storage in
     * an ObjectStore.  The `id` field is excluded (it is the hash of this JSON).
     */
    nlohmann::json to_json() const {
        nlohmann::json j;
        j["root_snapshot"] = root_snapshot.hex;
        j["parent_id"]     = parent_id.hex;   // "" for root commit
        j["author"]        = author;
        j["timestamp"]     = timestamp;
        j["message"]       = message;
        return j;
    }

    /**
     * Deserialise a Commit from a nlohmann::json object that was previously
     * stored in an ObjectStore.
     *
     * @param j    The JSON object as returned by ObjectStore::get().
     * @param id   The ObjectId under which this commit was stored.
     */
    static Commit from_json(const nlohmann::json& j, const ObjectId& id) {
        Commit c;
        c.id            = id;
        c.root_snapshot = ObjectId{ j.at("root_snapshot").get<std::string>() };
        c.parent_id     = ObjectId{ j.at("parent_id").get<std::string>() };
        c.author        = j.at("author").get<std::string>();
        c.timestamp     = j.at("timestamp").get<int64_t>();
        c.message       = j.at("message").get<std::string>();
        return c;
    }
};

} // namespace jgit
