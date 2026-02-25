#pragma once

#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace jgit {

/**
 * Serialize a JSON value to CBOR (RFC 7049) binary format.
 *
 * CBOR is chosen as the primary binary format because it:
 * - Is self-describing (no schema required)
 * - Supports all JSON types natively
 * - Is defined by an RFC standard
 * - Is natively supported by nlohmann/json
 */
inline std::vector<uint8_t> to_bytes(const nlohmann::json& doc) {
    return nlohmann::json::to_cbor(doc);
}

/**
 * Deserialize a JSON value from CBOR binary format.
 *
 * @throws nlohmann::json::parse_error if the data is not valid CBOR.
 */
inline nlohmann::json from_bytes(const std::vector<uint8_t>& data) {
    return nlohmann::json::from_cbor(data);
}

} // namespace jgit
