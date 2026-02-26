#pragma once
#include "pvector.h"
#include <cstring>

// pmap<K, V> — персистный ключ-значение контейнер (карта).
//
// Реализован на основе отсортированного pvector<pmap_entry<K, V>> (сортировка при вставке).
// Поиск выполняется бинарным поиском, удаление — линейным.
//
// Ограничения:
//   - K и V должны быть тривиально копируемыми (static_assert ниже).
//   - K должен поддерживать operator< (сравнение).
//   - K должен поддерживать operator== (равенство).
//   - pmap_data<K, V> тривиально копируем.
//
// Использование:
//   pmap_data<int, double> md{};
//   pmap<int, double> m(md);
//   m.insert(1, 3.14);
//   double* v = m.find(1);   // возвращает указатель на значение или nullptr
//   m.erase(1);

/// Одна пара ключ-значение в персистной карте.
template<typename K, typename V>
struct pmap_entry
{
    K key;   ///< Ключ
    V value; ///< Значение
};

// pmap_data<K,V> — обёртка над pvector_data<pmap_entry<K,V>>.
// Тривиально копируем при тривиально копируемых K и V.
template<typename K, typename V>
using pmap_data = pvector_data<pmap_entry<K, V>>;

template<typename K, typename V>
struct pmap_trivial_check
{
    static_assert(std::is_trivially_copyable<K>::value,
                  "pmap<K,V> требует, чтобы K был тривиально копируемым");
    static_assert(std::is_trivially_copyable<V>::value,
                  "pmap<K,V> требует, чтобы V был тривиально копируемым");
    static_assert(std::is_trivially_copyable<pmap_entry<K,V>>::value,
                  "pmap_entry<K,V> должен быть тривиально копируемым");
    static_assert(std::is_trivially_copyable<pmap_data<K,V>>::value,
                  "pmap_data<K,V> должен быть тривиально копируемым для использования с persist<T>");
};

// pmap — тонкая не-владеющая обёртка над ссылкой pmap_data<K,V>.
// Владелец pmap_data — вызывающий код (как правило, persist<pmap_data<K,V>>).
template<typename K, typename V>
class pmap : pmap_trivial_check<K, V>
{
    using Entry  = pmap_entry<K, V>;
    using VecData = pvector_data<Entry>;

    VecData& _vd;
    pvector<Entry> _vec;

    // lower_bound: найти индекс первой записи с ключом >= k.
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

    // insert: добавить или заменить ключ k со значением v.
    // Поддерживает отсортированный порядок.
    void insert(const K& k, const V& v)
    {
        unsigned idx = lower_bound(k);
        if( idx < _vd.size && !( k < _vd.data[idx].key ) && !( _vd.data[idx].key < k ) )
        {
            // Ключ уже существует — обновляем значение.
            _vd.data[idx].value = v;
            return;
        }
        // Вставляем в позицию idx, сдвигая элементы вправо.
        _vec.push_back(Entry{});   // обеспечиваем ёмкость, добавляем заглушку
        for( unsigned i = _vd.size - 1; i > idx; i-- )
            _vd.data[i] = _vd.data[i - 1];
        _vd.data[idx] = Entry{ k, v };
    }

    // find: вернуть указатель на значение по ключу k или nullptr, если не найдено.
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

    // erase: удалить запись с ключом k. Возвращает true, если найдена и удалена.
    bool erase(const K& k)
    {
        unsigned idx = lower_bound(k);
        if( idx >= _vd.size || ( k < _vd.data[idx].key ) || ( _vd.data[idx].key < k ) )
            return false;
        // Сдвигаем элементы влево.
        for( unsigned i = idx; i + 1 < _vd.size; i++ )
            _vd.data[i] = _vd.data[i + 1];
        _vd.size--;
        return true;
    }

    // operator[]: вставить значение по умолчанию, если ключ не найден; вернуть ссылку на значение.
    V& operator[](const K& k)
    {
        unsigned idx = lower_bound(k);
        if( idx < _vd.size && !( k < _vd.data[idx].key ) && !( _vd.data[idx].key < k ) )
            return _vd.data[idx].value;
        // Вставляем значение по умолчанию.
        V def{};
        insert(k, def);
        idx = lower_bound(k);
        return _vd.data[idx].value;
    }

    // clear: удалить все записи. НЕ освобождает выделенный буфер.
    void clear() { _vec.clear(); }

    // free: полностью освободить выделенный буфер.
    void free() { _vec.free(); }

    // Итерация (по объектам Entry в отсортированном по ключу порядке).
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
