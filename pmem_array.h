#pragma once
// pmem_array.h — Алиасы для совместимости: делегирует в pmem_array_pmm.h (Задача 15.1).
//
// После консолидации (Фаза 15) каноническая реализация массива находится
// в pmem_array_pmm.h (namespace pjson). Этот файл предоставляет имена
// без суффикса _pmm в глобальном пространстве имён для обратной совместимости.
//
// @see pmem_array_pmm.h — каноническая реализация
// @see plan.md Задача 15.1 — Консолидация pmem_array.h и pmem_array_pmm.h
//
// Все комментарии — на русском языке (Тр.6).

#include "pmem_array_pmm.h"

using namespace pjson;

// ═══════════════════════════════════════════════════════════════════════════
// Алиас типа заголовка (Задача 15.1)
// ═══════════════════════════════════════════════════════════════════════════

/// pmem_array_hdr — алиас для pjson::pmem_array_hdr_pmm (обратная совместимость).
using pmem_array_hdr = pjson::pmem_array_hdr_pmm;

// ═══════════════════════════════════════════════════════════════════════════
// Алиасы функций (Задача 15.1)
// ═══════════════════════════════════════════════════════════════════════════

/// Инициализация — делегирует в pjson::pmem_array_pmm_init<T>.
template <typename T> inline void pmem_array_init( uintptr_t hdr_off )
{
    pjson::pmem_array_pmm_init<T>( hdr_off );
}

/// Резервирование — делегирует в pjson::pmem_array_pmm_reserve<T>.
template <typename T> inline void pmem_array_reserve( uintptr_t hdr_off, uintptr_t min_cap )
{
    pjson::pmem_array_pmm_reserve<T>( hdr_off, min_cap );
}

/// Добавить элемент в конец — делегирует в pjson::pmem_array_pmm_push_back<T>.
template <typename T> inline T& pmem_array_push_back( uintptr_t hdr_off )
{
    return pjson::pmem_array_pmm_push_back<T>( hdr_off );
}

/// Удалить последний элемент — делегирует в pjson::pmem_array_pmm_pop_back<T>.
template <typename T> inline void pmem_array_pop_back( uintptr_t hdr_off )
{
    pjson::pmem_array_pmm_pop_back<T>( hdr_off );
}

/// Доступ по индексу — делегирует в pjson::pmem_array_pmm_at<T>.
template <typename T> inline T& pmem_array_at( uintptr_t hdr_off, uintptr_t idx )
{
    return pjson::pmem_array_pmm_at<T>( hdr_off, idx );
}

/// Доступ по индексу (const) — делегирует в pjson::pmem_array_pmm_at_const<T>.
template <typename T> inline const T& pmem_array_at_const( uintptr_t hdr_off, uintptr_t idx )
{
    return pjson::pmem_array_pmm_at_const<T>( hdr_off, idx );
}

/// Удаление по индексу — делегирует в pjson::pmem_array_pmm_erase_at<T>.
template <typename T> inline void pmem_array_erase_at( uintptr_t hdr_off, uintptr_t idx )
{
    pjson::pmem_array_pmm_erase_at<T>( hdr_off, idx );
}

/// Вставка в отсортированный массив — делегирует в pjson::pmem_array_pmm_insert_sorted<T>.
template <typename T, typename KeyOf, typename Less>
inline T* pmem_array_insert_sorted( uintptr_t hdr_off, const T& value, KeyOf key_of, Less less )
{
    return pjson::pmem_array_pmm_insert_sorted<T, KeyOf, Less>( hdr_off, value, key_of, less );
}

/// Бинарный поиск — делегирует в pjson::pmem_array_pmm_find_sorted<T>.
template <typename T, typename KeyType, typename KeyOf, typename Less>
inline T* pmem_array_find_sorted( uintptr_t hdr_off, const KeyType& key, KeyOf key_of, Less less )
{
    return pjson::pmem_array_pmm_find_sorted<T, KeyType, KeyOf, Less>( hdr_off, key, key_of, less );
}

/// Бинарный поиск (const) — делегирует в pjson::pmem_array_pmm_find_sorted_const<T>.
template <typename T, typename KeyType, typename KeyOf, typename Less>
inline const T* pmem_array_find_sorted_const( uintptr_t hdr_off, const KeyType& key, KeyOf key_of, Less less )
{
    return pjson::pmem_array_pmm_find_sorted_const<T, KeyType, KeyOf, Less>( hdr_off, key, key_of, less );
}

/// Освобождение буфера — делегирует в pjson::pmem_array_pmm_free<T>.
template <typename T> inline void pmem_array_free( uintptr_t hdr_off )
{
    pjson::pmem_array_pmm_free<T>( hdr_off );
}

/// Обнуление размера — делегирует в pjson::pmem_array_pmm_clear<T>.
template <typename T> inline void pmem_array_clear( uintptr_t hdr_off )
{
    pjson::pmem_array_pmm_clear<T>( hdr_off );
}

/// Получить размер — делегирует в pjson::pmem_array_pmm_size.
inline uintptr_t pmem_array_size( uintptr_t hdr_off )
{
    return pjson::pmem_array_pmm_size( hdr_off );
}

/// Получить ёмкость — делегирует в pjson::pmem_array_pmm_capacity.
inline uintptr_t pmem_array_capacity( uintptr_t hdr_off )
{
    return pjson::pmem_array_pmm_capacity( hdr_off );
}
