#pragma once
#include "persist.h"

// pvector<T> — a persistent dynamic array.
//
// The vector header (pvector_data<T>) is trivially copyable and can be stored
// via persist<pvector_data<T>>. Element data is stored in AddressManager<T>
// via an fptr<T> slot index.
//
// Design constraints:
//   - T must be trivially copyable (enforced below via static_assert).
//   - pvector_data<T> is trivially copyable.
//   - Growth strategy: double capacity when full (minimum initial capacity: 4).
//   - An empty pvector has data.addr() == 0, size == 0, capacity == 0.
//
// Usage:
//   pvector_data<int> vd{};
//   pvector<int> v(vd);
//   v.push_back(1);
//   v.push_back(2);
//   // vd.data.addr() holds slot; vd.size == 2, vd.capacity >= 2
//   // Persist vd via persist<pvector_data<int>> for cross-restart durability.

template<typename T>
struct pvector_data
{
    unsigned size;      // current number of elements
    unsigned capacity;  // allocated capacity
    fptr<T>  data;      // slot in AddressManager<T>; 0 = no allocation
};

template<typename T>
struct pvector_trivial_check
{
    static_assert(std::is_trivially_copyable<T>::value,
                  "pvector<T> requires T to be trivially copyable");
    static_assert(std::is_trivially_copyable<pvector_data<T>>::value,
                  "pvector_data<T> must be trivially copyable for use with persist<T>");
};

// pvector is a thin non-owning wrapper around a pvector_data<T> reference.
// The caller owns pvector_data<T> (typically via persist<pvector_data<T>>).
template<typename T>
class pvector : pvector_trivial_check<T>
{
    pvector_data<T>& _d;

    // grow: ensure capacity >= needed, reallocating if necessary.
    void grow(unsigned needed)
    {
        if( needed <= _d.capacity ) return;

        unsigned new_cap = (_d.capacity == 0) ? 4 : _d.capacity * 2;
        while( new_cap < needed ) new_cap *= 2;

        fptr<T> new_data;
        new_data.NewArray(new_cap);

        // Copy existing elements to new allocation.
        for( unsigned i = 0; i < _d.size; i++ )
            new_data[i] = _d.data[i];

        // Free old allocation.
        if( _d.data.addr() != 0 )
            _d.data.DeleteArray();

        _d.data     = new_data;
        _d.capacity = new_cap;
    }

public:
    explicit pvector(pvector_data<T>& data) : _d(data) {}

    unsigned size()     const { return _d.size; }
    unsigned capacity() const { return _d.capacity; }
    bool     empty()    const { return _d.size == 0; }

    void push_back(const T& val)
    {
        grow(_d.size + 1);
        _d.data[_d.size] = val;
        _d.size++;
    }

    void pop_back()
    {
        if( _d.size > 0 ) _d.size--;
    }

    T& operator[](unsigned idx)       { return _d.data[idx]; }
    const T& operator[](unsigned idx) const { return _d.data[idx]; }

    T& front()       { return _d.data[0]; }
    const T& front() const { return _d.data[0]; }

    T& back()       { return _d.data[_d.size - 1]; }
    const T& back() const { return _d.data[_d.size - 1]; }

    // clear: reset size to 0. Does NOT free the underlying allocation.
    void clear() { _d.size = 0; }

    // free: release the underlying allocation entirely.
    void free()
    {
        if( _d.data.addr() != 0 )
            _d.data.DeleteArray();
        _d.size     = 0;
        _d.capacity = 0;
    }

    // Simple pointer-style iterator.
    // Valid as long as the AddressManager<T> is alive and the slot is not freed.
    class iterator
    {
        pvector_data<T>* _pd;
        unsigned         _idx;
    public:
        iterator(pvector_data<T>* pd, unsigned idx) : _pd(pd), _idx(idx) {}
        T& operator*()  { return _pd->data[_idx]; }
        T* operator->() { return &_pd->data[_idx]; }
        iterator& operator++() { ++_idx; return *this; }
        iterator  operator++(int) { iterator tmp = *this; ++_idx; return tmp; }
        bool operator==(const iterator& o) const { return _idx == o._idx; }
        bool operator!=(const iterator& o) const { return _idx != o._idx; }
    };

    class const_iterator
    {
        const pvector_data<T>* _pd;
        unsigned               _idx;
    public:
        const_iterator(const pvector_data<T>* pd, unsigned idx) : _pd(pd), _idx(idx) {}
        const T& operator*()  const { return _pd->data[_idx]; }
        const T* operator->() const { return &_pd->data[_idx]; }
        const_iterator& operator++() { ++_idx; return *this; }
        const_iterator  operator++(int) { const_iterator tmp = *this; ++_idx; return tmp; }
        bool operator==(const const_iterator& o) const { return _idx == o._idx; }
        bool operator!=(const const_iterator& o) const { return _idx != o._idx; }
    };

    iterator begin() { return iterator(&_d, 0); }
    iterator end()   { return iterator(&_d, _d.size); }
    const_iterator begin() const { return const_iterator(&_d, 0); }
    const_iterator end()   const { return const_iterator(&_d, _d.size); }
    const_iterator cbegin() const { return const_iterator(&_d, 0); }
    const_iterator cend()   const { return const_iterator(&_d, _d.size); }
};
