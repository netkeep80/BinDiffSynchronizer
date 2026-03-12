#pragma once
/**
 * @file pmap_pmm.h
 * @brief Реализация pmap на базе PersistMemoryManager (Задача 14.3).
 *
 * Этот файл содержит PMM-версию персистной карты (key-value map),
 * совместимую по API с оригинальным pmap.h.
 *
 * pmap_pmm<K, V> — персистный sorted-array контейнер, аналог std::map<K, V>.
 * Все операции делегируются функциям из pmem_array_pmm.h.
 *
 * Ключевые отличия от оригинальной реализации:
 *   - Использует PMM для аллокации/деаллокации
 *   - Требует инициализации PamManager::create() перед использованием
 *   - Смещения кратны размеру гранулы PMM (16 байт)
 *
 * @see plan.md Задача 14.3 — Миграция pmap на PMM
 * @see pmem_array_pmm.h — примитив персистного массива на базе PMM
 */

#include "pmem_array_pmm.h"
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

    // Единственное поле — заголовок pmem_array_hdr_pmm (3 * sizeof(uintptr_t)).
    // Раскладка идентична pmap: [size | capacity | data_off].
    pmem_array_hdr_pmm hdr_{}; ///< Заголовок персистного массива (инициализирован нулями)

    // Вспомогательные функторы для pmem_array_pmm_insert_sorted / pmem_array_pmm_find_sorted.
    struct KeyOf
    {
        const K& operator()( const Entry& e ) const { return e.key; }
    };

    struct Less
    {
        bool operator()( const K& a, const K& b ) const { return a < b; }
    };

  public:
    /**
     * @brief Получить текущий размер карты (количество элементов).
     */
    uintptr_t size() const { return hdr_.size; }

    /**
     * @brief Проверить, пуста ли карта.
     */
    bool empty() const { return hdr_.size == 0; }

    /**
     * @brief Вставить или обновить пару ключ-значение.
     *
     * Если ключ уже существует — значение обновляется.
     * Если ключ новый — вставляется в отсортированном порядке.
     *
     * @param k Ключ.
     * @param v Значение.
     * @param self_off Байтовое смещение этого pmap_pmm в ПАП (для realloc-безопасности).
     */
    void insert( const K& k, const V& v, uintptr_t self_off )
    {
        Entry e;
        e.key   = k;
        e.value = v;
        pmem_array_pmm_insert_sorted<Entry, KeyOf, Less>( self_off, e, KeyOf{}, Less{} );
    }

    /**
     * @brief Вставить или обновить пару (упрощённая версия без self_off).
     *
     * Использует прямой доступ к hdr_. Требует, чтобы pmap_pmm находился
     * в PMM-памяти и смещение можно было вычислить.
     *
     * @param k Ключ.
     * @param v Значение.
     *
     * @warning Эта версия требует, чтобы объект находился в PMM-памяти.
     */
    void insert_direct( const K& k, const V& v )
    {
        Entry e;
        e.key   = k;
        e.value = v;

        // Бинарный поиск (lower_bound) для определения позиции вставки
        uintptr_t sz = hdr_.size;
        uintptr_t lo = 0, hi = sz;

        {
            Entry* raw = pmm_resolve<Entry>( hdr_.data_off );
            while ( lo < hi )
            {
                uintptr_t mid = ( lo + hi ) / 2;
                if ( Less{}( raw[mid].key, k ) )
                    lo = mid + 1;
                else
                    hi = mid;
            }
        }

        uintptr_t idx = lo;

        // Проверяем, существует ли уже такой ключ
        {
            Entry* raw = pmm_resolve<Entry>( hdr_.data_off );
            if ( raw != nullptr && idx < sz && !Less{}( k, raw[idx].key ) && !Less{}( raw[idx].key, k ) )
            {
                // Ключ найден — обновляем значение
                raw[idx].value = v;
                return;
            }
        }

        // Нужно вставить новый элемент в позицию idx
        // Резервируем место
        uintptr_t cur_cap  = hdr_.capacity;
        uintptr_t new_size = sz + 1;

        if ( new_size > cur_cap )
        {
            uintptr_t new_cap = ( cur_cap == 0 ) ? 4 : cur_cap * 2;
            while ( new_cap < new_size )
                new_cap *= 2;

            // Выделяем новый блок
            auto new_data_pptr = PamManager::template allocate_typed<Entry>( new_cap );
            if ( new_data_pptr.is_null() )
                return;

            uintptr_t new_data_off = pptr_to_offset( new_data_pptr );

            // Копируем существующие элементы
            if ( hdr_.data_off != 0 && sz > 0 )
            {
                Entry*       new_raw = new_data_pptr.resolve();
                const Entry* old_raw = pmm_resolve_const<Entry>( hdr_.data_off );
                if ( new_raw != nullptr && old_raw != nullptr )
                    std::memcpy( new_raw, old_raw, sz * sizeof( Entry ) );
            }

            // Освобождаем старый буфер
            if ( hdr_.data_off != 0 )
            {
                auto old_pptr = offset_to_pptr<Entry>( hdr_.data_off );
                PamManager::template deallocate_typed<Entry>( old_pptr );
            }

            hdr_.data_off = new_data_off;
            hdr_.capacity = new_cap;
        }

        // Сдвигаем элементы [idx..sz-1] вправо
        Entry* raw = pmm_resolve<Entry>( hdr_.data_off );
        if ( raw != nullptr && sz > idx )
            std::memmove( raw + idx + 1, raw + idx, ( sz - idx ) * sizeof( Entry ) );

        // Записываем новый элемент
        if ( raw != nullptr )
            raw[idx] = e;

        hdr_.size++;
    }

    /**
     * @brief Найти значение по ключу.
     *
     * @param k Ключ для поиска.
     * @return V* — указатель на значение или nullptr, если не найдено.
     */
    V* find( const K& k )
    {
        if ( hdr_.size == 0 || hdr_.data_off == 0 )
            return nullptr;

        Entry*    raw = pmm_resolve<Entry>( hdr_.data_off );
        uintptr_t lo = 0, hi = hdr_.size;
        while ( lo < hi )
        {
            uintptr_t mid = ( lo + hi ) / 2;
            if ( Less{}( raw[mid].key, k ) )
                lo = mid + 1;
            else
                hi = mid;
        }

        if ( lo < hdr_.size && !Less{}( k, raw[lo].key ) && !Less{}( raw[lo].key, k ) )
            return &raw[lo].value;

        return nullptr;
    }

    /**
     * @brief Найти значение по ключу (const версия).
     */
    const V* find( const K& k ) const
    {
        if ( hdr_.size == 0 || hdr_.data_off == 0 )
            return nullptr;

        const Entry* raw = pmm_resolve_const<Entry>( hdr_.data_off );
        uintptr_t    lo = 0, hi = hdr_.size;
        while ( lo < hi )
        {
            uintptr_t mid = ( lo + hi ) / 2;
            if ( Less{}( raw[mid].key, k ) )
                lo = mid + 1;
            else
                hi = mid;
        }

        if ( lo < hdr_.size && !Less{}( k, raw[lo].key ) && !Less{}( raw[lo].key, k ) )
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
        if ( hdr_.size == 0 || hdr_.data_off == 0 )
            return false;

        Entry*    raw = pmm_resolve<Entry>( hdr_.data_off );
        uintptr_t lo = 0, hi = hdr_.size;
        while ( lo < hi )
        {
            uintptr_t mid = ( lo + hi ) / 2;
            if ( Less{}( raw[mid].key, k ) )
                lo = mid + 1;
            else
                hi = mid;
        }

        if ( lo >= hdr_.size || Less{}( k, raw[lo].key ) || Less{}( raw[lo].key, k ) )
            return false;

        // Сдвигаем элементы [lo+1..size-1] влево
        if ( lo + 1 < hdr_.size )
            std::memmove( raw + lo, raw + lo + 1, ( hdr_.size - lo - 1 ) * sizeof( Entry ) );

        hdr_.size--;
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
        insert_direct( k, def );

        // После вставки ищем снова (может быть realloc)
        V* inserted = find( k );
        return *inserted;
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
            auto pptr = offset_to_pptr<Entry>( hdr_.data_off );
            PamManager::template deallocate_typed<Entry>( pptr );
        }
        hdr_.size     = 0;
        hdr_.capacity = 0;
        hdr_.data_off = 0;
    }

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
            Entry* raw = pmm_resolve<Entry>( _pm->hdr_.data_off );
            return raw[_idx];
        }

        Entry* operator->()
        {
            Entry* raw = pmm_resolve<Entry>( _pm->hdr_.data_off );
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
            const Entry* raw = pmm_resolve_const<Entry>( _pm->hdr_.data_off );
            return raw[_idx];
        }

        const Entry* operator->() const
        {
            const Entry* raw = pmm_resolve_const<Entry>( _pm->hdr_.data_off );
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
    const_iterator cbegin() const { return const_iterator( this, 0 ); }
    const_iterator cend() const { return const_iterator( this, hdr_.size ); }

  public:
    // Конструктор по умолчанию — public для совместимости с PMM::allocate_typed.
    // Предупреждение: создание на стеке не рекомендуется — pmap_pmm предназначен
    // для использования внутри персистного адресного пространства.
    pmap_pmm()  = default;
    ~pmap_pmm() = default;
};

// ═══════════════════════════════════════════════════════════════════════════
// Проверки размера
// ═══════════════════════════════════════════════════════════════════════════

static_assert( sizeof( pmap_pmm<int, int> ) == 3 * sizeof( void* ),
               "pmap_pmm<int,int> должен занимать 3 * sizeof(void*) байт" );
static_assert( sizeof( pmap_pmm<int, double> ) == 3 * sizeof( void* ),
               "pmap_pmm<int,double> должен занимать 3 * sizeof(void*) байт" );

} // namespace pjson
