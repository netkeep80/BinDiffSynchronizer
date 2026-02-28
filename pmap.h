#pragma once
#include "pmem_array.h"
#include <cstring>

// pmap<K, V> — персистный ключ-значение контейнер (карта), аналог std::map<K, V>.
//
// Объекты pmap<K, V> могут находиться ТОЛЬКО внутри ПАП.
// Для работы с pmap<K, V> из обычного кода используйте fptr<pmap<K, V>>.
//
// Требования:
//   - Конструктор и деструктор приватные; создание только через ПАМ (Тр.2, Тр.11).
//   - При загрузке образа ПАП конструкторы не вызываются (Тр.10).
//   - K и V должны быть тривиально копируемыми (static_assert ниже).
//   - K должен поддерживать operator< (сравнение).
//   - K должен поддерживать operator== (равенство).
//
// Реализован на основе отсортированного pmem_array<pmap_entry<K, V>> (сортировка при вставке).
// Поиск выполняется бинарным поиском через pmem_array_find_sorted,
// вставка — через pmem_array_insert_sorted, удаление — через pmem_array_erase_at.
//
// Реализация (задача 1.4): pmap<K,V> — тонкая обёртка над pmem_array_hdr.
//   Раскладка полностью совместима с предыдущей версией:
//     [size | capacity | data_off] — 3 * sizeof(uintptr_t).
//   Вся логика grow/copy/sort/search делегируется шаблонным функциям pmem_array.h.
//
// Phase 3: size и capacity обновлены до uintptr_t через pvector (задача 3.3).
//
// Использование:
//   fptr<pmap<int, double>> fm;
//   fm.New();             // выделяем pmap в ПАП
//   fm->insert(1, 3.14);
//   double* v = fm->find(1);   // возвращает указатель на значение или nullptr
//   fm->erase(1);
//   fm->free();
//   fm.Delete();

/// Одна пара ключ-значение в персистной карте.
template <typename K, typename V> struct pmap_entry
{
    K key;   ///< Ключ
    V value; ///< Значение
};

template <typename K, typename V> struct pmap_trivial_check
{
    static_assert( std::is_trivially_copyable<K>::value, "pmap<K,V> требует, чтобы K был тривиально копируемым" );
    static_assert( std::is_trivially_copyable<V>::value, "pmap<K,V> требует, чтобы V был тривиально копируемым" );
    static_assert( std::is_trivially_copyable<pmap_entry<K, V>>::value,
                   "pmap_entry<K,V> должен быть тривиально копируемым" );
};

// pmap<K,V> — персистная карта, живёт только в ПАП.
template <typename K, typename V> class pmap : pmap_trivial_check<K, V>
{
    using Entry = pmap_entry<K, V>;

    // Единственное поле — заголовок pmem_array_hdr (3 * sizeof(uintptr_t)).
    // Раскладка идентична предыдущей версии: [size_ | capacity_ | data_off].
    pmem_array_hdr hdr_; ///< Заголовок персистного массива (size, capacity, data_off)

    // Получить смещение заголовка в ПАП (realloc-безопасно).
    uintptr_t _hdr_off() const
    {
        return PersistentAddressSpace::Get().PtrToOffset( &hdr_ );
    }

    // Вспомогательные функторы для pmem_array_insert_sorted / pmem_array_find_sorted.
    struct KeyOf
    {
        const K& operator()( const Entry& e ) const { return e.key; }
    };

    struct Less
    {
        bool operator()( const K& a, const K& b ) const { return a < b; }
    };

  public:
    uintptr_t size() const { return hdr_.size; }
    bool      empty() const { return hdr_.size == 0; }

    // insert: добавить или заменить ключ k со значением v.
    // Делегирует в pmem_array_insert_sorted для поддержания отсортированного порядка.
    void insert( const K& k, const V& v )
    {
        Entry e;
        e.key   = k;
        e.value = v;
        uintptr_t hdr_off = _hdr_off();
        pmem_array_insert_sorted<Entry, KeyOf, Less>( hdr_off, e, KeyOf{}, Less{} );
    }

    // find: вернуть указатель на значение по ключу k или nullptr, если не найдено.
    V* find( const K& k )
    {
        uintptr_t hdr_off = _hdr_off();
        Entry* e = pmem_array_find_sorted<Entry, K, KeyOf, Less>( hdr_off, k, KeyOf{}, Less{} );
        return ( e != nullptr ) ? &e->value : nullptr;
    }

    const V* find( const K& k ) const
    {
        uintptr_t hdr_off = _hdr_off();
        const Entry* e = pmem_array_find_sorted_const<Entry, K, KeyOf, Less>( hdr_off, k, KeyOf{}, Less{} );
        return ( e != nullptr ) ? &e->value : nullptr;
    }

    // erase: удалить запись с ключом k. Возвращает true, если найдена и удалена.
    bool erase( const K& k )
    {
        uintptr_t hdr_off = _hdr_off();
        // Бинарный поиск для нахождения индекса.
        auto& pam = PersistentAddressSpace::Get();
        pmem_array_hdr* hdr = pam.Resolve<pmem_array_hdr>( hdr_off );
        if ( hdr == nullptr || hdr->size == 0 || hdr->data_off == 0 )
            return false;

        Entry*    raw = pam.Resolve<Entry>( hdr->data_off );
        uintptr_t lo  = 0, hi = hdr->size;
        while ( lo < hi )
        {
            uintptr_t mid = ( lo + hi ) / 2;
            if ( raw[mid].key < k )
                lo = mid + 1;
            else
                hi = mid;
        }
        if ( lo >= hdr->size || ( k < raw[lo].key ) || ( raw[lo].key < k ) )
            return false;

        pmem_array_erase_at<Entry>( hdr_off, lo );
        return true;
    }

    // operator[]: вставить значение по умолчанию, если ключ не найден; вернуть ссылку на значение.
    V& operator[]( const K& k )
    {
        // Пытаемся найти существующий ключ.
        uintptr_t hdr_off = _hdr_off();
        {
            Entry* e = pmem_array_find_sorted<Entry, K, KeyOf, Less>( hdr_off, k, KeyOf{}, Less{} );
            if ( e != nullptr )
                return e->value;
        }
        // Вставляем значение по умолчанию.
        Entry def;
        def.key   = k;
        std::memset( &def.value, 0, sizeof( V ) );
        pmem_array_insert_sorted<Entry, KeyOf, Less>( hdr_off, def, KeyOf{}, Less{} );
        // После insert возможен realloc — обновляем hdr_off и ищем снова.
        hdr_off   = _hdr_off();
        Entry* e2 = pmem_array_find_sorted<Entry, K, KeyOf, Less>( hdr_off, k, KeyOf{}, Less{} );
        return e2->value;
    }

    // clear: удалить все записи. НЕ освобождает выделенный буфер.
    void clear() { hdr_.size = 0; }

    // free: полностью освободить выделенный буфер.
    void free()
    {
        uintptr_t hdr_off = _hdr_off();
        pmem_array_free<Entry>( hdr_off );
    }

    // Итерация (по объектам Entry в отсортированном по ключу порядке).
    class iterator
    {
        pmap<K, V>* _pm;
        uintptr_t   _idx;

      public:
        iterator( pmap<K, V>* pm, uintptr_t idx ) : _pm( pm ), _idx( idx ) {}
        Entry& operator*()
        {
            auto& pam = PersistentAddressSpace::Get();
            Entry* raw = pam.Resolve<Entry>( _pm->hdr_.data_off );
            return raw[_idx];
        }
        Entry* operator->()
        {
            auto& pam = PersistentAddressSpace::Get();
            Entry* raw = pam.Resolve<Entry>( _pm->hdr_.data_off );
            return &raw[_idx];
        }
        iterator& operator++()
        {
            ++_idx;
            return *this;
        }
        iterator operator++( int )
        {
            iterator tmp = *this;
            ++_idx;
            return tmp;
        }
        bool operator==( const iterator& o ) const { return _idx == o._idx; }
        bool operator!=( const iterator& o ) const { return _idx != o._idx; }
    };

    class const_iterator
    {
        const pmap<K, V>* _pm;
        uintptr_t         _idx;

      public:
        const_iterator( const pmap<K, V>* pm, uintptr_t idx ) : _pm( pm ), _idx( idx ) {}
        const Entry& operator*() const
        {
            const auto& pam = PersistentAddressSpace::Get();
            const Entry* raw = pam.Resolve<Entry>( _pm->hdr_.data_off );
            return raw[_idx];
        }
        const Entry* operator->() const
        {
            const auto& pam = PersistentAddressSpace::Get();
            const Entry* raw = pam.Resolve<Entry>( _pm->hdr_.data_off );
            return &raw[_idx];
        }
        const_iterator& operator++()
        {
            ++_idx;
            return *this;
        }
        const_iterator operator++( int )
        {
            const_iterator tmp = *this;
            ++_idx;
            return tmp;
        }
        bool operator==( const const_iterator& o ) const { return _idx == o._idx; }
        bool operator!=( const const_iterator& o ) const { return _idx != o._idx; }
    };

    iterator       begin() { return iterator( this, 0 ); }
    iterator       end() { return iterator( this, hdr_.size ); }
    const_iterator begin() const { return const_iterator( this, 0 ); }
    const_iterator end() const { return const_iterator( this, hdr_.size ); }

  private:
    // Создание pmap<K,V> на стеке или как статической переменной запрещено.
    // Используйте fptr<pmap<K,V>>::New() для создания в ПАП (Тр.11).
    pmap()  = default;
    ~pmap() = default;

    // Разрешаем доступ к приватному конструктору только для фабричных методов ПАМ.
    template <class U> friend class AddressManager;
    friend class PersistentAddressSpace;
};
