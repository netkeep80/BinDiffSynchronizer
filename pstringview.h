#pragma once
// pstringview.h — Алиас для совместимости: делегирует в pstringview_pmm.h (Задача 15.5).
//
// После консолидации (Фаза 15) каноническая реализация персистной интернированной
// строки и словаря находится в pstringview_pmm.h (namespace pjson). Этот файл
// предоставляет имена без суффикса _pmm для обратной совместимости.
//
// @see pstringview_pmm.h — каноническая реализация
// @see plan.md Задача 15.5 — Консолидация pstringview.h и pstringview_pmm.h
//
// Все комментарии — на русском языке (Тр.6).

#include "pstringview_pmm.h"
#include "pstring.h"
#include "pmap.h"
#include "fptr_pmm.h"

using namespace pjson;

// ═══════════════════════════════════════════════════════════════════════════
// Алиас типа (Задача 15.5)
// ═══════════════════════════════════════════════════════════════════════════

/// pstringview — алиас для pjson::pstringview_pmm (обратная совместимость).
using pstringview = pjson::pstringview_pmm;

// Проверки размера (обратная совместимость).
static_assert( sizeof( pstringview ) == 2 * sizeof( void* ), "pstringview должна занимать 2 * sizeof(void*) байт" );
