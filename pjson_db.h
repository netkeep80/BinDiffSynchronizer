#pragma once
// pjson_db.h — Совместимый фасад для pjson_db_pmm (Задача 14.10).
//
// После миграции на PMM (Фаза 14) pjson_db является псевдонимом для
// pjson::pjson_db_pmm, а db_metrics — для pjson::db_metrics_pmm.
//
// Этот заголовок обеспечивает обратную совместимость для существующего кода,
// использующего имена pjson_db и db_metrics без квалификации пространства имён.
//
// Все комментарии — на русском языке (Тр.6).

#include "pjson_db_pmm.h"

/// Псевдоним для обратной совместимости: pjson_db = pjson::pjson_db_pmm.
using pjson_db = pjson::pjson_db_pmm;

/// Псевдоним для обратной совместимости: db_metrics = pjson::db_metrics_pmm.
using db_metrics = pjson::db_metrics_pmm;
