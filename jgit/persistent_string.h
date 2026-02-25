#pragma once

#include <cstdint>
#include <cstring>
#include <cassert>
#include <string>
#include <iterator>

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
//
// Task 3.3.2 — Extended to satisfy the nlohmann::basic_json StringType concept:
//   Added: value_type, npos, fill constructor, operator[], push_back, append,
//          operator+=, begin/end iterators, find, find_first_of, substr.
//   These are all ordinary member functions; the special member functions
//   remain trivial/defaulted, so std::is_trivially_copyable is preserved.
// =============================================================================

namespace jgit {

struct persistent_string {
    // ---- StringType interface types (required by nlohmann::basic_json) ----

    // Character type — must be exposed as value_type for StringType concept.
    using value_type = char;

    // "Not found" sentinel, mirroring std::string::npos.
    static const size_t npos = static_cast<size_t>(-1);

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

    // Construct from C string (implicit — required for StringType compatibility,
    // so that string literals like "hello" can be used directly as keys/values
    // in expressions like j["key"] = "value" when string_t = persistent_string).
    persistent_string(const char* s) noexcept {  // NOLINT(google-explicit-constructor)
        sso_buf[0] = '\0';
        is_long    = false;
        long_buf[0]= '\0';
        if (s) assign(s);
    }

    // Construct from std::string (implicit — required for interoperability with
    // code that constructs string_t from std::string, e.g. nlohmann internals).
    persistent_string(const std::string& s) noexcept {  // NOLINT(google-explicit-constructor)
        sso_buf[0] = '\0';
        is_long    = false;
        long_buf[0]= '\0';
        assign(s.c_str(), s.size());
    }

    // Fill constructor: repeat character c, count times.
    // Required by nlohmann::basic_json StringType for indent/padding.
    // If count exceeds the buffer capacity, it is clamped to LONG_BUF_SIZE.
    persistent_string(size_t count, char c) noexcept {
        sso_buf[0]  = '\0';
        long_buf[0] = '\0';
        is_long     = false;
        if (count == 0) return;
        size_t actual = (count <= LONG_BUF_SIZE) ? count : LONG_BUF_SIZE;
        if (actual <= SSO_SIZE) {
            std::memset(sso_buf, c, actual);
            sso_buf[actual] = '\0';
        } else {
            is_long = true;
            std::memset(long_buf, c, actual);
            long_buf[actual] = '\0';
        }
    }

    // Construct from a range [first, last) of characters.
    // Required by nlohmann::basic_json StringType for iterator-based construction.
    template<typename InputIt>
    persistent_string(InputIt first, InputIt last) noexcept {
        sso_buf[0]  = '\0';
        long_buf[0] = '\0';
        is_long     = false;
        size_t i = 0;
        for (auto it = first; it != last; ++it, ++i) {
            if (i < SSO_SIZE) {
                sso_buf[i] = static_cast<char>(*it);
            } else if (i == SSO_SIZE) {
                // Transition: copy SSO content to long_buf
                is_long = true;
                std::memcpy(long_buf, sso_buf, SSO_SIZE);
                sso_buf[0] = '\0';
                long_buf[SSO_SIZE] = static_cast<char>(*it);
            } else if (i < LONG_BUF_SIZE) {
                long_buf[i] = static_cast<char>(*it);
            } else {
                break;  // clamped
            }
        }
        // NUL-terminate
        if (!is_long) {
            sso_buf[i < SSO_SIZE ? i : SSO_SIZE] = '\0';
        } else {
            long_buf[i <= LONG_BUF_SIZE ? i : LONG_BUF_SIZE] = '\0';
        }
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

    // ---- mutable access (non-const c_str ptr) ----

    char* data() noexcept {
        return is_long ? long_buf : sso_buf;
    }

    const char* data() const noexcept {
        return c_str();
    }

    // ---- random access (required by nlohmann::basic_json for serialisation) ----

    char& operator[](size_t idx) noexcept {
        return data()[idx];
    }

    const char& operator[](size_t idx) const noexcept {
        return c_str()[idx];
    }

    // ---- iterators (required by nlohmann::basic_json StringType) ----

    char* begin() noexcept { return data(); }
    const char* begin() const noexcept { return c_str(); }
    char* end()   noexcept { return data() + size(); }
    const char* end() const noexcept { return c_str() + size(); }

    // ---- modifiers ----

    // Append a single character.
    // If the current string is at LONG_BUF_SIZE capacity, the call is a no-op.
    persistent_string& push_back(char c) noexcept {
        size_t current = size();
        if (current >= LONG_BUF_SIZE) return *this;  // capacity limit
        if (current < SSO_SIZE && !is_long) {
            sso_buf[current]     = c;
            sso_buf[current + 1] = '\0';
        } else {
            if (!is_long) {
                // Promote from SSO to long_buf
                is_long = true;
                std::memcpy(long_buf, sso_buf, current);
                sso_buf[0] = '\0';
            }
            long_buf[current]     = c;
            long_buf[current + 1] = '\0';
        }
        return *this;
    }

    // Append from a buffer of n characters.
    persistent_string& append(const char* s, size_t n) noexcept {
        if (!s || n == 0) return *this;
        size_t current = size();
        size_t new_len = current + n;
        if (new_len > LONG_BUF_SIZE) n = LONG_BUF_SIZE - current;  // clamp
        if (n == 0) return *this;
        new_len = current + n;

        if (new_len <= SSO_SIZE && !is_long) {
            std::memcpy(sso_buf + current, s, n);
            sso_buf[new_len] = '\0';
        } else {
            if (!is_long) {
                // Promote SSO content to long_buf
                is_long = true;
                std::memcpy(long_buf, sso_buf, current);
                sso_buf[0] = '\0';
            }
            std::memcpy(long_buf + current, s, n);
            long_buf[new_len] = '\0';
        }
        return *this;
    }

    // Append another persistent_string.
    persistent_string& append(const persistent_string& other) noexcept {
        return append(other.c_str(), other.size());
    }

    // Append a std::string.
    persistent_string& append(const std::string& s) noexcept {
        return append(s.c_str(), s.size());
    }

    // Range-based append [first, last).
    template<typename InputIt>
    persistent_string& append(InputIt first, InputIt last) noexcept {
        for (auto it = first; it != last; ++it) {
            push_back(static_cast<char>(*it));
        }
        return *this;
    }

    // operator+= overloads
    persistent_string& operator+=(const persistent_string& other) noexcept {
        return append(other);
    }

    persistent_string& operator+=(char c) noexcept {
        return push_back(c);
    }

    persistent_string& operator+=(const char* s) noexcept {
        return append(s, s ? std::strlen(s) : 0);
    }

    // ---- search (required by nlohmann::basic_json JSON pointer operations) ----

    // Find first occurrence of character c starting from pos.
    // Returns npos if not found.
    size_t find(char c, size_t pos = 0) const noexcept {
        const char* buf = c_str();
        size_t len = size();
        for (size_t i = pos; i < len; ++i) {
            if (buf[i] == c) return i;
        }
        return npos;
    }

    // Find first occurrence of any character in s starting from pos.
    size_t find_first_of(const char* s, size_t pos = 0) const noexcept {
        if (!s) return npos;
        const char* buf = c_str();
        size_t len = size();
        for (size_t i = pos; i < len; ++i) {
            if (std::strchr(s, buf[i])) return i;
        }
        return npos;
    }

    // find_first_of with a single character (required by nlohmann json_pointer
    // internals which call find_first_of(value_type, size_t)).
    size_t find_first_of(char c, size_t pos = 0) const noexcept {
        return find(c, pos);
    }

    size_t find_first_of(const persistent_string& s, size_t pos = 0) const noexcept {
        return find_first_of(s.c_str(), pos);
    }

    // Find first occurrence of character c (as a string), matching std::string API.
    size_t find(const char* s, size_t pos = 0) const noexcept {
        if (!s || s[0] == '\0') return npos;
        if (s[1] == '\0') return find(s[0], pos);  // single char
        const char* buf = c_str();
        size_t len  = size();
        size_t slen = std::strlen(s);
        if (slen > len) return npos;
        for (size_t i = pos; i + slen <= len; ++i) {
            if (std::strncmp(buf + i, s, slen) == 0) return i;
        }
        return npos;
    }

    // Find substring (persistent_string overload).
    size_t find(const persistent_string& s, size_t pos = 0) const noexcept {
        return find(s.c_str(), pos);
    }

    // Find substring (std::string overload).
    size_t find(const std::string& s, size_t pos = 0) const noexcept {
        return find(s.c_str(), pos);
    }

    // ---- substring (required by nlohmann::basic_json JSON pointer) ----

    // Return a new persistent_string with characters [pos, pos+len).
    // If len == npos, returns the suffix starting at pos.
    persistent_string substr(size_t pos, size_t len = npos) const noexcept {
        const char* buf = c_str();
        size_t current = size();
        if (pos >= current) return persistent_string{};
        size_t avail = current - pos;
        size_t actual = (len == npos || len > avail) ? avail : len;
        persistent_string result{};
        result.assign(buf + pos, actual);
        return result;
    }

    // ---- capacity (extra std::string-compatible) ----

    size_t length() const noexcept { return size(); }

    size_t max_size() const noexcept { return LONG_BUF_SIZE; }

    size_t capacity() const noexcept { return LONG_BUF_SIZE; }

    // front/back for character access (required by nlohmann::basic_json serialiser)
    char& front() noexcept { return data()[0]; }
    const char& front() const noexcept { return c_str()[0]; }

    char& back() noexcept {
        size_t n = size();
        return data()[n > 0 ? n - 1 : 0];
    }
    const char& back() const noexcept {
        size_t n = size();
        return c_str()[n > 0 ? n - 1 : 0];
    }

    // pop_back (for completeness — used in some JSON pointer operations)
    void pop_back() noexcept {
        size_t n = size();
        if (n == 0) return;
        if (!is_long) {
            sso_buf[n - 1] = '\0';
        } else {
            long_buf[n - 1] = '\0';
            if (n - 1 <= SSO_SIZE) {
                // Demote back to SSO
                is_long = false;
                std::memcpy(sso_buf, long_buf, n - 1);
                sso_buf[n - 1] = '\0';
                long_buf[0] = '\0';
            }
        }
    }

    void clear() noexcept {
        is_long     = false;
        sso_buf[0]  = '\0';
        long_buf[0] = '\0';
    }

    void resize(size_t new_size, char fill = '\0') noexcept {
        size_t current = size();
        if (new_size <= current) {
            // Truncate
            if (!is_long) {
                sso_buf[new_size] = '\0';
            } else {
                long_buf[new_size] = '\0';
            }
        } else {
            // Extend with fill character
            size_t add = new_size - current;
            append(std::string(add, fill).c_str(), add);
        }
    }

    // reserve is a no-op (fixed-size buffer) but required by some nlohmann internals.
    void reserve(size_t /*new_cap*/) noexcept {}

    // ---- replace (used in JSON pointer escape/unescape) ----

    // Replace count characters starting at pos with the string s.
    // Matches the minimal std::string::replace(pos, count, s) signature used by
    // nlohmann::json_pointer.
    persistent_string& replace(size_t pos, size_t count, const char* s) noexcept {
        const char* orig = c_str();
        size_t len = size();
        if (pos > len) pos = len;
        if (count > len - pos) count = len - pos;
        size_t slen = s ? std::strlen(s) : 0;

        // Build result: orig[0..pos) + s + orig[pos+count..len)
        persistent_string result{};
        result.assign(orig, pos);
        if (s && slen > 0) result.append(s, slen);
        result.append(orig + pos + count, len - pos - count);
        *this = result;
        return *this;
    }

    persistent_string& replace(size_t pos, size_t count, const persistent_string& s) noexcept {
        return replace(pos, count, s.c_str());
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

    // ---- additional comparison operators ----

    bool operator<=(const persistent_string& other) const noexcept {
        return !(other < *this);
    }

    bool operator>(const persistent_string& other) const noexcept {
        return other < *this;
    }

    bool operator>=(const persistent_string& other) const noexcept {
        return !(*this < other);
    }

    bool operator!=(const char* s) const noexcept {
        return !(*this == s);
    }

    bool operator!=(const std::string& s) const noexcept {
        return !(*this == s);
    }

    // ---- conversion ----

    // Implicit conversion to std::string.
    // Required by nlohmann::basic_json to satisfy is_assignable<string_t&, string_t>
    // when string_t = persistent_string.
    operator std::string() const {
        return std::string(c_str());
    }

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
// Note: adding ordinary member functions does NOT affect trivial copyability.
// Only special member functions (copy ctor, copy assign, destructor) matter.
static_assert(std::is_trivially_copyable<persistent_string>::value,
              "jgit::persistent_string must be trivially copyable for use with persist<T>");

// Cross-type comparison operators between jgit::persistent_string and
// std::string.  Required when nlohmann::basic_json uses transparent comparison
// (std::less<void>) in its std::map — e.g. when looking up by std::string key.
//
// Note: equality operators are intentionally NOT provided as free functions
// here to avoid ambiguity with the member operator==(const std::string&) and
// the implicit operator std::string() conversion.
// The member operators (persistent_string::operator==(const std::string&))
// handle comparisons in both directions via implicit conversions.
inline bool operator<(const std::string& a, const jgit::persistent_string& b) noexcept {
    return std::strcmp(a.c_str(), b.c_str()) < 0;
}
inline bool operator<(const jgit::persistent_string& a, const std::string& b) noexcept {
    return std::strcmp(a.c_str(), b.c_str()) < 0;
}

// Cross-type comparison with const char*.
inline bool operator<(const char* a, const jgit::persistent_string& b) noexcept {
    return std::strcmp(a ? a : "", b.c_str()) < 0;
}

// Free operator+ for string concatenation (used by nlohmann::basic_json).
inline persistent_string operator+(const persistent_string& a, const persistent_string& b) noexcept {
    persistent_string result = a;
    result.append(b);
    return result;
}

inline persistent_string operator+(const persistent_string& a, char c) noexcept {
    persistent_string result = a;
    result.push_back(c);
    return result;
}

inline persistent_string operator+(char c, const persistent_string& b) noexcept {
    persistent_string result{};
    result.push_back(c);
    result.append(b);
    return result;
}

inline persistent_string operator+(const persistent_string& a, const char* b) noexcept {
    persistent_string result = a;
    if (b) result.append(b, std::strlen(b));
    return result;
}

inline persistent_string operator+(const char* a, const persistent_string& b) noexcept {
    persistent_string result{};
    if (a) result.assign(a);
    result.append(b);
    return result;
}

} // namespace jgit

// std::hash specialisation for jgit::persistent_string.
// Required by nlohmann::basic_json when used with unordered_map ObjectType.
namespace std {
template<>
struct hash<jgit::persistent_string> {
    size_t operator()(const jgit::persistent_string& s) const noexcept {
        // FNV-1a hash over the string bytes.
        size_t hash = 14695981039346656037ULL;
        const char* buf = s.c_str();
        while (*buf) {
            hash ^= static_cast<size_t>(static_cast<unsigned char>(*buf++));
            hash *= 1099511628211ULL;
        }
        return hash;
    }
};
} // namespace std

// Stream output operator — required for printing jgit::persistent_string and
// for using it as a dump() return type with std::cout.
#include <ostream>
inline std::ostream& operator<<(std::ostream& os, const jgit::persistent_string& s) {
    return os << s.c_str();
}
