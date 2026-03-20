#pragma once
#include "pstringview_pmm.h"
#include "pam_pmm_config.h"
#include "pvector_pmm.h"
#include "pmap_pmm.h"
#include "pam_adapter.h"
#include "fptr_pmm.h"

using namespace pjson;
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>

// pjson_node.h — Расширенная модель узлов JSON.
//
// Цель: переработать pjson на node_id-адресацию. Добавить типы ref и binary.
//
// Ключевые архитектурные принципы:
//   - Все узлы в ПАП — POD-структуры, доступ через смещения (node_id).
//   - node_id = uintptr_t (смещение узла в ПАП; 0 = null/invalid).
//   - Строки JSON-значений: pstring (readwrite, изменяемые на лету).
//   - Ключи объектов и пути $ref: pstringview (readonly, интернированные).
//   - node_view — безопасный accessor для чтения узлов из ПАП.
//
// Все комментарии — на русском языке.

// ---------------------------------------------------------------------------
// node_tag — дискриминант типа узла
// ---------------------------------------------------------------------------

/// Расширенный набор типов узлов JSON.
enum class node_tag : uint32_t
{
    null     = 0,           ///< null
    boolean  = 1,           ///< true / false
    integer  = 2,           ///< int64_t
    uinteger = 3,           ///< uint64_t
    real     = 4,           ///< double
    string   = 5,           ///< pstring (readwrite, изменяемое строковое значение JSON)
    binary   = 6,           ///< pvector<uint8_t> в ПАП ($base64 при сериализации)
    array    = 7,           ///< pvector<node_id>
    object   = 8,           ///< pmap<pstringview, node_id> — ключи readonly (pstringview)
    ref      = 9,           ///< pstringview path (readonly) + node_id target ($ref при сериализации)
    _free    = 0xFFFFFFFFu, ///< Служебный тег: слот освобождён (для free-list пула)
};

// ---------------------------------------------------------------------------
// node_error — код ошибки в node_view
// ---------------------------------------------------------------------------

/// Код ошибки, возвращаемый в node_view при неудачных операциях.
///
/// node_view с ненулевым кодом ошибки создаётся фабричной функцией node_view_error().
/// Позволяет отличить «узел не найден» от «JSON null-значение».
enum class node_error : uintptr_t
{
    none      = 0, ///< Нет ошибки
    not_found = 1, ///< Узел не найден по пути
    wrong_type = 2, ///< Неверный тип узла при навигации (не объект / не массив)
    index_out_of_range = 3, ///< Индекс массива вне диапазона
    readonly           = 4, ///< Попытка записи в readonly-пространство (/$metrics)
    ref_cycle = 5, ///< Обнаружен цикл или превышена глубина при разыменовании $ref
    parse_error = 6, ///< Ошибка парсинга JSON
};

/// Базовое значение sentinel-идентификаторов для ошибочных node_view.
/// Область ~uintptr_t(255)..~uintptr_t(0) недостижима реальным ПАП.
/// Ошибочный node_view имеет id = NODE_ERROR_BASE + static_cast<uintptr_t>(node_error).
static constexpr uintptr_t NODE_ERROR_BASE = ~uintptr_t( 255 );

/// Получить человекочитаемое сообщение для кода ошибки.
/// Возвращает статическую строку (не требует освобождения).
/// Для неизвестных кодов возвращает "unknown error".
inline const char* node_error_message( node_error err )
{
    switch ( err )
    {
    case node_error::none:
        return "no error";
    case node_error::not_found:
        return "node not found";
    case node_error::wrong_type:
        return "wrong node type for navigation";
    case node_error::index_out_of_range:
        return "array index out of range";
    case node_error::readonly:
        return "cannot modify read-only path";
    case node_error::ref_cycle:
        return "cyclic $ref detected or max depth exceeded";
    case node_error::parse_error:
        return "JSON parse error";
    default:
        return "unknown error";
    }
}

// ---------------------------------------------------------------------------
// node_id — идентификатор узла (смещение в ПАП)
// ---------------------------------------------------------------------------

/// Смещение узла в ПАП; 0 = null/invalid.
using node_id = uintptr_t;

// ---------------------------------------------------------------------------
// object_entry — запись в объектной карте (ключ + значение)
// ---------------------------------------------------------------------------
//
// Раскладка идентична pmap_entry<pstringview, node_id>:
//   key_length:       uintptr_t  — длина ключа (pstringview::length)
//   key_chars_offset: uintptr_t  — смещение символов ключа в ПАП (pstringview::chars_offset)
//   value:            node_id    — смещение узла-значения в ПАП
//
// Используется вместо pmap_entry<pstringview, node_id> для избежания
// конфликта с приватным конструктором pstringview.
// Физическая раскладка совместима с pmap_entry<pstringview, node_id>,
// позволяет хранить сортированный массив пар ключ-значение в ПАП.

struct object_entry
{
    uintptr_t key_length; ///< Длина строки ключа (аналог pstringview::length)
    uintptr_t key_chars_offset; ///< Смещение массива char ключа в ПАП (аналог pstringview::chars_offset)
    node_id value; ///< node_id узла-значения; 0 = invalid
};

static_assert( std::is_trivially_copyable<object_entry>::value, "object_entry должен быть тривиально копируемым" );
// Проверяем, что раскладка совместима с pmap_entry<pstringview, node_id>:
// pstringview занимает 2 * sizeof(void*), node_id занимает sizeof(void*).
static_assert(
    sizeof( object_entry ) == 3 * sizeof( void* ),
    "object_entry должен занимать 3 * sizeof(void*) байт (совместимость с pmap_entry<pstringview, node_id>)" );

// ---------------------------------------------------------------------------
// node — структура узла JSON в ПАП
// ---------------------------------------------------------------------------
//
// Раскладка:
//   tag:  4 байта — дискриминант (node_tag : uint32_t)
//   _pad: 4 байта — выравнивание для 8-байтового union (на 64-битных платформах)
//   union: payload
//
// Все поля — POD (тривиально копируемые), живут только в ПАП.
// Доступ — только через смещение (node_id) и pmm_resolve<node>.
//
// Типы строк:
//   string_val: pstring (readwrite) — JSON string-value узлы, изменяемые на лету.
//   ref_val.path_*: pstringview-совместимые поля (readonly, интернированные).

struct node
{
    node_tag tag; ///< 4 байта — дискриминант

    uint32_t _pad; ///< Выравнивание для корректного доступа к 8-байтным полям union

    union
    {
        uint32_t boolean_val; ///< boolean: 0 = false, ненулевое = true

        int64_t int_val; ///< integer: int64_t

        uint64_t uint_val; ///< uinteger: uint64_t

        double real_val; ///< real: double

        /// string: PamManager::pstring (readwrite, из PMM).
        /// Строковые значения JSON — изменяемые на лету (необходимо для jsonRVM).
        PamManager::pstring string_val;

        /// binary: массив uint8_t в ПАП ($base64 при сериализации).
        /// PamManager::parray<uint8_t>: { uint32_t _size; uint32_t _capacity; index_type _data_idx; }.
        PamManager::parray<uint8_t> binary_val;

        /// array: массив node_id в ПАП.
        /// PamManager::parray<node_id>: { uint32_t _size; uint32_t _capacity; index_type _data_idx; }.
        PamManager::parray<node_id> array_val;

        /// object: сортированный массив object_entry в ПАП.
        /// Ключи — readonly pstringview (интернированные), значения — node_id.
        /// PamManager::parray<object_entry>: { uint32_t _size; uint32_t _capacity; index_type _data_idx; }.
        PamManager::parray<object_entry> object_val;

        /// ref: путь ($ref) + целевой node_id.
        /// path_* — pstringview-совместимые поля (readonly, интернированные).
        /// target — node_id целевого узла (0 = не разрешён).
        struct
        {
            uintptr_t path_length; ///< Длина строки пути
            uintptr_t path_chars_offset; ///< Смещение символов пути в словаре pstringview; 0 = пустой путь
            uintptr_t target; ///< node_id целевого узла; 0 = не разрешён
        } ref_val;

        /// _free_next: следующий свободный слот в free-list пула (tag == _free).
        uintptr_t _free_next; ///< node_id следующего свободного узла; 0 = конец списка
    };
};

// Проверяем размеры раскладки.
static_assert( std::is_trivially_copyable<node>::value, "node должен быть тривиально копируемым" );
static_assert( std::is_trivially_copyable<PamManager::pstring>::value,
               "PamManager::pstring (node::string_val) должен быть тривиально копируемым" );
static_assert( std::is_trivially_copyable<PamManager::parray<node_id>>::value,
               "PamManager::parray<node_id> (node::array_val) должен быть тривиально копируемым" );
static_assert( sizeof( node::ref_val ) == 3 * sizeof( void* ), "node::ref_val должен занимать 3 * sizeof(void*) байт" );

// ---------------------------------------------------------------------------
// Предварительные объявления итераторов
// ---------------------------------------------------------------------------
// Необходимы до объявления node_view, чтобы методы begin()/end()/items()
// могли использовать эти типы как возвращаемые.

struct node_view_iterator; ///< Итератор элементов array-узла
struct object_item;        ///< Пара ключ-значение для object-итерации
struct object_iterator;    ///< Итератор пар ключ-значение object-узла
struct object_items_range; ///< Диапазон для items()
struct array_range;        ///< Диапазон для range-based for по массиву

// ---------------------------------------------------------------------------
// Предварительные объявления helpers для бинарного поиска
// ---------------------------------------------------------------------------
// Необходимы до объявления node_view, чтобы node_view::at(const char*)
// мог вызывать node_object_find_key.

/// Результат бинарного поиска по объектному ключу.
struct object_find_result
{
    uintptr_t index; ///< Индекс найденного элемента (== size при отсутствии ключа)
    bool found;      ///< true если ключ найден
};

/// Бинарный поиск ключа в отсортированном массиве object_entry.
inline object_find_result node_object_find_key( const object_entry* entries, uintptr_t sz, const char* key );

// ---------------------------------------------------------------------------
// node_view — безопасный accessor для чтения узлов
// ---------------------------------------------------------------------------
//
// node_view хранит node_id (смещение узла в ПАП) и предоставляет типобезопасный
// read-only интерфейс для работы с узлами через PMM.
//
// Использование:
//   node_view v{ some_node_id };
//   if (v.is_string()) printf("%s\n", v.as_string().data());
//   node_view child = v.at("key");  // для object
//   node_view elem = v.at(0);       // для array

struct node_view
{
    node_id id; ///< Смещение узла в ПАП; 0 = null/invalid

    // ----------------------------------------------------------------
    // Конструкторы

    /// Создать пустой (null) node_view.
    node_view() : id( 0 ) {}

    /// Создать node_view для узла с заданным node_id.
    explicit node_view( node_id nid ) : id( nid ) {}

    // ----------------------------------------------------------------
    // Запросы типа

    /// Проверить, является ли node_view ошибочным.
    /// Ошибочный node_view создаётся через node_view_error(); имеет id >= NODE_ERROR_BASE.
    bool is_error() const { return id >= NODE_ERROR_BASE && id != ~uintptr_t( 0 ); }

    /// Получить код ошибки.
    /// Возвращает node_error::none для обычных и null node_view.
    node_error error() const
    {
        if ( !is_error() )
            return node_error::none;
        return static_cast<node_error>( id - NODE_ERROR_BASE );
    }

    /// Получить человекочитаемое сообщение об ошибке.
    /// Для обычных и null node_view возвращает "no error".
    const char* error_message() const { return node_error_message( error() ); }

    /// Проверить, является ли node_view действительным (не null/invalid/error).
    bool valid() const { return id != 0 && !is_error(); }

    /// Получить тег типа узла.
    node_tag tag() const
    {
        if ( id == 0 )
            return node_tag::null;
        // Ошибочный node_view не является узлом — возвращаем _free как sentinel.
        if ( is_error() )
            return node_tag::_free;
        const node* n = _resolve();
        if ( n == nullptr )
            return node_tag::null;
        return n->tag;
    }

    /// Проверить, является ли узел JSON null-значением.
    /// Возвращает true только для id==0 или узлов с tag==null (но не для ошибочных view).
    bool is_null() const { return !is_error() && tag() == node_tag::null; }
    bool is_boolean() const { return tag() == node_tag::boolean; }
    bool is_integer() const { return tag() == node_tag::integer; }
    bool is_uinteger() const { return tag() == node_tag::uinteger; }
    bool is_real() const { return tag() == node_tag::real; }
    bool is_number() const { return is_integer() || is_uinteger() || is_real(); }
    bool is_string() const { return tag() == node_tag::string; }
    bool is_binary() const { return tag() == node_tag::binary; }
    bool is_array() const { return tag() == node_tag::array; }
    bool is_object() const { return tag() == node_tag::object; }
    bool is_ref() const { return tag() == node_tag::ref; }

    // ----------------------------------------------------------------
    // Получение значений

    /// Получить boolean-значение узла.
    bool as_bool() const
    {
        const node* n = _resolve();
        if ( n == nullptr || n->tag != node_tag::boolean )
            return false;
        return n->boolean_val != 0u;
    }

    /// Получить integer-значение узла.
    int64_t as_int() const
    {
        const node* n = _resolve();
        if ( n == nullptr )
            return 0;
        if ( n->tag == node_tag::integer )
            return n->int_val;
        if ( n->tag == node_tag::uinteger )
            return static_cast<int64_t>( n->uint_val );
        if ( n->tag == node_tag::real )
            return static_cast<int64_t>( n->real_val );
        return 0;
    }

    /// Получить uinteger-значение узла.
    uint64_t as_uint() const
    {
        const node* n = _resolve();
        if ( n == nullptr )
            return 0u;
        if ( n->tag == node_tag::uinteger )
            return n->uint_val;
        if ( n->tag == node_tag::integer )
            return static_cast<uint64_t>( n->int_val );
        if ( n->tag == node_tag::real )
            return static_cast<uint64_t>( n->real_val );
        return 0u;
    }

    /// Получить real-значение узла.
    double as_double() const
    {
        const node* n = _resolve();
        if ( n == nullptr )
            return 0.0;
        if ( n->tag == node_tag::real )
            return n->real_val;
        if ( n->tag == node_tag::integer )
            return static_cast<double>( n->int_val );
        if ( n->tag == node_tag::uinteger )
            return static_cast<double>( n->uint_val );
        return 0.0;
    }

    /// Получить string-значение узла (вид на pstring — readwrite JSON string).
    /// Возвращает std::string_view на символьные данные pstring в ПАП.
    /// Действителен, пока ПАМ жив и узел не изменён.
    std::string_view as_string() const
    {
        const node* n = _resolve();
        if ( n == nullptr || n->tag != node_tag::string )
            return std::string_view{};
        if ( n->string_val.empty() )
            return std::string_view{};
        const char* s = n->string_val.c_str();
        if ( s == nullptr || s[0] == '\0' )
            return std::string_view{};
        return std::string_view{ s, n->string_val.size() };
    }

    /// Получить путь ref-узла (pstringview-совместимые поля — readonly, интернированные).
    /// Возвращает std::string_view на символьные данные пути в ПАП.
    std::string_view ref_path() const
    {
        const node* n = _resolve();
        if ( n == nullptr || n->tag != node_tag::ref )
            return std::string_view{};
        if ( n->ref_val.path_chars_offset == 0 )
            return std::string_view{};
        const char* s = pmm_resolve<char>( n->ref_val.path_chars_offset );
        if ( s == nullptr )
            return std::string_view{};
        return std::string_view{ s, static_cast<std::size_t>( n->ref_val.path_length ) };
    }

    /// Получить target node_id ref-узла (0 = не разрешён).
    node_id ref_target() const
    {
        const node* n = _resolve();
        if ( n == nullptr || n->tag != node_tag::ref )
            return 0;
        return n->ref_val.target;
    }

    // ----------------------------------------------------------------
    // Навигация

    /// Получить размер array или object узла.
    uintptr_t size() const
    {
        const node* n = _resolve();
        if ( n == nullptr )
            return 0;
        if ( n->tag == node_tag::array )
            return n->array_val.size();
        if ( n->tag == node_tag::object )
            return n->object_val.size();
        if ( n->tag == node_tag::binary )
            return n->binary_val.size();
        if ( n->tag == node_tag::string )
            return static_cast<uintptr_t>( n->string_val.size() );
        return 0;
    }

    bool empty() const { return size() == 0; }

    /// Получить элемент массива по индексу idx.
    /// Возвращает node_view(0) при ошибке индекса или неверном типе узла.
    node_view at( uintptr_t idx ) const
    {
        const node* n = _resolve();
        if ( n == nullptr || n->tag != node_tag::array )
            return node_view{};
        if ( idx >= n->array_val.size() )
            return node_view{};
        const node_id* arr = n->array_val.data();
        if ( arr == nullptr )
            return node_view{};
        return node_view{ arr[idx] };
    }

    /// Получить значение объекта по ключу key (C-строка).
    /// Ищет ключ в отсортированном массиве object_entry через бинарный поиск.
    /// Возвращает node_view(0) при отсутствии ключа или неверном типе.
    node_view at( const char* key ) const
    {
        if ( key == nullptr )
            return node_view{};
        const node* n = _resolve();
        if ( n == nullptr || n->tag != node_tag::object )
            return node_view{};
        if ( n->object_val.empty() )
            return node_view{};

        // Делегируем бинарный поиск в node_object_find_key.
        const object_entry* entries = n->object_val.data();
        if ( entries == nullptr )
            return node_view{};
        auto result = node_object_find_key( entries, n->object_val.size(), key );
        if ( !result.found )
            return node_view{};
        return node_view{ entries[result.index].value };
    }

    /// Получить ключ i-го поля объекта (для итерации).
    /// Возвращает std::string_view; "" при ошибке.
    std::string_view key_at( uintptr_t idx ) const
    {
        const node* n = _resolve();
        if ( n == nullptr || n->tag != node_tag::object )
            return std::string_view{};
        if ( idx >= n->object_val.size() )
            return std::string_view{};
        const object_entry* entries = n->object_val.data();
        if ( entries == nullptr )
            return std::string_view{};
        const object_entry& e = entries[idx];
        if ( e.key_chars_offset == 0 )
            return std::string_view{};
        const char* s = pmm_resolve<char>( e.key_chars_offset );
        if ( s == nullptr )
            return std::string_view{};
        return std::string_view{ s, static_cast<std::size_t>( e.key_length ) };
    }

    /// Получить значение i-го поля объекта (для итерации).
    node_view value_at( uintptr_t idx ) const
    {
        const node* n = _resolve();
        if ( n == nullptr || n->tag != node_tag::object )
            return node_view{};
        if ( idx >= n->object_val.size() )
            return node_view{};
        const object_entry* entries = n->object_val.data();
        if ( entries == nullptr )
            return node_view{};
        return node_view{ entries[idx].value };
    }

    // ----------------------------------------------------------------
    // Разыменование ref

    /// Разыменовать ref-узел.
    /// Если текущий узел — ref, возвращает node_view на целевой узел.
    /// Если recursive == true, разыменовывает цепочку ref-узлов.
    /// Защита от зависания: ограничение глубиной max_depth.
    /// Если текущий узел не ref — возвращает себя.
    node_view deref( bool recursive = true, uintptr_t max_depth = 32 ) const
    {
        node_view cur = *this;
        for ( uintptr_t depth = 0; depth < max_depth; depth++ )
        {
            if ( !cur.is_ref() )
                return cur;
            node_id target = cur.ref_target();
            if ( target == 0 )
                return node_view{}; // не разрешён
            node_view next{ target };
            if ( !recursive )
                return next;
            // Проверка на цикл: если target == текущий id, останавливаемся.
            if ( target == cur.id )
                return node_view{}; // цикл обнаружен
            cur = next;
        }
        // Превышена максимальная глубина.
        return node_view{};
    }

    // ----------------------------------------------------------------
    // Удобные операторы

    /// Доступ к элементу массива через operator[].
    node_view operator[]( uintptr_t idx ) const { return at( idx ); }

    /// Доступ к полю объекта через operator[].
    node_view operator[]( const char* key ) const { return at( key ); }

    bool operator==( const node_view& other ) const { return id == other.id; }
    bool operator!=( const node_view& other ) const { return id != other.id; }

    // ----------------------------------------------------------------
    // Поддержка range-based for

    /// Итератор начала для array-узла (для range-based for).
    /// Использует node_view_iterator; для не-массивов возвращает end().
    node_view_iterator begin() const;

    /// Итератор конца для array-узла (для range-based for).
    node_view_iterator end() const;

    /// Диапазон для итерации по парам ключ-значение object-узла.
    /// Использование: for (auto [key, val] : obj.items()) { ... }
    object_items_range items() const;

  private:
    /// Разрешить указатель на узел в ПАП.
    const node* _resolve() const
    {
        if ( id == 0 )
            return nullptr;
        return pmm_resolve<node>( id );
    }
};

// ---------------------------------------------------------------------------
// Фабричная функция для создания ошибочного node_view
// ---------------------------------------------------------------------------

/// Создать node_view с кодом ошибки err.
/// Использование: return node_view_error(node_error::not_found);
/// is_error() == true, valid() == false, is_null() == false.
inline node_view node_view_error( node_error err )
{
    return node_view{ NODE_ERROR_BASE + static_cast<uintptr_t>( err ) };
}

// ---------------------------------------------------------------------------
// Вспомогательные функции для работы с узлами в ПАП
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Шаблонные helpers для устранения дублирования boilerplate
// ---------------------------------------------------------------------------

/// Разрешить узел по смещению, установить тег и обнулить _pad.
/// Возвращает указатель на узел (nullptr при невалидном смещении).
/// Используется внутренне для устранения дублирования в node_set_* функциях.
inline node* node_resolve_and_set_tag( uintptr_t node_off, node_tag tag )
{
    if ( node_off == 0 )
        return nullptr;
    node* n = pmm_resolve<node>( node_off );
    if ( n == nullptr )
        return nullptr;
    n->tag  = tag;
    n->_pad = 0;
    return n;
}

/// Инициализировать контейнерный узел (array/object/binary) — пустой.
/// Устанавливает тег, обнуляет всю область данных union через array_val
/// (все контейнерные поля имеют одинаковую раскладку: _size/_capacity/_data_idx).
inline void node_set_container_empty( uintptr_t node_off, node_tag tag )
{
    node* n = node_resolve_and_set_tag( node_off, tag );
    if ( n == nullptr )
        return;
    // array_val, object_val, binary_val имеют одинаковую раскладку {_size, _capacity, _data_idx}.
    // Обнуляем через array_val (все три union-члена перекрывают одну и ту же область памяти).
    n->array_val = PamManager::parray<node_id>{};
}

// ---------------------------------------------------------------------------
// Вспомогательные функторы для сортированного массива object_entry
// ---------------------------------------------------------------------------

/// Извлечение ключа (chars_offset) из object_entry.
struct object_entry_key_of
{
    uintptr_t operator()( const object_entry& e ) const { return e.key_chars_offset; }
};

/// Лексикографическое сравнение ключей по chars_offset.
struct object_entry_less
{
    bool operator()( uintptr_t a_offset, uintptr_t b_offset ) const
    {
        if ( a_offset == 0 && b_offset == 0 )
            return false;
        if ( a_offset == 0 )
            return true; // "" < any non-empty
        if ( b_offset == 0 )
            return false;
        const char* a = pmm_resolve<char>( a_offset );
        const char* b = pmm_resolve<char>( b_offset );
        if ( a == nullptr || b == nullptr )
            return false;
        return std::strcmp( a, b ) < 0;
    }
};

/// Вставить object_entry в отсортированный parray (вставка или обновление).
/// Выполняет бинарный поиск, вставляет со сдвигом или обновляет существующий.
/// Вставить object_entry в отсортированный parray. Безопасна при росте PMM-пула:
/// push_back выполняется на стековой копии parray, результат записывается обратно
/// в node после повторной резолвации node_off.
/// @param node_off Смещение object-узла в ПАП (для переразрешения после push_back).
/// @param value    Вставляемый object_entry.
inline void parray_insert_sorted_object_entry( uintptr_t node_off, const object_entry& value )
{
    node* n = pmm_resolve<node>( node_off );
    if ( n == nullptr )
        return;

    PamManager::parray<object_entry> arr = n->object_val;
    uintptr_t                        sz  = arr.size();
    uintptr_t                        lo = 0, hi = sz;

    // Бинарный поиск (lower_bound)
    {
        const object_entry* raw = arr.data();
        if ( raw != nullptr )
        {
            while ( lo < hi )
            {
                uintptr_t mid = ( lo + hi ) / 2;
                if ( object_entry_less{}( object_entry_key_of{}( raw[mid] ), object_entry_key_of{}( value ) ) )
                    lo = mid + 1;
                else
                    hi = mid;
            }
        }
    }

    uintptr_t idx = lo;

    // Проверяем, существует ли уже такой ключ
    {
        const object_entry* raw = arr.data();
        if ( raw != nullptr && idx < sz &&
             !object_entry_less{}( object_entry_key_of{}( value ), object_entry_key_of{}( raw[idx] ) ) &&
             !object_entry_less{}( object_entry_key_of{}( raw[idx] ), object_entry_key_of{}( value ) ) )
        {
            // Ключ найден — обновляем элемент
            arr.set( idx, value );
            // Записываем обратно в node (set не вызывает аллокацию, но для единообразия).
            n             = pmm_resolve<node>( node_off );
            n->object_val = arr;
            return;
        }
    }

    // Нужно вставить новый элемент в позицию idx.
    // Добавляем пустой элемент в конец, затем сдвигаем.
    // push_back может вызвать рост PMM-пула, инвалидируя все указатели.
    // Работаем на стековой копии arr (безопасно).
    object_entry empty{};
    arr.push_back( empty );

    // После push_back данные могли переместиться — получаем свежий указатель.
    object_entry* raw = arr.data();
    if ( raw != nullptr && sz > idx )
        std::memmove( raw + idx + 1, raw + idx, ( sz - idx ) * sizeof( object_entry ) );

    // Записываем новый элемент.
    if ( raw != nullptr )
        raw[idx] = value;

    // Записываем обновлённый parray обратно в node (после возможного роста пула).
    n             = pmm_resolve<node>( node_off );
    n->object_val = arr;
}

// ---------------------------------------------------------------------------
// Бинарный поиск по ключу в отсортированном массиве object_entry
// ---------------------------------------------------------------------------

/// Реализация бинарного поиска ключа key в отсортированном массиве object_entry.
/// entries — указатель на массив, sz — число элементов.
/// Возвращает object_find_result { index, found }.
/// Делегирует три ранее дублировавшихся реализации бинарного поиска.
inline object_find_result node_object_find_key( const object_entry* entries, uintptr_t sz, const char* key )
{
    if ( entries == nullptr )
        return { sz, false };
    uintptr_t lo = 0, hi = sz;
    while ( lo < hi )
    {
        uintptr_t           mid = ( lo + hi ) / 2;
        const object_entry& e   = entries[mid];
        int                 cmp;
        if ( e.key_chars_offset == 0 )
            cmp = ( key[0] == '\0' ) ? 0 : 1;
        else
        {
            const char* ks = pmm_resolve<char>( e.key_chars_offset );
            cmp            = ( ks != nullptr ) ? std::strcmp( ks, key ) : -1;
        }
        if ( cmp < 0 )
            lo = mid + 1;
        else if ( cmp > 0 )
            hi = mid;
        else
            return { mid, true };
    }
    return { sz, false };
}

// ---------------------------------------------------------------------------
// Шаблонный helper для безопасного разрешения узла с проверкой тега
// ---------------------------------------------------------------------------

/// Разрешить узел по смещению id и проверить, что тег == expected_tag.
/// Возвращает указатель на узел (nullptr при невалидном смещении или несовпадении тега).
/// Устраняет дублирование паттерна «resolve + tag check» в методах pjson_db_pmm.
inline node* node_resolve_checked( node_id id, node_tag expected_tag )
{
    if ( id == 0 )
        return nullptr;
    node* n = pmm_resolve<node>( id );
    if ( n == nullptr || n->tag != expected_tag )
        return nullptr;
    return n;
}

// ---------------------------------------------------------------------------

/// Инициализировать узел нулями (null-узел) по смещению node_off в ПАП.
inline void node_init_null( uintptr_t node_off )
{
    node* n = node_resolve_and_set_tag( node_off, node_tag::null );
    if ( n == nullptr )
        return;
    n->ref_val.path_length       = 0;
    n->ref_val.path_chars_offset = 0;
    n->ref_val.target            = 0;
}

/// Установить boolean-значение узла.
inline void node_set_bool( uintptr_t node_off, bool v )
{
    node* n = node_resolve_and_set_tag( node_off, node_tag::boolean );
    if ( n != nullptr )
        n->boolean_val = v ? 1u : 0u;
}

/// Установить integer-значение узла.
inline void node_set_int( uintptr_t node_off, int64_t v )
{
    node* n = node_resolve_and_set_tag( node_off, node_tag::integer );
    if ( n != nullptr )
        n->int_val = v;
}

/// Установить uinteger-значение узла.
inline void node_set_uint( uintptr_t node_off, uint64_t v )
{
    node* n = node_resolve_and_set_tag( node_off, node_tag::uinteger );
    if ( n != nullptr )
        n->uint_val = v;
}

/// Установить real-значение узла.
inline void node_set_real( uintptr_t node_off, double v )
{
    node* n = node_resolve_and_set_tag( node_off, node_tag::real );
    if ( n != nullptr )
        n->real_val = v;
}

/// Установить строковое значение узла (PamManager::pstring, readwrite).
/// Использует PMM pstring::assign() для управления памятью.
/// Безопасна при realloc: переразрешает node после операций с памятью.
inline void node_set_string( uintptr_t node_off, const char* s )
{
    if ( node_off == 0 )
        return;

    // Освобождаем предыдущие данные (если были).
    {
        node* n = pmm_resolve<node>( node_off );
        if ( n == nullptr )
            return;
        if ( n->tag == node_tag::string )
        {
            n->string_val.free_data();
            // free_data() может вызвать deallocate, но не allocate — re-resolve не нужен.
        }
        n->tag        = node_tag::string;
        n->_pad       = 0;
        n->string_val = PamManager::pstring{};
    }

    if ( s == nullptr || s[0] == '\0' )
        return;

    // Выполняем assign на стековой копии, чтобы обойти проблему с realloc.
    // pstring::assign() может вызвать allocate(), который может вызвать realloc хранилища,
    // инвалидируя указатель на node. Поэтому работаем со стековой копией.
    PamManager::pstring tmp{};
    tmp.assign( s );

    // Переразрешаем node после возможного realloc.
    node* n = pmm_resolve<node>( node_off );
    if ( n == nullptr )
        return;
    n->string_val = tmp;
}

/// Переназначить строковое значение узла (pstring assign — readwrite).
/// Освобождает старые символьные данные и выделяет новые.
inline void node_assign_string( uintptr_t node_off, const char* s )
{
    node_set_string( node_off, s );
}

/// Инициализировать array-узел (пустой массив).
inline void node_set_array( uintptr_t node_off )
{
    node_set_container_empty( node_off, node_tag::array );
}

/// Инициализировать object-узел (пустой объект).
inline void node_set_object( uintptr_t node_off )
{
    node_set_container_empty( node_off, node_tag::object );
}

/// Инициализировать binary-узел (пустой массив байт).
inline void node_set_binary( uintptr_t node_off )
{
    node_set_container_empty( node_off, node_tag::binary );
}

/// Установить ref-узел (путь + не разрешённый target).
/// Путь интернируется через pstringview_manager.
inline void node_set_ref( uintptr_t node_off, const char* path )
{
    if ( node_off == 0 )
        return;
    {
        node* n = pmm_resolve<node>( node_off );
        if ( n == nullptr )
            return;
        n->tag                       = node_tag::ref;
        n->_pad                      = 0;
        n->ref_val.path_length       = 0;
        n->ref_val.path_chars_offset = 0;
        n->ref_val.target            = 0;
    }

    if ( path == nullptr || path[0] == '\0' )
        return;

    // Интернируем путь через pstringview_table (readonly).
    auto result = pam_intern_string( path );

    // Переразрешаем node после возможного realloc внутри intern().
    node* n = pmm_resolve<node>( node_off );
    if ( n == nullptr )
        return;
    n->ref_val.path_length       = result.length;
    n->ref_val.path_chars_offset = result.chars_offset;
}

/// Установить target для ref-узла (после разрешения ссылки).
inline void node_set_ref_target( uintptr_t node_off, node_id target )
{
    if ( node_off == 0 )
        return;
    node* n = pmm_resolve<node>( node_off );
    if ( n == nullptr || n->tag != node_tag::ref )
        return;
    n->ref_val.target = target;
}

/// Добавить элемент в array-узел (push_back).
/// Возвращает node_id нового слота (инициализирован как null).
/// Безопасна при realloc.
inline node_id node_array_push_back( uintptr_t node_off )
{
    if ( node_off == 0 )
        return 0;

    // Аллоцируем новый node-слот (null).
    fptr<node> new_slot;
    new_slot.New(); // Может вызвать realloc!
    uintptr_t slot_off = new_slot.addr();

    // Инициализируем слот нулями.
    node* slot = pmm_resolve<node>( slot_off );
    if ( slot != nullptr )
    {
        slot->tag                       = node_tag::null;
        slot->_pad                      = 0;
        slot->ref_val.path_length       = 0;
        slot->ref_val.path_chars_offset = 0;
        slot->ref_val.target            = 0;
    }

    // Переразрешаем array-узел после возможного realloc.
    node* n = pmm_resolve<node>( node_off );
    if ( n == nullptr || n->tag != node_tag::array )
        return slot_off;

    // Добавляем slot_off в массив через parray::push_back.
    // push_back может вызвать рост PMM-пула, инвалидируя n.
    // Используем стековую копию parray для безопасности.
    {
        PamManager::parray<node_id> arr_copy = n->array_val;
        arr_copy.push_back( slot_off );
        // Переразрешаем node после возможного роста пула.
        n            = pmm_resolve<node>( node_off );
        n->array_val = arr_copy;
    }

    return slot_off;
}

/// Вставить или обновить ключ key в object-узле.
/// Возвращает node_id нового/существующего слота (инициализирован как null при новом).
/// Ключ интернируется как pstringview (readonly).
/// Ключи хранятся в отсортированном массиве object_entry (совместимо с pmap_entry<pstringview, node_id>).
inline node_id node_object_insert( uintptr_t node_off, const char* key )
{
    if ( node_off == 0 || key == nullptr )
        return 0;

    // Интернируем ключ через pstringview_table (readonly).
    auto key_result = pam_intern_string( key );
    // После intern — переразрешаем node.
    node* n = pmm_resolve<node>( node_off );
    if ( n == nullptr || n->tag != node_tag::object )
        return 0;

    uintptr_t sz = n->object_val.size();

    // Ищем существующий ключ через бинарный поиск.
    if ( sz > 0 )
    {
        const object_entry* entries = n->object_val.data();
        if ( entries != nullptr )
        {
            auto find_result = node_object_find_key( entries, sz, key );
            if ( find_result.found )
                return entries[find_result.index].value; // ключ существует — возвращаем slot
        }
    }

    // Ключ не найден — аллоцируем новый node-слот.
    fptr<node> new_slot;
    new_slot.New(); // Может вызвать realloc!
    uintptr_t slot_off = new_slot.addr();

    // Инициализируем слот нулями.
    node* slot = pmm_resolve<node>( slot_off );
    if ( slot != nullptr )
    {
        slot->tag                       = node_tag::null;
        slot->_pad                      = 0;
        slot->ref_val.path_length       = 0;
        slot->ref_val.path_chars_offset = 0;
        slot->ref_val.target            = 0;
    }

    // Переразрешаем node после realloc.
    n = pmm_resolve<node>( node_off );
    if ( n == nullptr )
        return slot_off;

    // Вставляем новую запись в отсортированный массив object_entry.
    object_entry new_entry;
    new_entry.key_length       = key_result.length;
    new_entry.key_chars_offset = key_result.chars_offset;
    new_entry.value            = slot_off;

    parray_insert_sorted_object_entry( node_off, new_entry );

    return slot_off;
}

/// Добавить байт в binary-узел (push_back).
inline void node_binary_push_back( uintptr_t node_off, uint8_t byte )
{
    if ( node_off == 0 )
        return;
    node* n = pmm_resolve<node>( node_off );
    if ( n == nullptr || n->tag != node_tag::binary )
        return;
    // push_back может вызвать рост PMM-пула, инвалидируя n.
    // Используем стековую копию parray для безопасности.
    PamManager::parray<uint8_t> arr_copy = n->binary_val;
    arr_copy.push_back( byte );
    n             = pmm_resolve<node>( node_off );
    n->binary_val = arr_copy;
}

// ---------------------------------------------------------------------------
// Глубокое копирование узлов (node_clone)
// ---------------------------------------------------------------------------
//
// Функция node_clone создаёт глубокую копию поддерева JSON в ПАП.
// Копируются все вложенные узлы, строки, массивы и объекты.
// ref-узлы копируются как ref (путь интернируется, target не разрешается).
//
// Все комментарии — на русском языке.

/// Глубоко скопировать узел.
/// Создаёт полную копию поддерева, включая все вложенные структуры.
/// ref-узлы копируются как ref (путь копируется, target = 0, не разрешается автоматически).
/// Возвращает node_id нового узла (копии); 0 при ошибке.
inline node_id node_clone( node_id src_id )
{
    if ( src_id == 0 )
        return 0;

    // Разрешаем исходный узел.
    const node* src = pmm_resolve<node>( src_id );
    if ( src == nullptr )
        return 0;

    // Аллоцируем новый узел.
    fptr<node> dst_fptr;
    dst_fptr.New(); // Может вызвать realloc!
    node_id dst_id = dst_fptr.addr();

    // Переразрешаем src после возможного realloc.
    src = pmm_resolve<node>( src_id );
    if ( src == nullptr )
        return dst_id;

    switch ( src->tag )
    {
    case node_tag::null:
        node_init_null( dst_id );
        break;

    case node_tag::boolean:
        node_set_bool( dst_id, src->boolean_val != 0 );
        break;

    case node_tag::integer:
        node_set_int( dst_id, src->int_val );
        break;

    case node_tag::uinteger:
        node_set_uint( dst_id, src->uint_val );
        break;

    case node_tag::real:
        node_set_real( dst_id, src->real_val );
        break;

    case node_tag::string:
    {
        // Копируем pstring (readwrite): читаем символы из исходного узла.
        std::string_view sv = node_view{ src_id }.as_string();
        std::string      s( sv );
        node_set_string( dst_id, s.c_str() );
        break;
    }

    case node_tag::binary:
    {
        // Инициализируем binary-узел и копируем данные побайтно.
        node_set_binary( dst_id );
        // Переразрешаем src после set_binary (может вызвать realloc).
        src = pmm_resolve<node>( src_id );
        if ( src == nullptr )
            break;
        uintptr_t bin_size = src->binary_val.size();
        if ( bin_size > 0 )
        {
            const uint8_t* bin_data = src->binary_val.data();
            if ( bin_data != nullptr )
            {
                for ( uintptr_t i = 0; i < bin_size; ++i )
                {
                    uint8_t byte = bin_data[i];
                    node_binary_push_back( dst_id, byte );
                    // Переразрешаем bin_data после push_back (может вызвать realloc).
                    src = pmm_resolve<node>( src_id );
                    if ( src == nullptr )
                        break;
                    bin_data = src->binary_val.data();
                    if ( bin_data == nullptr )
                        break;
                }
            }
        }
        break;
    }

    case node_tag::array:
    {
        // Инициализируем array-узел и рекурсивно копируем элементы.
        node_set_array( dst_id );
        // Переразрешаем src после set_array.
        src = pmm_resolve<node>( src_id );
        if ( src == nullptr )
            break;
        uintptr_t arr_size = src->array_val.size();
        for ( uintptr_t i = 0; i < arr_size; ++i )
        {
            // Получаем node_id i-го элемента исходного массива.
            node_view src_elem = node_view{ src_id }.at( i );
            if ( !src_elem.valid() )
                continue;
            // Рекурсивно клонируем элемент.
            node_id elem_clone = node_clone( src_elem.id );
            // Добавляем слот в целевой массив.
            node_id slot_id = node_array_push_back( dst_id );
            // Копируем содержимое склонированного элемента в слот.
            // Так как push_back создаёт null-узел, а clone создаёт полную копию,
            // нужно переместить данные. Проще: записываем node_id прямо в массив.
            node* dst_node = pmm_resolve<node>( dst_id );
            if ( dst_node != nullptr && dst_node->tag == node_tag::array )
            {
                node_id* arr = dst_node->array_val.data();
                if ( arr != nullptr && dst_node->array_val.size() > 0 )
                {
                    arr[dst_node->array_val.size() - 1] = elem_clone;
                }
            }
            // Удаляем временный null-слот.
            if ( slot_id != 0 && slot_id != elem_clone )
            {
                fptr<node> tmp;
                tmp.set_addr( slot_id );
                tmp.Delete();
            }
        }
        break;
    }

    case node_tag::object:
    {
        // Инициализируем object-узел и рекурсивно копируем пары ключ-значение.
        node_set_object( dst_id );
        node_view src_view{ src_id };
        uintptr_t obj_size = src_view.size();
        for ( uintptr_t i = 0; i < obj_size; ++i )
        {
            std::string_view key_sv = src_view.key_at( i );
            node_view        val_v  = src_view.value_at( i );
            if ( key_sv.empty() && !val_v.valid() )
                continue;
            std::string key_s( key_sv );
            // Рекурсивно клонируем значение.
            node_id val_clone = node_clone( val_v.id );
            // Вставляем ключ в целевой объект.
            node_id slot_id = node_object_insert( dst_id, key_s.c_str() );
            // Записываем склонированное значение в слот.
            node* dst_node = pmm_resolve<node>( dst_id );
            if ( dst_node != nullptr && dst_node->tag == node_tag::object )
            {
                object_entry* entries = dst_node->object_val.data();
                if ( entries != nullptr )
                {
                    // Ищем запись по slot_id (value) и заменяем.
                    for ( uintptr_t j = 0; j < dst_node->object_val.size(); ++j )
                    {
                        if ( entries[j].value == slot_id )
                        {
                            entries[j].value = val_clone;
                            break;
                        }
                    }
                }
            }
            // Удаляем временный null-слот, если он не был использован.
            if ( slot_id != 0 && slot_id != val_clone )
            {
                fptr<node> tmp;
                tmp.set_addr( slot_id );
                tmp.Delete();
            }
        }
        break;
    }

    case node_tag::ref:
    {
        // Копируем ref-узел: копируем путь, target = 0 (не разрешён).
        std::string_view path_sv = node_view{ src_id }.ref_path();
        std::string      path_s( path_sv );
        node_set_ref( dst_id, path_s.c_str() );
        // target не копируем (остаётся 0) — ссылка не разрешена в копии.
        break;
    }

    case node_tag::_free:
        // Служебный тег — не копируем.
        node_init_null( dst_id );
        break;
    }

    return dst_id;
}

// ---------------------------------------------------------------------------
// Итераторы для обхода дерева JSON
// ---------------------------------------------------------------------------
//
// Поддерживают range-based for для массивов и объектов node_view:
//   for (node_view elem : arr_view) { ... }         // для array
//   for (auto [key, val] : obj_view.items()) { ... } // для object
//
// Все итераторы — forward-only, read-only.
// Комментарии — на русском языке.

// ---------------------------------------------------------------------------
// node_view_iterator — итератор элементов массива
// ---------------------------------------------------------------------------

/// Итератор для обхода элементов array-узла.
/// Возвращает node_view для каждого элемента массива по индексу.
struct node_view_iterator
{
    node_id   arr_id; ///< node_id массива
    uintptr_t idx;    ///< Текущий индекс

    using iterator_category = std::forward_iterator_tag;
    using value_type        = node_view;
    using difference_type   = std::ptrdiff_t;
    using pointer           = const node_view*;
    using reference         = node_view;

    node_view_iterator( node_id aid, uintptr_t i ) : arr_id( aid ), idx( i ) {}

    /// Разыменование: возвращает node_view для текущего элемента.
    node_view operator*() const { return node_view{ arr_id }.at( idx ); }

    /// Инкремент (префиксный).
    node_view_iterator& operator++()
    {
        ++idx;
        return *this;
    }

    /// Инкремент (постфиксный).
    node_view_iterator operator++( int )
    {
        node_view_iterator tmp = *this;
        ++idx;
        return tmp;
    }

    bool operator==( const node_view_iterator& other ) const { return arr_id == other.arr_id && idx == other.idx; }
    bool operator!=( const node_view_iterator& other ) const { return !( *this == other ); }
};

// ---------------------------------------------------------------------------
// object_item — пара ключ-значение для итерации по объекту
// ---------------------------------------------------------------------------

/// Пара ключ-значение при итерации по object-узлу.
struct object_item
{
    std::string_view key;   ///< Ключ (pstringview, readonly, интернированный)
    node_view        value; ///< Значение (node_view на узел-значение)
};

// ---------------------------------------------------------------------------
// object_iterator — итератор пар ключ-значение объекта
// ---------------------------------------------------------------------------

/// Итератор для обхода пар ключ-значение object-узла.
/// Возвращает object_item{key, value} для каждого поля объекта.
struct object_iterator
{
    node_id   obj_id; ///< node_id объекта
    uintptr_t idx;    ///< Текущий индекс

    using iterator_category = std::forward_iterator_tag;
    using value_type        = object_item;
    using difference_type   = std::ptrdiff_t;
    using pointer           = const object_item*;
    using reference         = object_item;

    object_iterator( node_id oid, uintptr_t i ) : obj_id( oid ), idx( i ) {}

    /// Разыменование: возвращает object_item для текущего поля объекта.
    object_item operator*() const
    {
        node_view v{ obj_id };
        return object_item{ v.key_at( idx ), v.value_at( idx ) };
    }

    /// Инкремент (префиксный).
    object_iterator& operator++()
    {
        ++idx;
        return *this;
    }

    /// Инкремент (постфиксный).
    object_iterator operator++( int )
    {
        object_iterator tmp = *this;
        ++idx;
        return tmp;
    }

    bool operator==( const object_iterator& other ) const { return obj_id == other.obj_id && idx == other.idx; }
    bool operator!=( const object_iterator& other ) const { return !( *this == other ); }
};

// ---------------------------------------------------------------------------
// object_items_range — вспомогательный диапазон для items()
// ---------------------------------------------------------------------------

/// Диапазон для range-based for по парам ключ-значение объекта.
/// Возвращается методом node_view::items().
struct object_items_range
{
    node_id   obj_id; ///< node_id объекта
    uintptr_t sz;     ///< Число элементов

    object_items_range( node_id oid, uintptr_t n ) : obj_id( oid ), sz( n ) {}

    object_iterator begin() const { return object_iterator{ obj_id, 0 }; }
    object_iterator end() const { return object_iterator{ obj_id, sz }; }
};

// ---------------------------------------------------------------------------
// array_range — вспомогательный диапазон для range-based for по массиву
// ---------------------------------------------------------------------------

/// Диапазон для range-based for по элементам array-узла.
/// Используется при итерации через node_view::begin()/end().
struct array_range
{
    node_id   arr_id; ///< node_id массива
    uintptr_t sz;     ///< Число элементов

    array_range( node_id aid, uintptr_t n ) : arr_id( aid ), sz( n ) {}

    node_view_iterator begin() const { return node_view_iterator{ arr_id, 0 }; }
    node_view_iterator end() const { return node_view_iterator{ arr_id, sz }; }
};

// ---------------------------------------------------------------------------
// Определения методов node_view для итерации
// ---------------------------------------------------------------------------
// Определяются после полных объявлений итераторных структур.

/// Итератор начала для array-узла.
/// Для не-массивов возвращает end() (пустой диапазон).
inline node_view_iterator node_view::begin() const
{
    return node_view_iterator{ id, 0 };
}

/// Итератор конца для array-узла.
inline node_view_iterator node_view::end() const
{
    return node_view_iterator{ id, size() };
}

/// Диапазон для итерации по парам ключ-значение object-узла.
/// Использование: for (auto& [key, val] : obj.items()) { ... }
/// Для не-объектов возвращает пустой диапазон.
inline object_items_range node_view::items() const
{
    if ( !is_object() )
        return object_items_range{ 0, 0 };
    return object_items_range{ id, size() };
}
