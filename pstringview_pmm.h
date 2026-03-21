#pragma once
/**
 * @file pstringview_pmm.h
 * @brief Тонкая обёртка над PMM pstringview: типовые алиасы, сброс словаря, AVL-обход.
 *
 * PMM v0.21.0+ содержит оптимизированный pstringview:
 *   - Single-block хранение (длина + строка в одном блоке)
 *   - AVL-дерево для интернирования (O(log n) поиск)
 *   - Блоки заблокированы навечно (не освобождаются)
 *
 * Этот файл предоставляет:
 *   - pmm_pstringview / pmm_pstringview_pptr — типовые алиасы
 *   - pstringview_pmm_reset() — сброс словаря (для тестов)
 *   - Hooks для ленивого восстановления/сохранения корня AVL-дерева из ПАП
 *   - detail::pstringview_pmm_inorder() — обход AVL-дерева (для pam_search_strings)
 *   - pstringview_manager — менеджер таблицы интернирования
 *
 * Структура pstringview_pmm удалена (Issue #167, План 2.5):
 *   Интернирование — через pam_intern_string() из pam_pmm.h.
 *   Поиск — через pam_search_strings() / pam_all_strings() из pam_pmm.h.
 *
 * @see pam_pmm.h — API словаря строк (pam_intern_string, pam_search_strings)
 * @see pam_adapter.h — адаптер pptr<T> <-> uintptr_t
 */

#include "pam_pmm_config.h"
#include "pam_adapter.h"
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace pjson
{

// ═══════════════════════════════════════════════════════════════════════════
// Тип pstringview PMM
// ═══════════════════════════════════════════════════════════════════════════

/// Тип pmm::pstringview, привязанный к PamManager.
using pmm_pstringview = typename PamManager::pstringview;

/// Тип pptr<pstringview> для PamManager.
using pmm_pstringview_pptr = typename PamManager::template pptr<pmm_pstringview>;

// ═══════════════════════════════════════════════════════════════════════════
// Сброс словаря PMM pstringview
// ═══════════════════════════════════════════════════════════════════════════

/// Опциональный callback для ленивого восстановления корня AVL-дерева из ПАП.
/// Устанавливается в pam_pmm.h после определения pstringview_pmm_restore_root().
inline void ( *&pstringview_pmm_pre_intern_hook() )()
{
    static void ( *hook )() = nullptr;
    return hook;
}

/// Опциональный callback для сохранения корня AVL-дерева в ПАП после интернирования.
/// Устанавливается в pam_pmm.h после определения pstringview_pmm_save_root().
inline void ( *&pstringview_pmm_post_intern_hook() )()
{
    static void ( *hook )() = nullptr;
    return hook;
}

/// Опциональный callback для сброса флагов персистентности при reset().
/// Устанавливается в pam_pmm.h.
inline void ( *&pstringview_pmm_reset_hook() )()
{
    static void ( *hook )() = nullptr;
    return hook;
}

/**
 * @brief Сбросить синглтон словаря PMM (для тестов).
 */
inline void pstringview_pmm_reset()
{
    pmm_pstringview::reset();
    if ( pstringview_pmm_reset_hook() != nullptr )
        pstringview_pmm_reset_hook()();
}

// ═══════════════════════════════════════════════════════════════════════════
// Обход AVL-дерева интернированных строк PMM
// ═══════════════════════════════════════════════════════════════════════════

/// Результат поиска строки в словаре PMM.
/// Используется pam_search_strings() / pam_all_strings() из pam_pmm.h.
struct pstringview_pmm_search_result
{
    std::string value;        ///< Найденная строка
    uintptr_t   chars_offset; ///< Байтовое смещение блока pstringview в ПАП
    uintptr_t   length;       ///< Длина строки
};

namespace detail
{

/// Рекурсивный in-order обход AVL-дерева pmm::pstringview.
/// Использует PamManager::get_tree_left_offset() / get_tree_right_offset()
/// для навигации по AVL-дереву.
inline void pstringview_pmm_inorder( pmm_pstringview_pptr node, const char* pattern,
                                     std::vector<pstringview_pmm_search_result>& results )
{
    if ( node.is_null() )
        return;

    // Тип гранульного индекса PMM.
    using index_type        = typename PamManager::index_type;
    constexpr auto no_block = std::numeric_limits<index_type>::max();

    // Обход левого поддерева.
    auto left_idx = PamManager::get_tree_left_offset( node );
    if ( left_idx != static_cast<index_type>( 0 ) && left_idx != no_block )
    {
        pmm_pstringview_pptr left_node( left_idx );
        pstringview_pmm_inorder( left_node, pattern, results );
    }

    // Обработка текущего узла.
    pmm_pstringview* sv = node.resolve();
    if ( sv != nullptr )
    {
        const char* str = sv->c_str();
        if ( str != nullptr && std::strstr( str, pattern ) != nullptr )
        {
            pstringview_pmm_search_result r;
            r.value        = str;
            r.chars_offset = pptr_to_offset( node );
            r.length       = static_cast<uintptr_t>( sv->size() );
            results.push_back( std::move( r ) );
        }
    }

    // Обход правого поддерева.
    auto right_idx = PamManager::get_tree_right_offset( node );
    if ( right_idx != static_cast<index_type>( 0 ) && right_idx != no_block )
    {
        pmm_pstringview_pptr right_node( right_idx );
        pstringview_pmm_inorder( right_node, pattern, results );
    }
}

} // namespace detail

/**
 * @brief Найти все интернированные строки, содержащие подстроку pattern.
 *
 * Выполняет in-order обход AVL-дерева интернированных строк PMM.
 *
 * @param pattern Подстрока для поиска. nullptr трактуется как "".
 * @return Вектор результатов поиска.
 */
inline std::vector<pstringview_pmm_search_result> pstringview_pmm_search( const char* pattern )
{
    std::vector<pstringview_pmm_search_result> results;
    if ( pattern == nullptr )
        pattern = "";

    // Корень AVL-дерева интернированных строк.
    auto root_idx = pmm_pstringview::_root_idx;

    using index_type = typename PamManager::index_type;
    if ( root_idx == static_cast<index_type>( 0 ) )
        return results;

    pmm_pstringview_pptr root( root_idx );
    detail::pstringview_pmm_inorder( root, pattern, results );

    return results;
}

/**
 * @brief Вернуть все интернированные строки из словаря PMM.
 *
 * @return Вектор всех строк.
 */
inline std::vector<pstringview_pmm_search_result> pstringview_pmm_all()
{
    return pstringview_pmm_search( "" );
}

// ═══════════════════════════════════════════════════════════════════════════
// Менеджер словаря строк: pstringview_manager
// ═══════════════════════════════════════════════════════════════════════════

/// Менеджер таблицы интернирования.
/// Делегирует в PMM (AVL-дерево).
struct pstringview_manager
{
    /// Сбросить синглтон (для тестов). Делегирует в pstringview_pmm_reset().
    static void reset() { pstringview_pmm_reset(); }

    /// Смещение таблицы интернирования (не используется, PMM хранит дерево внутри).
    static inline uintptr_t _table_offset = 0;
};

} // namespace pjson
