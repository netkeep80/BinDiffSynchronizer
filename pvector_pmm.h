#pragma once
/**
 * @file pvector_pmm.h
 * @brief Реализация pvector на базе PersistMemoryManager.
 *
 * pvector_pmm<T> — персистный динамический массив, тонкая обёртка
 * над pmem_array_hdr_pmm. Все операции делегируются функциям из pmem_array_pmm.h.
 *
 * Особенности:
 *   - Использует PMM для аллокации/деаллокации
 *   - Требует инициализации PamManager::create() перед использованием
 *   - Смещения кратны размеру гранулы PMM (16 байт)
 *
 * @see pmem_array_pmm.h — примитив персистного массива на базе PMM
 */

#include "pmem_array_pmm.h"

namespace pjson
{

/**
 * @brief Персистный динамический массив на базе PMM.
 *
 * Аналог std::vector<T>, но все данные хранятся в персистном адресном
 * пространстве, управляемом PersistMemoryManager.
 *
 * @tparam T Тип элементов. Должен быть тривиально копируемым.
 *
 * Примечание: pvector_pmm<T> предназначен для использования через смещение
 * (offset), а не через сырой указатель. Создание на стеке не рекомендуется.
 */
template <typename T> class pvector_pmm
{
    static_assert( std::is_trivially_copyable<T>::value, "pvector_pmm<T> требует, чтобы T был тривиально копируемым" );

    // Единственное поле — заголовок pmem_array_hdr_pmm (3 * sizeof(uintptr_t)).
    // NOLINTBEGIN(cppcoreguidelines-pro-type-member-init)
    pmem_array_hdr_pmm hdr_{}; ///< Заголовок персистного массива (инициализирован нулями)
                               // NOLINTEND(cppcoreguidelines-pro-type-member-init)

  public:
    /**
     * @brief Получить текущий размер массива.
     */
    uintptr_t size() const { return hdr_.size; }

    /**
     * @brief Получить текущую ёмкость массива.
     */
    uintptr_t capacity() const { return hdr_.capacity; }

    /**
     * @brief Проверить, пуст ли массив.
     */
    bool empty() const { return hdr_.size == 0; }

    /**
     * @brief Добавить элемент в конец массива.
     *
     * Самостоятельно управляет ростом буфера (удвоение ёмкости).
     *
     * @param val Добавляемое значение.
     */
    void push_back( const T& val )
    {
        // Резервируем место
        uintptr_t cur_cap  = hdr_.capacity;
        uintptr_t new_size = hdr_.size + 1;

        if ( new_size > cur_cap )
        {
            uintptr_t new_cap = ( cur_cap == 0 ) ? 4 : cur_cap * 2;
            while ( new_cap < new_size )
                new_cap *= 2;

            // Выделяем новый блок
            auto new_data_pptr = PamManager::template allocate_typed<T>( new_cap );
            if ( new_data_pptr.is_null() )
                return;

            uintptr_t new_data_off = pptr_to_offset( new_data_pptr );

            // Копируем существующие элементы
            if ( hdr_.data_off != 0 && hdr_.size > 0 )
            {
                T*       new_raw = new_data_pptr.resolve();
                const T* old_raw = pmm_resolve_const<T>( hdr_.data_off );
                if ( new_raw != nullptr && old_raw != nullptr )
                    std::memcpy( new_raw, old_raw, hdr_.size * sizeof( T ) );
            }

            // Освобождаем старый буфер
            if ( hdr_.data_off != 0 )
            {
                auto old_pptr = offset_to_pptr<T>( hdr_.data_off );
                PamManager::template deallocate_typed<T>( old_pptr );
            }

            hdr_.data_off = new_data_off;
            hdr_.capacity = new_cap;
        }

        // Добавляем элемент
        T* raw         = pmm_resolve<T>( hdr_.data_off );
        raw[hdr_.size] = val;
        hdr_.size++;
    }

    /**
     * @brief Удалить последний элемент.
     */
    void pop_back()
    {
        if ( hdr_.size > 0 )
            hdr_.size--;
    }

    /**
     * @brief Доступ к элементу по индексу.
     *
     * @warning Не проверяет границы.
     */
    T& operator[]( uintptr_t idx )
    {
        T* raw = pmm_resolve<T>( hdr_.data_off );
        return raw[idx];
    }

    /**
     * @brief Доступ к элементу по индексу (const версия).
     */
    const T& operator[]( uintptr_t idx ) const
    {
        const T* raw = pmm_resolve_const<T>( hdr_.data_off );
        return raw[idx];
    }

    /**
     * @brief Доступ к первому элементу.
     */
    T& front()
    {
        T* raw = pmm_resolve<T>( hdr_.data_off );
        return raw[0];
    }

    const T& front() const
    {
        const T* raw = pmm_resolve_const<T>( hdr_.data_off );
        return raw[0];
    }

    /**
     * @brief Доступ к последнему элементу.
     */
    T& back()
    {
        T* raw = pmm_resolve<T>( hdr_.data_off );
        return raw[hdr_.size - 1];
    }

    const T& back() const
    {
        const T* raw = pmm_resolve_const<T>( hdr_.data_off );
        return raw[hdr_.size - 1];
    }

    /**
     * @brief Обнулить размер. Не освобождает выделенный буфер.
     */
    void clear() { hdr_.size = 0; }

    /**
     * @brief Полностью освободить выделенный буфер.
     */
    void free()
    {
        if ( hdr_.data_off != 0 )
        {
            auto pptr = offset_to_pptr<T>( hdr_.data_off );
            PamManager::template deallocate_typed<T>( pptr );
        }
        hdr_.size     = 0;
        hdr_.capacity = 0;
        hdr_.data_off = 0;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Итераторы
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Итератор для pvector_pmm.
     */
    class iterator
    {
        pvector_pmm<T>* _pv;
        uintptr_t       _idx;

      public:
        iterator( pvector_pmm<T>* pv, uintptr_t idx ) : _pv( pv ), _idx( idx ) {}
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

    /**
     * @brief Константный итератор для pvector_pmm.
     */
    class const_iterator
    {
        const pvector_pmm<T>* _pv;
        uintptr_t             _idx;

      public:
        const_iterator( const pvector_pmm<T>* pv, uintptr_t idx ) : _pv( pv ), _idx( idx ) {}
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
    // Создание pvector_pmm<T> на стеке запрещено.
    // Используйте fptr<pvector_pmm<T>>::New() для создания в ПАП.
    pvector_pmm()  = default;
    ~pvector_pmm() = default;

    // Разрешаем доступ к приватному конструктору только для фабричных методов ПАМ.
    template <typename U> friend class fptr_pmm;
};

// Проверка размера
static_assert( sizeof( pvector_pmm<int> ) == 3 * sizeof( void* ),
               "pvector_pmm<int> должен занимать 3 * sizeof(void*) байт" );
static_assert( sizeof( pvector_pmm<double> ) == 3 * sizeof( void* ),
               "pvector_pmm<double> должен занимать 3 * sizeof(void*) байт" );

} // namespace pjson
