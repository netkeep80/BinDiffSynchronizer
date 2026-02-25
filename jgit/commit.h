#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <nlohmann/json.hpp>
#include "hash.h"

namespace jgit {

/**
 * A jgit commit object.
 *
 * Analogous to a Git commit, it captures a snapshot of the root JSON document
 * at a point in time, together with authorship metadata and a link to its
 * parent commit(s).
 *
 * The commit is itself stored in the object store as a JSON value, making it
 * content-addressed like every other object.
 *
 * JSON representation (CBOR-serialised when stored):
 * {
 *   "type":        "commit",
 *   "root":        "<64-char hex ObjectId of the root JSON blob>",
 *   "parent":      "<64-char hex ObjectId of parent commit>" | null,
 *   "author":      "<author name or identifier>",
 *   "timestamp":   <unix epoch seconds as integer>,
 *   "message":     "<commit message>"
 * }
 */
struct Commit {
    ObjectId root;               ///< ObjectId of the committed JSON value
    std::optional<ObjectId> parent; ///< Parent commit, or nullopt for the root commit
    std::string author;
    int64_t     timestamp;       ///< Unix timestamp (seconds since epoch)
    std::string message;

    /** Serialize this commit to a nlohmann::json value. */
    nlohmann::json to_json() const {
        nlohmann::json j;
        j["type"]      = "commit";
        j["root"]      = root.hex;
        j["parent"]    = parent.has_value() ? nlohmann::json(parent->hex) : nlohmann::json(nullptr);
        j["author"]    = author;
        j["timestamp"] = timestamp;
        j["message"]   = message;
        return j;
    }

    /** Deserialize a commit from a nlohmann::json value.
     *  @throws std::invalid_argument if the JSON does not have the expected shape. */
    static Commit from_json(const nlohmann::json& j) {
        if (!j.contains("type") || j["type"] != "commit") {
            throw std::invalid_argument("Commit::from_json: not a commit object");
        }
        Commit c;
        c.root      = ObjectId{ j.at("root").get<std::string>() };
        c.author    = j.at("author").get<std::string>();
        c.timestamp = j.at("timestamp").get<int64_t>();
        c.message   = j.at("message").get<std::string>();

        const auto& p = j.at("parent");
        if (p.is_null()) {
            c.parent = std::nullopt;
        } else {
            c.parent = ObjectId{ p.get<std::string>() };
        }
        return c;
    }
};

} // namespace jgit
