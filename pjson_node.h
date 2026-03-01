#pragma once
#include "pstringview.h"
#include "pstring.h"
#include "pvector.h"
#include "pmap.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>

// pjson_node.h — Расширенная модель узлов JSON (Фаза 3).
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
// Все комментарии — на русском языке (Тр.6).

// ---------------------------------------------------------------------------
// node_tag — дискриминант типа узла
// ---------------------------------------------------------------------------

/// Расширенный набор типов узлов JSON (Задача 3.1).
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
// node_id — идентификатор узла (смещение в ПАП)
// ---------------------------------------------------------------------------

/// Смещение узла в ПАП; 0 = null/invalid (Задача 3.3).
using node_id = uintptr_t;

// ---------------------------------------------------------------------------
// node — структура узла JSON в ПАП (Задача 3.2)
// ---------------------------------------------------------------------------
//
// Раскладка:
//   tag:  4 байта — дискриминант (node_tag : uint32_t)
//   _pad: 4 байта — выравнивание для 8-байтового union (на 64-битных платформах)
//   union: 3 * sizeof(void*) байт — payload (максимальный из вариантов)
//
// Все поля — POD (тривиально копируемые), живут только в ПАП.
// Доступ — только через смещение (node_id) и PersistentAddressSpace::Resolve<node>.
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

        /// string: pstring (readwrite, length + chars).
        /// Раскладка идентична pstring: { uintptr_t length; fptr<char> chars; }.
        /// Строковые значения JSON — изменяемые на лету (необходимо для jsonRVM).
        struct
        {
            uintptr_t length;       ///< Число символов (без нулевого терминатора)
            uintptr_t chars_offset; ///< Смещение массива char в ПАП; 0 = пустая строка
        } string_val;

        /// binary: pvector<uint8_t>-совместимая раскладка в ПАП.
        /// ($base64 при сериализации)
        struct
        {
            uintptr_t size;     ///< Текущее число байт
            uintptr_t capacity; ///< Выделенная ёмкость
            uintptr_t data_off; ///< Смещение массива uint8_t в ПАП; 0 = пусто
        } binary_val;

        /// array: pvector<node_id>-совместимая раскладка.
        struct
        {
            uintptr_t size;     ///< Текущее число элементов
            uintptr_t capacity; ///< Выделенная ёмкость
            uintptr_t data_off; ///< Смещение массива node_id в ПАП; 0 = пусто
        } array_val;

        /// object: pmap<pstringview, node_id>-совместимая раскладка.
        /// Ключи — readonly pstringview (интернированные), значения — node_id.
        struct
        {
            uintptr_t size;     ///< Текущее число пар
            uintptr_t capacity; ///< Выделенная ёмкость
            uintptr_t data_off; ///< Смещение массива object_entry в ПАП; 0 = пусто
        } object_val;

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
static_assert( sizeof( node::string_val ) == 2 * sizeof( void* ),
               "node::string_val должен занимать 2 * sizeof(void*) байт (совместимость с pstring)" );
static_assert( sizeof( node::binary_val ) == 3 * sizeof( void* ),
               "node::binary_val должен занимать 3 * sizeof(void*) байт (совместимость с pvector)" );
static_assert( sizeof( node::array_val ) == 3 * sizeof( void* ),
               "node::array_val должен занимать 3 * sizeof(void*) байт (совместимость с pvector)" );
static_assert( sizeof( node::object_val ) == 3 * sizeof( void* ),
               "node::object_val должен занимать 3 * sizeof(void*) байт (совместимость с pmap)" );
static_assert( sizeof( node::ref_val ) == 3 * sizeof( void* ), "node::ref_val должен занимать 3 * sizeof(void*) байт" );

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
// node_view — безопасный accessor для чтения узлов (Задача 3.4)
// ---------------------------------------------------------------------------
//
// node_view хранит node_id (смещение узла в ПАП) и предоставляет типобезопасный
// read-only интерфейс для работы с узлами через PersistentAddressSpace.
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

    /// Проверить, является ли node_view действительным (не null/invalid).
    bool valid() const { return id != 0; }

    /// Получить тег типа узла.
    node_tag tag() const
    {
        if ( id == 0 )
            return node_tag::null;
        const node* n = _resolve();
        if ( n == nullptr )
            return node_tag::null;
        return n->tag;
    }

    bool is_null() const { return tag() == node_tag::null; }
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
        if ( n->string_val.chars_offset == 0 )
            return std::string_view{};
        auto&       pam = PersistentAddressSpace::Get();
        const char* s   = pam.Resolve<char>( n->string_val.chars_offset );
        if ( s == nullptr )
            return std::string_view{};
        return std::string_view{ s, static_cast<std::size_t>( n->string_val.length ) };
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
        auto&       pam = PersistentAddressSpace::Get();
        const char* s   = pam.Resolve<char>( n->ref_val.path_chars_offset );
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
            return n->array_val.size;
        if ( n->tag == node_tag::object )
            return n->object_val.size;
        if ( n->tag == node_tag::binary )
            return n->binary_val.size;
        if ( n->tag == node_tag::string )
            return n->string_val.length;
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
        if ( idx >= n->array_val.size )
            return node_view{};
        if ( n->array_val.data_off == 0 )
            return node_view{};
        auto&          pam = PersistentAddressSpace::Get();
        const node_id* arr = pam.Resolve<node_id>( n->array_val.data_off );
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
        if ( n->object_val.size == 0 || n->object_val.data_off == 0 )
            return node_view{};

        auto&               pam      = PersistentAddressSpace::Get();
        uintptr_t           sz       = n->object_val.size;
        uintptr_t           data_off = n->object_val.data_off;
        const object_entry* entries  = pam.Resolve<object_entry>( data_off );
        if ( entries == nullptr )
            return node_view{};

        // Бинарный поиск по ключу (лексикографический порядок).
        uintptr_t lo = 0, hi = sz;
        while ( lo < hi )
        {
            uintptr_t mid         = ( lo + hi ) / 2;
            entries               = pam.Resolve<object_entry>( data_off );
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
                return node_view{ entries[mid].value };
        }
        return node_view{};
    }

    /// Получить ключ i-го поля объекта (для итерации).
    /// Возвращает std::string_view; "" при ошибке.
    std::string_view key_at( uintptr_t idx ) const
    {
        const node* n = _resolve();
        if ( n == nullptr || n->tag != node_tag::object )
            return std::string_view{};
        if ( idx >= n->object_val.size || n->object_val.data_off == 0 )
            return std::string_view{};
        auto&               pam     = PersistentAddressSpace::Get();
        const object_entry* entries = pam.Resolve<object_entry>( n->object_val.data_off );
        if ( entries == nullptr )
            return std::string_view{};
        const object_entry& e = entries[idx];
        if ( e.key_chars_offset == 0 )
            return std::string_view{};
        const char* s = pam.Resolve<char>( e.key_chars_offset );
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
        if ( idx >= n->object_val.size || n->object_val.data_off == 0 )
            return node_view{};
        auto&               pam     = PersistentAddressSpace::Get();
        const object_entry* entries = pam.Resolve<object_entry>( n->object_val.data_off );
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

  private:
    /// Разрешить указатель на узел в ПАП.
    const node* _resolve() const
    {
        if ( id == 0 )
            return nullptr;
        return PersistentAddressSpace::Get().Resolve<node>( id );
    }
};

// ---------------------------------------------------------------------------
// Вспомогательные функции для работы с узлами в ПАП
// ---------------------------------------------------------------------------

/// Инициализировать узел нулями (null-узел) по смещению node_off в ПАП.
inline void node_init_null( uintptr_t node_off )
{
    if ( node_off == 0 )
        return;
    auto& pam = PersistentAddressSpace::Get();
    node* n   = pam.Resolve<node>( node_off );
    if ( n == nullptr )
        return;
    n->tag                       = node_tag::null;
    n->_pad                      = 0;
    n->ref_val.path_length       = 0;
    n->ref_val.path_chars_offset = 0;
    n->ref_val.target            = 0;
}

/// Установить boolean-значение узла.
inline void node_set_bool( uintptr_t node_off, bool v )
{
    if ( node_off == 0 )
        return;
    auto& pam = PersistentAddressSpace::Get();
    node* n   = pam.Resolve<node>( node_off );
    if ( n == nullptr )
        return;
    n->tag         = node_tag::boolean;
    n->_pad        = 0;
    n->boolean_val = v ? 1u : 0u;
}

/// Установить integer-значение узла.
inline void node_set_int( uintptr_t node_off, int64_t v )
{
    if ( node_off == 0 )
        return;
    auto& pam = PersistentAddressSpace::Get();
    node* n   = pam.Resolve<node>( node_off );
    if ( n == nullptr )
        return;
    n->tag     = node_tag::integer;
    n->_pad    = 0;
    n->int_val = v;
}

/// Установить uinteger-значение узла.
inline void node_set_uint( uintptr_t node_off, uint64_t v )
{
    if ( node_off == 0 )
        return;
    auto& pam = PersistentAddressSpace::Get();
    node* n   = pam.Resolve<node>( node_off );
    if ( n == nullptr )
        return;
    n->tag      = node_tag::uinteger;
    n->_pad     = 0;
    n->uint_val = v;
}

/// Установить real-значение узла.
inline void node_set_real( uintptr_t node_off, double v )
{
    if ( node_off == 0 )
        return;
    auto& pam = PersistentAddressSpace::Get();
    node* n   = pam.Resolve<node>( node_off );
    if ( n == nullptr )
        return;
    n->tag      = node_tag::real;
    n->_pad     = 0;
    n->real_val = v;
}

/// Установить строковое значение узла (pstring, readwrite).
/// Аллоцирует массив char в ПАП и сохраняет смещение.
/// Безопасна при realloc: сохраняет node_off до вызова NewArray.
inline void node_set_string( uintptr_t node_off, const char* s )
{
    if ( node_off == 0 )
        return;
    auto& pam = PersistentAddressSpace::Get();

    // Освобождаем предыдущие данные (если были).
    {
        node* n = pam.Resolve<node>( node_off );
        if ( n == nullptr )
            return;
        if ( n->tag == node_tag::string && n->string_val.chars_offset != 0 )
        {
            uintptr_t old_chars = n->string_val.chars_offset;
            pam.Delete( old_chars );
            // После Delete — re-resolve.
            n = pam.Resolve<node>( node_off );
            if ( n == nullptr )
                return;
        }
        n->tag                     = node_tag::string;
        n->_pad                    = 0;
        n->string_val.length       = 0;
        n->string_val.chars_offset = 0;
    }

    if ( s == nullptr || s[0] == '\0' )
        return;

    uintptr_t len = static_cast<uintptr_t>( std::strlen( s ) );

    // Выделяем массив символов.
    fptr<char> arr;
    arr.NewArray( static_cast<unsigned>( len + 1 ) ); // Может вызвать realloc!
    uintptr_t chars_off = arr.addr();

    // Переразрешаем node после возможного realloc.
    node* n = pam.Resolve<node>( node_off );
    if ( n == nullptr )
        return;
    n->string_val.length       = len;
    n->string_val.chars_offset = chars_off;

    // Копируем строку.
    char* dst = pam.Resolve<char>( chars_off );
    if ( dst != nullptr )
        std::memcpy( dst, s, static_cast<std::size_t>( len + 1 ) );
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
    if ( node_off == 0 )
        return;
    auto& pam = PersistentAddressSpace::Get();
    node* n   = pam.Resolve<node>( node_off );
    if ( n == nullptr )
        return;
    n->tag                = node_tag::array;
    n->_pad               = 0;
    n->array_val.size     = 0;
    n->array_val.capacity = 0;
    n->array_val.data_off = 0;
}

/// Инициализировать object-узел (пустой объект).
inline void node_set_object( uintptr_t node_off )
{
    if ( node_off == 0 )
        return;
    auto& pam = PersistentAddressSpace::Get();
    node* n   = pam.Resolve<node>( node_off );
    if ( n == nullptr )
        return;
    n->tag                 = node_tag::object;
    n->_pad                = 0;
    n->object_val.size     = 0;
    n->object_val.capacity = 0;
    n->object_val.data_off = 0;
}

/// Инициализировать binary-узел (пустой массив байт).
inline void node_set_binary( uintptr_t node_off )
{
    if ( node_off == 0 )
        return;
    auto& pam = PersistentAddressSpace::Get();
    node* n   = pam.Resolve<node>( node_off );
    if ( n == nullptr )
        return;
    n->tag                 = node_tag::binary;
    n->_pad                = 0;
    n->binary_val.size     = 0;
    n->binary_val.capacity = 0;
    n->binary_val.data_off = 0;
}

/// Установить ref-узел (путь + не разрешённый target).
/// Путь интернируется через pstringview_manager.
inline void node_set_ref( uintptr_t node_off, const char* path )
{
    if ( node_off == 0 )
        return;
    auto& pam = PersistentAddressSpace::Get();
    {
        node* n = pam.Resolve<node>( node_off );
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
    node* n = pam.Resolve<node>( node_off );
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
    auto& pam = PersistentAddressSpace::Get();
    node* n   = pam.Resolve<node>( node_off );
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
    auto& pam = PersistentAddressSpace::Get();

    // Аллоцируем новый node-слот (null).
    fptr<node> new_slot;
    new_slot.New(); // Может вызвать realloc!
    uintptr_t slot_off = new_slot.addr();

    // Инициализируем слот нулями.
    node* slot = pam.Resolve<node>( slot_off );
    if ( slot != nullptr )
    {
        slot->tag                       = node_tag::null;
        slot->_pad                      = 0;
        slot->ref_val.path_length       = 0;
        slot->ref_val.path_chars_offset = 0;
        slot->ref_val.target            = 0;
    }

    // Переразрешаем array-узел после возможного realloc.
    node* n = pam.Resolve<node>( node_off );
    if ( n == nullptr || n->tag != node_tag::array )
        return slot_off;

    uintptr_t old_size = n->array_val.size;
    uintptr_t new_size = old_size + 1;
    uintptr_t cap      = n->array_val.capacity;

    if ( new_size > cap )
    {
        uintptr_t new_cap = ( cap == 0 ) ? 4 : cap * 2;
        if ( new_cap < new_size )
            new_cap = new_size;
        uintptr_t old_data = n->array_val.data_off;

        fptr<node_id> new_arr;
        new_arr.NewArray( static_cast<unsigned>( new_cap ) ); // Может вызвать realloc!
        uintptr_t new_data = new_arr.addr();

        // Инициализируем новые слоты нулём.
        node_id* new_raw = pam.Resolve<node_id>( new_data );
        if ( new_raw != nullptr )
        {
            for ( uintptr_t i = 0; i < new_cap; i++ )
                new_raw[i] = 0;
        }

        // Копируем существующие элементы.
        if ( old_data != 0 )
        {
            const node_id* old_raw = pam.Resolve<node_id>( old_data );
            new_raw                = pam.Resolve<node_id>( new_data );
            if ( old_raw != nullptr && new_raw != nullptr )
            {
                for ( uintptr_t i = 0; i < old_size; i++ )
                    new_raw[i] = old_raw[i];
            }
            // Освобождаем старый массив.
            fptr<node_id> old_fptr;
            old_fptr.set_addr( old_data );
            old_fptr.DeleteArray();
        }

        // Переразрешаем n после возможного realloc.
        n = pam.Resolve<node>( node_off );
        if ( n == nullptr )
            return slot_off;
        n->array_val.data_off = new_data;
        n->array_val.capacity = new_cap;
    }

    // Записываем slot_off в массив.
    n = pam.Resolve<node>( node_off );
    if ( n == nullptr )
        return slot_off;
    node_id* arr = pam.Resolve<node_id>( n->array_val.data_off );
    if ( arr != nullptr )
        arr[old_size] = slot_off;
    n->array_val.size = new_size;

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

    auto& pam = PersistentAddressSpace::Get();

    // Интернируем ключ через pstringview_table (readonly).
    auto key_result = pam_intern_string( key );
    // После intern — переразрешаем node.
    node* n = pam.Resolve<node>( node_off );
    if ( n == nullptr || n->tag != node_tag::object )
        return 0;

    uintptr_t sz       = n->object_val.size;
    uintptr_t data_off = n->object_val.data_off;

    // Ищем существующий ключ через бинарный поиск по key_chars_offset.
    if ( sz > 0 && data_off != 0 )
    {
        object_entry* entries = pam.Resolve<object_entry>( data_off );
        uintptr_t     lo = 0, hi = sz;
        while ( lo < hi )
        {
            uintptr_t mid = ( lo + hi ) / 2;
            entries       = pam.Resolve<object_entry>( data_off );
            if ( entries == nullptr )
                break;
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
                return entries[mid].value; // ключ существует — возвращаем slot
        }
    }

    // Ключ не найден — аллоцируем новый node-слот.
    fptr<node> new_slot;
    new_slot.New(); // Может вызвать realloc!
    uintptr_t slot_off = new_slot.addr();

    // Инициализируем слот нулями.
    node* slot = pam.Resolve<node>( slot_off );
    if ( slot != nullptr )
    {
        slot->tag                       = node_tag::null;
        slot->_pad                      = 0;
        slot->ref_val.path_length       = 0;
        slot->ref_val.path_chars_offset = 0;
        slot->ref_val.target            = 0;
    }

    // Переразрешаем node после realloc.
    n = pam.Resolve<node>( node_off );
    if ( n == nullptr )
        return slot_off;

    // Вставляем новую запись в отсортированный массив object_entry.
    // Используем pmem_array_insert_sorted с KeyOf и Less для object_entry.
    struct ObjKeyOf
    {
        uintptr_t operator()( const object_entry& e ) const { return e.key_chars_offset; }
    };
    struct ObjLess
    {
        // Сравниваем ключи лексикографически через c_str().
        // Оба chars_offset должны указывать на интернированные строки.
        bool operator()( uintptr_t a_offset, uintptr_t b_offset ) const
        {
            auto& pam = PersistentAddressSpace::Get();
            if ( a_offset == 0 && b_offset == 0 )
                return false;
            if ( a_offset == 0 )
                return true; // "" < any non-empty
            if ( b_offset == 0 )
                return false;
            const char* a = pam.Resolve<char>( a_offset );
            const char* b = pam.Resolve<char>( b_offset );
            if ( a == nullptr || b == nullptr )
                return false;
            return std::strcmp( a, b ) < 0;
        }
    };

    object_entry new_entry;
    new_entry.key_length       = key_result.length;
    new_entry.key_chars_offset = key_result.chars_offset;
    new_entry.value            = slot_off;

    // Получаем смещение заголовка pmem_array_hdr внутри object_val.
    // object_val.{size, capacity, data_off} имеют ту же раскладку что pmem_array_hdr,
    // поэтому reinterpret_cast допустим (только для POD-структур).
    uintptr_t hdr_off =
        pam.PtrToOffset( reinterpret_cast<pmem_array_hdr*>( &( pam.Resolve<node>( node_off )->object_val.size ) ) );

    pmem_array_insert_sorted<object_entry, ObjKeyOf, ObjLess>( hdr_off, new_entry, ObjKeyOf{}, ObjLess{} );

    return slot_off;
}

/// Добавить байт в binary-узел (push_back).
inline void node_binary_push_back( uintptr_t node_off, uint8_t byte )
{
    if ( node_off == 0 )
        return;
    auto& pam = PersistentAddressSpace::Get();

    // Получаем заголовок массива binary_val.
    uintptr_t hdr_off =
        pam.PtrToOffset( reinterpret_cast<pmem_array_hdr*>( &( pam.Resolve<node>( node_off )->binary_val.size ) ) );

    uint8_t& slot = pmem_array_push_back<uint8_t>( hdr_off );
    slot          = byte;
}
