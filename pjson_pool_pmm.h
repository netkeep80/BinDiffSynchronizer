#pragma once
/**
 * @file pjson_pool_pmm.h
 * @brief Пул памяти для узлов pjson на базе pmm::ppool (Issue #166, План 2.4).
 *
 * Пул узлов node для pjson на базе pmm::ppool<node, PamManager>.
 *
 * Принцип работы:
 *   - Пул использует pmm::ppool<node> для O(1) аллокации/деаллокации узлов.
 *   - ppool выделяет крупные чанки из PMM и разбивает их на
 *     гранулярно-выровненные слоты.
 *   - Свободные слоты связаны через встроенный free-list в ppool.
 *   - node_id = байтовое смещение узла в ПАП (формат совместим с остальной системой).
 *
 * @see pam_adapter.h — адаптер pptr<T> <-> uintptr_t
 * @see pjson_node.h — структура node и node_tag
 */

// Важно: pjson_node.h должен быть включен первым, чтобы определить ::node
// до включения pam_adapter.h.
#include "pjson_node.h"
#include "pam_adapter.h" // Для pmm_resolve, pmm_resolve_const, pptr_to_offset, offset_to_pptr
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace pjson
{

// ═══════════════════════════════════════════════════════════════════════════
// pjson_pool_pmm — тип пула узлов на базе pmm::ppool (Issue #166, План 2.4)
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Персистный пул для узлов node на базе pmm::ppool.
 *
 * pjson_pool_pmm — это алиас для PamManager::ppool<node>.
 * Структура живёт в ПАП; создаётся через pjson_pool_pmm_create().
 *
 * pmm::ppool<node> обеспечивает:
 *   - O(1) аллокацию через встроенный free-list / выделение нового чанка
 *   - O(1) деаллокацию через возврат слота в free-list
 *   - Гранулярное выравнивание слотов для корректной адресации через pptr
 *   - Персистентность: все поля — POD, пригодны для хранения в ПАП
 *
 * node_id — смещение узла в ПАП (как везде в системе).
 * Преобразование: node* <-> node_id через base_ptr арифметику.
 */
using pjson_pool_pmm = PamManager::ppool<node>;

static_assert( std::is_trivially_copyable<pjson_pool_pmm>::value, "pjson_pool_pmm должен быть тривиально копируемым" );

// ═══════════════════════════════════════════════════════════════════════════
// Вспомогательная функция: node* -> node_id (байтовое смещение)
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Преобразовать указатель node* в node_id (байтовое смещение в ПАП).
 *
 * @param ptr Указатель на узел (из ppool::allocate()).
 * @return node_id (байтовое смещение); 0 если ptr == nullptr.
 */
inline node_id node_ptr_to_id( const node* ptr ) noexcept
{
    if ( ptr == nullptr )
        return 0;
    const uint8_t* base = PamManager::backend().base_ptr();
    return static_cast<node_id>( reinterpret_cast<const uint8_t*>( ptr ) - base );
}

// ═══════════════════════════════════════════════════════════════════════════
// Функции управления пулом
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Инициализировать пул (пустой пул с корректными значениями по умолчанию).
 *
 * Использует placement new для вызова конструктора ppool по умолчанию,
 * который устанавливает _objects_per_chunk = 64 и все остальные поля в 0.
 *
 * @param pool_off Байтовое смещение структуры pjson_pool_pmm в ПАП.
 */
inline void pjson_pool_pmm_init( uintptr_t pool_off )
{
    if ( pool_off == 0 )
        return;
    pjson_pool_pmm* pool = pmm_resolve<pjson_pool_pmm>( pool_off );
    if ( pool != nullptr )
        new ( pool ) pjson_pool_pmm();
}

/**
 * @brief Выделить новый узел из пула.
 *
 * Возвращает node_id (байтовое смещение в ПАП).
 * O(1): ppool выделяет из free-list или создаёт новый чанк.
 * Узел инициализируется как null (ppool обнуляет слот при выделении).
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

    // Если _objects_per_chunk == 0 (например, после zero-init через pam_pmm_create),
    // устанавливаем значение по умолчанию.
    if ( pool->_objects_per_chunk == 0 )
        pool->_objects_per_chunk = pjson_pool_pmm::default_objects_per_chunk;

    node* ptr = pool->allocate();
    if ( ptr == nullptr )
        return 0;

    // ppool::allocate() обнуляет слот, поэтому tag == 0 == node_tag::null.
    // Явно устанавливаем tag для ясности.
    ptr->tag  = node_tag::null;
    ptr->_pad = 0;

    return node_ptr_to_id( ptr );
}

/**
 * @brief Вернуть узел с node_id id в free-list пула.
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

    node* n = pmm_resolve<node>( id );
    if ( n == nullptr )
        return;

    pool->deallocate( n );
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
 * @brief Всего слотов в пуле (ёмкость, включая свободные).
 */
inline uintptr_t pjson_pool_pmm_total_count( uintptr_t pool_off )
{
    if ( pool_off == 0 )
        return 0;
    const pjson_pool_pmm* pool = pmm_resolve_const<pjson_pool_pmm>( pool_off );
    return ( pool != nullptr ) ? pool->total_capacity() : 0;
}

/**
 * @brief Число узлов в free-list (доступны для повторного использования).
 */
inline uintptr_t pjson_pool_pmm_free_in_pool( uintptr_t pool_off )
{
    if ( pool_off == 0 )
        return 0;
    const pjson_pool_pmm* pool = pmm_resolve_const<pjson_pool_pmm>( pool_off );
    return ( pool != nullptr ) ? pool->free_count() : 0;
}

/**
 * @brief Число занятых (выделенных пользователем) узлов.
 */
inline uintptr_t pjson_pool_pmm_used_count( uintptr_t pool_off )
{
    if ( pool_off == 0 )
        return 0;
    const pjson_pool_pmm* pool = pmm_resolve_const<pjson_pool_pmm>( pool_off );
    return ( pool != nullptr ) ? pool->allocated_count() : 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// Управление памятью пула
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Освободить все чанки пула (возвращает память в PMM).
 *
 * После вызова пул пуст, но может быть использован повторно
 * (ppool::free_all() сбрасывает состояние).
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

    pool->free_all();
}

/**
 * @brief Создать новый пул через PMM.
 *
 * Аллоцирует структуру pjson_pool_pmm (ppool<node>) в ПАП и инициализирует её.
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

    // Сначала освобождаем все чанки.
    pjson_pool_pmm_free_pool( pool_off );

    // Затем освобождаем саму структуру пула.
    auto pool_pptr = offset_to_pptr<pjson_pool_pmm>( pool_off );
    PamManager::template deallocate_typed<pjson_pool_pmm>( pool_pptr );
}

} // namespace pjson
