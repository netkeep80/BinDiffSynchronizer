/**
 * @file pam_adapter.h
 * @brief Адаптер pptr<T> <-> uintptr_t для совместимости с node_id (Задача 14.1).
 *
 * Этот файл обеспечивает плавный переход между текущей моделью node_id (uintptr_t байтовые
 * смещения) и новой моделью PMM pptr<T> (гранульные индексы).
 *
 * Стратегия миграции:
 *   - На этапе миграции сохраняем node_id = uintptr_t для минимизации изменений в
 *     pjson_node.h, pjson_db.h, pjson_codec.h.
 *   - Функции pptr_to_offset() и offset_to_pptr() обеспечивают конверсию между форматами.
 *   - В будущем (фаза 15+) возможен переход на node_id = pptr<node>.
 *
 * Ключевые отличия форматов:
 *   - Текущий ПАМ: node_id = байтовое смещение в ПАП (uintptr_t).
 *   - PMM: pptr<T> хранит гранульный индекс (index_type), где байтовое смещение =
 *          granule_index * granule_size.
 *
 * Формула конверсии (для 16-байтовых гранул, CacheManagerConfig):
 *   - pptr.offset() = гранульный индекс (uint32_t)
 *   - байтовое смещение = pptr.offset() * 16
 *   - гранульный индекс = байтовое смещение / 16 (должно быть кратно 16)
 *
 * @see plan.md Задача 14.1 — Адаптер pptr<T> <-> uintptr_t
 * @see pam_pmm_config.h — определение PamManager
 * @see https://github.com/netkeep80/PersistMemoryManager
 */

#pragma once

#include "pam_pmm_config.h"
#include <cassert>
#include <type_traits>

namespace pjson
{

// ═══════════════════════════════════════════════════════════════════════════
// КОНВЕРСИЯ pptr<T> <-> uintptr_t (Задача 14.1)
// ═══════════════════════════════════════════════════════════════════════════

/// Размер гранулы PMM в байтах (берётся из конфигурации менеджера).
/// Для CacheManagerConfig это 16 байт (DefaultAddressTraits).
inline constexpr std::size_t PMM_GRANULE_SIZE = PamManager::address_traits::granule_size;

/**
 * @brief Преобразовать pptr<T> в байтовое смещение (uintptr_t).
 *
 * Вычисляет байтовое смещение из гранульного индекса pptr:
 *   byte_offset = pptr.offset() * granule_size
 *
 * @tparam T Тип данных, на который ссылается pptr.
 * @param p pptr<T> — персистный указатель PMM.
 * @return uintptr_t — байтовое смещение (совместимо с текущим node_id).
 *         Возвращает 0 для null-указателя.
 *
 * Пример:
 * @code
 *   auto p = PamManager::allocate_typed<node>();
 *   uintptr_t off = pptr_to_offset(p);  // байтовое смещение
 *   node_id id = off;                    // совместимо с node_id
 * @endcode
 */
template <typename T> inline uintptr_t pptr_to_offset( typename PamManager::template pptr<T> p ) noexcept
{
    if ( p.is_null() )
        return 0;
    // pptr.offset() возвращает гранульный индекс
    // Умножаем на размер гранулы для получения байтового смещения
    return static_cast<uintptr_t>( p.offset() ) * PMM_GRANULE_SIZE;
}

/**
 * @brief Преобразовать байтовое смещение (uintptr_t) в pptr<T>.
 *
 * Вычисляет гранульный индекс pptr из байтового смещения:
 *   granule_index = byte_offset / granule_size
 *
 * @tparam T Тип данных, на который будет ссылаться pptr.
 * @param off uintptr_t — байтовое смещение (формат текущего node_id).
 * @return pptr<T> — персистный указатель PMM.
 *         Возвращает null-pptr для смещения 0.
 *
 * @warning Байтовое смещение ДОЛЖНО быть кратно размеру гранулы (PMM_GRANULE_SIZE).
 *          В отладочной сборке выполняется assert.
 *
 * Пример:
 * @code
 *   node_id id = ...;  // существующий node_id
 *   auto p = offset_to_pptr<node>(id);  // pptr<node>
 *   node* raw = p.resolve();            // сырой указатель
 * @endcode
 */
template <typename T> inline typename PamManager::template pptr<T> offset_to_pptr( uintptr_t off ) noexcept
{
    if ( off == 0 )
        return typename PamManager::template pptr<T>{}; // null pptr

    // Проверяем выравнивание в отладочной сборке
    assert( off % PMM_GRANULE_SIZE == 0 && "pam_adapter: byte offset must be aligned to granule size" );

    // Вычисляем гранульный индекс
    using index_type = typename PamManager::template pptr<T>::index_type;
    auto granule_idx = static_cast<index_type>( off / PMM_GRANULE_SIZE );

    return typename PamManager::template pptr<T>{ granule_idx };
}

// ═══════════════════════════════════════════════════════════════════════════
// ВСПОМОГАТЕЛЬНЫЕ ТИПЫ И ФУНКЦИИ
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Тип гранульного индекса PMM (для node_id в будущем).
 *
 * Для CacheManagerConfig это uint32_t (DefaultAddressTraits).
 * Может использоваться как альтернативный node_id в будущих фазах.
 */
using pmm_index_type = typename PamManager::index_type;

/**
 * @brief Проверить, является ли байтовое смещение корректно выровненным.
 *
 * Полезно для валидации node_id перед конверсией в pptr.
 *
 * @param off Байтовое смещение.
 * @return true если смещение кратно размеру гранулы или равно 0.
 */
inline constexpr bool is_aligned_offset( uintptr_t off ) noexcept
{
    return off == 0 || ( off % PMM_GRANULE_SIZE == 0 );
}

/**
 * @brief Выровнять байтовое смещение вверх до границы гранулы.
 *
 * Полезно при создании новых блоков.
 *
 * @param off Байтовое смещение.
 * @return Выровненное смещение (>= off, кратно PMM_GRANULE_SIZE).
 */
inline constexpr uintptr_t align_offset_up( uintptr_t off ) noexcept
{
    return ( ( off + PMM_GRANULE_SIZE - 1 ) / PMM_GRANULE_SIZE ) * PMM_GRANULE_SIZE;
}

/**
 * @brief Получить размер гранулы PMM в байтах.
 *
 * Полезно для диагностики и отладки.
 *
 * @return Размер гранулы (16 для CacheManagerConfig).
 */
inline constexpr std::size_t get_granule_size() noexcept
{
    return PMM_GRANULE_SIZE;
}

// ═══════════════════════════════════════════════════════════════════════════
// ТИПЫ УКАЗАТЕЛЕЙ ДЛЯ ОСНОВНЫХ СТРУКТУР
// ═══════════════════════════════════════════════════════════════════════════

// Forward declarations (определены в соответствующих заголовках)
struct node;

/// pptr<node> — персистный указатель на узел JSON.
/// Используется внутри адаптера для работы с пулом узлов.
using node_pptr = typename PamManager::template pptr<node>;

/// pptr<char> — персистный указатель на символы строки.
/// Используется для pstring и pstringview.
using char_pptr = typename PamManager::template pptr<char>;

/// pptr<uint8_t> — персистный указатель на байты.
/// Используется для binary-узлов.
using byte_pptr = typename PamManager::template pptr<uint8_t>;

} // namespace pjson
