#pragma once
#include "pmem_array.h"

// pvector<T> — персистный динамический массив, аналог std::vector<T>.
//
// Объекты pvector<T> могут находиться ТОЛЬКО внутри ПАП.
// Для работы с pvector<T> из обычного кода используйте fptr<pvector<T>>.
//
// Требования:
//   - Конструктор и деструктор приватные; создание только через ПАМ (Тр.2, Тр.11).
//   - При загрузке образа ПАП конструкторы не вызываются (Тр.10).
//   - T должен быть тривиально копируемым (static_assert ниже).
//   - Стратегия роста: удвоение ёмкости при заполнении (начальная ёмкость: 4).
//   - Пустой pvector имеет data.addr() == 0, size == 0, capacity == 0.
//
// Реализация (задача 1.3): pvector<T> — тонкая обёртка над pmem_array_hdr.
//   Раскладка полностью совместима с предыдущей версией:
//     [size | capacity | data_off] — 3 * sizeof(uintptr_t).
//   Вся логика grow/copy/sync делегируется шаблонным функциям pmem_array.h.
//
// Phase 3: поля size и capacity имеют тип uintptr_t для полной совместимости
//   с Phase 2 PAM API (PersistentAddressSpace использует uintptr_t).
//
// Использование:
//   fptr<pvector<int>> fv;
//   fv.New();          // выделяем pvector<int> в ПАП (нулевая инициализация)
//   fv->push_back(1);
//   fv->push_back(2);
//   // fv->size() == 2, (*fv)[0] == 1
//   fv->free();
//   fv.Delete();

template <typename T> class pvector
{
    static_assert( std::is_trivially_copyable<T>::value, "pvector<T> требует, чтобы T был тривиально копируемым" );

    // Единственное поле — заголовок pmem_array_hdr (3 * sizeof(uintptr_t)).
    // Раскладка идентична предыдущей версии: [size_ | capacity_ | data_off].
    // Это гарантирует совместимость с pjson.h (array_layout, object_layout).
    pmem_array_hdr hdr_; ///< Заголовок персистного массива (size, capacity, data_off)

    // Вспомогательный метод: получить смещение заголовка в ПАП.
    // Используется для realloc-безопасного доступа к полям через pmem_array_*.
    uintptr_t _hdr_off() const
    {
        return PersistentAddressSpace::Get().PtrToOffset( &hdr_ );
    }

  public:
    uintptr_t size() const { return hdr_.size; }
    uintptr_t capacity() const { return hdr_.capacity; }
    bool      empty() const { return hdr_.size == 0; }

    // push_back: добавить элемент val в конец массива.
    // Делегирует grow/alloc в pmem_array_reserve, затем записывает значение.
    void push_back( const T& val )
    {
        auto& pam         = PersistentAddressSpace::Get();
        uintptr_t hdr_off = pam.PtrToOffset( &hdr_ );

        // Резервируем место для нового элемента (может вызвать realloc).
        pmem_array_reserve<T>( hdr_off, hdr_.size + 1 );

        // После reserve this мог переместиться — повторно разрешаем.
        pvector<T>* self = ( hdr_off != 0 ) ? pam.Resolve<pvector<T>>( hdr_off ) : this;
        T* raw           = pam.Resolve<T>( self->hdr_.data_off );
        raw[self->hdr_.size] = val;
        self->hdr_.size++;
    }

    void pop_back()
    {
        if ( hdr_.size > 0 )
            hdr_.size--;
    }

    T& operator[]( uintptr_t idx )
    {
        auto& pam = PersistentAddressSpace::Get();
        T* raw    = pam.Resolve<T>( hdr_.data_off );
        return raw[idx];
    }

    const T& operator[]( uintptr_t idx ) const
    {
        const auto& pam = PersistentAddressSpace::Get();
        const T* raw    = pam.Resolve<T>( hdr_.data_off );
        return raw[idx];
    }

    T& front()
    {
        auto& pam = PersistentAddressSpace::Get();
        T* raw    = pam.Resolve<T>( hdr_.data_off );
        return raw[0];
    }

    const T& front() const
    {
        const auto& pam = PersistentAddressSpace::Get();
        const T* raw    = pam.Resolve<T>( hdr_.data_off );
        return raw[0];
    }

    T& back()
    {
        auto& pam = PersistentAddressSpace::Get();
        T* raw    = pam.Resolve<T>( hdr_.data_off );
        return raw[hdr_.size - 1];
    }

    const T& back() const
    {
        const auto& pam = PersistentAddressSpace::Get();
        const T* raw    = pam.Resolve<T>( hdr_.data_off );
        return raw[hdr_.size - 1];
    }

    // clear: обнулить размер. НЕ освобождает выделенный буфер.
    void clear() { hdr_.size = 0; }

    // free: полностью освободить выделенный буфер.
    void free()
    {
        uintptr_t hdr_off = _hdr_off();
        pmem_array_free<T>( hdr_off );
    }

    // Простой итератор в стиле указателя.
    // Действителен, пока ПАМ жив и слот не освобождён.
    class iterator
    {
        pvector<T>* _pv;
        uintptr_t   _idx;

      public:
        iterator( pvector<T>* pv, uintptr_t idx ) : _pv( pv ), _idx( idx ) {}
        T&        operator*() { return ( *_pv )[_idx]; }
        T*        operator->() { return &( *_pv )[_idx]; }
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
        const pvector<T>* _pv;
        uintptr_t         _idx;

      public:
        const_iterator( const pvector<T>* pv, uintptr_t idx ) : _pv( pv ), _idx( idx ) {}
        const T&        operator*() const { return ( *_pv )[_idx]; }
        const T*        operator->() const { return &( *_pv )[_idx]; }
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
    const_iterator cbegin() const { return const_iterator( this, 0 ); }
    const_iterator cend() const { return const_iterator( this, hdr_.size ); }

  private:
    // Создание pvector<T> на стеке или как статической переменной запрещено.
    // Используйте fptr<pvector<T>>::New() для создания в ПАП (Тр.11).
    pvector()  = default;
    ~pvector() = default;

    // Разрешаем доступ к приватному конструктору только для фабричных методов ПАМ.
    template <class U> friend class AddressManager;
    friend class PersistentAddressSpace;
};

// Phase 3: проверяем, что поля size_ и capacity_ имеют размер void*.
template <typename T> struct pvector_size_check
{
    static_assert( sizeof( pvector<T> ) == 3 * sizeof( void* ),
                   "pvector<T> должен занимать 3 * sizeof(void*) байт (Phase 3)" );
};

// Проверяем для конкретных типов.
static_assert( sizeof( pvector<int> ) == 3 * sizeof( void* ),
               "pvector<int> должен занимать 3 * sizeof(void*) байт (Phase 3)" );
static_assert( sizeof( pvector<double> ) == 3 * sizeof( void* ),
               "pvector<double> должен занимать 3 * sizeof(void*) байт (Phase 3)" );

// Примечание: pvector<T> НЕ является тривиально копируемым (private конструктор/деструктор),
// но это допустимо, поскольку ПАМ выделяет сырую память без вызова конструкторов (Тр.10).
// pvector<T> хранится в ПАП как сырые байты, без вызова конструктора/деструктора.
