#pragma once
// pmap.h — Алиас для совместимости: делегирует в pmap_pmm.h (Задача 15.3).
//
// После консолидации (Фаза 15) каноническая реализация персистной карты
// находится в pmap_pmm.h (namespace pjson). Этот файл предоставляет имена
// pmap<K,V> и pmap_entry<K,V> без суффикса _pmm для обратной совместимости.
//
// @see pmap_pmm.h — каноническая реализация
// @see plan.md Задача 15.3 — Консолидация pmap.h и pmap_pmm.h
//
// Все комментарии — на русском языке (Тр.6).

#include "pmap_pmm.h"
#include "pam_pmm.h"
#include "fptr_pmm.h"

using namespace pjson;

// ═══════════════════════════════════════════════════════════════════════════
// Алиасы типов (Задача 15.3)
// ═══════════════════════════════════════════════════════════════════════════

/// pmap_entry<K,V> — алиас для pjson::pmap_entry_pmm<K,V> (обратная совместимость).
template <typename K, typename V> using pmap_entry = pjson::pmap_entry_pmm<K, V>;

/// pmap<K,V> — алиас для pjson::pmap_pmm<K,V> (обратная совместимость).
template <typename K, typename V> using pmap = pjson::pmap_pmm<K, V>;

// Проверки размера (обратная совместимость).
static_assert( sizeof( pmap<int, int> ) == 3 * sizeof( void* ),
               "pmap<int,int> должен занимать 3 * sizeof(void*) байт" );
static_assert( sizeof( pmap<int, double> ) == 3 * sizeof( void* ),
               "pmap<int,double> должен занимать 3 * sizeof(void*) байт" );
