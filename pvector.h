#pragma once
// pvector.h — Алиас для совместимости: делегирует в pvector_pmm.h (Задача 15.2).
//
// После консолидации (Фаза 15) каноническая реализация динамического массива
// находится в pvector_pmm.h (namespace pjson). Этот файл предоставляет имя
// pvector<T> без суффикса _pmm для обратной совместимости.
//
// @see pvector_pmm.h — каноническая реализация
// @see plan.md Задача 15.2 — Консолидация pvector.h и pvector_pmm.h
//
// Все комментарии — на русском языке (Тр.6).

#include "pvector_pmm.h"
#include "fptr_pmm.h"

using namespace pjson;

// ═══════════════════════════════════════════════════════════════════════════
// Алиас типа (Задача 15.2)
// ═══════════════════════════════════════════════════════════════════════════

/// pvector<T> — алиас для pjson::pvector_pmm<T> (обратная совместимость).
template <typename T> using pvector = pjson::pvector_pmm<T>;

// Проверки размера (обратная совместимость).
static_assert( sizeof( pvector<int> ) == 3 * sizeof( void* ),
               "pvector<int> должен занимать 3 * sizeof(void*) байт (Phase 3)" );
static_assert( sizeof( pvector<double> ) == 3 * sizeof( void* ),
               "pvector<double> должен занимать 3 * sizeof(void*) байт (Phase 3)" );
