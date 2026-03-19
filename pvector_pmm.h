#pragma once
/**
 * @file pvector_pmm.h
 * @brief Реализация pvector на базе PersistMemoryManager.
 *
 * pvector_pmm<T> — персистный динамический массив, тонкая обёртка
 * над PamManager::parray<T>. Все операции делегируются методам parray.
 *
 * Особенности:
 *   - Использует PMM для аллокации/деаллокации
 *   - Требует инициализации PamManager::create() перед использованием
 *   - Смещения кратны размеру гранулы PMM (16 байт)
 *
 * @see parray — pmm::parray<T, ManagerT> (persistent dynamic array)
 */

#include "pam_pmm_config.h"

namespace pjson
{

/**
 * @brief Персистный динамический массив на базе PMM.
 *
 * Аналог std::vector<T>, но все данные хранятся в персистном адресном
 * пространстве, управляемом PersistMemoryManager.
 * Тонкая обёртка над PamManager::parray<T>.
 *
 * @tparam T Тип элементов. Должен быть тривиально копируемым.
 *
 * Примечание: pvector_pmm<T> предназначен для использования через смещение
 * (offset), а не через сырой указатель. Создание на стеке не рекомендуется.
 */
template <typename T> class pvector_pmm
{
    static_assert( std::is_trivially_copyable<T>::value, "pvector_pmm<T> требует, чтобы T был тривиально копируемым" );

    PamManager::parray<T> arr_{}; ///< Внутренний parray (инициализирован нулями)

  public:
    /**
     * @brief Получить текущий размер массива.
     */
    uintptr_t size() const { return static_cast<uintptr_t>( arr_.size() ); }

    /**
     * @brief Получить текущую ёмкость массива.
     */
    uintptr_t capacity() const { return static_cast<uintptr_t>( arr_.capacity() ); }

    /**
     * @brief Проверить, пуст ли массив.
     */
    bool empty() const { return arr_.empty(); }

    /**
     * @brief Добавить элемент в конец массива.
     *
     * Самостоятельно управляет ростом буфера (удвоение ёмкости).
     *
     * @param val Добавляемое значение.
     */
    void push_back( const T& val ) { arr_.push_back( val ); }

    /**
     * @brief Удалить последний элемент.
     */
    void pop_back() { arr_.pop_back(); }

    /**
     * @brief Доступ к элементу по индексу.
     *
     * @warning Не проверяет границы.
     */
    T& operator[]( uintptr_t idx )
    {
        T* raw = arr_.data();
        return raw[idx];
    }

    /**
     * @brief Доступ к элементу по индексу (const версия).
     */
    const T& operator[]( uintptr_t idx ) const
    {
        const T* raw = arr_.data();
        return raw[idx];
    }

    /**
     * @brief Доступ к первому элементу.
     */
    T& front()
    {
        T* raw = arr_.data();
        return raw[0];
    }

    const T& front() const
    {
        const T* raw = arr_.data();
        return raw[0];
    }

    /**
     * @brief Доступ к последнему элементу.
     */
    T& back()
    {
        T* raw = arr_.data();
        return raw[arr_.size() - 1];
    }

    const T& back() const
    {
        const T* raw = arr_.data();
        return raw[arr_.size() - 1];
    }

    /**
     * @brief Обнулить размер. Не освобождает выделенный буфер.
     */
    void clear() { arr_.clear(); }

    /**
     * @brief Полностью освободить выделенный буфер.
     */
    void free() { arr_.free_data(); }

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
    iterator       end() { return iterator( this, arr_.size() ); }
    const_iterator begin() const { return const_iterator( this, 0 ); }
    const_iterator end() const { return const_iterator( this, arr_.size() ); }
    const_iterator cbegin() const { return const_iterator( this, 0 ); }
    const_iterator cend() const { return const_iterator( this, arr_.size() ); }

  private:
    // Создание pvector_pmm<T> на стеке запрещено.
    // Используйте fptr<pvector_pmm<T>>::New() для создания в ПАП.
    pvector_pmm()  = default;
    ~pvector_pmm() = default;

    // Разрешаем доступ к приватному конструктору только для фабричных методов ПАМ.
    template <typename U> friend class fptr_pmm;
};

// Проверка тривиальной копируемости
static_assert( std::is_trivially_copyable<pvector_pmm<int>>::value,
               "pvector_pmm<int> должен быть тривиально копируемым" );

} // namespace pjson
