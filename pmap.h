#pragma once
#include "pvector.h"
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
// Реализован на основе отсортированного pvector<pmap_entry<K, V>> (сортировка при вставке).
// Поиск выполняется бинарным поиском, удаление — линейным.
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

    uintptr_t size_; ///< Текущее число записей; uintptr_t для совместимости с Phase 2
    uintptr_t capacity_; ///< Выделенная ёмкость; uintptr_t для совместимости с Phase 2
    fptr<Entry> data_;   ///< Смещение в ПАП для массива записей; 0 = не выделено

    // lower_bound: найти индекс первой записи с ключом >= k.
    uintptr_t lower_bound( const K& k ) const
    {
        uintptr_t lo = 0, hi = size_;
        while ( lo < hi )
        {
            uintptr_t mid = ( lo + hi ) / 2;
            if ( data_[static_cast<unsigned>( mid )].key < k )
                lo = mid + 1;
            else
                hi = mid;
        }
        return lo;
    }

    // grow: обеспечить ёмкость >= needed, при необходимости перевыделить память.
    //
    // Использует PersistentAddressSpace::Realloc() для расширения на месте,
    // если data_ — последний аллоцированный блок (O(1) без копирования).
    // При невозможности расширить на месте — выделяет новый блок и копирует.
    pmap<K, V>* grow( uintptr_t needed )
    {
        if ( needed <= capacity_ )
            return this;

        uintptr_t new_cap = ( capacity_ == 0 ) ? 4 : capacity_ * 2;
        while ( new_cap < needed )
            new_cap *= 2;

        auto& pam = PersistentAddressSpace::Get();

        uintptr_t old_data_addr = data_.addr();
        uintptr_t self_offset   = pam.PtrToOffset( this );

        // Попытка расширить блок на месте через realloc (если это последний блок).
        if ( old_data_addr != 0 )
        {
            uintptr_t res = pam.Realloc( old_data_addr, capacity_, new_cap, sizeof( Entry ) );
            if ( res != 0 )
            {
                // Расширено на месте — буфер ПАМ мог переместиться, переприводим this.
                pmap<K, V>* self = ( self_offset != 0 ) ? pam.Resolve<pmap<K, V>>( self_offset ) : this;
                self->capacity_  = new_cap;
                return self;
            }
        }

        // Не удалось расширить на месте — выделяем новый блок и копируем.
        fptr<Entry> new_data;
        new_data.NewArray( static_cast<unsigned>( new_cap ) );

        pmap<K, V>* self = ( self_offset != 0 ) ? pam.Resolve<pmap<K, V>>( self_offset ) : this;

        fptr<Entry> old_data;
        old_data.set_addr( old_data_addr );
        for ( uintptr_t i = 0; i < self->size_; i++ )
            new_data[static_cast<unsigned>( i )] = old_data[static_cast<unsigned>( i )];

        if ( old_data_addr != 0 )
            old_data.DeleteArray();

        self->data_     = new_data;
        self->capacity_ = new_cap;
        return self;
    }

  public:
    uintptr_t size() const { return size_; }
    bool      empty() const { return size_ == 0; }

    // insert: добавить или заменить ключ k со значением v.
    // Поддерживает отсортированный порядок.
    //
    // Внимание: grow() может вызвать realloc буфера ПАМ, после чего this
    // становится недействительным. grow() возвращает актуальный self*, который
    // используется для всех последующих обращений к полям.
    void insert( const K& k, const V& v )
    {
        uintptr_t idx = lower_bound( k );
        if ( idx < size_ && !( k < data_[static_cast<unsigned>( idx )].key ) &&
             !( data_[static_cast<unsigned>( idx )].key < k ) )
        {
            // Ключ уже существует — обновляем значение.
            data_[static_cast<unsigned>( idx )].value = v;
            return;
        }
        // Вставляем в позицию idx, сдвигая элементы вправо.
        // grow() возвращает актуальный self* после возможного realloc буфера ПАМ.
        pmap<K, V>* self = grow( size_ + 1 );
        // Сдвигаем элементы вправо.
        for ( uintptr_t i = self->size_; i > idx; i-- )
            self->data_[static_cast<unsigned>( i )] = self->data_[static_cast<unsigned>( i - 1 )];
        self->data_[static_cast<unsigned>( idx )] = Entry{ k, v };
        self->size_++;
    }

    // find: вернуть указатель на значение по ключу k или nullptr, если не найдено.
    V* find( const K& k )
    {
        uintptr_t idx = lower_bound( k );
        if ( idx < size_ && !( k < data_[static_cast<unsigned>( idx )].key ) &&
             !( data_[static_cast<unsigned>( idx )].key < k ) )
            return &data_[static_cast<unsigned>( idx )].value;
        return nullptr;
    }

    const V* find( const K& k ) const
    {
        uintptr_t idx = lower_bound( k );
        if ( idx < size_ && !( k < data_[static_cast<unsigned>( idx )].key ) &&
             !( data_[static_cast<unsigned>( idx )].key < k ) )
            return &data_[static_cast<unsigned>( idx )].value;
        return nullptr;
    }

    // erase: удалить запись с ключом k. Возвращает true, если найдена и удалена.
    bool erase( const K& k )
    {
        uintptr_t idx = lower_bound( k );
        if ( idx >= size_ || ( k < data_[static_cast<unsigned>( idx )].key ) ||
             ( data_[static_cast<unsigned>( idx )].key < k ) )
            return false;
        // Сдвигаем элементы влево.
        for ( uintptr_t i = idx; i + 1 < size_; i++ )
            data_[static_cast<unsigned>( i )] = data_[static_cast<unsigned>( i + 1 )];
        size_--;
        return true;
    }

    // operator[]: вставить значение по умолчанию, если ключ не найден; вернуть ссылку на значение.
    V& operator[]( const K& k )
    {
        uintptr_t idx = lower_bound( k );
        if ( idx < size_ && !( k < data_[static_cast<unsigned>( idx )].key ) &&
             !( data_[static_cast<unsigned>( idx )].key < k ) )
            return data_[static_cast<unsigned>( idx )].value;
        // Вставляем значение по умолчанию.
        V def{};
        insert( k, def );
        idx = lower_bound( k );
        return data_[static_cast<unsigned>( idx )].value;
    }

    // clear: удалить все записи. НЕ освобождает выделенный буфер.
    void clear() { size_ = 0; }

    // free: полностью освободить выделенный буфер.
    void free()
    {
        if ( data_.addr() != 0 )
            data_.DeleteArray();
        size_     = 0;
        capacity_ = 0;
    }

    // Итерация (по объектам Entry в отсортированном по ключу порядке).
    class iterator
    {
        pmap<K, V>* _pm;
        uintptr_t   _idx;

      public:
        iterator( pmap<K, V>* pm, uintptr_t idx ) : _pm( pm ), _idx( idx ) {}
        Entry&    operator*() { return _pm->data_[static_cast<unsigned>( _idx )]; }
        Entry*    operator->() { return &_pm->data_[static_cast<unsigned>( _idx )]; }
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
        const Entry&    operator*() const { return _pm->data_[static_cast<unsigned>( _idx )]; }
        const Entry*    operator->() const { return &_pm->data_[static_cast<unsigned>( _idx )]; }
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
    iterator       end() { return iterator( this, size_ ); }
    const_iterator begin() const { return const_iterator( this, 0 ); }
    const_iterator end() const { return const_iterator( this, size_ ); }

  private:
    // Создание pmap<K,V> на стеке или как статической переменной запрещено.
    // Используйте fptr<pmap<K,V>>::New() для создания в ПАП (Тр.11).
    pmap()  = default;
    ~pmap() = default;

    // Разрешаем доступ к приватному конструктору только для фабричных методов ПАМ.
    template <class U> friend class AddressManager;
    friend class PersistentAddressSpace;
};
