#pragma once
/**
 * @file pmap_pmm.h
 * @brief Каноническая реализация персистной карты на базе PMM.
 *
 * pmap_pmm<K, V> — персистный sorted-array контейнер, аналог std::map<K, V>.
 * Внутренне использует PamManager::parray<Entry> для хранения.
 *
 * @see parray — pmm::parray<T, ManagerT> (persistent dynamic array)
 */

#include "pam_pmm_config.h"
#include <cstring>
#include <type_traits>

namespace pjson
{

// ═══════════════════════════════════════════════════════════════════════════
// pmap_entry_pmm<K, V> — Одна пара ключ-значение в персистной карте
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Одна пара ключ-значение в персистной карте PMM.
 *
 * @tparam K Тип ключа. Должен быть тривиально копируемым и поддерживать operator<.
 * @tparam V Тип значения. Должен быть тривиально копируемым.
 */
template <typename K, typename V> struct pmap_entry_pmm
{
    K key;   ///< Ключ
    V value; ///< Значение
};

// ═══════════════════════════════════════════════════════════════════════════
// Проверки типов
// ═══════════════════════════════════════════════════════════════════════════

template <typename K, typename V> struct pmap_pmm_trivial_check
{
    static_assert( std::is_trivially_copyable<K>::value, "pmap_pmm<K,V> требует, чтобы K был тривиально копируемым" );
    static_assert( std::is_trivially_copyable<V>::value, "pmap_pmm<K,V> требует, чтобы V был тривиально копируемым" );
    static_assert( std::is_trivially_copyable<pmap_entry_pmm<K, V>>::value,
                   "pmap_entry_pmm<K,V> должен быть тривиально копируемым" );
};

// ═══════════════════════════════════════════════════════════════════════════
// pmap_pmm<K, V> — Персистная карта на базе PMM
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Персистная карта (key-value map) на базе PMM.
 *
 * Аналог std::map<K, V>, но все данные хранятся в персистном адресном
 * пространстве, управляемом PersistMemoryManager.
 *
 * Реализована как sorted array пар {K, V}. Поиск — бинарный O(log n).
 * Вставка и удаление — O(n) из-за сдвигов.
 * Внутренне использует PamManager::parray<Entry> для хранения.
 *
 * @tparam K Тип ключа. Должен быть тривиально копируемым и поддерживать operator<.
 * @tparam V Тип значения. Должен быть тривиально копируемым.
 *
 * Примечание: pmap_pmm<K,V> предназначен для использования через смещение
 * (offset), а не через сырой указатель. Создание на стеке не рекомендуется.
 */
template <typename K, typename V> class pmap_pmm : pmap_pmm_trivial_check<K, V>
{
    using Entry = pmap_entry_pmm<K, V>;

    PamManager::parray<Entry> arr_{}; ///< Внутренний parray (инициализирован нулями)

    struct Less
    {
        bool operator()( const K& a, const K& b ) const { return a < b; }
    };

  public:
    /**
     * @brief Получить текущий размер карты (количество элементов).
     */
    uintptr_t size() const { return static_cast<uintptr_t>( arr_.size() ); }

    /**
     * @brief Проверить, пуста ли карта.
     */
    bool empty() const { return arr_.empty(); }

    /**
     * @brief Вставить или обновить пару ключ-значение.
     *
     * Если ключ уже существует — значение обновляется.
     * Если ключ новый — вставляется в отсортированном порядке.
     *
     * @param k Ключ.
     * @param v Значение.
     * @param self_off Байтовое смещение этого pmap_pmm в ПАП (игнорируется, сохранён для совместимости).
     */
    void insert( const K& k, const V& v, uintptr_t self_off ) { _insert_impl( k, v, self_off ); }

    /**
     * @brief Вставить или обновить пару.
     *
     * @param k Ключ.
     * @param v Значение.
     */
    void insert( const K& k, const V& v )
    {
        // Вычисляем смещение this в PMM для безопасной повторной резолвации
        // после возможного роста пула.
        std::uint8_t* base = PamManager::backend().base_ptr();
        uintptr_t     self_off =
            ( base != nullptr ) ? static_cast<uintptr_t>( reinterpret_cast<std::uint8_t*>( this ) - base ) : 0;
        _insert_impl( k, v, self_off );
    }

  private:
    /**
     * @brief Внутренняя реализация вставки, безопасная при росте PMM-пула.
     *
     * Когда pmap_pmm хранится внутри PMM-пула, операция push_back может
     * вызвать рост пула (malloc+memcpy+free), после чего указатель this
     * становится невалидным. Поэтому после push_back необходимо повторно
     * разрешить self_off для получения актуального this.
     */
    void _insert_impl( const K& k, const V& v, uintptr_t self_off )
    {
        uintptr_t sz = arr_.size();
        uintptr_t lo = 0, hi = sz;

        // Бинарный поиск (lower_bound)
        {
            const Entry* raw = arr_.data();
            if ( raw != nullptr )
            {
                while ( lo < hi )
                {
                    uintptr_t mid = ( lo + hi ) / 2;
                    if ( Less{}( raw[mid].key, k ) )
                        lo = mid + 1;
                    else
                        hi = mid;
                }
            }
        }

        uintptr_t idx = lo;

        // Проверяем, существует ли уже такой ключ
        {
            const Entry* raw = arr_.data();
            if ( raw != nullptr && idx < sz && !Less{}( k, raw[idx].key ) && !Less{}( raw[idx].key, k ) )
            {
                // Ключ найден — обновляем значение
                Entry updated;
                updated.key   = k;
                updated.value = v;
                arr_.set( idx, updated );
                return;
            }
        }

        // Вставляем новый элемент: push_back может вызвать рост PMM-пула,
        // что инвалидирует this. Сохраняем состояние arr_ и выполняем
        // push_back через копию, затем записываем результат обратно.
        {
            PamManager::parray<Entry> arr_copy = arr_;
            Entry                     empty_entry{};
            arr_copy.push_back( empty_entry );

            // После push_back пул мог вырасти — перечитываем this.
            pmap_pmm<K, V>* self =
                ( self_off != 0 ) ? reinterpret_cast<pmap_pmm<K, V>*>( PamManager::backend().base_ptr() + self_off )
                                  : this;
            self->arr_ = arr_copy;

            Entry* raw = self->arr_.data();
            if ( raw != nullptr && sz > idx )
                std::memmove( raw + idx + 1, raw + idx, ( sz - idx ) * sizeof( Entry ) );

            if ( raw != nullptr )
            {
                raw[idx].key   = k;
                raw[idx].value = v;
            }
        }
    }

  public:
    /// Псевдоним для insert(k, v).
    void insert_direct( const K& k, const V& v ) { insert( k, v ); }

    /**
     * @brief Найти значение по ключу.
     *
     * @param k Ключ для поиска.
     * @return V* — указатель на значение или nullptr, если не найдено.
     */
    V* find( const K& k )
    {
        if ( arr_.empty() )
            return nullptr;

        Entry* raw = arr_.data();
        if ( raw == nullptr )
            return nullptr;
        uintptr_t lo = 0, hi = arr_.size();
        while ( lo < hi )
        {
            uintptr_t mid = ( lo + hi ) / 2;
            if ( Less{}( raw[mid].key, k ) )
                lo = mid + 1;
            else
                hi = mid;
        }

        if ( lo < arr_.size() && !Less{}( k, raw[lo].key ) && !Less{}( raw[lo].key, k ) )
            return &raw[lo].value;

        return nullptr;
    }

    /**
     * @brief Найти значение по ключу (const версия).
     */
    const V* find( const K& k ) const
    {
        if ( arr_.empty() )
            return nullptr;

        const Entry* raw = arr_.data();
        if ( raw == nullptr )
            return nullptr;
        uintptr_t lo = 0, hi = arr_.size();
        while ( lo < hi )
        {
            uintptr_t mid = ( lo + hi ) / 2;
            if ( Less{}( raw[mid].key, k ) )
                lo = mid + 1;
            else
                hi = mid;
        }

        if ( lo < arr_.size() && !Less{}( k, raw[lo].key ) && !Less{}( raw[lo].key, k ) )
            return &raw[lo].value;

        return nullptr;
    }

    /**
     * @brief Удалить элемент по ключу.
     *
     * @param k Ключ удаляемого элемента.
     * @return true если элемент найден и удалён, false иначе.
     */
    bool erase( const K& k )
    {
        if ( arr_.empty() )
            return false;

        Entry* raw = arr_.data();
        if ( raw == nullptr )
            return false;
        uintptr_t lo = 0, hi = arr_.size();
        while ( lo < hi )
        {
            uintptr_t mid = ( lo + hi ) / 2;
            if ( Less{}( raw[mid].key, k ) )
                lo = mid + 1;
            else
                hi = mid;
        }

        if ( lo >= arr_.size() || Less{}( k, raw[lo].key ) || Less{}( raw[lo].key, k ) )
            return false;

        // Сдвигаем элементы [lo+1..size-1] влево
        uintptr_t sz = arr_.size();
        if ( lo + 1 < sz )
            std::memmove( raw + lo, raw + lo + 1, ( sz - lo - 1 ) * sizeof( Entry ) );

        arr_.pop_back();
        return true;
    }

    /**
     * @brief Доступ по ключу с автоматической вставкой.
     *
     * Если ключ не существует — вставляется с value, инициализированным нулями.
     *
     * @param k Ключ.
     * @return V& — ссылка на значение.
     */
    V& operator[]( const K& k )
    {
        // Пытаемся найти существующий ключ
        V* existing = find( k );
        if ( existing != nullptr )
            return *existing;

        // Вставляем значение по умолчанию
        V def;
        std::memset( &def, 0, sizeof( V ) );
        insert( k, def );

        // После вставки ищем снова (может быть realloc)
        V* inserted = find( k );
        return *inserted;
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
     * @brief Итератор для pmap_pmm.
     */
    class iterator
    {
        pmap_pmm<K, V>* _pm;
        uintptr_t       _idx;

      public:
        iterator( pmap_pmm<K, V>* pm, uintptr_t idx ) : _pm( pm ), _idx( idx ) {}

        Entry& operator*()
        {
            Entry* raw = _pm->arr_.data();
            return raw[_idx];
        }

        Entry* operator->()
        {
            Entry* raw = _pm->arr_.data();
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

    /**
     * @brief Константный итератор для pmap_pmm.
     */
    class const_iterator
    {
        const pmap_pmm<K, V>* _pm;
        uintptr_t             _idx;

      public:
        const_iterator( const pmap_pmm<K, V>* pm, uintptr_t idx ) : _pm( pm ), _idx( idx ) {}

        const Entry& operator*() const
        {
            const Entry* raw = _pm->arr_.data();
            return raw[_idx];
        }

        const Entry* operator->() const
        {
            const Entry* raw = _pm->arr_.data();
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
    iterator       end() { return iterator( this, arr_.size() ); }
    const_iterator begin() const { return const_iterator( this, 0 ); }
    const_iterator end() const { return const_iterator( this, arr_.size() ); }
    const_iterator cbegin() const { return const_iterator( this, 0 ); }
    const_iterator cend() const { return const_iterator( this, arr_.size() ); }

  public:
    // Конструктор по умолчанию — public для совместимости с PMM::allocate_typed.
    pmap_pmm()  = default;
    ~pmap_pmm() = default;
};

// ═══════════════════════════════════════════════════════════════════════════
// Проверки тривиальной копируемости
// ═══════════════════════════════════════════════════════════════════════════

static_assert( std::is_trivially_copyable<pmap_pmm<int, int>>::value,
               "pmap_pmm<int,int> должен быть тривиально копируемым" );

} // namespace pjson
