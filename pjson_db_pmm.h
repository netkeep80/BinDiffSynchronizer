#pragma once
/**
 * @file pjson_db_pmm.h
 * @brief Менеджер персистной JSON-БД на базе PMM (Задача 14.8).
 *
 * Менеджер персистной JSON-БД на базе PersistMemoryManager (PMM).
 *
 * Использует pam_pmm_* функции, pjson_pool_pmm и pmm_resolve<T>()
 * для работы с персистным адресным пространством.
 *
 * @see pam_pmm.h — фасад ПАМ на PMM
 * @see pjson_pool_pmm.h — пул узлов на PMM
 */

#include "pam_pmm.h"
#include "pjson_pool_pmm.h"
#include "pjson_codec.h"
#include "pjson_db_helpers.h"
#include <string>
#include <string_view>
#include <vector>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <ctime>

namespace pjson
{

// ===========================================================================
// Имена именованных объектов в ПАП (PMM версия)
// ===========================================================================

/// Имя пула узлов в PMM.
static constexpr const char* PJSON_DB_PMM_POOL_NAME = "pjson_db_pmm.pool";
/// Имя корневого узла в PMM.
static constexpr const char* PJSON_DB_PMM_ROOT_NAME = "pjson_db_pmm.root";
/// Имя структуры метрик в PMM (Фаза 7).
static constexpr const char* PJSON_DB_PMM_METRICS_NAME = "pjson_db_pmm.metrics";

// ===========================================================================
// db_metrics_pmm — структура метрик персистной JSON-БД (PMM версия)
// ===========================================================================

/// Метрики базы данных и ПАП для PMM, хранящиеся персистно.
///
/// Обновляются при каждой мутирующей операции (put/alloc/intern/erase).
/// Доступ только на чтение через путь /$metrics/<имя_поля>.
struct db_metrics_pmm
{
    uint64_t node_count_total; ///< Всего узлов в пуле (включая свободные)
    uint64_t string_count_total; ///< Всего интернированных строк (не используется в PMM напрямую)
    uint64_t binary_bytes_total; ///< Всего байт в binary-узлах (приближённо)
    uint64_t ref_count;          ///< Всего ref-узлов
    uint64_t array_count;        ///< Всего массивов (array-узлов)
    uint64_t object_count;       ///< Всего объектов (object-узлов)
    uint64_t last_save_time;     ///< Unix-время последнего сохранения

    // Метрики PMM (маппятся из PamManager stats)
    uint64_t pam_bump_offset;    ///< PamManager::used_size()
    uint64_t pam_free_list_size; ///< (не поддерживается напрямую в PMM)
    uint64_t pam_total_size;     ///< PamManager::total_size()
    uint64_t pam_slot_count;     ///< pam_pmm_slot_count()
    uint64_t pam_named_count;    ///< pam_pmm_named_count()
};

static_assert( std::is_trivially_copyable<db_metrics_pmm>::value, "db_metrics_pmm должен быть тривиально копируемым" );

// ===========================================================================
// Вспомогательные функции для подсчёта узлов (PMM версия)
// ===========================================================================

/// Рекурсивный обход поддерева для подсчёта узлов по типам (PMM версия).
/// Делегирует в pjson_count_nodes_in_subtree из pjson_db_helpers.h (Задача 16.2).
/// Логика идентична — обе версии используют node_view и pmm_resolve, которые
/// работают одинаково для PMM и не-PMM бэкендов.
inline void pjson_pmm_count_nodes_in_subtree( node_id id, uint64_t& node_cnt, uint64_t& ref_cnt, uint64_t& array_cnt,
                                              uint64_t& object_cnt, uint64_t& binary_bytes )
{
    pjson_count_nodes_in_subtree( id, node_cnt, ref_cnt, array_cnt, object_cnt, binary_bytes );
}

/// Рекурсивный обход поддерева для поиска string-узлов (PMM версия).
/// Делегирует в pjson_search_node_strings_in_subtree из pjson_db_helpers.h (Задача 16.2).
inline void pjson_pmm_search_node_strings_in_subtree( node_id id, const char* pattern, std::vector<node_id>& results )
{
    pjson_search_node_strings_in_subtree( id, pattern, results );
}

// ===========================================================================
// pjson_db_pmm — менеджер персистной JSON-БД на базе PMM (Задача 14.8)
// ===========================================================================

/**
 * @brief Персистная JSON-база данных на базе PMM.
 *
 * Управляет пулом узлов и корневым объектом-узлом в ПАП.
 * Предоставляет path-адресацию, поддержку $ref, метрики и сериализацию.
 *
 * API полностью совместим с pjson_db для упрощения миграции.
 *
 * Использование:
 *   auto db = pjson_db_pmm::open("data.pam");
 *   db.put("/users/alice/name", "Alice");
 *   db.save();
 */
class pjson_db_pmm
{
  public:
    // -----------------------------------------------------------------------
    // Открытие/создание БД
    // -----------------------------------------------------------------------

    /// Открыть или создать базу данных через PMM.
    static pjson_db_pmm open( const char* pam_file )
    {
        pam_pmm_init( pam_file );
        return pjson_db_pmm{};
    }

    /// Конструктор по умолчанию: привязывается к текущему PMM.
    /// PMM должен быть проинициализирован вызовом pam_pmm_init() или open().
    pjson_db_pmm()
    {
        _ensure_pool();
        _ensure_root();
        _get_metrics_struct(); // создаёт при необходимости
    }

    // -----------------------------------------------------------------------
    // Пакетные операции (batch)
    // -----------------------------------------------------------------------

    /// Начать пакетную операцию.
    /// Пока активна хотя бы одна пакетная операция, пересчёт метрик
    /// откладывается. Вызовы batch_begin/batch_end могут быть вложенными.
    void batch_begin() { ++_batch_depth; }

    /// Завершить пакетную операцию.
    /// Когда глубина вложенности возвращается к нулю, выполняется
    /// однократный пересчёт метрик.
    void batch_end()
    {
        if ( _batch_depth == 0 )
            return;
        --_batch_depth;
        if ( _batch_depth == 0 )
            _update_metrics_after_mutation();
    }

    /// Проверить, активна ли пакетная операция.
    bool in_batch() const { return _batch_depth > 0; }

    /// RAII-обёртка для пакетных операций.
    /// При создании вызывает batch_begin(), при разрушении — batch_end().
    /// Использование:
    ///   {
    ///       auto guard = db.batch();
    ///       db.put("/a", 1);
    ///       db.put("/b", 2);
    ///   } // метрики пересчитываются один раз здесь
    class batch_guard
    {
      public:
        explicit batch_guard( pjson_db_pmm& db ) : _db( db ) { _db.batch_begin(); }
        ~batch_guard() { _db.batch_end(); }

        batch_guard( const batch_guard& )            = delete;
        batch_guard& operator=( const batch_guard& ) = delete;
        batch_guard( batch_guard&& )                 = delete;
        batch_guard& operator=( batch_guard&& )      = delete;

      private:
        pjson_db_pmm& _db;
    };

    /// Создать RAII-обёртку для пакетной операции.
    batch_guard batch() { return batch_guard{ *this }; }

    // -----------------------------------------------------------------------
    // Корневой узел
    // -----------------------------------------------------------------------

    /// Получить node_id корневого узла.
    node_id root_id() const { return _find_root(); }

    /// Получить node_view корневого узла.
    node_view root() const { return node_view{ _find_root() }; }

    // -----------------------------------------------------------------------
    // Path-адресация: get
    // -----------------------------------------------------------------------

    /// Получить узел по пути path (абсолютный путь вида /a/b/0/c).
    node_view get( const char* path, bool deref_refs = true ) const
    {
        if ( path == nullptr )
            return node_view_error( node_error::not_found );

        if ( _is_metrics_path( path ) )
            return _get_metrics( path );

        node_id cur = _find_root();
        if ( cur == 0 )
            return node_view_error( node_error::not_found );

        const char* p = path;
        if ( *p == '/' )
            ++p;

        while ( *p != '\0' )
        {
            const char* seg_start = p;
            while ( *p != '\0' && *p != '/' )
                ++p;
            uintptr_t seg_len = static_cast<uintptr_t>( p - seg_start );
            if ( *p == '/' )
                ++p;

            if ( seg_len == 0 )
                continue;

            std::string seg( seg_start, seg_len );

            if ( deref_refs )
            {
                node_view cur_v{ cur };
                if ( cur_v.is_ref() )
                {
                    node_view resolved = cur_v.deref( true, 32 );
                    if ( !resolved.valid() )
                        return node_view_error( node_error::ref_cycle );
                    cur = resolved.id;
                }
            }

            node_view cur_v{ cur };

            if ( cur_v.is_object() )
            {
                node_view child = cur_v.at( seg.c_str() );
                if ( !child.valid() )
                    return node_view_error( node_error::not_found );
                cur = child.id;
            }
            else if ( cur_v.is_array() )
            {
                char*     end_ptr = nullptr;
                uintptr_t idx     = static_cast<uintptr_t>( std::strtoull( seg.c_str(), &end_ptr, 10 ) );
                if ( end_ptr == seg.c_str() || *end_ptr != '\0' )
                    return node_view_error( node_error::wrong_type );
                node_view elem = cur_v.at( idx );
                if ( !elem.valid() )
                    return node_view_error( node_error::index_out_of_range );
                cur = elem.id;
            }
            else
            {
                return node_view_error( node_error::wrong_type );
            }
        }

        node_view result{ cur };
        if ( deref_refs && result.is_ref() )
        {
            node_view derefed = result.deref( true, 32 );
            if ( !derefed.valid() )
                return node_view_error( node_error::ref_cycle );
            return derefed;
        }
        return result;
    }

    // -----------------------------------------------------------------------
    // Path-адресация: put
    // -----------------------------------------------------------------------

    bool put_null( const char* path )
    {
        node_id slot = _ensure_path( path );
        if ( slot == 0 )
            return false;
        node_init_null( slot );
        _update_metrics_after_mutation();
        return true;
    }

    bool put( const char* path, bool value )
    {
        if ( _is_metrics_path( path ) )
            return false;
        node_id slot = _ensure_path( path );
        if ( slot == 0 )
            return false;
        node_set_bool( slot, value );
        _update_metrics_after_mutation();
        return true;
    }

    bool put( const char* path, int64_t value )
    {
        if ( _is_metrics_path( path ) )
            return false;
        node_id slot = _ensure_path( path );
        if ( slot == 0 )
            return false;
        node_set_int( slot, value );
        _update_metrics_after_mutation();
        return true;
    }

    bool put( const char* path, uint64_t value )
    {
        if ( _is_metrics_path( path ) )
            return false;
        node_id slot = _ensure_path( path );
        if ( slot == 0 )
            return false;
        node_set_uint( slot, value );
        _update_metrics_after_mutation();
        return true;
    }

    bool put( const char* path, double value )
    {
        if ( _is_metrics_path( path ) )
            return false;
        node_id slot = _ensure_path( path );
        if ( slot == 0 )
            return false;
        node_set_real( slot, value );
        _update_metrics_after_mutation();
        return true;
    }

    bool put( const char* path, const char* value )
    {
        if ( _is_metrics_path( path ) )
            return false;
        node_id slot = _ensure_path( path );
        if ( slot == 0 )
            return false;
        node_set_string( slot, value );
        _update_metrics_after_mutation();
        return true;
    }

    bool put( const char* path, int value ) { return put( path, static_cast<int64_t>( value ) ); }

    bool put_ref( const char* path, const char* ref_path )
    {
        if ( _is_metrics_path( path ) )
            return false;
        node_id slot = _ensure_path( path );
        if ( slot == 0 )
            return false;
        node_set_ref( slot, ref_path );
        _update_metrics_after_mutation();
        return true;
    }

    bool parse_into( const char* path, const char* json )
    {
        if ( _is_metrics_path( path ) )
            return false;
        node_id slot = _ensure_path( path );
        if ( slot == 0 )
            return false;
        bool ok = node_from_string( json, slot );
        if ( ok )
            _update_metrics_after_mutation();
        return ok;
    }

    // -----------------------------------------------------------------------
    // Path-адресация: erase
    // -----------------------------------------------------------------------

    bool erase( const char* path )
    {
        if ( path == nullptr )
            return false;
        if ( _is_metrics_path( path ) )
            return false;

        std::string parent_path;
        std::string last_seg;
        pjson_split_path( path, parent_path, last_seg );

        node_id parent_id = 0;
        if ( parent_path.empty() || parent_path == "/" )
        {
            parent_id = _find_root();
        }
        else
        {
            node_view pv = get( parent_path.c_str(), false );
            parent_id    = pv.id;
        }

        if ( parent_id == 0 )
            return false;

        node_view parent{ parent_id };

        if ( parent.is_object() )
        {
            node_view child = parent.at( last_seg.c_str() );
            if ( !child.valid() )
                return false;
            _free_node_tree( child.id );
            bool ok = _object_erase( parent_id, last_seg.c_str() );
            if ( ok )
                _update_metrics_after_mutation();
            return ok;
        }
        else if ( parent.is_array() )
        {
            char*     end_ptr = nullptr;
            uintptr_t idx     = static_cast<uintptr_t>( std::strtoull( last_seg.c_str(), &end_ptr, 10 ) );
            if ( end_ptr == last_seg.c_str() || *end_ptr != '\0' )
                return false;
            node_view elem = parent.at( idx );
            if ( !elem.valid() )
                return false;
            _free_node_tree( elem.id );
            bool ok = _array_erase_at( parent_id, idx );
            if ( ok )
                _update_metrics_after_mutation();
            return ok;
        }
        return false;
    }

    bool exists( const char* path ) const
    {
        if ( path == nullptr )
            return false;
        if ( _is_metrics_path( path ) )
            return true;
        return get( path, false ).valid();
    }

    // -----------------------------------------------------------------------
    // $ref: разыменование
    // -----------------------------------------------------------------------

    node_view resolve_ref( node_id id, uintptr_t max_depth = 32 ) const
    {
        return node_view{ id }.deref( true, max_depth );
    }

    void resolve_all_refs()
    {
        node_id root = _find_root();
        if ( root == 0 )
            return;
        _resolve_refs_in_subtree( root );
    }

    // -----------------------------------------------------------------------
    // Сериализация
    // -----------------------------------------------------------------------

    std::string dump( node_id id ) const { return node_to_string( id ); }

    std::string dump() const { return node_to_string( _find_root() ); }

    bool parse( const char* json )
    {
        node_id root = _find_root();
        if ( root == 0 )
            return false;
        bool ok = node_from_string( json, root );
        if ( ok )
            _update_metrics_after_mutation();
        return ok;
    }

    // -----------------------------------------------------------------------
    // Сохранение
    // -----------------------------------------------------------------------

    void save()
    {
        db_metrics_pmm* m = _get_metrics_struct();
        if ( m != nullptr )
        {
            _fill_metrics( m );
            m->last_save_time = static_cast<uint64_t>( std::time( nullptr ) );
        }
        pam_pmm_save();
    }

    // -----------------------------------------------------------------------
    // Метрики
    // -----------------------------------------------------------------------

    node_view metrics( const char* subpath = nullptr ) const
    {
        if ( subpath == nullptr )
            return node_view{};
        std::string full = "/$metrics/";
        full += subpath;
        return _get_metrics( full.c_str() );
    }

    void update_metrics()
    {
        db_metrics_pmm* m = _get_metrics_struct();
        if ( m == nullptr )
            return;
        _fill_metrics( m );
    }

    // -----------------------------------------------------------------------
    // Поиск по строкам
    // -----------------------------------------------------------------------

    std::vector<pstringview_search_result> search_strings( const char* pattern ) const
    {
        return pam_search_strings( pattern );
    }

    std::vector<pstringview_search_result> all_strings() const { return pam_all_strings(); }

    std::vector<node_id> search_node_strings( const char* pattern ) const
    {
        std::vector<node_id> results;
        node_id              root = _find_root();
        if ( root == 0 )
            return results;
        pjson_pmm_search_node_strings_in_subtree( root, pattern != nullptr ? pattern : "", results );
        return results;
    }

    // -----------------------------------------------------------------------
    // Интерфейс pmap<pstringview, node_id>
    // -----------------------------------------------------------------------

    node_view operator[]( const char* path )
    {
        if ( path == nullptr )
            return node_view{};
        if ( _is_metrics_path( path ) )
            return _get_metrics( path );
        node_view existing = get( path, false );
        if ( existing.valid() )
            return existing;
        node_id slot = _ensure_path( path );
        if ( slot == 0 )
            return node_view{};
        _update_metrics_after_mutation();
        return node_view{ slot };
    }

    node_view find( const char* path ) const { return get( path, false ); }

    node_view insert( const char* path, const char* json_value )
    {
        if ( path == nullptr || json_value == nullptr )
            return node_view{};
        if ( _is_metrics_path( path ) )
            return node_view{};
        node_id slot = _ensure_path( path );
        if ( slot == 0 )
            return node_view{};
        bool ok = node_from_string( json_value, slot );
        if ( ok )
            _update_metrics_after_mutation();
        return ok ? node_view{ slot } : node_view{};
    }

    // -----------------------------------------------------------------------
    // Глубокое копирование
    // -----------------------------------------------------------------------

    bool clone( const char* src_path, const char* dest_path )
    {
        if ( src_path == nullptr || dest_path == nullptr )
            return false;
        if ( _is_metrics_path( src_path ) || _is_metrics_path( dest_path ) )
            return false;

        node_view src_view = get( src_path, false );
        if ( !src_view.valid() )
            return false;

        node_id cloned_id = node_clone( src_view.id );
        if ( cloned_id == 0 )
            return false;

        std::string parent_path;
        std::string last_seg;
        pjson_split_path( dest_path, parent_path, last_seg );

        node_id parent_id = 0;
        if ( parent_path.empty() || parent_path == "/" )
        {
            parent_id = _find_root();
        }
        else
        {
            parent_id = _ensure_path( parent_path.c_str() );
            if ( parent_id != 0 )
            {
                node_view pv{ parent_id };
                if ( pv.is_null() )
                    node_set_object( parent_id );
            }
        }

        if ( parent_id == 0 )
        {
            _free_node_tree( cloned_id );
            return false;
        }

        node* parent_node = pmm_resolve<node>( parent_id );
        if ( parent_node == nullptr )
        {
            _free_node_tree( cloned_id );
            return false;
        }

        if ( parent_node->tag == node_tag::object )
        {
            node_view existing = node_view{ parent_id }.at( last_seg.c_str() );
            if ( existing.valid() )
            {
                _free_node_tree( existing.id );
                _object_erase( parent_id, last_seg.c_str() );
            }
            _object_insert_direct( parent_id, last_seg.c_str(), cloned_id );
        }
        else if ( parent_node->tag == node_tag::array )
        {
            char*     end_ptr = nullptr;
            uintptr_t idx     = static_cast<uintptr_t>( std::strtoull( last_seg.c_str(), &end_ptr, 10 ) );
            if ( end_ptr == last_seg.c_str() || *end_ptr != '\0' )
            {
                _free_node_tree( cloned_id );
                return false;
            }
            node_view existing = node_view{ parent_id }.at( idx );
            if ( existing.valid() )
            {
                _free_node_tree( existing.id );
            }
            _array_set_direct( parent_id, idx, cloned_id );
        }
        else
        {
            _free_node_tree( cloned_id );
            return false;
        }

        _update_metrics_after_mutation();
        return true;
    }

    node_id clone( node_id src_id ) { return node_clone( src_id ); }

    // -----------------------------------------------------------------------
    // Доступ к PMM метрикам напрямую
    // -----------------------------------------------------------------------

    /// Получить использованный размер PMM (аналог GetBump).
    uintptr_t pmm_used_size() const { return pam_pmm_get_bump(); }

    /// Получить общий размер PMM (аналог GetDataSize).
    uintptr_t pmm_total_size() const { return pam_pmm_get_data_size(); }

    /// Получить количество слотов (объектов) в реестре.
    uintptr_t pmm_slot_count() const { return pam_pmm_slot_count(); }

    /// Получить количество именованных объектов.
    uintptr_t pmm_named_count() const { return pam_pmm_named_count(); }

  private:
    uintptr_t _batch_depth = 0; ///< Глубина вложенности пакетных операций.

    // -----------------------------------------------------------------------
    // Вспомогательные методы: пул и корень
    // -----------------------------------------------------------------------

    uintptr_t _find_pool_offset() const { return pam_pmm_find( PJSON_DB_PMM_POOL_NAME ); }

    pjson_pool_pmm* _get_pool() const
    {
        uintptr_t off = _find_pool_offset();
        if ( off == 0 )
            return nullptr;
        return pmm_resolve<pjson_pool_pmm>( off );
    }

    void _ensure_pool()
    {
        if ( _find_pool_offset() != 0 )
            return;

        // Создаём пул через PMM.
        uintptr_t pool_off = pjson_pool_pmm_create();
        if ( pool_off == 0 )
            return;

        // Регистрируем пул в реестре имён PMM.
        pam_pmm_registry* reg = pam_pmm_get_registry();
        if ( reg != nullptr )
        {
            pam_pmm_name_key nk{};
            std::strncpy( nk.name, PJSON_DB_PMM_POOL_NAME, PAM_PMM_NAME_SIZE - 1 );

            pam_pmm_slot_info slot_info{};
            slot_info.offset    = pool_off;
            slot_info.elem_size = sizeof( pjson_pool_pmm );
            slot_info.count     = 1;

            reg->name_map_.insert_direct( nk, slot_info );
        }
    }

    node_id _find_root() const { return pam_pmm_find( PJSON_DB_PMM_ROOT_NAME ); }

    void _ensure_root()
    {
        if ( _find_root() != 0 )
            return;

        // Создаём корневой узел через PMM.
        uintptr_t root_off = pam_pmm_create<node>( PJSON_DB_PMM_ROOT_NAME );
        if ( root_off == 0 )
            return;

        node_set_object( root_off );
    }

    // -----------------------------------------------------------------------
    // Вспомогательные методы: путевая адресация
    // -----------------------------------------------------------------------

    static bool _is_metrics_path( const char* path )
    {
        if ( path == nullptr )
            return false;
        return std::strncmp( path, "/$metrics", 9 ) == 0;
    }

    /// Кэш подсчёта узлов по типам для метрик (Задача 16.4).
    /// Вычисляется однократно при обращении к любой метрике из группы subtree.
    struct subtree_counts
    {
        uint64_t node_cnt     = 0;
        uint64_t ref_cnt      = 0;
        uint64_t array_cnt    = 0;
        uint64_t object_cnt   = 0;
        uint64_t binary_bytes = 0;
        bool     computed     = false;

        void ensure( node_id root )
        {
            if ( computed )
                return;
            if ( root != 0 )
                pjson_count_nodes_in_subtree( root, node_cnt, ref_cnt, array_cnt, object_cnt, binary_bytes );
            computed = true;
        }
    };

    node_view _get_metrics( const char* path ) const
    {
        const char* key = path + 9;
        if ( *key == '/' )
            ++key;

        // Создаём временный узел для возврата значения метрики.
        uintptr_t tmp_off = pam_pmm_create<node>();
        if ( tmp_off == 0 )
            return node_view{};

        uintptr_t pool_off = _find_pool_offset();

        // Задача 16.4: реестр метрик вместо цепочки strcmp.
        // Каждая запись: { имя, лямбда вычисления значения }.
        // Лямбды принимают ссылку на subtree_counts для ленивого вычисления.
        struct metric_entry
        {
            const char* name;
            uint64_t ( *getter )( uintptr_t pool_off, node_id root, subtree_counts& sc );
        };

        // Функции-геттеры определены как статические лямбды для компактности.
        static constexpr auto get_free_node_count = []( uintptr_t p, node_id, subtree_counts& ) -> uint64_t
        { return pjson_pool_pmm_free_in_pool( p ); };
        static constexpr auto get_used_node_count = []( uintptr_t p, node_id, subtree_counts& ) -> uint64_t
        { return pjson_pool_pmm_used_count( p ); };
        static constexpr auto get_string_count = []( uintptr_t, node_id, subtree_counts& ) -> uint64_t
        { return static_cast<uint64_t>( pam_all_strings().size() ); };
        static constexpr auto get_pam_bump = []( uintptr_t, node_id, subtree_counts& ) -> uint64_t
        { return static_cast<uint64_t>( pam_pmm_get_bump() ); };
        static constexpr auto get_pam_free_list = []( uintptr_t, node_id, subtree_counts& ) -> uint64_t
        {
            return 0u; // PMM не имеет прямого эквивалента free_list_size
        };
        static constexpr auto get_pam_total_size = []( uintptr_t, node_id, subtree_counts& ) -> uint64_t
        { return static_cast<uint64_t>( pam_pmm_get_data_size() ); };
        static constexpr auto get_pam_slot_count = []( uintptr_t, node_id, subtree_counts& ) -> uint64_t
        { return static_cast<uint64_t>( pam_pmm_slot_count() ); };
        static constexpr auto get_pam_named_count = []( uintptr_t, node_id, subtree_counts& ) -> uint64_t
        { return static_cast<uint64_t>( pam_pmm_named_count() ); };
        static constexpr auto get_node_count_total = []( uintptr_t p, node_id r, subtree_counts& sc ) -> uint64_t
        {
            sc.ensure( r );
            uint64_t pool_total = pjson_pool_pmm_total_count( p );
            return pool_total > 0 ? pool_total : sc.node_cnt;
        };
        static constexpr auto get_ref_count = []( uintptr_t, node_id r, subtree_counts& sc ) -> uint64_t
        {
            sc.ensure( r );
            return sc.ref_cnt;
        };
        static constexpr auto get_array_count = []( uintptr_t, node_id r, subtree_counts& sc ) -> uint64_t
        {
            sc.ensure( r );
            return sc.array_cnt;
        };
        static constexpr auto get_object_count = []( uintptr_t, node_id r, subtree_counts& sc ) -> uint64_t
        {
            sc.ensure( r );
            return sc.object_cnt;
        };
        static constexpr auto get_binary_bytes = []( uintptr_t, node_id r, subtree_counts& sc ) -> uint64_t
        {
            sc.ensure( r );
            return sc.binary_bytes;
        };

        // Реестр метрик.
        static const metric_entry registry[] = {
            { "free_node_count", get_free_node_count },
            { "used_node_count", get_used_node_count },
            { "string_count_total", get_string_count },
            { "string_count", get_string_count },
            { "pam_bump_offset", get_pam_bump },
            { "pam_free_list_size", get_pam_free_list },
            { "pam_total_size", get_pam_total_size },
            { "pam_slot_count", get_pam_slot_count },
            { "pam_named_count", get_pam_named_count },
            { "node_count_total", get_node_count_total },
            { "ref_count", get_ref_count },
            { "array_count", get_array_count },
            { "object_count", get_object_count },
            { "binary_bytes_total", get_binary_bytes },
        };

        node_id        root = _find_root();
        subtree_counts sc;

        // Поиск метрики в реестре.
        for ( const auto& entry : registry )
        {
            if ( std::strcmp( key, entry.name ) == 0 )
            {
                uint64_t val = entry.getter( pool_off, root, sc );
                node_set_uint( tmp_off, val );
                return node_view{ tmp_off };
            }
        }

        // last_save_time из структуры метрик (не в реестре — зависит от персистной структуры).
        const db_metrics_pmm* m = _get_metrics_struct_const();
        if ( m != nullptr && std::strcmp( key, "last_save_time" ) == 0 )
        {
            node_set_uint( tmp_off, m->last_save_time );
            return node_view{ tmp_off };
        }

        // Неизвестная метрика
        node_init_null( tmp_off );
        return node_view{ tmp_off };
    }

    node_id _ensure_path( const char* path )
    {
        if ( path == nullptr )
            return 0;

        node_id cur = _find_root();
        if ( cur == 0 )
            return 0;

        const char* p = path;
        if ( *p == '/' )
            ++p;

        while ( *p != '\0' )
        {
            const char* seg_start = p;
            while ( *p != '\0' && *p != '/' )
                ++p;
            uintptr_t seg_len = static_cast<uintptr_t>( p - seg_start );
            if ( *p == '/' )
                ++p;

            if ( seg_len == 0 )
                continue;

            std::string seg( seg_start, seg_len );

            node_view cur_v{ cur };

            if ( cur_v.is_ref() )
            {
                node_view resolved = cur_v.deref( true, 32 );
                if ( !resolved.valid() )
                    return 0;
                cur   = resolved.id;
                cur_v = resolved;
            }

            bool is_last = ( *p == '\0' );

            if ( cur_v.is_object() )
            {
                node_view child = cur_v.at( seg.c_str() );
                if ( child.valid() )
                {
                    cur = child.id;
                }
                else
                {
                    node_id slot = node_object_insert( cur, seg.c_str() );
                    if ( slot == 0 )
                        return 0;
                    if ( !is_last )
                    {
                        if ( pjson_next_seg_is_numeric( p ) )
                            node_set_array( slot );
                        else
                            node_set_object( slot );
                    }
                    cur = slot;
                }
            }
            else if ( cur_v.is_array() )
            {
                char*     end_ptr = nullptr;
                uintptr_t idx     = static_cast<uintptr_t>( std::strtoull( seg.c_str(), &end_ptr, 10 ) );
                if ( end_ptr == seg.c_str() || *end_ptr != '\0' )
                    return 0;
                node_view elem = cur_v.at( idx );
                if ( elem.valid() )
                {
                    cur = elem.id;
                }
                else
                {
                    uintptr_t cur_size = cur_v.size();
                    for ( uintptr_t i = cur_size; i <= idx; ++i )
                    {
                        node_id slot = node_array_push_back( cur );
                        if ( i == idx )
                        {
                            if ( !is_last )
                            {
                                if ( pjson_next_seg_is_numeric( p ) )
                                    node_set_array( slot );
                                else
                                    node_set_object( slot );
                            }
                            cur = slot;
                        }
                    }
                }
            }
            else if ( is_last )
            {
                return cur;
            }
            else
            {
                return 0;
            }
        }

        return cur;
    }

    // -----------------------------------------------------------------------
    // Вспомогательные методы: удаление узлов
    // -----------------------------------------------------------------------

    void _free_node_tree( node_id id )
    {
        if ( id == 0 )
            return;
        node_view v{ id };
        if ( !v.valid() )
            return;

        switch ( v.tag() )
        {
        case node_tag::array:
        {
            uintptr_t sz = v.size();
            for ( uintptr_t i = 0; i < sz; ++i )
            {
                node_view elem = v.at( i );
                if ( elem.valid() )
                    _free_node_tree( elem.id );
            }
            node* n = pmm_resolve<node>( id );
            if ( n != nullptr && n->array_val.data_off != 0 )
            {
                auto arr_pptr = offset_to_pptr<node_id>( n->array_val.data_off );
                PamManager::template deallocate_typed<node_id>( arr_pptr );
            }
            break;
        }
        case node_tag::object:
        {
            uintptr_t sz = v.size();
            for ( uintptr_t i = 0; i < sz; ++i )
            {
                node_view val = v.value_at( i );
                if ( val.valid() )
                    _free_node_tree( val.id );
            }
            node* n = pmm_resolve<node>( id );
            if ( n != nullptr && n->object_val.data_off != 0 )
            {
                auto arr_pptr = offset_to_pptr<object_entry>( n->object_val.data_off );
                PamManager::template deallocate_typed<object_entry>( arr_pptr );
            }
            break;
        }
        case node_tag::string:
        {
            node* n = pmm_resolve<node>( id );
            if ( n != nullptr && n->string_val.chars_offset != 0 )
            {
                auto str_pptr = offset_to_pptr<char>( n->string_val.chars_offset );
                PamManager::template deallocate_typed<char>( str_pptr );
            }
            break;
        }
        case node_tag::binary:
        {
            node* n = pmm_resolve<node>( id );
            if ( n != nullptr && n->binary_val.data_off != 0 )
            {
                auto bin_pptr = offset_to_pptr<uint8_t>( n->binary_val.data_off );
                PamManager::template deallocate_typed<uint8_t>( bin_pptr );
            }
            break;
        }
        case node_tag::ref:
            break;
        default:
            break;
        }

        // Освобождаем сам узел.
        pam_pmm_delete( id );
    }

    bool _object_erase( node_id obj_id, const char* key )
    {
        if ( obj_id == 0 || key == nullptr )
            return false;

        node* n = node_resolve_checked( obj_id, node_tag::object );
        if ( n == nullptr )
            return false;

        uintptr_t sz = n->object_val.size;
        if ( sz == 0 || n->object_val.data_off == 0 )
            return false;

        // Делегируем бинарный поиск в node_object_find_key (Задача 16.2).
        auto find_result = node_object_find_key( n->object_val.data_off, sz, key );
        if ( !find_result.found )
            return false;
        uintptr_t del_idx = find_result.index;

        uintptr_t     data_off = n->object_val.data_off;
        object_entry* entries  = pmm_resolve<object_entry>( data_off );
        if ( entries == nullptr )
            return false;
        for ( uintptr_t i = del_idx; i + 1 < sz; ++i )
            entries[i] = entries[i + 1];

        n = pmm_resolve<node>( obj_id );
        if ( n != nullptr )
            n->object_val.size--;

        return true;
    }

    bool _array_erase_at( node_id arr_id, uintptr_t idx )
    {
        node* n = node_resolve_checked( arr_id, node_tag::array );
        if ( n == nullptr )
            return false;

        uintptr_t sz = n->array_val.size;
        if ( idx >= sz )
            return false;

        uintptr_t data_off = n->array_val.data_off;
        node_id*  arr      = pmm_resolve<node_id>( data_off );
        if ( arr == nullptr )
            return false;

        for ( uintptr_t i = idx; i + 1 < sz; ++i )
            arr[i] = arr[i + 1];
        arr[sz - 1] = 0;

        n = pmm_resolve<node>( arr_id );
        if ( n != nullptr )
            n->array_val.size--;

        return true;
    }

    bool _object_insert_direct( node_id obj_id, const char* key, node_id value_id )
    {
        if ( obj_id == 0 || key == nullptr || value_id == 0 )
            return false;

        auto key_result = pam_intern_string( key );

        // Задача 16.3: используем node_resolve_checked вместо дублирования guard-паттерна.
        node* n = node_resolve_checked( obj_id, node_tag::object );
        if ( n == nullptr )
            return false;

        object_entry new_entry;
        new_entry.key_length       = key_result.length;
        new_entry.key_chars_offset = key_result.chars_offset;
        new_entry.value            = value_id;

        // Задача 16.2: используем общие функторы object_entry_key_of / object_entry_less.
        n = pmm_resolve<node>( obj_id );
        if ( n == nullptr )
            return false;

        uintptr_t hdr_off = pam_pmm_ptr_to_offset( reinterpret_cast<pmem_array_hdr_pmm*>( &( n->object_val.size ) ) );
        if ( hdr_off == 0 )
            return false;

        pmem_array_pmm_insert_sorted<object_entry, object_entry_key_of, object_entry_less>(
            hdr_off, new_entry, object_entry_key_of{}, object_entry_less{} );

        return true;
    }

    bool _array_set_direct( node_id arr_id, uintptr_t idx, node_id value_id )
    {
        if ( arr_id == 0 || value_id == 0 )
            return false;

        // Задача 16.3: используем node_resolve_checked.
        node* n = node_resolve_checked( arr_id, node_tag::array );
        if ( n == nullptr )
            return false;

        uintptr_t current_size = n->array_val.size;

        if ( idx < current_size )
        {
            node_id* arr = pmm_resolve<node_id>( n->array_val.data_off );
            if ( arr != nullptr )
            {
                arr[idx] = value_id;
                return true;
            }
            return false;
        }

        while ( current_size < idx )
        {
            node_array_push_back( arr_id );
            n = pmm_resolve<node>( arr_id );
            if ( n == nullptr )
                return false;
            current_size = n->array_val.size;
        }

        node_array_push_back( arr_id );
        n = pmm_resolve<node>( arr_id );
        if ( n == nullptr || n->array_val.data_off == 0 )
            return false;

        node_id* arr = pmm_resolve<node_id>( n->array_val.data_off );
        if ( arr == nullptr )
            return false;

        if ( arr[idx] != 0 )
        {
            pam_pmm_delete( arr[idx] );
        }
        arr[idx] = value_id;

        return true;
    }

    // -----------------------------------------------------------------------
    // Вспомогательные методы: метрики
    // -----------------------------------------------------------------------

    db_metrics_pmm* _get_metrics_struct()
    {
        uintptr_t off = pam_pmm_find( PJSON_DB_PMM_METRICS_NAME );
        if ( off == 0 )
        {
            off = pam_pmm_create<db_metrics_pmm>( PJSON_DB_PMM_METRICS_NAME );
            if ( off == 0 )
                return nullptr;
            db_metrics_pmm* mp = pmm_resolve<db_metrics_pmm>( off );
            if ( mp != nullptr )
                std::memset( mp, 0, sizeof( db_metrics_pmm ) );
        }

        return pmm_resolve<db_metrics_pmm>( off );
    }

    const db_metrics_pmm* _get_metrics_struct_const() const
    {
        uintptr_t off = pam_pmm_find( PJSON_DB_PMM_METRICS_NAME );
        if ( off == 0 )
            return nullptr;
        return pmm_resolve_const<db_metrics_pmm>( off );
    }

    void _fill_metrics( db_metrics_pmm* m ) const
    {
        if ( m == nullptr )
            return;

        uintptr_t pool_off = _find_pool_offset();

        // Базовые метрики пула
        m->node_count_total = pjson_pool_pmm_total_count( pool_off );

        // Метрики PMM
        m->pam_bump_offset    = static_cast<uint64_t>( pam_pmm_get_bump() );
        m->pam_free_list_size = 0; // PMM не имеет прямого эквивалента
        m->pam_total_size     = static_cast<uint64_t>( pam_pmm_get_data_size() );
        m->pam_slot_count     = static_cast<uint64_t>( pam_pmm_slot_count() );
        m->pam_named_count    = static_cast<uint64_t>( pam_pmm_named_count() );

        // Метрики строк
        m->string_count_total = static_cast<uint64_t>( pam_all_strings().size() );

        // Подсчёт по типам узлов
        uint64_t binary_bytes = 0;
        uint64_t ref_cnt      = 0;
        uint64_t array_cnt    = 0;
        uint64_t object_cnt   = 0;
        uint64_t node_cnt     = 0;

        node_id root = _find_root();
        if ( root != 0 )
            pjson_pmm_count_nodes_in_subtree( root, node_cnt, ref_cnt, array_cnt, object_cnt, binary_bytes );

        if ( m->node_count_total == 0 )
            m->node_count_total = node_cnt;

        m->binary_bytes_total = binary_bytes;
        m->ref_count          = ref_cnt;
        m->array_count        = array_cnt;
        m->object_count       = object_cnt;
    }

    void _update_metrics_after_mutation()
    {
        if ( _batch_depth > 0 )
            return; // откладываем пересчёт до завершения пакетной операции
        db_metrics_pmm* m = _get_metrics_struct();
        if ( m == nullptr )
            return;
        _fill_metrics( m );
    }

    // -----------------------------------------------------------------------
    // Вспомогательные методы: разрешение ref
    // -----------------------------------------------------------------------

    void _resolve_refs_in_subtree( node_id id )
    {
        if ( id == 0 )
            return;
        node_view v{ id };
        if ( !v.valid() )
            return;

        if ( v.is_ref() )
        {
            std::string_view path_sv = v.ref_path();
            if ( !path_sv.empty() )
            {
                std::string path_str( path_sv );
                node_view   target = get( path_str.c_str(), false );
                if ( target.valid() && !target.is_ref() )
                    node_set_ref_target( id, target.id );
            }
            return;
        }

        if ( v.is_array() )
        {
            uintptr_t sz = v.size();
            for ( uintptr_t i = 0; i < sz; ++i )
            {
                node_view elem = v.at( i );
                if ( elem.valid() )
                    _resolve_refs_in_subtree( elem.id );
            }
        }
        else if ( v.is_object() )
        {
            uintptr_t sz = v.size();
            for ( uintptr_t i = 0; i < sz; ++i )
            {
                node_view val = v.value_at( i );
                if ( val.valid() )
                    _resolve_refs_in_subtree( val.id );
            }
        }
    }
};

} // namespace pjson
