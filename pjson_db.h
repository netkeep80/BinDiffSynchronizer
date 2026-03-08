#pragma once
// pjson_db.h — Менеджер персистной JSON-базы данных (Фазы 6–7).
//
// pjson_db — высокоуровневый API для работы с персистной JSON-БД:
//   - Path-адресация через строковые пути вида /a/b/0/c
//   - Поддержка $ref (разыменование ссылок, обнаружение циклов)
//   - Метрики через зарезервированное пространство /$metrics (Фаза 7)
//   - Полнотекстовый поиск по всем строкам словаря
//   - Сохранение/загрузка образа ПАП
//
// Единственный заголовок для конечного пользователя (Тр.18).
// Все комментарии — на русском языке (Тр.6).

#include "pjson_codec.h"
#include <string>
#include <string_view>
#include <vector>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <ctime>

// ===========================================================================
// Имена именованных объектов в ПАП
// ===========================================================================

/// Имя пула узлов в ПАМ.
static constexpr const char* PJSON_DB_POOL_NAME = "pjson_db.pool";
/// Имя корневого узла в ПАМ.
static constexpr const char* PJSON_DB_ROOT_NAME = "pjson_db.root";
/// Имя структуры метрик в ПАМ (Фаза 7).
static constexpr const char* PJSON_DB_METRICS_NAME = "pjson_db.metrics";

// ===========================================================================
// db_metrics — структура метрик персистной JSON-БД (Задача 7.1)
// ===========================================================================

/// Метрики базы данных и ПАП, хранящиеся персистно в ПАМ.
///
/// Обновляются при каждой мутирующей операции (put/alloc/intern/erase).
/// Доступ только на чтение через путь /$metrics/<имя_поля>.
///
/// Все поля тривиально копируемы; структура живёт в ПАП.
struct db_metrics
{
    uint64_t node_count_total;   ///< Всего узлов в пуле (включая свободные)
    uint64_t string_count_total; ///< Всего интернированных строк в словаре
    uint64_t binary_bytes_total; ///< Всего байт в binary-узлах (приближённо)
    uint64_t ref_count;          ///< Всего ref-узлов в пуле
    uint64_t array_count;        ///< Всего массивов (array-узлов) в пуле
    uint64_t object_count;       ///< Всего объектов (object-узлов) в пуле
    uint64_t last_save_time; ///< Unix-время последнего сохранения (0 = не сохранялось)

    // Метрики ПАМ (проксируются из PersistentAddressSpace)
    uint64_t pam_bump_offset;    ///< Текущая позиция bump-аллокатора в ПАП (байт)
    uint64_t pam_free_list_size; ///< Число свободных блоков в free-list ПАМ
    uint64_t pam_total_size;     ///< Полный размер области данных ПАМ (байт)
    uint64_t pam_slot_count;     ///< Число аллоцированных слотов в ПАМ
    uint64_t pam_named_count;    ///< Число именованных объектов в ПАМ
};

static_assert( std::is_trivially_copyable<db_metrics>::value, "db_metrics должен быть тривиально копируемым" );

// ===========================================================================
// pjson_db — менеджер персистной JSON-базы данных (Задача 6.1)
// ===========================================================================

/// Персистная JSON-база данных поверх персистного адресного пространства (ПАП).
///
/// Управляет пулом узлов и корневым объектом-узлом в ПАП.
/// Предоставляет path-адресацию, поддержку $ref, метрики и сериализацию.
///
/// Использование:
///   auto db = pjson_db::open("data.pam");
///   db.put("/users/alice/name", "Alice");
///   db.save();
class pjson_db
{
  public:
    // -----------------------------------------------------------------------
    // Открытие/создание БД (Задача 6.1)
    // -----------------------------------------------------------------------

    /// Открыть или создать базу данных.
    /// Инициализирует ПАП (или загружает существующий образ) и гарантирует
    /// наличие пула узлов и корневого объекта.
    static pjson_db open( const char* pam_file )
    {
        auto& pam = PersistentAddressSpace::Get();
        pam.Init( pam_file );
        return pjson_db{};
    }

    /// Конструктор по умолчанию: привязывается к текущему ПАП.
    /// ПАП должен быть проинициализирован вызовом PersistentAddressSpace::Init()
    /// или pjson_db::open().
    pjson_db()
    {
        _ensure_pool();
        _ensure_root();
        // Инициализируем структуру метрик (Фаза 7).
        _get_metrics_struct(); // создаёт при необходимости
    }

    // -----------------------------------------------------------------------
    // Корневой узел
    // -----------------------------------------------------------------------

    /// Получить node_id корневого узла.
    node_id root_id() const { return _find_root(); }

    /// Получить node_view корневого узла.
    node_view root() const { return node_view{ _find_root() }; }

    // -----------------------------------------------------------------------
    // Path-адресация: get (Задача 6.3)
    // -----------------------------------------------------------------------

    /// Получить узел по пути path (абсолютный путь вида /a/b/0/c).
    /// По умолчанию разыменовывает $ref-узлы автоматически.
    /// Зарезервированные пространства: /$metrics — метрики (только чтение).
    /// Возвращает node_view(0) при ошибке.
    node_view get( const char* path, bool deref_refs = true ) const
    {
        if ( path == nullptr )
            return node_view{};

        // Проверяем зарезервированное пространство /$metrics
        if ( _is_metrics_path( path ) )
            return _get_metrics( path );

        node_id cur = _find_root();
        if ( cur == 0 )
            return node_view{};

        const char* p = path;
        // Пропускаем ведущий слэш.
        if ( *p == '/' )
            ++p;

        while ( *p != '\0' )
        {
            const char* seg_start = p;
            while ( *p != '\0' && *p != '/' )
                ++p;
            uintptr_t seg_len = static_cast<uintptr_t>( p - seg_start );
            if ( *p == '/' )
                ++p; // пропускаем разделитель

            if ( seg_len == 0 )
                continue;

            std::string seg( seg_start, seg_len );

            // Если текущий узел — ref, разыменовываем его перед навигацией.
            if ( deref_refs )
            {
                node_view cur_v{ cur };
                if ( cur_v.is_ref() )
                {
                    node_view resolved = cur_v.deref( true, 32 );
                    if ( !resolved.valid() )
                        return node_view{};
                    cur = resolved.id;
                }
            }

            node_view cur_v{ cur };

            if ( cur_v.is_object() )
            {
                node_view child = cur_v.at( seg.c_str() );
                if ( !child.valid() )
                    return node_view{};
                cur = child.id;
            }
            else if ( cur_v.is_array() )
            {
                // Интерпретируем сегмент как числовой индекс.
                char*     end_ptr = nullptr;
                uintptr_t idx     = static_cast<uintptr_t>( std::strtoull( seg.c_str(), &end_ptr, 10 ) );
                if ( end_ptr == seg.c_str() || *end_ptr != '\0' )
                    return node_view{};
                node_view elem = cur_v.at( idx );
                if ( !elem.valid() )
                    return node_view{};
                cur = elem.id;
            }
            else
            {
                return node_view{};
            }
        }

        node_view result{ cur };
        // Разыменовываем итоговый узел, если он ref.
        if ( deref_refs && result.is_ref() )
            return result.deref( true, 32 );
        return result;
    }

    // -----------------------------------------------------------------------
    // Path-адресация: put (Задача 6.3)
    // -----------------------------------------------------------------------

    /// Записать null-значение по пути path.
    bool put_null( const char* path )
    {
        node_id slot = _ensure_path( path );
        if ( slot == 0 )
            return false;
        node_init_null( slot );
        _update_metrics_after_mutation();
        return true;
    }

    /// Записать boolean-значение по пути path.
    bool put( const char* path, bool value )
    {
        // Запрет записи в /$metrics
        if ( _is_metrics_path( path ) )
            return false;
        node_id slot = _ensure_path( path );
        if ( slot == 0 )
            return false;
        node_set_bool( slot, value );
        _update_metrics_after_mutation();
        return true;
    }

    /// Записать integer-значение по пути path.
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

    /// Записать uinteger-значение по пути path.
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

    /// Записать real-значение по пути path.
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

    /// Записать string-значение по пути path.
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

    /// Записать int-значение по пути path (удобная перегрузка для int).
    bool put( const char* path, int value ) { return put( path, static_cast<int64_t>( value ) ); }

    /// Записать ref-узел по пути path (указатель на другой путь).
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

    /// Разобрать JSON-строку и записать результат по пути path.
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
    // Path-адресация: erase (Задача 6.3)
    // -----------------------------------------------------------------------

    /// Удалить узел по пути path.
    /// Рекурсивно удаляет поддерево (кроме целей $ref).
    /// Запрет удаления в /$metrics.
    bool erase( const char* path )
    {
        if ( path == nullptr )
            return false;
        if ( _is_metrics_path( path ) )
            return false;

        // Находим родительский узел и имя ключа/индекс.
        std::string parent_path;
        std::string last_seg;
        _split_path( path, parent_path, last_seg );

        node_id parent_id = 0;
        if ( parent_path.empty() || parent_path == "/" )
        {
            parent_id = _find_root();
        }
        else
        {
            node_view pv = get( parent_path.c_str(), /*deref_refs=*/false );
            parent_id    = pv.id;
        }

        if ( parent_id == 0 )
            return false;

        node_view parent{ parent_id };

        if ( parent.is_object() )
        {
            // Находим ключ в объекте.
            node_view child = parent.at( last_seg.c_str() );
            if ( !child.valid() )
                return false;
            // Рекурсивно удаляем поддерево (без target ref).
            _free_node_tree( child.id );
            // Удаляем запись из объекта.
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

    /// Проверить наличие узла по пути path.
    bool exists( const char* path ) const
    {
        if ( path == nullptr )
            return false;
        if ( _is_metrics_path( path ) )
        {
            // Метрики всегда "существуют".
            return true;
        }
        return get( path, false ).valid();
    }

    // -----------------------------------------------------------------------
    // $ref: разыменование (Задача 6.4)
    // -----------------------------------------------------------------------

    /// Явно разыменовать ref-узел.
    /// Обнаруживает циклические ссылки (возвращает node_view(0) при цикле).
    node_view resolve_ref( node_id id, uintptr_t max_depth = 32 ) const
    {
        return node_view{ id }.deref( true, max_depth );
    }

    /// Разрешить все ref-узлы в дереве после загрузки образа (Задача 6.5).
    /// Устанавливает target для каждого ref-узла по его path.
    void resolve_all_refs()
    {
        node_id root = _find_root();
        if ( root == 0 )
            return;
        _resolve_refs_in_subtree( root );
    }

    // -----------------------------------------------------------------------
    // Сериализация (Задача 6.1)
    // -----------------------------------------------------------------------

    /// Сериализовать узел в JSON-строку.
    std::string dump( node_id id ) const { return node_to_string( id ); }

    /// Сериализовать весь граф от корневого узла.
    std::string dump() const { return node_to_string( _find_root() ); }

    /// Разобрать JSON-строку и записать в корень БД.
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
    // Сохранение (Задача 6.1)
    // -----------------------------------------------------------------------

    /// Сохранить образ ПАП в файл.
    /// Перед сохранением обновляет поле last_save_time в метриках (Задача 7.2).
    void save()
    {
        // Обновляем время сохранения перед записью на диск.
        db_metrics* m = _get_metrics_struct();
        if ( m != nullptr )
        {
            _fill_metrics( m );
            m->last_save_time = static_cast<uint64_t>( std::time( nullptr ) );
        }
        PersistentAddressSpace::Get().Save();
    }

    // -----------------------------------------------------------------------
    // Метрики (Фаза 7: персистные метрики в ПАП)
    // -----------------------------------------------------------------------

    /// Получить метрики ПАП/БД как node_view (read-only).
    /// Путь вида /$metrics/node_count_total, /$metrics/pam_bump_offset, etc.
    node_view metrics( const char* subpath = nullptr ) const
    {
        if ( subpath == nullptr )
            return node_view{}; // агрегированный узел не поддерживается пока
        std::string full = "/$metrics/";
        full += subpath;
        return _get_metrics( full.c_str() );
    }

    /// Пересчитать все метрики из текущего состояния ПАМ и пула.
    /// Вызывается автоматически при каждой мутации; можно вызвать явно.
    void update_metrics()
    {
        db_metrics* m = _get_metrics_struct();
        if ( m == nullptr )
            return;
        _fill_metrics( m );
    }

    // -----------------------------------------------------------------------
    // Поиск по строкам (Задача 6.1 / Фаза 8)
    // -----------------------------------------------------------------------

    /// Найти все интернированные строки, содержащие подстроку pattern.
    std::vector<pstringview_search_result> search_strings( const char* pattern ) const
    {
        return pam_search_strings( pattern );
    }

    /// Получить все интернированные строки словаря.
    std::vector<pstringview_search_result> all_strings() const { return pam_all_strings(); }

    // -----------------------------------------------------------------------
    // Фаза 8.2: Расширенный поиск по строкам (pstringview + pstring узлы)
    // -----------------------------------------------------------------------

    /// Найти все узлы типа string (pstring), значение которых содержит подстроку pattern.
    /// В отличие от search_strings(), который ищет только в словаре интернированных ключей,
    /// данный метод ищет по строковым ЗНАЧЕНИЯМ JSON (readwrite pstring-узлы).
    ///
    /// Обходит всё дерево от корня и возвращает node_id всех string-узлов,
    /// чьё значение содержит pattern.
    std::vector<node_id> search_node_strings( const char* pattern ) const
    {
        std::vector<node_id> results;
        node_id              root = _find_root();
        if ( root == 0 )
            return results;
        _search_node_strings_in_subtree( root, pattern != nullptr ? pattern : "", results );
        return results;
    }

    // -----------------------------------------------------------------------
    // Фаза 8.1: Интерфейс pmap<pstringview, node_id> для иерархического доступа
    // -----------------------------------------------------------------------

    /// Доступ к узлу по пути (аналог operator[] для map).
    /// Если узел не существует — создаёт пустой object-узел по пути.
    /// Эквивалентно: если exists(path) — возвращает get(path), иначе put(path, {}).
    node_view operator[]( const char* path )
    {
        if ( path == nullptr )
            return node_view{};
        if ( _is_metrics_path( path ) )
            return _get_metrics( path );
        // Если узел уже существует — возвращаем его.
        node_view existing = get( path, /*deref_refs=*/false );
        if ( existing.valid() )
            return existing;
        // Создаём промежуточные узлы и возвращаем финальный слот.
        node_id slot = _ensure_path( path );
        if ( slot == 0 )
            return node_view{};
        // Финальный слот инициализирован как null — оставляем null (как std::map).
        _update_metrics_after_mutation();
        return node_view{ slot };
    }

    /// Найти узел по пути без создания.
    /// Возвращает node_view(0) если не существует.
    /// Не разыменовывает ref-узлы.
    node_view find( const char* path ) const { return get( path, /*deref_refs=*/false ); }

    /// Вставить значение по пути.
    /// Если узел уже существует — перезаписывает значение.
    /// Возвращает node_view на созданный/обновлённый узел.
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
    // Доступ к ПАП напрямую
    // -----------------------------------------------------------------------

    /// Получить ссылку на персистный адресный менеджер.
    PersistentAddressSpace& pam() const { return PersistentAddressSpace::Get(); }

  private:
    // -----------------------------------------------------------------------
    // Вспомогательные методы: пул и корень
    // -----------------------------------------------------------------------

    /// Получить смещение пула узлов (или 0 если не создан).
    uintptr_t _find_pool_offset() const { return PersistentAddressSpace::Get().Find( PJSON_DB_POOL_NAME ); }

    /// Получить указатель на пул (или nullptr).
    pjson_pool* _get_pool() const
    {
        uintptr_t off = _find_pool_offset();
        if ( off == 0 )
            return nullptr;
        return PersistentAddressSpace::Get().Resolve<pjson_pool>( off );
    }

    /// Создать пул если не существует.
    void _ensure_pool()
    {
        if ( _find_pool_offset() != 0 )
            return;
        fptr<pjson_pool> pool;
        pool.New( PJSON_DB_POOL_NAME );
        // После New пул инициализирован нулями (конструктор = default).
        // Инициализируем поля пула вручную.
        auto&       pam = PersistentAddressSpace::Get();
        pjson_pool* p   = pam.Resolve<pjson_pool>( pam.Find( PJSON_DB_POOL_NAME ) );
        if ( p != nullptr )
        {
            p->nodes_size_     = 0;
            p->nodes_cap_      = 0;
            p->nodes_data_off_ = 0;
            p->free_head_      = 0;
            p->free_count_     = 0;
        }
    }

    /// Получить node_id корневого узла (или 0).
    node_id _find_root() const
    {
        uintptr_t off = PersistentAddressSpace::Get().Find( PJSON_DB_ROOT_NAME );
        return off;
    }

    /// Создать корневой объект-узел если не существует.
    void _ensure_root()
    {
        if ( _find_root() != 0 )
            return;
        fptr<node> root_node;
        root_node.New( PJSON_DB_ROOT_NAME );
        node_id root_off = root_node.addr();
        node_set_object( root_off );
    }

    // -----------------------------------------------------------------------
    // Вспомогательные методы: путевая адресация
    // -----------------------------------------------------------------------

    /// Проверить, относится ли путь к зарезервированному пространству /$metrics.
    static bool _is_metrics_path( const char* path )
    {
        if ( path == nullptr )
            return false;
        // Путь должен начинаться с "/$metrics"
        return std::strncmp( path, "/$metrics", 9 ) == 0;
    }

    /// Получить метрику по пути (только чтение, Задача 7.3).
    /// Все значения берутся из персистной структуры db_metrics в ПАП,
    /// которая обновляется при каждой мутации (Задача 7.2).
    node_view _get_metrics( const char* path ) const
    {
        // Извлекаем имя метрики из пути: /$metrics/<name>
        const char* key = path + 9; // пропускаем "/$metrics"
        if ( *key == '/' )
            ++key;

        // Аллоцируем временный узел для возврата значения метрики.
        // (Временный узел не хранится долго; используется только для возврата node_view.)
        fptr<node> tmp_node;
        tmp_node.New();
        uintptr_t tmp_off = tmp_node.addr();

        // Читаем актуальные данные из db_metrics (персистная структура, Задача 7.1).
        // Дополнительно обогащаем данными из ПАМ (pam_bump_offset, pam_total_size, etc.)
        // для метрик, которые нельзя закэшировать статически (они меняются постоянно).
        const auto& pam  = PersistentAddressSpace::Get();
        pjson_pool* pool = _get_pool();

        // Метрики пула узлов (свободные/занятые; node_count_total обрабатывается ниже)
        if ( std::strcmp( key, "free_node_count" ) == 0 )
        {
            uint64_t val = ( pool != nullptr ) ? static_cast<uint64_t>( pool->free_in_pool() ) : 0u;
            node_set_uint( tmp_off, val );
            return node_view{ tmp_off };
        }
        if ( std::strcmp( key, "used_node_count" ) == 0 )
        {
            uint64_t val = ( pool != nullptr ) ? static_cast<uint64_t>( pool->used_count() ) : 0u;
            node_set_uint( tmp_off, val );
            return node_view{ tmp_off };
        }

        // Метрики строк
        if ( std::strcmp( key, "string_count_total" ) == 0 || std::strcmp( key, "string_count" ) == 0 )
        {
            uint64_t val = static_cast<uint64_t>( pam_all_strings().size() );
            node_set_uint( tmp_off, val );
            return node_view{ tmp_off };
        }

        // Метрики ПАМ (читаются напрямую из PersistentAddressSpace)
        if ( std::strcmp( key, "pam_bump_offset" ) == 0 )
        {
            node_set_uint( tmp_off, static_cast<uint64_t>( pam.GetBump() ) );
            return node_view{ tmp_off };
        }
        if ( std::strcmp( key, "pam_free_list_size" ) == 0 )
        {
            node_set_uint( tmp_off, static_cast<uint64_t>( pam.GetFreeListSize() ) );
            return node_view{ tmp_off };
        }
        if ( std::strcmp( key, "pam_total_size" ) == 0 )
        {
            node_set_uint( tmp_off, static_cast<uint64_t>( pam.GetDataSize() ) );
            return node_view{ tmp_off };
        }
        if ( std::strcmp( key, "pam_slot_count" ) == 0 )
        {
            node_set_uint( tmp_off, static_cast<uint64_t>( pam.GetSlotCount() ) );
            return node_view{ tmp_off };
        }
        if ( std::strcmp( key, "pam_named_count" ) == 0 )
        {
            node_set_uint( tmp_off, static_cast<uint64_t>( pam.GetNamedCount() ) );
            return node_view{ tmp_off };
        }

        // Метрики типов узлов: вычисляются обходом дерева в реальном времени (Задача 7.3).
        // Это гарантирует актуальность даже без явного вызова update_metrics().
        if ( std::strcmp( key, "ref_count" ) == 0 || std::strcmp( key, "array_count" ) == 0 ||
             std::strcmp( key, "object_count" ) == 0 || std::strcmp( key, "binary_bytes_total" ) == 0 ||
             std::strcmp( key, "node_count_total" ) == 0 )
        {
            uint64_t node_cnt = 0, ref_cnt = 0, array_cnt = 0, object_cnt = 0, binary_bytes = 0;
            node_id  root = _find_root();
            if ( root != 0 )
                _count_nodes_in_subtree( root, node_cnt, ref_cnt, array_cnt, object_cnt, binary_bytes );

            if ( std::strcmp( key, "node_count_total" ) == 0 )
            {
                // Если пул не используется — считаем узлы в дереве.
                uint64_t pool_total = ( pool != nullptr ) ? static_cast<uint64_t>( pool->total_count() ) : 0u;
                node_set_uint( tmp_off, pool_total > 0 ? pool_total : node_cnt );
            }
            else if ( std::strcmp( key, "ref_count" ) == 0 )
                node_set_uint( tmp_off, ref_cnt );
            else if ( std::strcmp( key, "array_count" ) == 0 )
                node_set_uint( tmp_off, array_cnt );
            else if ( std::strcmp( key, "object_count" ) == 0 )
                node_set_uint( tmp_off, object_cnt );
            else
                node_set_uint( tmp_off, binary_bytes );

            return node_view{ tmp_off };
        }

        // Метрики из персистной структуры db_metrics (только last_save_time, Задача 7.1)
        const db_metrics* m = _get_metrics_struct_const();
        if ( m != nullptr )
        {
            if ( std::strcmp( key, "last_save_time" ) == 0 )
            {
                node_set_uint( tmp_off, m->last_save_time );
                return node_view{ tmp_off };
            }
        }

        // Неизвестная метрика — возвращаем null.
        node_init_null( tmp_off );
        return node_view{ tmp_off };
    }

    /// Разбить путь на родительский путь и последний сегмент.
    static void _split_path( const char* path, std::string& parent, std::string& last )
    {
        if ( path == nullptr || path[0] == '\0' )
        {
            parent = "";
            last   = "";
            return;
        }

        std::string full( path );
        // Убираем trailing slash.
        while ( full.size() > 1 && full.back() == '/' )
            full.pop_back();

        auto pos = full.rfind( '/' );
        if ( pos == std::string::npos )
        {
            parent = "";
            last   = full;
        }
        else if ( pos == 0 )
        {
            parent = "/";
            last   = full.substr( 1 );
        }
        else
        {
            parent = full.substr( 0, pos );
            last   = full.substr( pos + 1 );
        }
    }

    /// Обеспечить существование узла по пути, создавая промежуточные объекты.
    /// Возвращает node_id конечного слота (может быть создан или найден).
    node_id _ensure_path( const char* path )
    {
        if ( path == nullptr )
            return 0;

        node_id cur = _find_root();
        if ( cur == 0 )
            return 0;

        const char* p = path;
        // Пропускаем ведущий слэш.
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

            // Если текущий узел — ref, разыменовываем.
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
                    // Создаём новый слот.
                    node_id slot = node_object_insert( cur, seg.c_str() );
                    if ( slot == 0 )
                        return 0;
                    if ( !is_last )
                    {
                        // Определяем тип следующего узла по первому символу следующего сегмента.
                        // Если следующий сегмент — число, создаём массив, иначе — объект.
                        if ( _next_seg_is_numeric( p ) )
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
                    // Расширяем массив до нужного размера.
                    uintptr_t cur_size = cur_v.size();
                    for ( uintptr_t i = cur_size; i <= idx; ++i )
                    {
                        node_id slot = node_array_push_back( cur );
                        if ( i == idx )
                        {
                            if ( !is_last )
                            {
                                if ( _next_seg_is_numeric( p ) )
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
                // Достигли конечного скалярного узла — возвращаем его для перезаписи.
                return cur;
            }
            else
            {
                return 0;
            }
        }

        return cur;
    }

    /// Проверить, является ли следующий сегмент пути числовым индексом.
    static bool _next_seg_is_numeric( const char* p )
    {
        if ( p == nullptr || *p == '\0' )
            return false;
        char c = *p;
        return ( c >= '0' && c <= '9' );
    }

    // -----------------------------------------------------------------------
    // Вспомогательные методы: удаление узлов
    // -----------------------------------------------------------------------

    /// Рекурсивно освободить поддерево узлов.
    /// Не удаляет цели $ref (только сам ref-узел).
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
            // Освобождаем массив данных.
            auto& pam = PersistentAddressSpace::Get();
            node* n   = pam.Resolve<node>( id );
            if ( n != nullptr && n->array_val.data_off != 0 )
            {
                fptr<node_id> arr;
                arr.set_addr( n->array_val.data_off );
                arr.DeleteArray();
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
            // Освобождаем массив записей объекта.
            auto& pam = PersistentAddressSpace::Get();
            node* n   = pam.Resolve<node>( id );
            if ( n != nullptr && n->object_val.data_off != 0 )
            {
                fptr<object_entry> arr;
                arr.set_addr( n->object_val.data_off );
                arr.DeleteArray();
            }
            break;
        }
        case node_tag::string:
        {
            auto& pam = PersistentAddressSpace::Get();
            node* n   = pam.Resolve<node>( id );
            if ( n != nullptr && n->string_val.chars_offset != 0 )
                pam.Delete( n->string_val.chars_offset );
            break;
        }
        case node_tag::binary:
        {
            auto& pam = PersistentAddressSpace::Get();
            node* n   = pam.Resolve<node>( id );
            if ( n != nullptr && n->binary_val.data_off != 0 )
            {
                fptr<uint8_t> arr;
                arr.set_addr( n->binary_val.data_off );
                arr.DeleteArray();
            }
            break;
        }
        case node_tag::ref:
            // Не удаляем цель ref-узла.
            break;
        default:
            break;
        }

        // Освобождаем сам узел.
        auto& pam = PersistentAddressSpace::Get();
        pam.Delete( id );
    }

    /// Удалить запись с ключом key из объекта obj_id.
    bool _object_erase( node_id obj_id, const char* key )
    {
        if ( obj_id == 0 || key == nullptr )
            return false;

        auto& pam = PersistentAddressSpace::Get();
        node* n   = pam.Resolve<node>( obj_id );
        if ( n == nullptr || n->tag != node_tag::object )
            return false;

        uintptr_t sz = n->object_val.size;
        if ( sz == 0 || n->object_val.data_off == 0 )
            return false;

        // Ищем индекс записи для удаления.
        uintptr_t del_idx = sz; // недействительный
        {
            object_entry* entries = pam.Resolve<object_entry>( n->object_val.data_off );
            uintptr_t     lo = 0, hi = sz;
            while ( lo < hi )
            {
                uintptr_t mid         = ( lo + hi ) / 2;
                entries               = pam.Resolve<object_entry>( n->object_val.data_off );
                const object_entry& e = entries[mid];
                int                 cmp;
                if ( e.key_chars_offset == 0 )
                    cmp = ( key[0] == '\0' ) ? 0 : 1;
                else
                {
                    const char* ks = pam.Resolve<char>( e.key_chars_offset );
                    cmp            = ( ks != nullptr ) ? std::strcmp( ks, key ) : -1;
                }
                if ( cmp < 0 )
                    lo = mid + 1;
                else if ( cmp > 0 )
                    hi = mid;
                else
                {
                    del_idx = mid;
                    break;
                }
            }
        }

        if ( del_idx == sz )
            return false; // ключ не найден

        // Сдвигаем элементы влево, удаляя запись del_idx.
        uintptr_t     data_off = n->object_val.data_off;
        object_entry* entries  = pam.Resolve<object_entry>( data_off );
        if ( entries == nullptr )
            return false;
        for ( uintptr_t i = del_idx; i + 1 < sz; ++i )
            entries[i] = entries[i + 1];

        n = pam.Resolve<node>( obj_id );
        if ( n != nullptr )
            n->object_val.size--;

        return true;
    }

    /// Удалить элемент массива по индексу.
    bool _array_erase_at( node_id arr_id, uintptr_t idx )
    {
        if ( arr_id == 0 )
            return false;

        auto& pam = PersistentAddressSpace::Get();
        node* n   = pam.Resolve<node>( arr_id );
        if ( n == nullptr || n->tag != node_tag::array )
            return false;

        uintptr_t sz = n->array_val.size;
        if ( idx >= sz )
            return false;

        uintptr_t data_off = n->array_val.data_off;
        node_id*  arr      = pam.Resolve<node_id>( data_off );
        if ( arr == nullptr )
            return false;

        // Сдвигаем элементы влево.
        for ( uintptr_t i = idx; i + 1 < sz; ++i )
            arr[i] = arr[i + 1];
        arr[sz - 1] = 0;

        n = pam.Resolve<node>( arr_id );
        if ( n != nullptr )
            n->array_val.size--;

        return true;
    }

    // -----------------------------------------------------------------------
    // Вспомогательные методы: метрики (Фаза 7)
    // -----------------------------------------------------------------------

    /// Получить указатель на персистную структуру db_metrics.
    /// Создаёт структуру в ПАМ при первом обращении.
    db_metrics* _get_metrics_struct()
    {
        auto& pam = PersistentAddressSpace::Get();

        uintptr_t off = pam.Find( PJSON_DB_METRICS_NAME );
        if ( off == 0 )
        {
            // Создаём структуру метрик в ПАМ.
            fptr<db_metrics> m;
            m.New( PJSON_DB_METRICS_NAME );
            off = pam.Find( PJSON_DB_METRICS_NAME );
            if ( off == 0 )
                return nullptr;
            // Инициализируем нулями.
            db_metrics* mp = pam.Resolve<db_metrics>( off );
            if ( mp != nullptr )
                std::memset( mp, 0, sizeof( db_metrics ) );
        }

        return pam.Resolve<db_metrics>( off );
    }

    /// Получить константный указатель на структуру db_metrics (без создания).
    const db_metrics* _get_metrics_struct_const() const
    {
        const auto& pam = PersistentAddressSpace::Get();
        uintptr_t   off = pam.Find( PJSON_DB_METRICS_NAME );
        if ( off == 0 )
            return nullptr;
        return pam.Resolve<db_metrics>( off );
    }

    /// Заполнить структуру db_metrics актуальными данными из ПАМ.
    /// Пересчитывает все поля путём обхода дерева узлов от корня (Задача 7.2).
    void _fill_metrics( db_metrics* m ) const
    {
        if ( m == nullptr )
            return;

        const auto& pam  = PersistentAddressSpace::Get();
        pjson_pool* pool = _get_pool();

        // Базовые метрики пула
        m->node_count_total = ( pool != nullptr ) ? static_cast<uint64_t>( pool->total_count() ) : 0u;

        // Метрики ПАМ
        m->pam_bump_offset    = static_cast<uint64_t>( pam.GetBump() );
        m->pam_free_list_size = static_cast<uint64_t>( pam.GetFreeListSize() );
        m->pam_total_size     = static_cast<uint64_t>( pam.GetDataSize() );
        m->pam_slot_count     = static_cast<uint64_t>( pam.GetSlotCount() );
        m->pam_named_count    = static_cast<uint64_t>( pam.GetNamedCount() );

        // Метрики строк
        m->string_count_total = static_cast<uint64_t>( pam_all_strings().size() );

        // Подсчёт по типам узлов: обходим дерево от корня (Задача 7.2).
        // pjson_db размещает узлы через fptr<node>::New() в ПАМ напрямую,
        // не через pjson_pool — поэтому считаем обходом дерева.
        uint64_t binary_bytes = 0;
        uint64_t ref_cnt      = 0;
        uint64_t array_cnt    = 0;
        uint64_t object_cnt   = 0;
        uint64_t node_cnt     = 0;

        node_id root = _find_root();
        if ( root != 0 )
            _count_nodes_in_subtree( root, node_cnt, ref_cnt, array_cnt, object_cnt, binary_bytes );

        // node_count_total: если пул не используется — считаем узлы в дереве.
        if ( pool == nullptr || pool->total_count() == 0 )
            m->node_count_total = node_cnt;

        m->binary_bytes_total = binary_bytes;
        m->ref_count          = ref_cnt;
        m->array_count        = array_cnt;
        m->object_count       = object_cnt;
    }

    /// Рекурсивный обход поддерева для подсчёта узлов по типам (Задача 7.2).
    void _count_nodes_in_subtree( node_id id, uint64_t& node_cnt, uint64_t& ref_cnt, uint64_t& array_cnt,
                                  uint64_t& object_cnt, uint64_t& binary_bytes ) const
    {
        if ( id == 0 )
            return;
        const node_view v{ id };
        if ( !v.valid() )
            return;

        ++node_cnt;

        switch ( v.tag() )
        {
        case node_tag::array:
            ++array_cnt;
            {
                uintptr_t sz = v.size();
                for ( uintptr_t i = 0; i < sz; ++i )
                {
                    node_view elem = v.at( i );
                    if ( elem.valid() )
                        _count_nodes_in_subtree( elem.id, node_cnt, ref_cnt, array_cnt, object_cnt, binary_bytes );
                }
            }
            break;
        case node_tag::object:
            ++object_cnt;
            {
                uintptr_t sz = v.size();
                for ( uintptr_t i = 0; i < sz; ++i )
                {
                    node_view val = v.value_at( i );
                    if ( val.valid() )
                        _count_nodes_in_subtree( val.id, node_cnt, ref_cnt, array_cnt, object_cnt, binary_bytes );
                }
            }
            break;
        case node_tag::ref:
            ++ref_cnt;
            // Не обходим цель ref (избегаем дублирования счёта).
            break;
        case node_tag::binary:
            // Считаем байты из binary_val.size.
            {
                const auto& pam = PersistentAddressSpace::Get();
                const node* n   = pam.Resolve<node>( id );
                if ( n != nullptr )
                    binary_bytes += static_cast<uint64_t>( n->binary_val.size );
            }
            break;
        default:
            break;
        }
    }

    /// Быстрое обновление метрик после мутации (Задача 7.2).
    /// Пересчитывает метрики и сохраняет их в персистной структуре.
    void _update_metrics_after_mutation()
    {
        db_metrics* m = _get_metrics_struct();
        if ( m == nullptr )
            return;
        _fill_metrics( m );
    }

    // -----------------------------------------------------------------------
    // Вспомогательные методы: разрешение ref
    // -----------------------------------------------------------------------

    // -----------------------------------------------------------------------
    // Вспомогательные методы: поиск по значениям pstring-узлов (Фаза 8.2)
    // -----------------------------------------------------------------------

    /// Рекурсивный обход поддерева для поиска string-узлов (pstring, readwrite)
    /// чьё значение содержит подстроку pattern.
    ///
    /// Заполняет results списком node_id найденных string-узлов.
    void _search_node_strings_in_subtree( node_id id, const char* pattern, std::vector<node_id>& results ) const
    {
        if ( id == 0 )
            return;
        const node_view v{ id };
        if ( !v.valid() )
            return;

        switch ( v.tag() )
        {
        case node_tag::string:
        {
            // Проверяем, содержит ли pstring-значение подстроку pattern.
            std::string_view sv = v.as_string();
            if ( pattern[0] == '\0' )
            {
                // Пустой pattern — возвращаем все string-узлы.
                results.push_back( id );
            }
            else if ( !sv.empty() )
            {
                // Поиск подстроки через std::strstr (безопасно: sv нуль-терминирована pstring).
                const char* s = sv.data();
                if ( s != nullptr && std::strstr( s, pattern ) != nullptr )
                    results.push_back( id );
            }
            break;
        }
        case node_tag::array:
        {
            uintptr_t sz = v.size();
            for ( uintptr_t i = 0; i < sz; ++i )
            {
                node_view elem = v.at( i );
                if ( elem.valid() )
                    _search_node_strings_in_subtree( elem.id, pattern, results );
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
                    _search_node_strings_in_subtree( val.id, pattern, results );
            }
            break;
        }
        case node_tag::ref:
            // Не обходим цель ref (избегаем дублирования).
            break;
        default:
            break;
        }
    }

    // -----------------------------------------------------------------------
    // Вспомогательные методы: разрешение ref
    // -----------------------------------------------------------------------

    /// Рекурсивно обойти поддерево и разрешить все ref-узлы.
    void _resolve_refs_in_subtree( node_id id )
    {
        if ( id == 0 )
            return;
        node_view v{ id };
        if ( !v.valid() )
            return;

        if ( v.is_ref() )
        {
            // Разрешаем путь: ищем узел по пути ref.
            std::string_view path_sv = v.ref_path();
            if ( !path_sv.empty() )
            {
                std::string path_str( path_sv );
                node_view   target = get( path_str.c_str(), /*deref_refs=*/false );
                if ( target.valid() && !target.is_ref() )
                    node_set_ref_target( id, target.id );
            }
            return; // не обходим цель (она может вести в другую ветку)
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
