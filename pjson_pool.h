#pragma once
// pjson_pool.h — Обёртка для совместимости: делегирует в pjson_pool_pmm.h (Задача 15.7).
//
// После консолидации (Фаза 15) каноническая реализация пула узлов node
// находится в pjson_pool_pmm.h (namespace pjson, free-функции).
// Этот файл предоставляет struct pjson_pool с member-функциями
// для обратной совместимости с кодом, использующим pool->alloc() и т.д.
//
// @see pjson_pool_pmm.h — каноническая реализация
// @see plan.md Задача 15.7 — Консолидация pjson_pool.h и pjson_pool_pmm.h
//
// Все комментарии — на русском языке (Тр.6).

#include "pjson_pool_pmm.h"

using namespace pjson;

// ===========================================================================
// pjson_pool — обёртка с member-функциями поверх pjson_pool_pmm (Задача 15.7)
// ===========================================================================

/// Персистный пул для узлов node.
/// Обёртка совместимости: те же поля, что и pjson_pool_pmm,
/// member-функции делегируют в pjson_pool_pmm_* free-функции.
struct pjson_pool
{
    uintptr_t nodes_size_;     ///< Число узлов в массиве
    uintptr_t nodes_cap_;      ///< Ёмкость массива
    uintptr_t nodes_data_off_; ///< Смещение массива node[] в ПАП

    node_id   free_head_;  ///< node_id головы free-list; 0 = пусто
    uintptr_t free_count_; ///< Число узлов в free-list

    // -----------------------------------------------------------------------
    // Публичный API пула (совместимость с Фазой 4)
    // -----------------------------------------------------------------------

    /// Выделить новый узел из пула. Возвращает node_id (смещение в ПАП).
    node_id alloc()
    {
        uintptr_t self_off = pam_pmm_ptr_to_offset( this );
        return pjson_pool_pmm_alloc( self_off );
    }

    /// Вернуть узел с node_id id в free-list пула.
    void free( node_id id )
    {
        uintptr_t self_off = pam_pmm_ptr_to_offset( this );
        pjson_pool_pmm_free( self_off, id );
    }

    /// Получить ссылку на узел по node_id.
    node& get( node_id id ) { return *pjson_pool_pmm_get( id ); }

    /// Получить константную ссылку на узел по node_id.
    const node& get( node_id id ) const { return *pjson_pool_pmm_get_const( id ); }

    // -----------------------------------------------------------------------
    // Метрики
    // -----------------------------------------------------------------------

    uintptr_t total_count() const { return nodes_size_; }
    uintptr_t free_in_pool() const { return free_count_; }
    uintptr_t used_count() const { return nodes_size_ - free_count_; }

    // -----------------------------------------------------------------------
    // Управление памятью пула
    // -----------------------------------------------------------------------

    /// Освободить весь массив узлов (возвращает память в ПАМ).
    void free_pool()
    {
        uintptr_t self_off = pam_pmm_ptr_to_offset( this );
        pjson_pool_pmm_free_pool( self_off );
    }

  private:
    pjson_pool()  = default;
    ~pjson_pool() = default;

    template <typename U> friend class pjson::fptr_pmm;
};

static_assert( std::is_trivially_copyable<pjson_pool>::value, "pjson_pool должен быть тривиально копируемым" );
static_assert( sizeof( pjson_pool ) == 5 * sizeof( uintptr_t ), "pjson_pool должен занимать 5 * sizeof(uintptr_t) байт" );
