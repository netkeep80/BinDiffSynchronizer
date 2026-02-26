#pragma once
#include "persist.h"
#include <cstring>
#include <algorithm>

// pstring — a persistent string.
//
// The string header (pstring_data) is trivially copyable and can be stored
// via persist<pstring_data>. Character data is stored in AddressManager<char>
// via an fptr<char> slot index.
//
// Design constraints:
//   - pstring_data is trivially copyable (no virtual, no non-trivial ctor/dtor).
//   - Characters are stored as a char array in AddressManager<char>.
//   - The NUL terminator is always written as the last character.
//   - An empty / null pstring has chars.addr() == 0 and length == 0.
//
// Usage:
//   pstring_data sd{};
//   pstring ps(sd);
//   ps.assign("hello");
//   // sd.chars.addr() now holds the slot index; sd.length == 5
//   // On next run, load sd from persist<pstring_data> and wrap it again.

struct pstring_data
{
    unsigned   length;   // number of characters (not counting NUL terminator)
    fptr<char> chars;    // slot index in AddressManager<char>; 0 = null/empty
};

static_assert(std::is_trivially_copyable<pstring_data>::value,
              "pstring_data must be trivially copyable for use with persist<T>");

// pstring is a thin non-owning wrapper around a pstring_data reference.
// The caller owns pstring_data (typically via persist<pstring_data>).
class pstring
{
    pstring_data& _d;

public:
    explicit pstring(pstring_data& data) : _d(data) {}

    // assign: store a C-string value.
    // Frees the previous character array if any, then allocates a new one.
    void assign(const char* s)
    {
        if( _d.chars.addr() != 0 )
        {
            _d.chars.DeleteArray();
        }
        if( s == nullptr || s[0] == '\0' )
        {
            _d.length = 0;
            return;
        }
        unsigned len = static_cast<unsigned>(std::strlen(s));
        _d.length = len;
        // Allocate len+1 chars (includes NUL terminator).
        _d.chars.NewArray(len + 1);
        for( unsigned i = 0; i <= len; i++ )
            _d.chars[i] = s[i];
    }

    // c_str: return a raw pointer to the character data.
    // Valid as long as AddressManager<char> is alive and the slot is not freed.
    const char* c_str() const
    {
        if( _d.chars.addr() == 0 ) return "";
        return &(_d.chars[0]);
    }

    unsigned size() const { return _d.length; }
    bool     empty() const { return _d.length == 0; }

    // clear: free character data and reset to empty.
    void clear()
    {
        if( _d.chars.addr() != 0 )
        {
            _d.chars.DeleteArray();
        }
        _d.length = 0;
    }

    // operator[]: access character at index (no bounds check).
    char& operator[](unsigned idx) { return _d.chars[idx]; }
    const char& operator[](unsigned idx) const { return _d.chars[idx]; }

    bool operator==(const char* s) const
    {
        if( s == nullptr ) return _d.length == 0;
        return std::strcmp(c_str(), s) == 0;
    }

    bool operator==(const pstring& other) const
    {
        if( _d.length != other._d.length ) return false;
        if( _d.length == 0 ) return true;
        return std::strcmp(c_str(), other.c_str()) == 0;
    }

    bool operator!=(const char* s) const { return !(*this == s); }
    bool operator!=(const pstring& other) const { return !(*this == other); }

    bool operator<(const pstring& other) const
    {
        return std::strcmp(c_str(), other.c_str()) < 0;
    }
};
