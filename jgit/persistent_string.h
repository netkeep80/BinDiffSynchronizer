#pragma once

#include <cstdint>
#include <cstring>
#include <cassert>
#include <string>
#include <iterator>

#include "persist.h"

// =============================================================================
// Task 2.2.1 — jgit::persistent_string
//
// Drop-in persistent replacement for std::string.
//
// Design:
//   - Short String Optimization (SSO): strings up to SSO_SIZE chars are stored
//     inline in sso_buf[], with no heap allocation.
//   - Long strings: the content is stored in dynamic PERSISTENT memory via
//     fptr<char>, which points to a char array managed by AddressManager<char>.
//     This replaces the previous 65 KB static long_buf[].
//
// Key property: sizeof(persistent_string) is a compile-time constant with NO
// regular (non-persistent) heap-allocated members, making it fully compatible
// with persist<T>.  The fptr<char> long_ptr member stores only an address
// index (unsigned) into AddressManager<char>, which is also trivially copyable.
//
// IMPORTANT MEMORY MANAGEMENT CONSTRAINT (Task 3.4):
//   - The constructor and destructor of persistent_string only LOAD/SAVE state.
//     They do NOT allocate or free persistent memory.
//   - Allocation of persistent long-string storage is performed explicitly via
//     alloc_long(n) or assign_persistent(s, n).
//   - Deallocation is performed explicitly via free_long().
//   - This is consistent with the jgit persistent-memory design principle:
//     constructor/destructor = load/save; allocation/deallocation = explicit.
//
// Task 3.3.2 — Extended to satisfy the nlohmann::basic_json StringType concept:
//   Added: value_type, npos, fill constructor, operator[], push_back, append,
//          operator+=, begin/end iterators, find, find_first_of, substr.
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

    // Maximum number of characters for a long string stored in persistent memory.
    // AddressManager<char> supports up to ADDRESS_SPACE slots (default 1024).
    // Each slot can hold an array of up to MAX_LONG_SIZE chars.
    static constexpr size_t MAX_LONG_SIZE = 65535;

    // ---- data members (all fixed-size, no regular heap pointers) ----

    // When is_long == false: the string is stored NUL-terminated in sso_buf[].
    // When is_long == true:  sso_buf is unused; the string is in persistent
    //                        memory pointed to by long_ptr.
    char   sso_buf[SSO_SIZE + 1];  // +1 for NUL terminator
    bool   is_long;                // true  → data is in persistent memory via long_ptr
                                   // false → data is in sso_buf[]
    // Persistent pointer to the char array for long strings.
    // long_ptr.addr() == 0 means no persistent array is allocated.
    // This field is trivially copyable (stores only an unsigned address index).
    fptr<char> long_ptr;

    // Cached length of the long string (number of chars, excluding NUL).
    // Kept in sync whenever the long string content changes.
    unsigned   long_len;

    // ---- constructors ----

    // Default constructor: empty string, no persistent allocation.
    persistent_string() noexcept {
        sso_buf[0] = '\0';
        is_long    = false;
        // long_ptr default-constructs to __addr=0 (null, no allocation)
        long_len   = 0;
    }

    // Construct from C string (implicit — required for StringType compatibility,
    // so that string literals like "hello" can be used directly as keys/values
    // in expressions like j["key"] = "value" when string_t = persistent_string).
    //
    // For short strings (≤ SSO_SIZE): stored inline in sso_buf[], no allocation.
    // For long strings (> SSO_SIZE):  stored in persistent memory via long_ptr.
    //   This constructor performs persistent allocation for long strings.
    persistent_string(const char* s) noexcept {  // NOLINT(google-explicit-constructor)
        sso_buf[0] = '\0';
        is_long    = false;
        long_len   = 0;
        if (s) _assign(s, std::strlen(s));
    }

    // Construct from std::string (implicit — required for interoperability with
    // code that constructs string_t from std::string, e.g. nlohmann internals).
    persistent_string(const std::string& s) noexcept {  // NOLINT(google-explicit-constructor)
        sso_buf[0] = '\0';
        is_long    = false;
        long_len   = 0;
        _assign(s.c_str(), s.size());
    }

    // Fill constructor: repeat character c, count times.
    // Required by nlohmann::basic_json StringType for indent/padding.
    persistent_string(size_t count, char c) noexcept {
        sso_buf[0] = '\0';
        is_long    = false;
        long_len   = 0;
        if (count == 0) return;
        size_t actual = (count <= MAX_LONG_SIZE) ? count : MAX_LONG_SIZE;
        if (actual <= SSO_SIZE) {
            std::memset(sso_buf, c, actual);
            sso_buf[actual] = '\0';
        } else {
            // Allocate persistent memory for the fill string.
            _alloc_long(actual);
            if (long_ptr.addr() != 0) {
                for (size_t i = 0; i < actual; ++i) long_ptr[i] = c;
                long_ptr[actual] = '\0';
                long_len = static_cast<unsigned>(actual);
            }
        }
    }

    // Construct from a range [first, last) of characters.
    // Required by nlohmann::basic_json StringType for iterator-based construction.
    template<typename InputIt>
    persistent_string(InputIt first, InputIt last) noexcept {
        sso_buf[0] = '\0';
        is_long    = false;
        long_len   = 0;
        // Materialise the range into a temporary buffer first.
        // This avoids multiple persistent allocations.
        std::string tmp(first, last);
        _assign(tmp.c_str(), tmp.size());
    }

    // Copy constructor: SHALLOW copy (trivially copyable).
    // For short strings: trivial copy of sso_buf.
    // For long strings: copies the fptr<char> address index (shares the same
    //   AddressManager<char> slot).  Two persistent_string objects referring
    //   to the same slot is valid in the persistent memory model — the slot
    //   is owned by the address space, not by a single string object.
    //   Explicit free_long() must be called only ONCE to release the slot.
    //
    // Defaulted = trivially copyable, required by persist<T> and by
    // persistent_map/persistent_json_value which embed persistent_string.
    persistent_string(const persistent_string& other) noexcept = default;

    // Copy assignment operator: SHALLOW copy (trivially copyable).
    // Same semantics as copy constructor — shares the fptr<char> slot.
    persistent_string& operator=(const persistent_string& other) noexcept = default;

    // Destructor: ONLY marks the persistent string as unloaded (LOAD/SAVE).
    // Does NOT free persistent memory.
    //
    // IMPORTANT: Persistent memory allocated for long strings is NOT freed here.
    // To avoid leaks, callers must call free_long() explicitly before destroying
    // a persistent_string that owns a long allocation.
    //
    // Rationale: consistent with jgit persistent-memory design — constructor/
    // destructor = load/save; persistent allocation/deallocation = explicit.
    ~persistent_string() noexcept = default;

    // ---- explicit persistent-memory management ----

    // alloc_long(n): allocate persistent memory for n characters (+1 for NUL).
    // Sets is_long=true and long_ptr to point to the new allocation.
    // No-op if is_long is already true and enough space is available.
    // NOTE: does NOT copy any content — caller must write via long_ptr[].
    void alloc_long(size_t n) noexcept {
        _alloc_long(n);
    }

    // free_long(): explicitly release the persistent char array.
    // Resets is_long=false and long_ptr to null.
    // This is the explicit persistent-memory deallocation entry point.
    void free_long() noexcept {
        if (is_long && long_ptr.addr() != 0) {
            long_ptr.DeleteArray();
        }
        is_long  = false;
        long_len = 0;
        sso_buf[0] = '\0';
    }

    // assign_persistent(s, n): allocate persistent memory and copy n chars from s.
    // Combines alloc_long + memcpy for convenience.
    void assign_persistent(const char* s, size_t n) noexcept {
        if (is_long && long_ptr.addr() != 0) {
            long_ptr.DeleteArray();
            is_long  = false;
            long_len = 0;
        }
        _assign(s, n);
    }

    // ---- assignment ----

    void assign(const char* s) noexcept {
        assign(s, s ? std::strlen(s) : 0);
    }

    void assign(const char* s, size_t len) noexcept {
        // Free old long allocation if switching.
        if (is_long && long_ptr.addr() != 0) {
            long_ptr.DeleteArray();
            is_long  = false;
            long_len = 0;
        }
        sso_buf[0] = '\0';
        _assign(s, len);
    }

    persistent_string& operator=(const char* s) noexcept {
        assign(s);
        return *this;
    }

    persistent_string& operator=(const std::string& s) noexcept {
        assign(s.c_str(), s.size());
        return *this;
    }

    // ---- accessors ----

    // Returns a pointer to the NUL-terminated string data.
    const char* c_str() const noexcept {
        if (!is_long) return sso_buf;
        if (long_ptr.addr() == 0) return "";
        return &long_ptr[0];
    }

    // Returns the length of the stored string (excluding NUL).
    size_t size() const noexcept {
        if (!is_long) return std::strlen(sso_buf);
        return static_cast<size_t>(long_len);
    }

    bool empty() const noexcept {
        if (!is_long) return sso_buf[0] == '\0';
        return long_len == 0;
    }

    // ---- mutable access (non-const data ptr) ----

    char* data() noexcept {
        if (!is_long) return sso_buf;
        if (long_ptr.addr() == 0) return sso_buf;  // fallback
        return &long_ptr[0];
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
    persistent_string& push_back(char c) noexcept {
        size_t current = size();
        if (current >= MAX_LONG_SIZE) return *this;  // capacity limit
        if (!is_long && current < SSO_SIZE) {
            sso_buf[current]     = c;
            sso_buf[current + 1] = '\0';
        } else {
            _promote_to_long_if_needed(current);
            if (long_ptr.addr() != 0) {
                long_ptr[current]     = c;
                long_ptr[current + 1] = '\0';
                long_len = static_cast<unsigned>(current + 1);
            }
        }
        return *this;
    }

    // Append from a buffer of n characters.
    persistent_string& append(const char* s, size_t n) noexcept {
        if (!s || n == 0) return *this;
        size_t current = size();
        size_t avail   = MAX_LONG_SIZE - current;
        if (n > avail) n = avail;
        if (n == 0) return *this;
        size_t new_len = current + n;

        if (!is_long && new_len <= SSO_SIZE) {
            std::memcpy(sso_buf + current, s, n);
            sso_buf[new_len] = '\0';
        } else {
            _promote_to_long_if_needed(current);
            if (long_ptr.addr() != 0) {
                for (size_t i = 0; i < n; ++i) long_ptr[current + i] = s[i];
                long_ptr[new_len] = '\0';
                long_len = static_cast<unsigned>(new_len);
            }
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
        std::string tmp(first, last);
        return append(tmp.c_str(), tmp.size());
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
        result._assign(buf + pos, actual);
        return result;
    }

    // ---- capacity (extra std::string-compatible) ----

    size_t length() const noexcept { return size(); }

    size_t max_size() const noexcept { return MAX_LONG_SIZE; }

    size_t capacity() const noexcept { return MAX_LONG_SIZE; }

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
            if (long_ptr.addr() != 0) {
                long_ptr[n - 1] = '\0';
                long_len = static_cast<unsigned>(n - 1);
                // Demote to SSO if short enough (saves persistent memory).
                if (long_len <= SSO_SIZE) {
                    std::memcpy(sso_buf, &long_ptr[0], long_len);
                    sso_buf[long_len] = '\0';
                    long_ptr.DeleteArray();
                    is_long  = false;
                    long_len = 0;
                }
            }
        }
    }

    void clear() noexcept {
        if (is_long && long_ptr.addr() != 0) {
            long_ptr.DeleteArray();
        }
        is_long    = false;
        long_len   = 0;
        sso_buf[0] = '\0';
    }

    void resize(size_t new_size, char fill = '\0') noexcept {
        size_t current = size();
        if (new_size <= current) {
            // Truncate
            if (!is_long) {
                sso_buf[new_size] = '\0';
            } else if (long_ptr.addr() != 0) {
                long_ptr[new_size] = '\0';
                long_len = static_cast<unsigned>(new_size);
            }
        } else {
            // Extend with fill character
            size_t add = new_size - current;
            // fill `add` characters
            for (size_t i = 0; i < add; ++i) push_back(fill);
        }
    }

    // reserve is effectively a no-op (persistent memory is allocated on demand)
    // but required by some nlohmann internals.
    void reserve(size_t /*new_cap*/) noexcept {}

    // ---- replace (used in JSON pointer escape/unescape) ----

    // Replace count characters starting at pos with the string s.
    // Matches the minimal std::string::replace(pos, count, s) signature used by
    // nlohmann::json_pointer.
    persistent_string& replace(size_t pos, size_t count, const char* s) noexcept {
        // Build result via std::string for simplicity (no allocation concern here
        // since this is called from nlohmann internals for short strings).
        std::string orig(c_str());
        if (pos > orig.size()) pos = orig.size();
        if (count > orig.size() - pos) count = orig.size() - pos;
        std::string result = orig.substr(0, pos)
                           + (s ? s : "")
                           + orig.substr(pos + count);
        // Free old and reassign.
        if (is_long && long_ptr.addr() != 0) {
            long_ptr.DeleteArray();
            is_long  = false;
            long_len = 0;
        }
        _assign(result.c_str(), result.size());
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

private:
    // ---- internal helpers ----

    // _alloc_long(n): allocate a persistent char array of n+1 bytes (+1 for NUL).
    // Sets is_long=true, long_ptr to the new allocation, long_len=0.
    // Does NOT write any content.
    void _alloc_long(size_t n) noexcept {
        if (n > MAX_LONG_SIZE) n = MAX_LONG_SIZE;
        // Allocate n+1 chars (+1 for NUL).
        long_ptr.NewArray(static_cast<unsigned>(n + 1), nullptr);
        if (long_ptr.addr() != 0) {
            is_long  = true;
            long_len = 0;
            long_ptr[0] = '\0';
        }
    }

    // _promote_to_long_if_needed(current_len): if is_long is false, allocates
    // persistent memory and copies sso_buf content to it.
    void _promote_to_long_if_needed(size_t current_len) noexcept {
        if (is_long) return;  // already in long mode
        // Allocate persistent array large enough for future content.
        // We allocate MAX_LONG_SIZE so we don't need to reallocate later.
        long_ptr.NewArray(static_cast<unsigned>(MAX_LONG_SIZE + 1), nullptr);
        if (long_ptr.addr() != 0) {
            // Copy sso_buf to the persistent array.
            for (size_t i = 0; i <= current_len; ++i) long_ptr[i] = sso_buf[i];
            is_long  = true;
            long_len = static_cast<unsigned>(current_len);
            sso_buf[0] = '\0';
        }
    }

    // _assign(s, len): assign string data without freeing first.
    // Assumes the caller has already handled any prior allocation.
    void _assign(const char* s, size_t len) noexcept {
        if (!s || len == 0) {
            sso_buf[0] = '\0';
            is_long    = false;
            long_len   = 0;
            return;
        }
        if (len > MAX_LONG_SIZE) len = MAX_LONG_SIZE;
        if (len <= SSO_SIZE) {
            std::memcpy(sso_buf, s, len);
            sso_buf[len] = '\0';
            is_long  = false;
            long_len = 0;
        } else {
            // Allocate persistent memory for long string.
            _alloc_long(len);
            if (long_ptr.addr() != 0) {
                for (size_t i = 0; i < len; ++i) long_ptr[i] = s[i];
                long_ptr[len] = '\0';
                long_len = static_cast<unsigned>(len);
            }
        }
    }

};

// Verify that fptr<char> is trivially copyable (it stores only an unsigned).
static_assert(std::is_trivially_copyable<fptr<char>>::value,
              "fptr<char> must be trivially copyable");

// Verify layout consistency.
static_assert(sizeof(fptr<char>) == sizeof(unsigned),
              "fptr<char> must be the size of an unsigned");

// Verify that persistent_string is trivially copyable.
// Task 3.4: the copy constructor and copy assignment are now defaulted (shallow
// copy) so that persistent_string satisfies trivially copyable.  For long
// strings, the shallow copy shares the same AddressManager<char> slot — this
// is valid in the persistent model (the slot is owned by the address space,
// not by a single string object).  Explicit free_long() must be called once
// to release the slot, and raw-byte copies (e.g. via persist<T>) will share
// the underlying persistent char array.
static_assert(std::is_trivially_copyable<persistent_string>::value,
              "jgit::persistent_string must be trivially copyable for use with persist<T>");

// Cross-type comparison operators between jgit::persistent_string and
// std::string.  Required when nlohmann::basic_json uses transparent comparison
// (std::less<void>) in its std::map.
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
    if (a) result.assign(a, std::strlen(a));
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
