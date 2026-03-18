#pragma once
// pjson_pool.h — Пул памяти для узлов pjson (Фаза 4).
//
// pjson_pool реализует быструю аллокацию узлов node (O(1) амортизированно)
// на основе компактного массива pvector<node> в ПАП и free-list.
//
// Принцип работы:
//   - Пул хранит pvector<node> — непрерывный массив узлов в ПАП.
//   - Свободные слоты помечаются тегом node_tag::_free и связываются
//     через поле _free_next в free-list.
//   - При аллокации: берём голову free-list (O(1)).
//     Если free-list пуст — добавляем узел в конец вектора (push_back), O(1) амортизированно.
//   - При освобождении: узел помечается node_tag::_free и добавляется в голову free-list (O(1)).
//
// Преимущества перед fptr<node>::New():
//   - O(1) амортизированная аллокация без поиска в ПАМ.
//   - Узлы пула хранятся непрерывно → лучшая локальность кэша.
//   - Повторное использование слотов без возврата памяти в ПАМ.
//
// Ограничения:
//   - Пул не уменьшает выделенный вектор при освобождении (только пометка _free).
//   - Каждый узел, выделенный из пула, должен быть возвращён в ТОТ ЖЕ пул.
//   - node_id из пула — это смещение узла внутри pvector::data_off массива, т.е.
//     реальное смещение в ПАП (как для всех node_id в системе).
//
// Использование:
//   fptr<pjson_pool> pool;
//   pool.New();
//
//   node_id id = pool->alloc();   // O(1) амортизированно
//   node& n = pool->get(id);      // прямой доступ по node_id
//   node_set_int(id, 42);         // стандартные node-хелперы
//   pool->free(id);               // вернуть слот в free-list
//
//   pool.Delete();
//
// Все комментарии — на русском языке (Тр.6).

#include "pjson_node.h"
#include <cstdint>
#include <cstring>

using namespace pjson;

// ===========================================================================
// pjson_pool — пул памяти для узлов node (Фаза 4)
// ===========================================================================

/// Персистный пул для узлов node.
/// Живёт только в ПАП; создаётся через fptr<pjson_pool>::New().
///
/// Внутренняя раскладка:
///   nodes_    — pvector<node>: непрерывный массив всех узлов пула в ПАП.
///   free_head_ — node_id головы free-list (0 = пусто).
///   free_count_ — число узлов в free-list.
///
/// node_id — смещение узла в ПАП (как везде в системе).
/// Для узлов из пула node_id == смещение узла в массиве nodes_.data_off.
struct pjson_pool
{
    // pvector<node>: непрерывный массив узлов в ПАП.
    // Раскладка: [size | capacity | data_off] = 3 * sizeof(uintptr_t).
    // Хранится inline как POD (как pmem_array_hdr).
    uintptr_t nodes_size_;     ///< Число узлов в массиве (pvector<node>::size)
    uintptr_t nodes_cap_;      ///< Ёмкость массива (pvector<node>::capacity)
    uintptr_t nodes_data_off_; ///< Смещение массива node[] в ПАП (pvector<node>::data_off)

    node_id   free_head_;  ///< node_id головы free-list; 0 = пусто
    uintptr_t free_count_; ///< Число узлов в free-list

    // -----------------------------------------------------------------------
    // Публичный API пула (Задача 4.2)
    // -----------------------------------------------------------------------

    /// Выделить новый узел из пула. Возвращает node_id (смещение в ПАП).
    /// O(1) амортизированно: сначала берём из free-list, иначе push_back.
    node_id alloc()
    {
        // Сохраняем self_offset ДО любых аллокаций (realloc-безопасность).
        uintptr_t self_off = pam_pmm_ptr_to_offset( this );

        if ( free_head_ != 0 )
        {
            // Берём голову free-list.
            node_id slot_id = free_head_;
            // Читаем _free_next через pmm_resolve (безопасно при realloc).
            node*   fn  = pmm_resolve<node>( slot_id );
            node_id nxt = ( fn != nullptr ) ? fn->_free_next : 0;

            // Обновляем self.
            pjson_pool* self = pmm_resolve<pjson_pool>( self_off );
            self->free_head_ = nxt;
            self->free_count_--;

            // Инициализируем слот нулём (null-узел).
            node* n = pmm_resolve<node>( slot_id );
            if ( n != nullptr )
            {
                std::memset( n, 0, sizeof( node ) );
                n->tag = node_tag::null;
            }

            return slot_id;
        }

        // Free-list пуст — добавляем новый узел в конец вектора.
        return _push_node( self_off );
    }

    /// Вернуть узел с node_id id в free-list пула.
    /// Узел помечается тегом node_tag::_free.
    void free( node_id id )
    {
        if ( id == 0 )
            return;

        // Помечаем узел как свободный и записываем текущую голову free-list.
        node* n = pmm_resolve<node>( id );
        if ( n == nullptr )
            return;
        n->tag        = node_tag::_free;
        n->_pad       = 0;
        n->_free_next = free_head_;

        // Обновляем голову free-list.
        free_head_ = id;
        free_count_++;
    }

    /// Получить ссылку на узел по node_id (для изменения через хелперы).
    /// Вызывающий код не должен хранить указатель после любой аллокации в ПАП.
    node& get( node_id id )
    {
        node* n = pmm_resolve<node>( id );
        return *n;
    }

    /// Получить константную ссылку на узел по node_id.
    const node& get( node_id id ) const
    {
        const node* n = pmm_resolve<node>( id );
        return *n;
    }

    // -----------------------------------------------------------------------
    // Метрики
    // -----------------------------------------------------------------------

    /// Всего узлов в пуле (выделено из ПАМ, включая свободные).
    uintptr_t total_count() const { return nodes_size_; }

    /// Число узлов в free-list (доступны для повторного использования).
    uintptr_t free_in_pool() const { return free_count_; }

    /// Число занятых (выделенных пользователем) узлов: total - free.
    uintptr_t used_count() const { return nodes_size_ - free_count_; }

    // -----------------------------------------------------------------------
    // Управление памятью пула
    // -----------------------------------------------------------------------

    /// Освободить весь массив узлов (возвращает память в ПАМ).
    /// После вызова пул не пригоден для использования.
    void free_pool()
    {
        if ( nodes_data_off_ != 0 )
        {
            // Освобождаем массив узлов через fptr.
            fptr<node> arr;
            arr.set_addr( nodes_data_off_ );
            arr.DeleteArray();
        }
        nodes_size_     = 0;
        nodes_cap_      = 0;
        nodes_data_off_ = 0;
        free_head_      = 0;
        free_count_     = 0;
    }

  private:
    // -----------------------------------------------------------------------
    // Вспомогательные методы
    // -----------------------------------------------------------------------

    /// Добавить новый null-узел в конец массива nodes_.
    /// Возвращает node_id (смещение в ПАП) нового узла.
    /// Безопасна при realloc: все обращения к self выполняются через self_off.
    node_id _push_node( uintptr_t self_off )
    {
        // Получаем текущее состояние.
        pjson_pool* self = pmm_resolve<pjson_pool>( self_off );
        if ( self == nullptr )
            return 0;

        uintptr_t sz  = self->nodes_size_;
        uintptr_t cap = self->nodes_cap_;

        if ( sz >= cap )
        {
            // Нужен grow: удваиваем ёмкость (начальная = 4).
            uintptr_t new_cap = ( cap == 0 ) ? 4u : cap * 2u;

            // Выделяем новый массив.
            fptr<node> new_arr;
            new_arr.NewArray( static_cast<unsigned>( new_cap ) ); // Может вызвать realloc!
            uintptr_t new_data = new_arr.addr();

            // Переразрешаем self после возможного realloc.
            self = pmm_resolve<pjson_pool>( self_off );
            if ( self == nullptr )
                return 0;

            // Инициализируем новые слоты нулём.
            node* raw = pmm_resolve<node>( new_data );
            if ( raw != nullptr )
                std::memset( raw, 0, new_cap * sizeof( node ) );

            // Копируем существующие узлы.
            uintptr_t old_data = self->nodes_data_off_;
            if ( old_data != 0 && sz > 0 )
            {
                const node* old_raw = pmm_resolve<node>( old_data );
                raw                 = pmm_resolve<node>( new_data );
                if ( old_raw != nullptr && raw != nullptr )
                    std::memcpy( raw, old_raw, sz * sizeof( node ) );

                // Освобождаем старый массив.
                fptr<node> old_fptr;
                old_fptr.set_addr( old_data );
                old_fptr.DeleteArray();

                // Переразрешаем self после Delete.
                self = pmm_resolve<pjson_pool>( self_off );
                if ( self == nullptr )
                    return 0;
            }

            self->nodes_data_off_ = new_data;
            self->nodes_cap_      = new_cap;
        }

        // Вычисляем смещение нового слота в ПАП.
        self           = pmm_resolve<pjson_pool>( self_off );
        uintptr_t idx  = self->nodes_size_;
        uintptr_t data = self->nodes_data_off_;

        // node_id — это смещение узла в ПАП.
        node*   raw     = pmm_resolve<node>( data );
        node_id slot_id = 0;
        if ( raw != nullptr )
        {
            node* slot = &raw[idx];
            std::memset( slot, 0, sizeof( node ) );
            slot->tag = node_tag::null;
            slot_id   = pam_pmm_ptr_to_offset( slot );
        }

        // Увеличиваем размер.
        self = pmm_resolve<pjson_pool>( self_off );
        self->nodes_size_++;

        return slot_id;
    }

    pjson_pool()  = default;
    ~pjson_pool() = default;

    template <typename U> friend class pjson::fptr_pmm;
};

static_assert( std::is_trivially_copyable<pjson_pool>::value, "pjson_pool должен быть тривиально копируемым" );
