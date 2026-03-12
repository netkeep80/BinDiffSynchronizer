/**
 * @file pam_pmm_config.h
 * @brief Конфигурация менеджера ПАП на основе PersistMemoryManager (Фаза 14).
 *
 * Этот файл определяет тип PamManager для использования в pjson_db вместо
 * текущего встроенного менеджера ПАП (pam_core.h / persist.h / pam.h).
 *
 * @see plan.md Задача 14.0 — Подготовка: подключение PMM
 * @see https://github.com/netkeep80/PersistMemoryManager
 */

#pragma once

#include "pmm.h"

namespace pjson {

/**
 * @brief Рекомендуемый тип менеджера ПАП для pjson_db.
 *
 * Используем CacheManagerConfig (однопоточный, NoLock, HeapStorage, 16B гранула,
 * рост 25%) — соответствует текущему однопоточному сценарию использования.
 *
 * При необходимости многопоточности следует использовать:
 *   using PamManager = pmm::PersistMemoryManager<pmm::PersistentDataConfig>;
 *
 * Доступные конфигурации PMM:
 *   - CacheManagerConfig      — однопоточный, NoLock, HeapStorage, 16B, рост 25%
 *   - PersistentDataConfig    — многопоточный, SharedMutexLock, HeapStorage, 16B, рост 25%
 *   - EmbeddedManagerConfig   — однопоточный, NoLock, HeapStorage, 16B, рост 50%
 *   - IndustrialDBConfig      — многопоточный, SharedMutexLock, HeapStorage, 16B, рост 100%
 *   - LargeDBConfig           — многопоточный, SharedMutexLock, HeapStorage, 64B, рост 100%
 */
using PamManager = pmm::PersistMemoryManager<pmm::CacheManagerConfig>;

// Альтернативный тип для многопоточных сценариев (закомментировано):
// using PamManager = pmm::PersistMemoryManager<pmm::PersistentDataConfig>;

} // namespace pjson
