#pragma once
/**
 * @file pjson_pool_pmm.h
 * @brief Пул памяти для узлов pjson на базе PersistMemoryManager (Задача 14.5).
 *
 * Этот файл содержит PMM-версию пула узлов node, совместимую по API
 * с оригинальным pjson_pool.h.
 *
 * Принцип работы:
 *   - Пул хранит непрерывный массив узлов node в ПАП через PMM.
 *   - Свободные слоты помечаются тегом node_tag::_free и связываются
 *     через поле _free_next в free-list.
 *   - При аллокации: берём голову free-list (O(1)).
 *     Если free-list пуст — добавляем узел в конец массива (push_back), O(1) амортизированно.
 *   - При освобождении: узел помечается node_tag::_free и добавляется в голову free-list (O(1)).
 *
 * Ключевые отличия от оригинальной реализации (pjson_pool.h):
 *   - Использует PamManager::allocate_typed<T>() вместо fptr<T>::NewArray()
 *   - Использует pptr<T>::resolve() вместо PersistentAddressSpace::Resolve<T>()
 *   - Использует pjson::pptr_to_offset() / pjson::offset_to_pptr() для конверсии
 *   - Все смещения кратны размеру гранулы PMM (16 байт)
 *
 * @see plan.md Задача 14.5 — Миграция pjson_pool на PMM
 * @see pam_adapter.h — адаптер pptr<T> <-> uintptr_t
 * @see pjson_node.h — структура node и node_tag
 */

// Важно: pjson_node.h должен быть включен первым, чтобы определить ::node
// до включения pam_adapter.h.
#include "pjson_node.h"
#include "pmem_array_pmm.h" // Для pmm_resolve, pmm_resolve_const, pptr_to_offset, offset_to_pptr
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace pjson
{

// ═══════════════════════════════════════════════════════════════════════════
// pjson_pool_pmm — Пул памяти для узлов node на базе PMM (Задача 14.5)
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Персистный пул для узлов node на базе PMM.
 *
 * Структура живёт в ПАП; создаётся через pjson_pool_pmm_create().
 *
 * Внутренняя раскладка:
 *   nodes_size_     — текущее число узлов в массиве
 *   nodes_cap_      — ёмкость массива
 *   nodes_data_off_ — байтовое смещение массива node[] в ПАП
 *   free_head_      — node_id головы free-list (0 = пусто)
 *   free_count_     — число узлов в free-list
 *
 * node_id — смещение узла в ПАП (как везде в системе).
 * Для узлов из пула node_id == байтовое смещение узла в массиве nodes_data_off_.
 */
struct pjson_pool_pmm
{
    uintptr_t nodes_size_; ///< Число узлов в массиве
    uintptr_t nodes_cap_;  ///< Ёмкость массива
    uintptr_t nodes_data_off_; ///< Байтовое смещение массива node[] в ПАП; 0 = не выделено

    node_id   free_head_;  ///< node_id головы free-list; 0 = пусто
    uintptr_t free_count_; ///< Число узлов в free-list
};

static_assert( std::is_trivially_copyable<pjson_pool_pmm>::value, "pjson_pool_pmm должен быть тривиально копируемым" );
static_assert( sizeof( pjson_pool_pmm ) == 5 * sizeof( uintptr_t ),
               "pjson_pool_pmm должен занимать 5 * sizeof(uintptr_t) байт" );

// ═══════════════════════════════════════════════════════════════════════════
// Вспомогательные функции
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Инициализировать пул нулями (пустой пул).
 *
 * @param pool_off Байтовое смещение структуры pjson_pool_pmm в ПАП.
 */
inline void pjson_pool_pmm_init( uintptr_t pool_off )
{
    if ( pool_off == 0 )
        return;
    pjson_pool_pmm* pool = pmm_resolve<pjson_pool_pmm>( pool_off );
    if ( pool != nullptr )
    {
        pool->nodes_size_     = 0;
        pool->nodes_cap_      = 0;
        pool->nodes_data_off_ = 0;
        pool->free_head_      = 0;
        pool->free_count_     = 0;
    }
}

/**
 * @brief Добавить новый null-узел в конец массива nodes_.
 *
 * Внутренняя функция для pjson_pool_pmm_alloc().
 * Возвращает node_id (байтовое смещение в ПАП) нового узла.
 *
 * @param pool_off Байтовое смещение структуры pjson_pool_pmm в ПАП.
 * @return node_id нового узла; 0 при ошибке.
 */
inline node_id pjson_pool_pmm_push_node( uintptr_t pool_off )
{
    if ( pool_off == 0 )
        return 0;

    pjson_pool_pmm* pool = pmm_resolve<pjson_pool_pmm>( pool_off );
    if ( pool == nullptr )
        return 0;

    uintptr_t sz  = pool->nodes_size_;
    uintptr_t cap = pool->nodes_cap_;

    if ( sz >= cap )
    {
        // Нужен grow: удваиваем ёмкость (начальная = 4).
        uintptr_t new_cap = ( cap == 0 ) ? 4u : cap * 2u;

        // Выделяем новый массив через PMM.
        auto new_data_pptr = PamManager::template allocate_typed<node>( new_cap );
        if ( new_data_pptr.is_null() )
            return 0;

        uintptr_t new_data_off = pptr_to_offset( new_data_pptr );

        // Инициализируем новые слоты нулём.
        node* new_raw = new_data_pptr.resolve();
        if ( new_raw != nullptr )
            std::memset( new_raw, 0, new_cap * sizeof( node ) );

        // Переразрешаем pool после аллокации.
        pool = pmm_resolve<pjson_pool_pmm>( pool_off );
        if ( pool == nullptr )
            return 0;

        // Копируем существующие узлы.
        uintptr_t old_data_off = pool->nodes_data_off_;
        uintptr_t old_size     = pool->nodes_size_;

        if ( old_data_off != 0 && old_size > 0 )
        {
            const node* old_raw = pmm_resolve_const<node>( old_data_off );
            new_raw             = pmm_resolve<node>( new_data_off );
            if ( old_raw != nullptr && new_raw != nullptr )
                std::memcpy( new_raw, old_raw, old_size * sizeof( node ) );

            // Освобождаем старый массив.
            auto old_pptr = offset_to_pptr<node>( old_data_off );
            PamManager::template deallocate_typed<node>( old_pptr );

            // Переразрешаем pool после деаллокации.
            pool = pmm_resolve<pjson_pool_pmm>( pool_off );
            if ( pool == nullptr )
                return 0;
        }

        pool->nodes_data_off_ = new_data_off;
        pool->nodes_cap_      = new_cap;
    }

    // Вычисляем смещение нового слота в ПАП.
    pool               = pmm_resolve<pjson_pool_pmm>( pool_off );
    uintptr_t idx      = pool->nodes_size_;
    uintptr_t data_off = pool->nodes_data_off_;

    // node_id — это байтовое смещение узла в ПАП.
    node*   raw     = pmm_resolve<node>( data_off );
    node_id slot_id = 0;
    if ( raw != nullptr )
    {
        node* slot = &raw[idx];
        std::memset( slot, 0, sizeof( node ) );
        slot->tag = node_tag::null;

        // Вычисляем байтовое смещение: data_off + idx * sizeof(node)
        slot_id = data_off + idx * sizeof( node );
    }

    // Увеличиваем размер.
    pool = pmm_resolve<pjson_pool_pmm>( pool_off );
    pool->nodes_size_++;

    return slot_id;
}

/**
 * @brief Выделить новый узел из пула.
 *
 * Возвращает node_id (байтовое смещение в ПАП).
 * O(1) амортизированно: сначала берём из free-list, иначе push_back.
 *
 * @param pool_off Байтовое смещение структуры pjson_pool_pmm в ПАП.
 * @return node_id нового узла; 0 при ошибке.
 */
inline node_id pjson_pool_pmm_alloc( uintptr_t pool_off )
{
    if ( pool_off == 0 )
        return 0;

    pjson_pool_pmm* pool = pmm_resolve<pjson_pool_pmm>( pool_off );
    if ( pool == nullptr )
        return 0;

    if ( pool->free_head_ != 0 )
    {
        // Берём голову free-list.
        node_id slot_id = pool->free_head_;

        // Читаем _free_next через resolve.
        node*   fn  = pmm_resolve<node>( slot_id );
        node_id nxt = ( fn != nullptr ) ? fn->_free_next : 0;

        // Обновляем pool.
        pool             = pmm_resolve<pjson_pool_pmm>( pool_off );
        pool->free_head_ = nxt;
        pool->free_count_--;

        // Инициализируем слот нулём (null-узел).
        node* n = pmm_resolve<node>( slot_id );
        if ( n != nullptr )
        {
            std::memset( n, 0, sizeof( node ) );
            n->tag = node_tag::null;
        }

        return slot_id;
    }

    // Free-list пуст — добавляем новый узел в конец массива.
    return pjson_pool_pmm_push_node( pool_off );
}

/**
 * @brief Вернуть узел с node_id id в free-list пула.
 *
 * Узел помечается тегом node_tag::_free.
 *
 * @param pool_off Байтовое смещение структуры pjson_pool_pmm в ПАП.
 * @param id node_id освобождаемого узла.
 */
inline void pjson_pool_pmm_free( uintptr_t pool_off, node_id id )
{
    if ( pool_off == 0 || id == 0 )
        return;

    pjson_pool_pmm* pool = pmm_resolve<pjson_pool_pmm>( pool_off );
    if ( pool == nullptr )
        return;

    // Помечаем узел как свободный и записываем текущую голову free-list.
    node* n = pmm_resolve<node>( id );
    if ( n == nullptr )
        return;

    n->tag        = node_tag::_free;
    n->_pad       = 0;
    n->_free_next = pool->free_head_;

    // Обновляем голову free-list.
    pool             = pmm_resolve<pjson_pool_pmm>( pool_off );
    pool->free_head_ = id;
    pool->free_count_++;
}

/**
 * @brief Получить указатель на узел по node_id.
 *
 * @warning Возвращаемый указатель действителен только до следующей аллокации.
 *
 * @param id node_id узла.
 * @return node* — указатель на узел или nullptr.
 */
inline node* pjson_pool_pmm_get( node_id id )
{
    if ( id == 0 )
        return nullptr;
    return pmm_resolve<node>( id );
}

/**
 * @brief Получить константный указатель на узел по node_id.
 */
inline const node* pjson_pool_pmm_get_const( node_id id )
{
    if ( id == 0 )
        return nullptr;
    return pmm_resolve_const<node>( id );
}

// ═══════════════════════════════════════════════════════════════════════════
// Метрики пула
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Всего узлов в пуле (выделено из ПАП, включая свободные).
 */
inline uintptr_t pjson_pool_pmm_total_count( uintptr_t pool_off )
{
    if ( pool_off == 0 )
        return 0;
    const pjson_pool_pmm* pool = pmm_resolve_const<pjson_pool_pmm>( pool_off );
    return ( pool != nullptr ) ? pool->nodes_size_ : 0;
}

/**
 * @brief Число узлов в free-list (доступны для повторного использования).
 */
inline uintptr_t pjson_pool_pmm_free_in_pool( uintptr_t pool_off )
{
    if ( pool_off == 0 )
        return 0;
    const pjson_pool_pmm* pool = pmm_resolve_const<pjson_pool_pmm>( pool_off );
    return ( pool != nullptr ) ? pool->free_count_ : 0;
}

/**
 * @brief Число занятых (выделенных пользователем) узлов: total - free.
 */
inline uintptr_t pjson_pool_pmm_used_count( uintptr_t pool_off )
{
    if ( pool_off == 0 )
        return 0;
    const pjson_pool_pmm* pool = pmm_resolve_const<pjson_pool_pmm>( pool_off );
    return ( pool != nullptr ) ? ( pool->nodes_size_ - pool->free_count_ ) : 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// Управление памятью пула
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Освободить весь массив узлов (возвращает память в PMM).
 *
 * После вызова пул не пригоден для использования без повторной инициализации.
 *
 * @param pool_off Байтовое смещение структуры pjson_pool_pmm в ПАП.
 */
inline void pjson_pool_pmm_free_pool( uintptr_t pool_off )
{
    if ( pool_off == 0 )
        return;

    pjson_pool_pmm* pool = pmm_resolve<pjson_pool_pmm>( pool_off );
    if ( pool == nullptr )
        return;

    if ( pool->nodes_data_off_ != 0 )
    {
        auto data_pptr = offset_to_pptr<node>( pool->nodes_data_off_ );
        PamManager::template deallocate_typed<node>( data_pptr );
    }

    // Переразрешаем pool после деаллокации.
    pool = pmm_resolve<pjson_pool_pmm>( pool_off );
    if ( pool != nullptr )
    {
        pool->nodes_size_     = 0;
        pool->nodes_cap_      = 0;
        pool->nodes_data_off_ = 0;
        pool->free_head_      = 0;
        pool->free_count_     = 0;
    }
}

/**
 * @brief Создать новый пул через PMM.
 *
 * Аллоцирует структуру pjson_pool_pmm в ПАП и инициализирует её.
 *
 * @return Байтовое смещение нового пула; 0 при ошибке.
 */
inline uintptr_t pjson_pool_pmm_create()
{
    auto pool_pptr = PamManager::template allocate_typed<pjson_pool_pmm>();
    if ( pool_pptr.is_null() )
        return 0;

    uintptr_t pool_off = pptr_to_offset( pool_pptr );
    pjson_pool_pmm_init( pool_off );

    return pool_off;
}

/**
 * @brief Удалить пул и освободить всю память.
 *
 * @param pool_off Байтовое смещение структуры pjson_pool_pmm в ПАП.
 */
inline void pjson_pool_pmm_destroy( uintptr_t pool_off )
{
    if ( pool_off == 0 )
        return;

    // Сначала освобождаем массив узлов.
    pjson_pool_pmm_free_pool( pool_off );

    // Затем освобождаем саму структуру пула.
    auto pool_pptr = offset_to_pptr<pjson_pool_pmm>( pool_off );
    PamManager::template deallocate_typed<pjson_pool_pmm>( pool_pptr );
}

} // namespace pjson
