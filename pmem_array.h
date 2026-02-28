#pragma once
#include "persist.h"
#include <cstring>
#include <type_traits>

// pmem_array.h — Общий примитив персистного массива в ПАП (задача #1.1–1.2).
//
// Цель: устранить дублирование кода grow/copy/sync в pvector, pmap и внутренних
// структурах ПАМ. Сейчас аналогичные паттерны повторяются независимо в каждом месте.
//
// Принцип работы:
//   pmem_array_hdr — 3-полевой заголовок, совместимый с pvector<T> и pmap<K,V>.
//   Шаблонные функции реализуют всю логику grow/copy/sort/search.
//
// Все операции realloc-безопасны: используют смещения (offsets) вместо
// сырых указателей и повторно разрешают this после любой аллокации.
//
// Все комментарии — на русском языке (Тр.6).

// ---------------------------------------------------------------------------
// pmem_array_hdr — заголовок персистного массива
// ---------------------------------------------------------------------------
//
// Раскладка идентична pvector<T> и pmap<K,V>:
//   [size | capacity | data_off]  — 3 * sizeof(uintptr_t) = 3 * sizeof(void*)
//
// Все поля хранятся как uintptr_t для совместимости с ПАП (Тр.1, Тр.12).
//
// Использование:
//   pmem_array_hdr содержится как первое поле в pvector/pmap внутри ПАП.
//   Доступ к заголовку только через смещение (hdr_off) или fptr<pmem_array_hdr>.

struct pmem_array_hdr
{
    uintptr_t size;     ///< Текущее количество элементов
    uintptr_t capacity; ///< Ёмкость (число элементов в выделенном буфере)
    uintptr_t data_off; ///< Смещение массива данных в ПАП; 0 = не выделено
};

static_assert( std::is_trivially_copyable<pmem_array_hdr>::value, "pmem_array_hdr должен быть тривиально копируемым" );
static_assert( sizeof( pmem_array_hdr ) == 3 * sizeof( void* ),
               "pmem_array_hdr должен занимать 3 * sizeof(void*) байт" );

// ---------------------------------------------------------------------------
// Шаблонные функции для работы с pmem_array_hdr
// ---------------------------------------------------------------------------
//
// Все функции принимают hdr_off — смещение pmem_array_hdr в ПАП.
// Это обеспечивает realloc-безопасность: указатель на hdr может инвалидироваться
// после любой аллокации, но смещение всегда остаётся корректным.
//
// Тип T должен быть тривиально копируемым (требование ПАП).

// ---------------------------------------------------------------------------
// pmem_array_init<T> — инициализировать заголовок нулями (пустой массив)
// ---------------------------------------------------------------------------
//
// Устанавливает size=0, capacity=0, data_off=0.
// Вызывается при создании нового массива через fptr<pvector<T>>::New() или аналог.
template <typename T> inline void pmem_array_init( uintptr_t hdr_off )
{
    static_assert( std::is_trivially_copyable<T>::value, "pmem_array<T> требует, чтобы T был тривиально копируемым" );
    if ( hdr_off == 0 )
        return;
    auto&           pam = PersistentAddressSpace::Get();
    pmem_array_hdr* hdr = pam.Resolve<pmem_array_hdr>( hdr_off );
    if ( hdr != nullptr )
    {
        hdr->size     = 0;
        hdr->capacity = 0;
        hdr->data_off = 0;
    }
}

// ---------------------------------------------------------------------------
// pmem_array_reserve<T> — зарезервировать ёмкость >= min_cap
// ---------------------------------------------------------------------------
//
// Если текущая ёмкость уже достаточна — ничего не делает.
// Иначе: пытается расширить на месте (Realloc), если это последний блок.
// При неудаче — выделяет новый блок и копирует данные.
//
// Возвращает hdr_off (смещение заголовка не меняется, только data_off внутри).
template <typename T> inline void pmem_array_reserve( uintptr_t hdr_off, uintptr_t min_cap )
{
    static_assert( std::is_trivially_copyable<T>::value, "pmem_array<T> требует, чтобы T был тривиально копируемым" );
    if ( hdr_off == 0 )
        return;

    auto& pam = PersistentAddressSpace::Get();

    {
        pmem_array_hdr* hdr = pam.Resolve<pmem_array_hdr>( hdr_off );
        if ( hdr == nullptr || min_cap <= hdr->capacity )
            return;
    }

    uintptr_t old_data_off;
    uintptr_t old_cap;
    uintptr_t old_size;
    {
        pmem_array_hdr* hdr = pam.Resolve<pmem_array_hdr>( hdr_off );
        old_data_off        = hdr->data_off;
        old_cap             = hdr->capacity;
        old_size            = hdr->size;
    }

    // Новая ёмкость: удваиваем до тех пор, пока >= min_cap.
    uintptr_t new_cap = ( old_cap == 0 ) ? 4 : old_cap * 2;
    while ( new_cap < min_cap )
        new_cap *= 2;

    // Попытка расширить на месте через realloc (только если это последний блок).
    if ( old_data_off != 0 )
    {
        uintptr_t res = pam.Realloc( old_data_off, old_cap, new_cap, sizeof( T ) );
        if ( res != 0 )
        {
            // Расширено на месте — обновляем capacity в заголовке.
            pmem_array_hdr* hdr = pam.Resolve<pmem_array_hdr>( hdr_off );
            hdr->capacity       = new_cap;
            return;
        }
    }

    // Не удалось расширить на месте — выделяем новый блок.
    fptr<T> new_data;
    new_data.NewArray( static_cast<unsigned>( new_cap ) );

    // После NewArray буфер ПАМ мог переместиться — повторно разрешаем заголовок.
    pmem_array_hdr* hdr = pam.Resolve<pmem_array_hdr>( hdr_off );

    // Копируем существующие элементы.
    if ( old_data_off != 0 && old_size > 0 )
    {
        T* new_raw = pam.Resolve<T>( new_data.addr() );
        T* old_raw = pam.Resolve<T>( old_data_off );
        if ( new_raw != nullptr && old_raw != nullptr )
            std::memcpy( new_raw, old_raw, old_size * sizeof( T ) );
    }

    // Освобождаем старый буфер.
    if ( old_data_off != 0 )
    {
        fptr<T> old_arr;
        old_arr.set_addr( old_data_off );
        old_arr.DeleteArray();
        // После DeleteArray буфер ПАМ мог переместиться — повторно разрешаем заголовок.
        hdr = pam.Resolve<pmem_array_hdr>( hdr_off );
    }

    hdr->data_off = new_data.addr();
    hdr->capacity = new_cap;
}

// ---------------------------------------------------------------------------
// pmem_array_push_back<T> — добавить элемент в конец массива
// ---------------------------------------------------------------------------
//
// Возвращает ссылку на новый элемент (инициализирован нулями).
// При необходимости расширяет массив через pmem_array_reserve.
//
// ВАЖНО: возвращаемая ссылка действительна только до следующей аллокации.
template <typename T> inline T& pmem_array_push_back( uintptr_t hdr_off )
{
    static_assert( std::is_trivially_copyable<T>::value, "pmem_array<T> требует, чтобы T был тривиально копируемым" );

    auto& pam = PersistentAddressSpace::Get();

    // Получаем текущий размер.
    uintptr_t cur_size;
    {
        pmem_array_hdr* hdr = pam.Resolve<pmem_array_hdr>( hdr_off );
        cur_size            = hdr->size;
    }

    // Резервируем место для нового элемента.
    pmem_array_reserve<T>( hdr_off, cur_size + 1 );

    // Обновляем размер и возвращаем ссылку на новый элемент.
    pmem_array_hdr* hdr = pam.Resolve<pmem_array_hdr>( hdr_off );
    T*              raw = pam.Resolve<T>( hdr->data_off );
    // Инициализируем новый слот нулями.
    std::memset( raw + hdr->size, 0, sizeof( T ) );
    hdr->size++;
    return raw[hdr->size - 1];
}

// ---------------------------------------------------------------------------
// pmem_array_pop_back<T> — удалить последний элемент массива
// ---------------------------------------------------------------------------
//
// Уменьшает size на 1. Не освобождает память (capacity остаётся).
// Ничего не делает, если массив пуст.
template <typename T> inline void pmem_array_pop_back( uintptr_t hdr_off )
{
    if ( hdr_off == 0 )
        return;
    auto&           pam = PersistentAddressSpace::Get();
    pmem_array_hdr* hdr = pam.Resolve<pmem_array_hdr>( hdr_off );
    if ( hdr != nullptr && hdr->size > 0 )
        hdr->size--;
}

// ---------------------------------------------------------------------------
// pmem_array_at<T> — доступ к элементу по индексу
// ---------------------------------------------------------------------------
//
// Возвращает ссылку на элемент по индексу idx.
// Не проверяет границы (undefined behavior при idx >= size).
//
// ВАЖНО: ссылка действительна только до следующей аллокации.
template <typename T> inline T& pmem_array_at( uintptr_t hdr_off, uintptr_t idx )
{
    auto&           pam = PersistentAddressSpace::Get();
    pmem_array_hdr* hdr = pam.Resolve<pmem_array_hdr>( hdr_off );
    T*              raw = pam.Resolve<T>( hdr->data_off );
    return raw[idx];
}

template <typename T> inline const T& pmem_array_at_const( uintptr_t hdr_off, uintptr_t idx )
{
    const auto&           pam = PersistentAddressSpace::Get();
    const pmem_array_hdr* hdr = pam.Resolve<pmem_array_hdr>( hdr_off );
    const T*              raw = pam.Resolve<T>( hdr->data_off );
    return raw[idx];
}

// ---------------------------------------------------------------------------
// pmem_array_erase_at<T> — удалить элемент по индексу (сдвиг влево)
// ---------------------------------------------------------------------------
//
// Сдвигает элементы [idx+1..size-1] влево на одну позицию.
// Уменьшает size на 1. Не освобождает память.
template <typename T> inline void pmem_array_erase_at( uintptr_t hdr_off, uintptr_t idx )
{
    if ( hdr_off == 0 )
        return;
    auto&           pam = PersistentAddressSpace::Get();
    pmem_array_hdr* hdr = pam.Resolve<pmem_array_hdr>( hdr_off );
    if ( hdr == nullptr || idx >= hdr->size )
        return;
    T* raw = pam.Resolve<T>( hdr->data_off );
    if ( raw != nullptr && idx + 1 < hdr->size )
        std::memmove( raw + idx, raw + idx + 1, ( hdr->size - idx - 1 ) * sizeof( T ) );
    hdr->size--;
}

// ---------------------------------------------------------------------------
// pmem_array_insert_sorted<T, KeyOf, Less> — вставка в отсортированный массив
// ---------------------------------------------------------------------------
//
// KeyOf — функтор T → ключ (возвращает ключ из элемента).
// Less  — функтор сравнения ключей (по умолчанию operator<).
//
// Если ключ уже существует — обновляет элемент (возвращает указатель на него).
// Если не существует — вставляет в правильную позицию, сохраняя порядок.
//
// Возвращает указатель на вставленный/обновлённый элемент.
//
// ВАЖНО: возвращаемый указатель действителен только до следующей аллокации.
template <typename T, typename KeyOf, typename Less>
inline T* pmem_array_insert_sorted( uintptr_t hdr_off, const T& value, KeyOf key_of, Less less )
{
    static_assert( std::is_trivially_copyable<T>::value, "pmem_array<T> требует, чтобы T был тривиально копируемым" );
    if ( hdr_off == 0 )
        return nullptr;

    auto& pam = PersistentAddressSpace::Get();

    // Бинарный поиск (lower_bound).
    pmem_array_hdr* hdr = pam.Resolve<pmem_array_hdr>( hdr_off );
    uintptr_t       sz  = hdr->size;
    uintptr_t       lo = 0, hi = sz;

    {
        T* raw = pam.Resolve<T>( hdr->data_off );
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

    // Проверяем, существует ли уже такой ключ.
    {
        T* raw = pam.Resolve<T>( hdr->data_off );
        if ( raw != nullptr && idx < sz && !less( key_of( value ), key_of( raw[idx] ) ) &&
             !less( key_of( raw[idx] ), key_of( value ) ) )
        {
            // Ключ найден — обновляем элемент.
            raw[idx] = value;
            return &raw[idx];
        }
    }

    // Нужно вставить новый элемент в позицию idx.
    pmem_array_reserve<T>( hdr_off, sz + 1 );

    // После reserve заголовок и данные могли переместиться.
    hdr    = pam.Resolve<pmem_array_hdr>( hdr_off );
    T* raw = pam.Resolve<T>( hdr->data_off );

    // Сдвигаем элементы [idx..sz-1] вправо.
    if ( raw != nullptr && sz > idx )
        std::memmove( raw + idx + 1, raw + idx, ( sz - idx ) * sizeof( T ) );

    // Записываем новый элемент.
    if ( raw != nullptr )
        raw[idx] = value;

    hdr->size++;

    hdr = pam.Resolve<pmem_array_hdr>( hdr_off );
    raw = pam.Resolve<T>( hdr->data_off );
    return ( raw != nullptr ) ? &raw[idx] : nullptr;
}

// ---------------------------------------------------------------------------
// pmem_array_find_sorted<T, KeyOf, Less> — бинарный поиск в отсортированном массиве
// ---------------------------------------------------------------------------
//
// KeyType — тип ключа поиска.
// KeyOf   — функтор T → ключ.
// Less    — функтор сравнения ключей.
//
// Возвращает указатель на найденный элемент или nullptr.
//
// ВАЖНО: возвращаемый указатель действителен только до следующей аллокации.
template <typename T, typename KeyType, typename KeyOf, typename Less>
inline T* pmem_array_find_sorted( uintptr_t hdr_off, const KeyType& key, KeyOf key_of, Less less )
{
    if ( hdr_off == 0 )
        return nullptr;

    auto&           pam = PersistentAddressSpace::Get();
    pmem_array_hdr* hdr = pam.Resolve<pmem_array_hdr>( hdr_off );
    if ( hdr == nullptr || hdr->size == 0 || hdr->data_off == 0 )
        return nullptr;

    T*        raw = pam.Resolve<T>( hdr->data_off );
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

template <typename T, typename KeyType, typename KeyOf, typename Less>
inline const T* pmem_array_find_sorted_const( uintptr_t hdr_off, const KeyType& key, KeyOf key_of, Less less )
{
    if ( hdr_off == 0 )
        return nullptr;

    const auto&           pam = PersistentAddressSpace::Get();
    const pmem_array_hdr* hdr = pam.Resolve<pmem_array_hdr>( hdr_off );
    if ( hdr == nullptr || hdr->size == 0 || hdr->data_off == 0 )
        return nullptr;

    const T*  raw = pam.Resolve<T>( hdr->data_off );
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

// ---------------------------------------------------------------------------
// pmem_array_free<T> — освободить весь выделенный буфер
// ---------------------------------------------------------------------------
//
// Освобождает data_off и обнуляет size/capacity/data_off.
// После вызова заголовок снова пустой (как после pmem_array_init).
template <typename T> inline void pmem_array_free( uintptr_t hdr_off )
{
    if ( hdr_off == 0 )
        return;
    auto&           pam = PersistentAddressSpace::Get();
    pmem_array_hdr* hdr = pam.Resolve<pmem_array_hdr>( hdr_off );
    if ( hdr == nullptr )
        return;
    if ( hdr->data_off != 0 )
    {
        fptr<T> arr;
        arr.set_addr( hdr->data_off );
        arr.DeleteArray();
        // После DeleteArray буфер ПАМ мог переместиться — повторно разрешаем.
        hdr = pam.Resolve<pmem_array_hdr>( hdr_off );
    }
    if ( hdr != nullptr )
    {
        hdr->size     = 0;
        hdr->capacity = 0;
        hdr->data_off = 0;
    }
}

// ---------------------------------------------------------------------------
// pmem_array_clear<T> — обнулить размер без освобождения буфера
// ---------------------------------------------------------------------------
template <typename T> inline void pmem_array_clear( uintptr_t hdr_off )
{
    if ( hdr_off == 0 )
        return;
    auto&           pam = PersistentAddressSpace::Get();
    pmem_array_hdr* hdr = pam.Resolve<pmem_array_hdr>( hdr_off );
    if ( hdr != nullptr )
        hdr->size = 0;
}

// ---------------------------------------------------------------------------
// pmem_array_size / pmem_array_capacity — геттеры для size и capacity
// ---------------------------------------------------------------------------
inline uintptr_t pmem_array_size( uintptr_t hdr_off )
{
    if ( hdr_off == 0 )
        return 0;
    auto&           pam = PersistentAddressSpace::Get();
    pmem_array_hdr* hdr = pam.Resolve<pmem_array_hdr>( hdr_off );
    return ( hdr != nullptr ) ? hdr->size : 0;
}

inline uintptr_t pmem_array_capacity( uintptr_t hdr_off )
{
    if ( hdr_off == 0 )
        return 0;
    auto&           pam = PersistentAddressSpace::Get();
    pmem_array_hdr* hdr = pam.Resolve<pmem_array_hdr>( hdr_off );
    return ( hdr != nullptr ) ? hdr->capacity : 0;
}
