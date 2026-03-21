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
     * @brief Бинарный поиск (lower_bound) по отсортированному массиву.
     *
     * @param raw Указатель на массив записей.
     * @param sz  Количество элементов.
     * @param k   Искомый ключ.
     * @return Индекс первого элемента >= k.
     */
    static uintptr_t _lower_bound( const Entry* raw, uintptr_t sz, const K& k )
    {
        uintptr_t lo = 0, hi = sz;
        while ( lo < hi )
        {
            uintptr_t mid = ( lo + hi ) / 2;
            if ( Less{}( raw[mid].key, k ) )
                lo = mid + 1;
            else
                hi = mid;
        }
        return lo;
    }

    /**
     * @brief Внутренняя реализация вставки, безопасная при росте PMM-пула.
     *
     * Когда pmap_pmm хранится внутри PMM-пула, операция insert может
     * вызвать рост пула (malloc+memcpy+free), после чего указатель this
     * становится невалидным. Поэтому insert выполняется на стековой копии
     * arr_, а результат записывается обратно после повторной резолвации self_off.
     */
    void _insert_impl( const K& k, const V& v, uintptr_t self_off )
    {
        uintptr_t sz  = arr_.size();
        uintptr_t idx = 0;

        {
            const Entry* raw = arr_.data();
            if ( raw != nullptr )
                idx = _lower_bound( raw, sz, k );
        }

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

        // Вставляем новый элемент: insert может вызвать рост PMM-пула,
        // что инвалидирует this. Выполняем insert на стековой копии arr_,
        // затем записываем результат обратно после повторной резолвации.
        {
            PamManager::parray<Entry> arr_copy = arr_;
            Entry                     new_entry{};
            new_entry.key   = k;
            new_entry.value = v;
            arr_copy.insert( idx, new_entry );

            // После insert пул мог вырасти — перечитываем this.
            pmap_pmm<K, V>* self =
                ( self_off != 0 ) ? reinterpret_cast<pmap_pmm<K, V>*>( PamManager::backend().base_ptr() + self_off )
                                  : this;
            self->arr_ = arr_copy;
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
    V* find( const K& k ) { return const_cast<V*>( static_cast<const pmap_pmm*>( this )->find( k ) ); }

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

        uintptr_t sz = arr_.size();
        uintptr_t lo = _lower_bound( raw, sz, k );

        if ( lo < sz && !Less{}( k, raw[lo].key ) && !Less{}( raw[lo].key, k ) )
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

        const Entry* raw = arr_.data();
        if ( raw == nullptr )
            return false;

        uintptr_t sz = arr_.size();
        uintptr_t lo = _lower_bound( raw, sz, k );

        if ( lo >= sz || Less{}( k, raw[lo].key ) || Less{}( raw[lo].key, k ) )
            return false;

        arr_.erase( lo );
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

        // Вставляем значение по умолчанию (нулевая инициализация)
        V def{};
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
     * @brief Шаблонный итератор для pmap_pmm (const/non-const).
     */
    template <bool IsConst> class iterator_base
    {
        using MapPtr = std::conditional_t<IsConst, const pmap_pmm*, pmap_pmm*>;
        using Ref    = std::conditional_t<IsConst, const Entry&, Entry&>;
        using Ptr    = std::conditional_t<IsConst, const Entry*, Entry*>;

        MapPtr    _pm;
        uintptr_t _idx;

      public:
        iterator_base( MapPtr pm, uintptr_t idx ) : _pm( pm ), _idx( idx ) {}

        Ref operator*() const
        {
            auto* raw = _pm->arr_.data();
            return raw[_idx];
        }

        Ptr operator->() const
        {
            auto* raw = _pm->arr_.data();
            return &raw[_idx];
        }

        iterator_base& operator++()
        {
            ++_idx;
            return *this;
        }

        iterator_base operator++( int )
        {
            iterator_base tmp = *this;
            ++_idx;
            return tmp;
        }

        bool operator==( const iterator_base& o ) const { return _idx == o._idx; }
        bool operator!=( const iterator_base& o ) const { return _idx != o._idx; }
    };

    using iterator       = iterator_base<false>;
    using const_iterator = iterator_base<true>;

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
