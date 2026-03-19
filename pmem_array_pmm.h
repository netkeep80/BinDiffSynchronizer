#pragma once
/**
 * @file pmem_array_pmm.h
 * @brief Реализация pmem_array на базе PersistMemoryManager (Задача 14.2).
 *
 * Примитив персистного массива на базе PersistMemoryManager (PMM).
 *
 * Формат данных:
 *   pmem_array_hdr_pmm — структура заголовка:
 *     [size | capacity | data_off]  — 3 * sizeof(uintptr_t)
 *   data_off — байтовое смещение массива данных в ПАП (кратно 16)
 * @see pam_adapter.h — адаптер pptr<T> <-> uintptr_t
 */

#include "pam_pmm_config.h"
#include "pam_adapter.h"
#include <cstring>
#include <type_traits>

namespace pjson
{

// ═══════════════════════════════════════════════════════════════════════════
// pmem_array_hdr_pmm — заголовок персистного массива
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Заголовок персистного массива для PMM.
 *
 * Раскладка:
 *   [size | capacity | data_off]  — 3 * sizeof(uintptr_t)
 *
 * data_off хранит байтовое смещение (не гранульный индекс).
 */
struct pmem_array_hdr_pmm
{
    uintptr_t size;     ///< Текущее количество элементов
    uintptr_t capacity; ///< Ёмкость (число элементов в выделенном буфере)
    uintptr_t data_off; ///< Байтовое смещение массива данных в ПАП; 0 = не выделено
};

static_assert( std::is_trivially_copyable<pmem_array_hdr_pmm>::value,
               "pmem_array_hdr_pmm должен быть тривиально копируемым" );
static_assert( sizeof( pmem_array_hdr_pmm ) == 3 * sizeof( void* ),
               "pmem_array_hdr_pmm должен занимать 3 * sizeof(void*) байт" );

// pmm_resolve<T> и pmm_resolve_const<T> определены в pam_adapter.h

// ═══════════════════════════════════════════════════════════════════════════
// Шаблонные функции для работы с pmem_array_hdr_pmm
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Инициализировать заголовок нулями (пустой массив).
 *
 * @tparam T Тип элементов массива.
 * @param hdr_off Байтовое смещение заголовка pmem_array_hdr_pmm в ПАП.
 */
template <typename T> inline void pmem_array_pmm_init( uintptr_t hdr_off )
{
    static_assert( std::is_trivially_copyable<T>::value,
                   "pmem_array_pmm<T> требует, чтобы T был тривиально копируемым" );
    if ( hdr_off == 0 )
        return;
    pmem_array_hdr_pmm* hdr = pmm_resolve<pmem_array_hdr_pmm>( hdr_off );
    if ( hdr != nullptr )
    {
        hdr->size     = 0;
        hdr->capacity = 0;
        hdr->data_off = 0;
    }
}

/**
 * @brief Зарезервировать ёмкость >= min_cap.
 *
 * Если текущая ёмкость достаточна — ничего не делает.
 * Иначе: выделяет новый блок через PMM, копирует данные, освобождает старый.
 *
 * Примечание: PMM не поддерживает realloc на месте,
 * поэтому всегда выполняется аллокация + копирование + деаллокация.
 *
 * @tparam T Тип элементов массива.
 * @param hdr_off Байтовое смещение заголовка в ПАП.
 * @param min_cap Минимальная требуемая ёмкость.
 */
template <typename T> inline void pmem_array_pmm_reserve( uintptr_t hdr_off, uintptr_t min_cap )
{
    static_assert( std::is_trivially_copyable<T>::value,
                   "pmem_array_pmm<T> требует, чтобы T был тривиально копируемым" );
    if ( hdr_off == 0 )
        return;

    // Читаем текущие значения из заголовка
    pmem_array_hdr_pmm* hdr = pmm_resolve<pmem_array_hdr_pmm>( hdr_off );
    if ( hdr == nullptr || min_cap <= hdr->capacity )
        return;

    uintptr_t old_data_off = hdr->data_off;
    uintptr_t old_cap      = hdr->capacity;
    uintptr_t old_size     = hdr->size;

    // Новая ёмкость: удваиваем до тех пор, пока >= min_cap.
    uintptr_t new_cap = ( old_cap == 0 ) ? 4 : old_cap * 2;
    while ( new_cap < min_cap )
        new_cap *= 2;

    // Выделяем новый блок через PMM
    auto new_data_pptr = PamManager::template allocate_typed<T>( new_cap );
    if ( new_data_pptr.is_null() )
        return; // Ошибка аллокации

    uintptr_t new_data_off = pptr_to_offset( new_data_pptr );

    // Копируем существующие элементы
    if ( old_data_off != 0 && old_size > 0 )
    {
        T*       new_raw = new_data_pptr.resolve();
        const T* old_raw = pmm_resolve_const<T>( old_data_off );
        if ( new_raw != nullptr && old_raw != nullptr )
            std::memcpy( new_raw, old_raw, old_size * sizeof( T ) );
    }

    // Освобождаем старый буфер
    if ( old_data_off != 0 )
    {
        auto old_pptr = offset_to_pptr<T>( old_data_off );
        PamManager::template deallocate_typed<T>( old_pptr );
    }

    // Обновляем заголовок (после деаллокации указатель может быть валидным)
    hdr           = pmm_resolve<pmem_array_hdr_pmm>( hdr_off );
    hdr->data_off = new_data_off;
    hdr->capacity = new_cap;
}

/**
 * @brief Добавить элемент в конец массива.
 *
 * Возвращает ссылку на новый элемент (инициализирован нулями).
 * При необходимости расширяет массив через pmem_array_pmm_reserve.
 *
 * @warning Возвращаемая ссылка действительна только до следующей аллокации.
 *
 * @tparam T Тип элементов массива.
 * @param hdr_off Байтовое смещение заголовка в ПАП.
 * @return T& — ссылка на добавленный элемент.
 */
template <typename T> inline T& pmem_array_pmm_push_back( uintptr_t hdr_off )
{
    static_assert( std::is_trivially_copyable<T>::value,
                   "pmem_array_pmm<T> требует, чтобы T был тривиально копируемым" );

    // Получаем текущий размер
    pmem_array_hdr_pmm* hdr      = pmm_resolve<pmem_array_hdr_pmm>( hdr_off );
    uintptr_t           cur_size = hdr->size;

    // Резервируем место для нового элемента
    pmem_array_pmm_reserve<T>( hdr_off, cur_size + 1 );

    // Обновляем размер и возвращаем ссылку на новый элемент
    hdr    = pmm_resolve<pmem_array_hdr_pmm>( hdr_off );
    T* raw = pmm_resolve<T>( hdr->data_off );

    // Инициализируем новый слот нулями
    std::memset( raw + hdr->size, 0, sizeof( T ) );
    hdr->size++;
    return raw[hdr->size - 1];
}

/**
 * @brief Удалить последний элемент массива.
 *
 * Уменьшает size на 1. Не освобождает память (capacity остаётся).
 *
 * @tparam T Тип элементов массива.
 * @param hdr_off Байтовое смещение заголовка в ПАП.
 */
template <typename T> inline void pmem_array_pmm_pop_back( uintptr_t hdr_off )
{
    if ( hdr_off == 0 )
        return;
    pmem_array_hdr_pmm* hdr = pmm_resolve<pmem_array_hdr_pmm>( hdr_off );
    if ( hdr != nullptr && hdr->size > 0 )
        hdr->size--;
}

/**
 * @brief Доступ к элементу по индексу.
 *
 * @warning Ссылка действительна только до следующей аллокации.
 * @warning Не проверяет границы (undefined behavior при idx >= size).
 *
 * @tparam T Тип элементов массива.
 * @param hdr_off Байтовое смещение заголовка в ПАП.
 * @param idx Индекс элемента.
 * @return T& — ссылка на элемент.
 */
template <typename T> inline T& pmem_array_pmm_at( uintptr_t hdr_off, uintptr_t idx )
{
    pmem_array_hdr_pmm* hdr = pmm_resolve<pmem_array_hdr_pmm>( hdr_off );
    T*                  raw = pmm_resolve<T>( hdr->data_off );
    return raw[idx];
}

/**
 * @brief Доступ к элементу по индексу (const версия).
 */
template <typename T> inline const T& pmem_array_pmm_at_const( uintptr_t hdr_off, uintptr_t idx )
{
    const pmem_array_hdr_pmm* hdr = pmm_resolve_const<pmem_array_hdr_pmm>( hdr_off );
    const T*                  raw = pmm_resolve_const<T>( hdr->data_off );
    return raw[idx];
}

/**
 * @brief Удалить элемент по индексу (сдвиг влево).
 *
 * Сдвигает элементы [idx+1..size-1] влево на одну позицию.
 * Уменьшает size на 1. Не освобождает память.
 *
 * @tparam T Тип элементов массива.
 * @param hdr_off Байтовое смещение заголовка в ПАП.
 * @param idx Индекс удаляемого элемента.
 */
template <typename T> inline void pmem_array_pmm_erase_at( uintptr_t hdr_off, uintptr_t idx )
{
    if ( hdr_off == 0 )
        return;
    pmem_array_hdr_pmm* hdr = pmm_resolve<pmem_array_hdr_pmm>( hdr_off );
    if ( hdr == nullptr || idx >= hdr->size )
        return;
    T* raw = pmm_resolve<T>( hdr->data_off );
    if ( raw != nullptr && idx + 1 < hdr->size )
        std::memmove( raw + idx, raw + idx + 1, ( hdr->size - idx - 1 ) * sizeof( T ) );
    hdr->size--;
}

/**
 * @brief Вставка в отсортированный массив.
 *
 * @tparam T Тип элементов.
 * @tparam KeyOf Функтор T -> ключ.
 * @tparam Less Функтор сравнения ключей.
 * @param hdr_off Байтовое смещение заголовка в ПАП.
 * @param value Вставляемое значение.
 * @param key_of Функтор извлечения ключа.
 * @param less Функтор сравнения.
 * @return T* — указатель на вставленный/обновлённый элемент.
 */
template <typename T, typename KeyOf, typename Less>
inline T* pmem_array_pmm_insert_sorted( uintptr_t hdr_off, const T& value, KeyOf key_of, Less less )
{
    static_assert( std::is_trivially_copyable<T>::value,
                   "pmem_array_pmm<T> требует, чтобы T был тривиально копируемым" );
    if ( hdr_off == 0 )
        return nullptr;

    // Бинарный поиск (lower_bound)
    pmem_array_hdr_pmm* hdr = pmm_resolve<pmem_array_hdr_pmm>( hdr_off );
    uintptr_t           sz  = hdr->size;
    uintptr_t           lo = 0, hi = sz;

    {
        T* raw = pmm_resolve<T>( hdr->data_off );
        while ( lo < hi )
        {
            uintptr_t mid = ( lo + hi ) / 2;
            if ( less( key_of( raw[mid] ), key_of( value ) ) )
                lo = mid + 1;
            else
                hi = mid;
        }
    }

    uintptr_t idx = lo;

    // Проверяем, существует ли уже такой ключ
    {
        T* raw = pmm_resolve<T>( hdr->data_off );
        if ( raw != nullptr && idx < sz && !less( key_of( value ), key_of( raw[idx] ) ) &&
             !less( key_of( raw[idx] ), key_of( value ) ) )
        {
            // Ключ найден — обновляем элемент
            raw[idx] = value;
            return &raw[idx];
        }
    }

    // Нужно вставить новый элемент в позицию idx
    pmem_array_pmm_reserve<T>( hdr_off, sz + 1 );

    // После reserve заголовок и данные могли измениться
    hdr    = pmm_resolve<pmem_array_hdr_pmm>( hdr_off );
    T* raw = pmm_resolve<T>( hdr->data_off );

    // Сдвигаем элементы [idx..sz-1] вправо
    if ( raw != nullptr && sz > idx )
        std::memmove( raw + idx + 1, raw + idx, ( sz - idx ) * sizeof( T ) );

    // Записываем новый элемент
    if ( raw != nullptr )
        raw[idx] = value;

    hdr->size++;

    hdr = pmm_resolve<pmem_array_hdr_pmm>( hdr_off );
    raw = pmm_resolve<T>( hdr->data_off );
    return ( raw != nullptr ) ? &raw[idx] : nullptr;
}

/**
 * @brief Бинарный поиск в отсортированном массиве.
 *
 * @tparam T Тип элементов.
 * @tparam KeyType Тип ключа поиска.
 * @tparam KeyOf Функтор T -> ключ.
 * @tparam Less Функтор сравнения.
 * @param hdr_off Байтовое смещение заголовка в ПАП.
 * @param key Ключ для поиска.
 * @param key_of Функтор извлечения ключа.
 * @param less Функтор сравнения.
 * @return T* — указатель на найденный элемент или nullptr.
 */
template <typename T, typename KeyType, typename KeyOf, typename Less>
inline T* pmem_array_pmm_find_sorted( uintptr_t hdr_off, const KeyType& key, KeyOf key_of, Less less )
{
    if ( hdr_off == 0 )
        return nullptr;

    pmem_array_hdr_pmm* hdr = pmm_resolve<pmem_array_hdr_pmm>( hdr_off );
    if ( hdr == nullptr || hdr->size == 0 || hdr->data_off == 0 )
        return nullptr;

    T*        raw = pmm_resolve<T>( hdr->data_off );
    uintptr_t lo = 0, hi = hdr->size;
    while ( lo < hi )
    {
        uintptr_t mid = ( lo + hi ) / 2;
        if ( less( key_of( raw[mid] ), key ) )
            lo = mid + 1;
        else
            hi = mid;
    }

    if ( lo < hdr->size && !less( key, key_of( raw[lo] ) ) && !less( key_of( raw[lo] ), key ) )
        return &raw[lo];

    return nullptr;
}

/**
 * @brief Бинарный поиск (const версия).
 */
template <typename T, typename KeyType, typename KeyOf, typename Less>
inline const T* pmem_array_pmm_find_sorted_const( uintptr_t hdr_off, const KeyType& key, KeyOf key_of, Less less )
{
    if ( hdr_off == 0 )
        return nullptr;

    const pmem_array_hdr_pmm* hdr = pmm_resolve_const<pmem_array_hdr_pmm>( hdr_off );
    if ( hdr == nullptr || hdr->size == 0 || hdr->data_off == 0 )
        return nullptr;

    const T*  raw = pmm_resolve_const<T>( hdr->data_off );
    uintptr_t lo = 0, hi = hdr->size;
    while ( lo < hi )
    {
        uintptr_t mid = ( lo + hi ) / 2;
        if ( less( key_of( raw[mid] ), key ) )
            lo = mid + 1;
        else
            hi = mid;
    }

    if ( lo < hdr->size && !less( key, key_of( raw[lo] ) ) && !less( key_of( raw[lo] ), key ) )
        return &raw[lo];

    return nullptr;
}

/**
 * @brief Освободить весь выделенный буфер.
 *
 * Освобождает data_off и обнуляет size/capacity/data_off.
 *
 * @tparam T Тип элементов массива.
 * @param hdr_off Байтовое смещение заголовка в ПАП.
 */
template <typename T> inline void pmem_array_pmm_free( uintptr_t hdr_off )
{
    if ( hdr_off == 0 )
        return;
    pmem_array_hdr_pmm* hdr = pmm_resolve<pmem_array_hdr_pmm>( hdr_off );
    if ( hdr == nullptr )
        return;
    if ( hdr->data_off != 0 )
    {
        auto pptr = offset_to_pptr<T>( hdr->data_off );
        PamManager::template deallocate_typed<T>( pptr );
    }
    hdr           = pmm_resolve<pmem_array_hdr_pmm>( hdr_off );
    hdr->size     = 0;
    hdr->capacity = 0;
    hdr->data_off = 0;
}

/**
 * @brief Обнулить размер без освобождения буфера.
 *
 * @tparam T Тип элементов массива.
 * @param hdr_off Байтовое смещение заголовка в ПАП.
 */
template <typename T> inline void pmem_array_pmm_clear( uintptr_t hdr_off )
{
    if ( hdr_off == 0 )
        return;
    pmem_array_hdr_pmm* hdr = pmm_resolve<pmem_array_hdr_pmm>( hdr_off );
    if ( hdr != nullptr )
        hdr->size = 0;
}

/**
 * @brief Получить текущий размер массива.
 */
inline uintptr_t pmem_array_pmm_size( uintptr_t hdr_off )
{
    if ( hdr_off == 0 )
        return 0;
    pmem_array_hdr_pmm* hdr = pmm_resolve<pmem_array_hdr_pmm>( hdr_off );
    return ( hdr != nullptr ) ? hdr->size : 0;
}

/**
 * @brief Получить текущую ёмкость массива.
 */
inline uintptr_t pmem_array_pmm_capacity( uintptr_t hdr_off )
{
    if ( hdr_off == 0 )
        return 0;
    pmem_array_hdr_pmm* hdr = pmm_resolve<pmem_array_hdr_pmm>( hdr_off );
    return ( hdr != nullptr ) ? hdr->capacity : 0;
}

} // namespace pjson
