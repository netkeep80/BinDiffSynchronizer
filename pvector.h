#pragma once
#include "persist.h"

// pvector<T> — персистный динамический массив.
//
// Заголовок вектора (pvector_data<T>) тривиально копируем и может быть сохранён
// через persist<pvector_data<T>>. Данные элементов хранятся в ПАП через fptr<T>.
//
// Ограничения:
//   - T должен быть тривиально копируемым (static_assert ниже).
//   - pvector_data<T> тривиально копируем.
//   - Стратегия роста: удвоение ёмкости при заполнении (начальная ёмкость: 4).
//   - Пустой pvector имеет data.addr() == 0, size == 0, capacity == 0.
//
// Использование:
//   pvector_data<int> vd{};
//   pvector<int> v(vd);
//   v.push_back(1);
//   v.push_back(2);
//   // vd.data.addr() содержит смещение в ПАП; vd.size == 2, vd.capacity >= 2
//   // Сохраните vd через persist<pvector_data<int>> для восстановления после перезапуска.

/// Заголовок персистного вектора (тривиально копируем).
template<typename T>
struct pvector_data
{
    unsigned size;      ///< Текущее число элементов
    unsigned capacity;  ///< Выделенная ёмкость
    fptr<T>  data;      ///< Смещение в ПАП для массива элементов; 0 = не выделено
};

template<typename T>
struct pvector_trivial_check
{
    static_assert(std::is_trivially_copyable<T>::value,
                  "pvector<T> требует, чтобы T был тривиально копируемым");
    static_assert(std::is_trivially_copyable<pvector_data<T>>::value,
                  "pvector_data<T> должен быть тривиально копируемым для использования с persist<T>");
};

// pvector — тонкая не-владеющая обёртка над ссылкой pvector_data<T>.
// Владелец pvector_data<T> — вызывающий код (как правило, persist<pvector_data<T>>).
template<typename T>
class pvector : pvector_trivial_check<T>
{
    pvector_data<T>& _d;

    // grow: обеспечить ёмкость >= needed, при необходимости перевыделить память.
    void grow(unsigned needed)
    {
        if( needed <= _d.capacity ) return;

        unsigned new_cap = (_d.capacity == 0) ? 4 : _d.capacity * 2;
        while( new_cap < needed ) new_cap *= 2;

        fptr<T> new_data;
        new_data.NewArray(new_cap);

        // Копируем существующие элементы в новый буфер.
        for( unsigned i = 0; i < _d.size; i++ )
            new_data[i] = _d.data[i];

        // Освобождаем старый буфер.
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

    // clear: обнулить размер. НЕ освобождает выделенный буфер.
    void clear() { _d.size = 0; }

    // free: полностью освободить выделенный буфер.
    void free()
    {
        if( _d.data.addr() != 0 )
            _d.data.DeleteArray();
        _d.size     = 0;
        _d.capacity = 0;
    }

    // Простой итератор в стиле указателя.
    // Действителен, пока AddressManager<T> жив и слот не освобождён.
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
