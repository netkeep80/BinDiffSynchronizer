#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "sha256.hpp"

namespace jgit {

/**
 * A 256-bit content identifier represented as a 64-character lowercase hex string.
 * Follows Git's object naming convention: first 2 characters are the directory name,
 * the remaining 62 characters are the file name.
 */
struct ObjectId {
    std::string hex;  // 64-character lowercase hex string

    /** First 2 hex characters — used as subdirectory name under objects/. */
    std::string dir() const { return hex.substr(0, 2); }

    /** Remaining 62 hex characters — used as file name within the subdirectory. */
    std::string file() const { return hex.substr(2); }

    bool operator==(const ObjectId& other) const { return hex == other.hex; }
    bool operator!=(const ObjectId& other) const { return hex != other.hex; }
    bool operator<(const ObjectId& other) const  { return hex <  other.hex; }
};

/**
 * Compute SHA-256 of the given bytes and return the hex-encoded ObjectId.
 */
inline ObjectId hash_object(const std::vector<uint8_t>& data) {
    return ObjectId{ sha256_hex(data) };
}

} // namespace jgit
