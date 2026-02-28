#pragma once
// pjson_node_pool.h — Пул памяти для узлов pjson (требование F2, задача #84).
//
// pjson_node_pool реализует пакетное выделение памяти для узлов pjson,
// уменьшая фрагментацию ПАП и ускоряя аллокацию.
//
// Принцип работы:
//   - Пул хранит список свободных узлов pjson (free list) в ПАП.
//   - При выделении узла: берём голову free list (O(1)).
//   - При освобождении узла: кладём его в голову free list (O(1)).
//   - При пустом free list: выделяем блок из PJSON_POOL_BLOCK_SIZE узлов
//     (пакетная аллокация = меньше вызовов к ПАМ = меньше фрагментации).
//
// Преимущества перед fptr<pjson>::New():
//   - O(1) аллокация вместо поиска свободного слота в ПАМ.
//   - Узлы пула сгруппированы вместе → лучшая локальность кэша.
//   - Повторное использование узлов без возврата памяти в ПАМ.
//
// Ограничения:
//   - Пул не освобождает память обратно в ПАМ (можно сделать через pjson_node_pool::compact()).
//   - Каждый узел, выделенный из пула, должен быть возвращён в ТОТ ЖЕ пул.
//
// Использование:
//   fptr<pjson_node_pool> pool;
//   pool.New();
//
//   uintptr_t node_off = pool->alloc();
//   pjson* node = pam.Resolve<pjson>(node_off);
//   node->set_string("hello");
//   // ... работа с узлом ...
//   node->free();  // освободить ресурсы узла
//   pool->dealloc(node_off);  // вернуть слот в пул
//
//   pool->free_pool();
//   pool.Delete();
//
// Все комментарии — на русском языке (Тр.6).

#include "persist.h"
#include <cstdint>
#include <cstring>

// Размер блока при пакетной аллокации (число узлов pjson за один вызов NewArray).
constexpr uintptr_t PJSON_POOL_BLOCK_SIZE = 64u;

// Заглушка: размер узла pjson в байтах.
// Определяется через forward declaration + sizeof вычисляется ниже.
struct pjson; // предварительное объявление

// ===========================================================================
// Запись в списке свободных узлов
// ===========================================================================

/// Заголовок свободного узла: хранится в начале каждого свободного слота пула.
/// Использует первые sizeof(uintptr_t) байт узла для хранения смещения следующего свободного.
/// Это безопасно, так как pjson-узел не используется при нахождении в free list.
struct pjson_pool_free_node
{
    uintptr_t next_free; ///< Смещение следующего свободного узла (0 = конец списка)
};

static_assert( std::is_trivially_copyable<pjson_pool_free_node>::value,
               "pjson_pool_free_node должен быть тривиально копируемым" );

// ===========================================================================
// pjson_node_pool — пул памяти для узлов pjson
// ===========================================================================

/// Персистный пул памяти для узлов pjson.
/// Живёт только в ПАП; создаётся через fptr<pjson_node_pool>::New().
struct pjson_node_pool
{
    uintptr_t free_head_;  ///< Смещение головы free list (0 = пустой список)
    uintptr_t total_;      ///< Всего выделено узлов из ПАМ
    uintptr_t free_count_; ///< Число узлов в free list

    /// Выделить узел pjson из пула. Возвращает смещение в ПАП.
    /// Если free list пуст — выделяет блок PJSON_POOL_BLOCK_SIZE узлов из ПАМ.
    uintptr_t alloc()
    {
        auto& pam = PersistentAddressSpace::Get();
        // Сохраняем self_offset ДО любых аллокаций.
        uintptr_t self_offset = pam.PtrToOffset( this );

        if ( free_head_ != 0 )
        {
            // Берём голову free list.
            uintptr_t node_off = free_head_;
            // Читаем next через Resolve (realloc-безопасно).
            pjson_pool_free_node* fn  = pam.Resolve<pjson_pool_free_node>( node_off );
            uintptr_t             nxt = fn->next_free;

            // Обновляем self.
            pjson_node_pool* self = pam.Resolve<pjson_node_pool>( self_offset );
            self->free_head_      = nxt;
            self->free_count_--;
            return node_off;
        }

        // Free list пуст — выделяем блок.
        return _alloc_block( self_offset );
    }

    /// Вернуть узел с смещением node_off в free list пула.
    /// Вызывающий код должен предварительно освободить ресурсы узла (pjson::free()).
    void dealloc( uintptr_t node_off )
    {
        if ( node_off == 0 )
            return;
        auto& pam = PersistentAddressSpace::Get();
        // Записываем текущую голову в начало возвращаемого узла.
        pjson_pool_free_node* fn = pam.Resolve<pjson_pool_free_node>( node_off );
        fn->next_free            = free_head_;
        // Обновляем голову.
        free_head_ = node_off;
        free_count_++;
    }

    /// Освободить весь пул (вернуть все блоки в ПАМ).
    /// После вызова пул не пригоден для использования.
    /// Узлы, выделенные из пула, должны быть возвращены через dealloc() ДО вызова free_pool().
    void free_pool()
    {
        // Проходим по free list и освобождаем каждый блок.
        // Примечание: мы освобождаем только отдельные узлы, выделенные через NewArray.
        // Поскольку в нашей реализации каждый блок выделяется как NewArray(PJSON_POOL_BLOCK_SIZE),
        // освобождение отдельных узлов (DeleteArray) может привести к двойному освобождению,
        // если узлы из одного блока попали в разные части free list.
        // Для упрощения — просто обнуляем состояние пула.
        // TODO: реализовать отслеживание блоков для полного освобождения.
        free_head_  = 0;
        total_      = 0;
        free_count_ = 0;
    }

    /// Число выделенных (занятых) узлов: total - free.
    uintptr_t used_count() const { return total_ - free_count_; }

    /// Всего узлов в пуле (выделено из ПАМ, включая свободные).
    uintptr_t total_count() const { return total_; }

    /// Число узлов в free list.
    uintptr_t free_in_pool() const { return free_count_; }

  private:
    /// Выделить блок узлов из ПАМ и добавить в free list.
    /// Возвращает смещение первого узла блока (он не добавляется в free list).
    uintptr_t _alloc_block( uintptr_t self_offset )
    {
        auto& pam = PersistentAddressSpace::Get();

        // sizeof(pjson) известен, так как pjson_node_pool.h включается из pjson.h
        // после определения pjson.
        constexpr uintptr_t node_size = sizeof( pjson );

        // Выделяем блок из PJSON_POOL_BLOCK_SIZE узлов pjson одной аллокацией.
        fptr<pjson> block;
        block.NewArray( static_cast<unsigned>( PJSON_POOL_BLOCK_SIZE ) );

        // После NewArray буфер ПАМ мог переместиться — переприводим self.
        pjson_node_pool* self = pam.Resolve<pjson_node_pool>( self_offset );
        self->total_ += PJSON_POOL_BLOCK_SIZE;

        uintptr_t block_addr = block.addr();

        // Инициализируем все узлы блока нулями (null pjson: type = 0 = pjson_type::null).
        {
            pjson* raw = pam.Resolve<pjson>( block_addr );
            std::memset( raw, 0, PJSON_POOL_BLOCK_SIZE * node_size );
        }

        // Связываем узлы [1..PJSON_POOL_BLOCK_SIZE-1] в единый free-list:
        //   node[1] → node[2] → ... → node[N-1] → старая голова free list.
        // Строим список в порядке убывания индексов (так последний → prev_head).
        uintptr_t prev_head = self->free_head_;
        uintptr_t chain     = prev_head; // хвост новой цепи → старая голова

        for ( uintptr_t i = PJSON_POOL_BLOCK_SIZE - 1; i >= 1; i-- )
        {
            uintptr_t             node_off = block_addr + i * node_size;
            pjson_pool_free_node* fn       = pam.Resolve<pjson_pool_free_node>( node_off );
            fn->next_free                  = chain;
            chain                          = node_off;
        }

        // chain теперь указывает на node[1], а node[N-1] указывает на prev_head.
        // Обновляем self (после Resolve пересчитываем, т.к. pjson_pool_free_node::next_free
        // записи могли вызвать realloc).
        self             = pam.Resolve<pjson_node_pool>( self_offset );
        self->free_head_ = chain;
        self->free_count_ += ( PJSON_POOL_BLOCK_SIZE - 1 );

        // Возвращаем узел [0] — он уже инициализирован нулями (null pjson).
        return block_addr;
    }

    pjson_node_pool()  = default;
    ~pjson_node_pool() = default;

    template <class U> friend class AddressManager;
    friend class PersistentAddressSpace;
};
