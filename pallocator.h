#pragma once
// pallocator.h — Алиас для совместимости: делегирует в pallocator_pmm.h (Задача 15.6).
//
// После консолидации (Фаза 15) каноническая реализация STL-совместимого
// аллокатора на базе PMM находится в pallocator_pmm.h (namespace pjson).
// Этот файл предоставляет имя без суффикса _pmm для обратной совместимости.
//
// @see pallocator_pmm.h — каноническая реализация
// @see plan.md Задача 15.6 — Консолидация pallocator.h и pallocator_pmm.h
//
// Все комментарии — на русском языке (Тр.6).

#include "pallocator_pmm.h"

using namespace pjson;
