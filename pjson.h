#pragma once
#include "pstring.h"
#include "pvector.h"
#include "pmap.h"
#include <cstring>
#include <cstdint>
#include <type_traits>

// pjson — a persistent discriminated union (persistent analogue of nlohmann::json).
//
// Design: Option B from DEVELOPMENT_PLAN.md — a custom persistent discriminated
// union built directly on fptr<T> / pstring / pvector / pmap primitives.
//
// Why Option B and not Option A (nlohmann::basic_json instantiation)?
//   nlohmann::basic_json internally dereferences raw pointers and assumes heap
//   allocation.  AddressManager uses slot indices, not raw C++ pointers, so
//   Option A would require non-trivial pointer translation at every access.
//   Option B gives full control and keeps the design consistent with the rest
//   of the persistent infrastructure.
//
// Layout:
//   pjson_data is trivially copyable and can be stored via persist<pjson_data>
//   or embedded inside pvector<pjson_data> / pmap<K, pjson_data>.
//
//   The discriminant (pjson_type) occupies 4 bytes.  The payload is a union
//   of primitive values plus fptr slot indices for heap-allocated types.
//
// Value types:
//   null    — zero-size payload.
//   boolean — bool (stored as uint32_t for alignment).
//   integer — int64_t.
//   uinteger— uint64_t.
//   real    — double.
//   string  — fptr<char> (points to a char array in AddressManager<char>).
//   array   — fptr<pjson_data> (points to array of pjson_data elements).
//   object  — two parallel arrays:
//               fptr<pjson_data> values array
//               fptr<char>       keys    flat-packed C-strings (key_offsets array)
//             Keys are stored as indices into a parallel pvector of flat pjson_data.
//             For simplicity in Phase 1, objects are stored as sorted arrays of
//             (pstring_data key, pjson_data value) entries — see pjson_obj_entry.
//
// All fptr slot indices are unsigned (4 bytes).  pjson_data is 32 bytes on
// most 64-bit platforms.

// ---------------------------------------------------------------------------
// pjson_type — discriminant tag
// ---------------------------------------------------------------------------
enum class pjson_type : uint32_t
{
    null     = 0,
    boolean  = 1,
    integer  = 2,
    uinteger = 3,
    real     = 4,
    string   = 5,
    array    = 6,
    object   = 7,
};

// ---------------------------------------------------------------------------
// pjson_obj_entry — one key-value pair inside a pjson object.
// Both fields are trivially copyable.
// ---------------------------------------------------------------------------
struct pjson_obj_entry
{
    pstring_data key;    // persistent string header (length + fptr<char> slot)
    // The value is stored separately in a parallel pvector_data<pjson_data>.
    // We need a forward declaration, so we use a uint32_t slot index here and
    // resolve it at the pjson level.  To keep things simple in Phase 1 the
    // object stores (key, pjson_data) as a flat pair defined below.
};

// Forward declaration so pjson_data can self-referentially hold slot indices.
struct pjson_data;

// pjson_kv_entry — one (pstring_data key, pjson_data value) pair.
// Used as the element type of the sorted array backing pjson objects.
// Both pstring_data and pjson_data are trivially copyable so this struct is
// trivially copyable too (checked by static_assert below).
struct pjson_kv_entry
{
    pstring_data key;    // 8 bytes: unsigned length + unsigned fptr<char> slot
    pjson_data*  _pad;   // placeholder to be replaced with actual pjson_data below
};
// (We cannot embed pjson_data inside pjson_kv_entry before defining it, so we
//  use the two-level trick: define pjson_data first, then define pjson_kv_entry
//  as a concrete struct after.  See pjson_kv_pair further below.)

// ---------------------------------------------------------------------------
// pjson_data — trivially-copyable persistent JSON value header.
// ---------------------------------------------------------------------------
struct pjson_data
{
    pjson_type type;    // 4 bytes discriminant

    union payload_t
    {
        uint32_t    boolean_val;  // 0 = false, non-zero = true
        int64_t     int_val;
        uint64_t    uint_val;
        double      real_val;

        // For string: slot index of a char array in AddressManager<char>.
        // (We store the raw unsigned rather than fptr<char> to avoid a
        //  C++ union restriction on non-trivially-copyable members.
        //  fptr<char> stores only a uint so the representation is identical.)
        struct { unsigned length; unsigned chars_slot; } string_val;

        // For array: slot + size stored in persistent AddressManager<pjson_data>.
        struct { unsigned size; unsigned data_slot; } array_val;

        // For object: parallel pvector_data-equivalent fields.
        // We store (size, data_slot) where data_slot points to a
        // pjson_kv_pair[] array (defined below, but sizeof is known at
        // link time — we use a forward-declared helper struct).
        struct { unsigned size; unsigned pairs_slot; } object_val;
    } payload;

    // Padding to align total size to 16 bytes (4 + 4 + 8 = 16).
    // payload_t largest member is int64_t/uint64_t/double (8 bytes);
    // the string/array/object variants are 2×uint = 8 bytes.
    // Total: 4 (type) + 4 (pad inside union for alignment) + 8 (value) = 16 bytes.
};

static_assert(std::is_trivially_copyable<pjson_data>::value,
              "pjson_data must be trivially copyable for use with persist<T>");

// ---------------------------------------------------------------------------
// pjson_kv_pair — one (pstring_data key, pjson_data value) pair.
// Both components are trivially copyable so the pair is trivially copyable.
// Objects are backed by a pvector<pjson_kv_pair> (sorted by key for O(log n) lookup).
// ---------------------------------------------------------------------------
struct pjson_kv_pair
{
    pstring_data key;    // 8 bytes
    pjson_data   value;  // 16 bytes
};

static_assert(std::is_trivially_copyable<pjson_kv_pair>::value,
              "pjson_kv_pair must be trivially copyable");
static_assert(std::is_trivially_copyable<pvector_data<pjson_kv_pair>>::value,
              "pvector_data<pjson_kv_pair> must be trivially copyable");

// ---------------------------------------------------------------------------
// pjson — thin non-owning wrapper around a pjson_data reference.
// ---------------------------------------------------------------------------
class pjson
{
    pjson_data& _d;

    // ----- internal helpers -----------------------------------------------

    // Free any heap-allocated children of _d, then reset to null.
    void _free()
    {
        switch( _d.type )
        {
        case pjson_type::string:
            if( _d.payload.string_val.chars_slot != 0 )
            {
                fptr<char> tmp;
                tmp.set_addr( _d.payload.string_val.chars_slot );
                tmp.DeleteArray();
                _d.payload.string_val.chars_slot = 0;
                _d.payload.string_val.length = 0;
            }
            break;

        case pjson_type::array:
            if( _d.payload.array_val.data_slot != 0 )
            {
                // Recursively free each element.
                unsigned sz = _d.payload.array_val.size;
                for( unsigned i = 0; i < sz; i++ )
                {
                    pjson_data& elem =
                        AddressManager<pjson_data>::GetArrayElement(
                            _d.payload.array_val.data_slot, i );
                    pjson(elem)._free();
                }
                fptr<pjson_data> tmp;
                tmp.set_addr( _d.payload.array_val.data_slot );
                tmp.DeleteArray();
                _d.payload.array_val.data_slot = 0;
                _d.payload.array_val.size = 0;
            }
            break;

        case pjson_type::object:
            if( _d.payload.object_val.pairs_slot != 0 )
            {
                unsigned sz = _d.payload.object_val.size;
                for( unsigned i = 0; i < sz; i++ )
                {
                    pjson_kv_pair& pair =
                        AddressManager<pjson_kv_pair>::GetArrayElement(
                            _d.payload.object_val.pairs_slot, i );
                    // Free the key string.
                    if( pair.key.chars.addr() != 0 )
                        pair.key.chars.DeleteArray();
                    // Free the value recursively.
                    pjson(pair.value)._free();
                }
                fptr<pjson_kv_pair> tmp;
                tmp.set_addr( _d.payload.object_val.pairs_slot );
                tmp.DeleteArray();
                _d.payload.object_val.pairs_slot = 0;
                _d.payload.object_val.size = 0;
            }
            break;

        default:
            break;
        }
        _d.type = pjson_type::null;
        _d.payload.uint_val = 0;
    }

    // Assign pstring_data by copying a C-string into persistent storage.
    static void _assign_key( pstring_data& sd, const char* s )
    {
        if( sd.chars.addr() != 0 )
            sd.chars.DeleteArray();
        if( s == nullptr || s[0] == '\0' )
        {
            sd.length = 0;
            return;
        }
        unsigned len = static_cast<unsigned>(std::strlen(s));
        sd.length = len;
        sd.chars.NewArray(len + 1);
        for( unsigned i = 0; i <= len; i++ )
            sd.chars[i] = s[i];
    }

    // Binary search for a key in the object's sorted pairs array.
    // Returns the index of the first pair whose key >= s, or size if none.
    unsigned _obj_lower_bound( const char* s ) const
    {
        unsigned sz = _d.payload.object_val.size;
        unsigned lo = 0, hi = sz;
        while( lo < hi )
        {
            unsigned mid = (lo + hi) / 2;
            pjson_kv_pair& pair =
                AddressManager<pjson_kv_pair>::GetArrayElement(
                    _d.payload.object_val.pairs_slot, mid );
            const char* k = (pair.key.chars.addr() != 0)
                            ? &pair.key.chars[0]
                            : "";
            if( std::strcmp(k, s) < 0 )
                lo = mid + 1;
            else
                hi = mid;
        }
        return lo;
    }

public:
    // Wrap an existing pjson_data reference.  Does NOT take ownership.
    explicit pjson(pjson_data& data) : _d(data) {}

    // ----- type queries ---------------------------------------------------
    pjson_type type() const { return _d.type; }
    bool is_null()     const { return _d.type == pjson_type::null; }
    bool is_boolean()  const { return _d.type == pjson_type::boolean; }
    bool is_integer()  const { return _d.type == pjson_type::integer; }
    bool is_uinteger() const { return _d.type == pjson_type::uinteger; }
    bool is_real()     const { return _d.type == pjson_type::real; }
    bool is_number()   const { return is_integer() || is_uinteger() || is_real(); }
    bool is_string()   const { return _d.type == pjson_type::string; }
    bool is_array()    const { return _d.type == pjson_type::array; }
    bool is_object()   const { return _d.type == pjson_type::object; }

    // ----- setters --------------------------------------------------------

    void set_null()
    {
        _free();
    }

    void set_bool(bool v)
    {
        _free();
        _d.type = pjson_type::boolean;
        _d.payload.boolean_val = v ? 1u : 0u;
    }

    void set_int(int64_t v)
    {
        _free();
        _d.type = pjson_type::integer;
        _d.payload.int_val = v;
    }

    void set_uint(uint64_t v)
    {
        _free();
        _d.type = pjson_type::uinteger;
        _d.payload.uint_val = v;
    }

    void set_real(double v)
    {
        _free();
        _d.type = pjson_type::real;
        _d.payload.real_val = v;
    }

    void set_string(const char* s)
    {
        _free();
        _d.type = pjson_type::string;
        _d.payload.string_val.chars_slot = 0;
        _d.payload.string_val.length = 0;
        if( s == nullptr || s[0] == '\0' ) return;

        unsigned len = static_cast<unsigned>(std::strlen(s));
        _d.payload.string_val.length = len;
        fptr<char> chars;
        chars.NewArray(len + 1);
        for( unsigned i = 0; i <= len; i++ )
            chars[i] = s[i];
        _d.payload.string_val.chars_slot = chars.addr();
    }

    // set_array: reset to an empty persistent array.
    void set_array()
    {
        _free();
        _d.type = pjson_type::array;
        _d.payload.array_val.size = 0;
        _d.payload.array_val.data_slot = 0;
    }

    // set_object: reset to an empty persistent object.
    void set_object()
    {
        _free();
        _d.type = pjson_type::object;
        _d.payload.object_val.size = 0;
        _d.payload.object_val.pairs_slot = 0;
    }

    // ----- getters --------------------------------------------------------

    bool get_bool() const
    {
        return _d.payload.boolean_val != 0;
    }

    int64_t get_int() const
    {
        return _d.payload.int_val;
    }

    uint64_t get_uint() const
    {
        return _d.payload.uint_val;
    }

    double get_real() const
    {
        return _d.payload.real_val;
    }

    const char* get_string() const
    {
        if( _d.payload.string_val.chars_slot == 0 ) return "";
        return &AddressManager<char>::GetArrayElement(
                    _d.payload.string_val.chars_slot, 0);
    }

    // ----- array operations -----------------------------------------------

    unsigned size() const
    {
        if( _d.type == pjson_type::array )
            return _d.payload.array_val.size;
        if( _d.type == pjson_type::object )
            return _d.payload.object_val.size;
        if( _d.type == pjson_type::string )
            return _d.payload.string_val.length;
        return 0;
    }

    bool empty() const { return size() == 0; }

    // push_back: append a null element to an array, return reference to it.
    pjson_data& push_back()
    {
        unsigned old_size = _d.payload.array_val.size;
        unsigned new_size = old_size + 1;

        if( _d.payload.array_val.data_slot == 0 )
        {
            // First element: allocate a small initial capacity.
            fptr<pjson_data> arr;
            arr.NewArray(4);
            // Zero-initialise all slots.
            for( unsigned i = 0; i < 4; i++ )
            {
                pjson_data& e = arr[i];
                e.type = pjson_type::null;
                e.payload.uint_val = 0;
            }
            _d.payload.array_val.data_slot = arr.addr();
        }
        else if( new_size > AddressManager<pjson_data>::GetCount(
                                _d.payload.array_val.data_slot ) )
        {
            // Grow: double the capacity.
            unsigned old_cap = AddressManager<pjson_data>::GetCount(
                                   _d.payload.array_val.data_slot);
            unsigned new_cap = old_cap * 2;
            if( new_cap < new_size ) new_cap = new_size;

            fptr<pjson_data> new_arr;
            new_arr.NewArray(new_cap);
            for( unsigned i = 0; i < new_cap; i++ )
            {
                pjson_data& e = new_arr[i];
                e.type = pjson_type::null;
                e.payload.uint_val = 0;
            }
            // Move existing elements (shallow copy — primitives only;
            // complex children keep their slot indices intact).
            for( unsigned i = 0; i < old_size; i++ )
                new_arr[i] = AddressManager<pjson_data>::GetArrayElement(
                                 _d.payload.array_val.data_slot, i);

            fptr<pjson_data> old_arr;
            old_arr.set_addr(_d.payload.array_val.data_slot);
            old_arr.DeleteArray();
            _d.payload.array_val.data_slot = new_arr.addr();
        }

        _d.payload.array_val.size = new_size;
        return AddressManager<pjson_data>::GetArrayElement(
                   _d.payload.array_val.data_slot, old_size);
    }

    // operator[](idx): access element of an array by index.
    pjson_data& operator[](unsigned idx)
    {
        return AddressManager<pjson_data>::GetArrayElement(
                   _d.payload.array_val.data_slot, idx);
    }

    const pjson_data& operator[](unsigned idx) const
    {
        return AddressManager<pjson_data>::GetArrayElement(
                   _d.payload.array_val.data_slot, idx);
    }

    // ----- object operations ----------------------------------------------

    // obj_find: look up a key in the object; returns pointer to value or nullptr.
    pjson_data* obj_find(const char* key)
    {
        if( _d.type != pjson_type::object ) return nullptr;
        if( _d.payload.object_val.pairs_slot == 0 ) return nullptr;
        unsigned idx = _obj_lower_bound(key);
        unsigned sz  = _d.payload.object_val.size;
        if( idx >= sz ) return nullptr;
        pjson_kv_pair& pair =
            AddressManager<pjson_kv_pair>::GetArrayElement(
                _d.payload.object_val.pairs_slot, idx);
        const char* k = (pair.key.chars.addr() != 0) ? &pair.key.chars[0] : "";
        if( std::strcmp(k, key) != 0 ) return nullptr;
        return &pair.value;
    }

    const pjson_data* obj_find(const char* key) const
    {
        if( _d.type != pjson_type::object ) return nullptr;
        if( _d.payload.object_val.pairs_slot == 0 ) return nullptr;
        unsigned idx = _obj_lower_bound(key);
        unsigned sz  = _d.payload.object_val.size;
        if( idx >= sz ) return nullptr;
        const pjson_kv_pair& pair =
            AddressManager<pjson_kv_pair>::GetArrayElement(
                _d.payload.object_val.pairs_slot, idx);
        const char* k = (pair.key.chars.addr() != 0) ? &pair.key.chars[0] : "";
        if( std::strcmp(k, key) != 0 ) return nullptr;
        return &pair.value;
    }

    // obj_insert: insert or replace a key in the object.
    // Returns a reference to the value slot for that key.
    pjson_data& obj_insert(const char* key)
    {
        unsigned sz = _d.payload.object_val.size;

        if( _d.payload.object_val.pairs_slot == 0 )
        {
            // First entry: allocate initial capacity of 4 pairs.
            fptr<pjson_kv_pair> arr;
            arr.NewArray(4);
            for( unsigned i = 0; i < 4; i++ )
            {
                pjson_kv_pair& p = arr[i];
                p.key.length = 0;
                p.key.chars.set_addr(0);
                p.value.type = pjson_type::null;
                p.value.payload.uint_val = 0;
            }
            _d.payload.object_val.pairs_slot = arr.addr();
        }

        unsigned idx = _obj_lower_bound(key);

        // Check if key already exists.
        if( idx < sz )
        {
            pjson_kv_pair& pair =
                AddressManager<pjson_kv_pair>::GetArrayElement(
                    _d.payload.object_val.pairs_slot, idx);
            const char* k = (pair.key.chars.addr() != 0) ? &pair.key.chars[0] : "";
            if( std::strcmp(k, key) == 0 )
            {
                // Key found — free old value and return its slot.
                pjson(pair.value)._free();
                return pair.value;
            }
        }

        // Insert new entry at idx, shifting right.
        unsigned new_size = sz + 1;
        unsigned cap = AddressManager<pjson_kv_pair>::GetCount(
                           _d.payload.object_val.pairs_slot);
        if( new_size > cap )
        {
            unsigned new_cap = cap * 2;
            if( new_cap < new_size ) new_cap = new_size;
            fptr<pjson_kv_pair> new_arr;
            new_arr.NewArray(new_cap);
            for( unsigned i = 0; i < new_cap; i++ )
            {
                pjson_kv_pair& p = new_arr[i];
                p.key.length = 0;
                p.key.chars.set_addr(0);
                p.value.type = pjson_type::null;
                p.value.payload.uint_val = 0;
            }
            for( unsigned i = 0; i < sz; i++ )
                new_arr[i] = AddressManager<pjson_kv_pair>::GetArrayElement(
                                 _d.payload.object_val.pairs_slot, i);
            fptr<pjson_kv_pair> old_arr;
            old_arr.set_addr(_d.payload.object_val.pairs_slot);
            old_arr.DeleteArray();
            _d.payload.object_val.pairs_slot = new_arr.addr();
        }

        // Shift elements right to make room at idx.
        for( unsigned i = sz; i > idx; i-- )
        {
            AddressManager<pjson_kv_pair>::GetArrayElement(
                _d.payload.object_val.pairs_slot, i) =
            AddressManager<pjson_kv_pair>::GetArrayElement(
                _d.payload.object_val.pairs_slot, i - 1);
        }

        // Write new pair at idx.
        pjson_kv_pair& new_pair =
            AddressManager<pjson_kv_pair>::GetArrayElement(
                _d.payload.object_val.pairs_slot, idx);
        new_pair.value.type = pjson_type::null;
        new_pair.value.payload.uint_val = 0;
        // Zero out the key slot BEFORE calling _assign_key so that it does
        // not free the chars_slot that now belongs to the shifted neighbor
        // at [idx+1].  After the shift loop, both [idx] and [idx+1] hold
        // the same chars_slot pointer (shallow copy); resetting [idx].key
        // marks it as unowned and prevents a double-free / use-after-free.
        new_pair.key.chars.set_addr(0);
        new_pair.key.length = 0;
        _assign_key(new_pair.key, key);

        _d.payload.object_val.size = new_size;
        return new_pair.value;
    }

    // obj_erase: remove a key from the object. Returns true if found.
    bool obj_erase(const char* key)
    {
        if( _d.type != pjson_type::object ) return false;
        if( _d.payload.object_val.pairs_slot == 0 ) return false;
        unsigned sz  = _d.payload.object_val.size;
        unsigned idx = _obj_lower_bound(key);
        if( idx >= sz ) return false;
        pjson_kv_pair& pair =
            AddressManager<pjson_kv_pair>::GetArrayElement(
                _d.payload.object_val.pairs_slot, idx);
        const char* k = (pair.key.chars.addr() != 0) ? &pair.key.chars[0] : "";
        if( std::strcmp(k, key) != 0 ) return false;

        // Free key string and value.
        if( pair.key.chars.addr() != 0 )
            pair.key.chars.DeleteArray();
        pjson(pair.value)._free();

        // Shift remaining elements left.
        for( unsigned i = idx; i + 1 < sz; i++ )
        {
            AddressManager<pjson_kv_pair>::GetArrayElement(
                _d.payload.object_val.pairs_slot, i) =
            AddressManager<pjson_kv_pair>::GetArrayElement(
                _d.payload.object_val.pairs_slot, i + 1);
        }
        _d.payload.object_val.size--;
        return true;
    }

    // ----- free -----------------------------------------------------------

    // free: release all heap allocations and reset to null.
    void free() { _free(); }
};
