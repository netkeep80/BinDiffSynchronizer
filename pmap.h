#pragma once
#include "pvector.h"
#include <cstring>

// pmap<K, V> — a persistent key-value map.
//
// Backed by a sorted pvector<pmap_entry<K, V>> (insertion-sorted).
// Binary search is used for find(); linear search for erase().
//
// Design constraints:
//   - K and V must be trivially copyable (enforced below via static_assert).
//   - K must support operator< (comparison).
//   - K must support operator== (equality).
//   - pmap_data<K, V> is trivially copyable.
//
// Usage:
//   pmap_data<int, double> md{};
//   pmap<int, double> m(md);
//   m.insert(1, 3.14);
//   double* v = m.find(1);   // returns pointer to value, or nullptr if not found
//   m.erase(1);

template<typename K, typename V>
struct pmap_entry
{
    K key;
    V value;
};

// pmap_data<K,V> wraps pvector_data<pmap_entry<K,V>>.
// Since pvector_data is trivially copyable and pmap_entry<K,V> is trivially
// copyable (when K and V are), pmap_data<K,V> is also trivially copyable.
template<typename K, typename V>
using pmap_data = pvector_data<pmap_entry<K, V>>;

template<typename K, typename V>
struct pmap_trivial_check
{
    static_assert(std::is_trivially_copyable<K>::value,
                  "pmap<K,V> requires K to be trivially copyable");
    static_assert(std::is_trivially_copyable<V>::value,
                  "pmap<K,V> requires V to be trivially copyable");
    static_assert(std::is_trivially_copyable<pmap_entry<K,V>>::value,
                  "pmap_entry<K,V> must be trivially copyable");
    static_assert(std::is_trivially_copyable<pmap_data<K,V>>::value,
                  "pmap_data<K,V> must be trivially copyable for use with persist<T>");
};

// pmap is a thin non-owning wrapper around a pmap_data<K,V> reference.
// The caller owns pmap_data (typically via persist<pmap_data<K,V>>).
template<typename K, typename V>
class pmap : pmap_trivial_check<K, V>
{
    using Entry  = pmap_entry<K, V>;
    using VecData = pvector_data<Entry>;

    VecData& _vd;
    pvector<Entry> _vec;

    // lower_bound: find the index of the first entry with key >= k.
    unsigned lower_bound(const K& k) const
    {
        unsigned lo = 0, hi = _vd.size;
        while( lo < hi )
        {
            unsigned mid = (lo + hi) / 2;
            if( _vd.data[mid].key < k )
                lo = mid + 1;
            else
                hi = mid;
        }
        return lo;
    }

public:
    explicit pmap(pmap_data<K, V>& data) : _vd(data), _vec(data) {}

    unsigned size()  const { return _vd.size; }
    bool     empty() const { return _vd.size == 0; }

    // insert: add or replace key k with value v.
    // Maintains sorted order.
    void insert(const K& k, const V& v)
    {
        unsigned idx = lower_bound(k);
        if( idx < _vd.size && !( k < _vd.data[idx].key ) && !( _vd.data[idx].key < k ) )
        {
            // Key already exists — update value.
            _vd.data[idx].value = v;
            return;
        }
        // Insert at idx, shifting elements right.
        _vec.push_back(Entry{});   // ensure capacity, append placeholder
        for( unsigned i = _vd.size - 1; i > idx; i-- )
            _vd.data[i] = _vd.data[i - 1];
        _vd.data[idx] = Entry{ k, v };
    }

    // find: return pointer to the value for key k, or nullptr if not found.
    V* find(const K& k)
    {
        unsigned idx = lower_bound(k);
        if( idx < _vd.size && !( k < _vd.data[idx].key ) && !( _vd.data[idx].key < k ) )
            return &_vd.data[idx].value;
        return nullptr;
    }

    const V* find(const K& k) const
    {
        unsigned idx = lower_bound(k);
        if( idx < _vd.size && !( k < _vd.data[idx].key ) && !( _vd.data[idx].key < k ) )
            return &_vd.data[idx].value;
        return nullptr;
    }

    // erase: remove entry with key k. Returns true if found and removed.
    bool erase(const K& k)
    {
        unsigned idx = lower_bound(k);
        if( idx >= _vd.size || ( k < _vd.data[idx].key ) || ( _vd.data[idx].key < k ) )
            return false;
        // Shift elements left.
        for( unsigned i = idx; i + 1 < _vd.size; i++ )
            _vd.data[i] = _vd.data[i + 1];
        _vd.size--;
        return true;
    }

    // operator[]: insert a default value if key not present; return ref to value.
    V& operator[](const K& k)
    {
        unsigned idx = lower_bound(k);
        if( idx < _vd.size && !( k < _vd.data[idx].key ) && !( _vd.data[idx].key < k ) )
            return _vd.data[idx].value;
        // Insert default-constructed value.
        V def{};
        insert(k, def);
        idx = lower_bound(k);
        return _vd.data[idx].value;
    }

    // clear: remove all entries. Does NOT free the underlying allocation.
    void clear() { _vec.clear(); }

    // free: release the underlying allocation entirely.
    void free() { _vec.free(); }

    // Iteration (over Entry objects in sorted key order).
    class iterator
    {
        VecData* _pd;
        unsigned _idx;
    public:
        iterator(VecData* pd, unsigned idx) : _pd(pd), _idx(idx) {}
        Entry& operator*()  { return _pd->data[_idx]; }
        Entry* operator->() { return &_pd->data[_idx]; }
        iterator& operator++() { ++_idx; return *this; }
        iterator  operator++(int) { iterator tmp = *this; ++_idx; return tmp; }
        bool operator==(const iterator& o) const { return _idx == o._idx; }
        bool operator!=(const iterator& o) const { return _idx != o._idx; }
    };

    class const_iterator
    {
        const VecData* _pd;
        unsigned       _idx;
    public:
        const_iterator(const VecData* pd, unsigned idx) : _pd(pd), _idx(idx) {}
        const Entry& operator*()  const { return _pd->data[_idx]; }
        const Entry* operator->() const { return &_pd->data[_idx]; }
        const_iterator& operator++() { ++_idx; return *this; }
        const_iterator  operator++(int) { const_iterator tmp = *this; ++_idx; return tmp; }
        bool operator==(const const_iterator& o) const { return _idx == o._idx; }
        bool operator!=(const const_iterator& o) const { return _idx != o._idx; }
    };

    iterator begin() { return iterator(&_vd, 0); }
    iterator end()   { return iterator(&_vd, _vd.size); }
    const_iterator begin() const { return const_iterator(&_vd, 0); }
    const_iterator end()   const { return const_iterator(&_vd, _vd.size); }
};
