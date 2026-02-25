#pragma once

#include <cstdint>
#include <cstring>
#include <cassert>
#include <string>

// =============================================================================
// Task 2.2.1 — jgit::persistent_string
//
// Drop-in persistent replacement for std::string.
//
// Design:
//   - Short String Optimization (SSO): strings up to SSO_SIZE chars are stored
//     inline in sso_buf[], with no heap allocation.
//   - Long strings: the content is stored separately (e.g. in ObjectStore), and
//     only a fixed-size SHA-256 ObjectId (64-char hex string) is kept here.
//     For Task 2.2 we store long strings in a second fixed buffer so that the
//     entire struct remains trivially copyable and compatible with persist<T>.
//
// Key property: sizeof(persistent_string) is a compile-time constant with NO
// heap-allocated members, making it fully compatible with persist<T>.
// =============================================================================

namespace jgit {

struct persistent_string {
    // Short String Optimization threshold (characters, not including NUL).
    static constexpr size_t SSO_SIZE = 23;

    // Maximum length for a "long" string stored inline in long_buf[].
    // 65535 bytes covers virtually all real-world JSON string values while
    // keeping the struct fixed-size and compatible with persist<T>.
    static constexpr size_t LONG_BUF_SIZE = 65535;

    // ---- data members (all fixed-size, no pointers) ----

    // When is_long == false: the string is stored NUL-terminated in sso_buf[].
    // When is_long == true:  sso_buf is unused; the string is in long_buf[].
    char   sso_buf[SSO_SIZE + 1];  // +1 for NUL terminator
    bool   is_long;                // true  → data is in long_buf[]
                                   // false → data is in sso_buf[]
    // Storage for strings longer than SSO_SIZE.
    // Fixed-size so the struct is trivially copyable with no heap allocation.
    char   long_buf[LONG_BUF_SIZE + 1];  // +1 for NUL terminator

    // ---- constructors ----

    // Default constructor: empty string
    persistent_string() noexcept {
        sso_buf[0] = '\0';
        is_long    = false;
        long_buf[0]= '\0';
    }

    // Construct from C string
    explicit persistent_string(const char* s) noexcept {
        assign(s);
    }

    // Construct from std::string
    explicit persistent_string(const std::string& s) noexcept {
        assign(s.c_str(), s.size());
    }

    // ---- assignment ----

    void assign(const char* s) noexcept {
        assign(s, s ? std::strlen(s) : 0);
    }

    void assign(const char* s, size_t len) noexcept {
        long_buf[0] = '\0';
        if (len <= SSO_SIZE) {
            is_long = false;
            if (s && len > 0) std::memcpy(sso_buf, s, len);
            sso_buf[len] = '\0';
        } else {
            is_long = true;
            sso_buf[0] = '\0';
            // Clamp to LONG_BUF_SIZE if the string is unreasonably large.
            size_t copy_len = (len < LONG_BUF_SIZE) ? len : LONG_BUF_SIZE;
            if (s && copy_len > 0) std::memcpy(long_buf, s, copy_len);
            long_buf[copy_len] = '\0';
        }
    }

    persistent_string& operator=(const char* s) noexcept {
        assign(s);
        return *this;
    }

    persistent_string& operator=(const std::string& s) noexcept {
        assign(s.c_str(), s.size());
        return *this;
    }

    persistent_string& operator=(const persistent_string& other) noexcept = default;

    // ---- accessors ----

    // Returns a pointer to the NUL-terminated string data.
    const char* c_str() const noexcept {
        return is_long ? long_buf : sso_buf;
    }

    // Returns the length of the stored string (excluding NUL).
    size_t size() const noexcept {
        return std::strlen(c_str());
    }

    bool empty() const noexcept {
        return c_str()[0] == '\0';
    }

    // ---- comparison ----

    bool operator==(const persistent_string& other) const noexcept {
        return std::strcmp(c_str(), other.c_str()) == 0;
    }

    bool operator!=(const persistent_string& other) const noexcept {
        return !(*this == other);
    }

    bool operator<(const persistent_string& other) const noexcept {
        return std::strcmp(c_str(), other.c_str()) < 0;
    }

    bool operator==(const char* s) const noexcept {
        return std::strcmp(c_str(), s ? s : "") == 0;
    }

    bool operator==(const std::string& s) const noexcept {
        return std::strcmp(c_str(), s.c_str()) == 0;
    }

    // ---- conversion ----

    std::string to_std_string() const {
        return std::string(c_str());
    }
};

// Verify layout: the struct must have exactly the expected size (no padding surprises).
static_assert(
    persistent_string::SSO_SIZE + 1 + 1 + persistent_string::LONG_BUF_SIZE + 1
        == sizeof(persistent_string),
    "jgit::persistent_string layout mismatch — unexpected padding");

// Verify that the struct is trivially copyable (required for persist<T>).
static_assert(std::is_trivially_copyable<persistent_string>::value,
              "jgit::persistent_string must be trivially copyable for use with persist<T>");

} // namespace jgit
