#pragma once
/**
 * @file pstringview_pmm.h
 * @brief Каноническая реализация pstringview на базе PersistMemoryManager.
 *
 * Содержит PMM-версию персистной интернированной read-only строки,
 * а также полный API интернирования и поиска строк в словаре ПАП.
 *
 * PMM v0.21.0 уже содержит оптимизированный pstringview:
 *   - Single-block хранение (длина + строка в одном блоке)
 *   - AVL-дерево для интернирования (O(log n) поиск)
 *   - Блоки заблокированы навечно (не освобождаются)
 *
 * Ключевые особенности:
 *   - Read-only: строковые данные никогда не изменяются после создания
 *   - Интернирование: одинаковые строки → один pptr
 *   - Сравнение O(1): через сравнение pptr (гранульных индексов)
 *
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
// pstringview_pmm — каноническая реализация
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Облегчённая структура для хранения ссылки на интернированную строку PMM.
 *
 * Использует внутренний формат PMM (гранульный индекс вместо chars_offset + length).
 *
 * Для полной совместимости с существующим кодом поддерживает поля:
 *   - length: длина строки (кэшируется)
 *   - chars_offset: байтовое смещение символьных данных в ПАП
 */
struct pstringview_pmm
{
    uintptr_t length;       ///< Длина строки (без нулевого терминатора)
    uintptr_t chars_offset; ///< Байтовое смещение строковых данных в ПАП

    /**
     * @brief Интернировать строку s.
     *
     * Ищет s в AVL-дереве PMM. Если найдена — устанавливает chars_offset на существующий блок.
     * Если нет — создаёт новый блок pstringview в ПАП, добавляет в дерево.
     *
     * @param s C-строка для интернирования. nullptr трактуется как "".
     */
    void intern( const char* s )
    {
        if ( s == nullptr )
            s = "";

        // Ленивое восстановление корня AVL-дерева из ПАП (через callback из pam_pmm.h).
        if ( pstringview_pmm_pre_intern_hook() != nullptr )
            pstringview_pmm_pre_intern_hook()();

        // Используем PMM pstringview::intern()
        pmm_pstringview_pptr p = pmm_pstringview::intern( s );

        if ( p.is_null() )
        {
            length       = 0;
            chars_offset = 0;
            return;
        }

        // Получаем указатель на строковые данные внутри блока.
        pmm_pstringview* sv = p.resolve();
        if ( sv == nullptr )
        {
            length       = 0;
            chars_offset = 0;
            return;
        }

        // chars_offset указывает на str[] внутри блока pmm_pstringview,
        // обеспечивая совместимость с pmm_resolve<char>(chars_offset).
        const std::uint8_t* base = PamManager::backend().base_ptr();
        chars_offset = static_cast<uintptr_t>( reinterpret_cast<const std::uint8_t*>( sv->c_str() ) - base );

        // Кэшируем длину
        length = static_cast<uintptr_t>( sv->size() );

        // Сохраняем корень AVL-дерева в ПАП (через callback из pam_pmm.h).
        if ( pstringview_pmm_post_intern_hook() != nullptr )
            pstringview_pmm_post_intern_hook()();
    }

    /**
     * @brief Получить C-строку (нуль-терминированную).
     *
     * chars_offset указывает на символьные данные внутри блока pmm_pstringview.
     *
     * @return Указатель на символьные данные или "" для пустой строки.
     */
    const char* c_str() const
    {
        if ( chars_offset == 0 )
            return "";
        const char* s = pmm_resolve<char>( chars_offset );
        return ( s != nullptr ) ? s : "";
    }

    /**
     * @brief Получить длину строки (без нулевого терминатора).
     */
    uintptr_t size() const { return length; }

    /**
     * @brief Проверить, пустая ли строка.
     */
    bool empty() const { return length == 0; }

    /**
     * @brief Сравнение с C-строкой.
     */
    bool operator==( const char* s ) const
    {
        if ( s == nullptr )
            return length == 0;
        return std::strcmp( c_str(), s ) == 0;
    }

    /**
     * @brief Сравнение двух pstringview_pmm.
     *
     * Интернирование гарантирует: одинаковые строки → один chars_offset.
     * Сравнение O(1).
     */
    bool operator==( const pstringview_pmm& other ) const { return chars_offset == other.chars_offset; }

    bool operator!=( const char* s ) const { return !( *this == s ); }
    bool operator!=( const pstringview_pmm& other ) const { return !( *this == other ); }

    /**
     * @brief Лексикографическое сравнение.
     */
    bool operator<( const pstringview_pmm& other ) const { return std::strcmp( c_str(), other.c_str() ) < 0; }
};

static_assert( sizeof( pstringview_pmm ) == 2 * sizeof( void* ),
               "pstringview_pmm должна занимать 2 * sizeof(void*) байт" );
static_assert( std::is_trivially_copyable<pstringview_pmm>::value,
               "pstringview_pmm должен быть тривиально копируемым" );

// ═══════════════════════════════════════════════════════════════════════════
// Вспомогательные функции для интернирования и поиска строк
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Результат интернирования строки.
 */
struct pstringview_pmm_intern_result
{
    uintptr_t chars_offset; ///< Байтовое смещение блока pstringview в ПАП
    uintptr_t length;       ///< Длина строки
};

/**
 * @brief Интернировать строку через PMM.
 *
 * @param s C-строка для интернирования. nullptr трактуется как "".
 * @return Результат с chars_offset и length.
 */
inline pstringview_pmm_intern_result pstringview_pmm_intern( const char* s )
{
    if ( s == nullptr )
        s = "";

    pmm_pstringview_pptr p = pmm_pstringview::intern( s );

    pstringview_pmm_intern_result result{};
    if ( p.is_null() )
        return result;

    result.chars_offset = pptr_to_offset( p );
    pmm_pstringview* sv = p.resolve();
    result.length       = ( sv != nullptr ) ? static_cast<uintptr_t>( sv->size() ) : 0;

    return result;
}

/**
 * @brief Результат поиска строки в словаре PMM.
 */
struct pstringview_pmm_search_result
{
    std::string value;        ///< Найденная строка
    uintptr_t   chars_offset; ///< Байтовое смещение блока pstringview в ПАП
    uintptr_t   length;       ///< Длина строки
};

// ═══════════════════════════════════════════════════════════════════════════
// Обход AVL-дерева интернированных строк PMM
// ═══════════════════════════════════════════════════════════════════════════

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
