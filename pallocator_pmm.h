#pragma once
/**
 * @file pallocator_pmm.h
 * @brief STL-совместимый аллокатор на базе PMM (делегирует к pmm::pallocator).
 *
 * Начиная с PMM v0.31.0, pmm предоставляет pmm::pallocator<T, ManagerT> —
 * полноценный STL-совместимый аллокатор. pallocator_pmm<T> теперь является
 * алиасом для pmm::pallocator<T, PamManager>.
 *
 * Типичное использование:
 *   std::vector<int, pjson::pallocator_pmm<int>> v;
 *   v.push_back(42);
 *
 * @see pam_pmm_config.h — определение PamManager
 * @see pmm.h — pmm::pallocator<T, ManagerT>
 */

#include "pam_pmm_config.h"

namespace pjson
{

// ═══════════════════════════════════════════════════════════════════════════
// pallocator_pmm<T> — алиас для pmm::pallocator<T, PamManager>
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief STL-совместимый аллокатор, использующий PMM для выделения памяти.
 *
 * Делегирует к pmm::pallocator<T, PamManager>, предоставляемому PMM v0.31.0+.
 *
 * @tparam T Тип элементов.
 */
template <typename T> using pallocator_pmm = PamManager::pallocator<T>;

// ═══════════════════════════════════════════════════════════════════════════
// АЛИАС pallocator ДЛЯ СОВМЕСТИМОСТИ (в пространстве имён pjson)
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Алиас pallocator<T> = pallocator_pmm<T>.
 *
 * Используйте pjson::pallocator<T> для работы с PMM-бэкендом.
 */
template <typename T> using pallocator = pallocator_pmm<T>;

} // namespace pjson
