#pragma once
#include "pstring.h"
#include "pvector.h"
#include "pmap.h"
#include "pallocator.h"
#include <cstring>
#include <cstdint>
#include <string>
#include <type_traits>
// nlohmann/json.hpp сохранён для совместимости с тестами, использующими
// nlohmann::json напрямую. Прямая сериализация pjson реализована в
// pjson_serializer.h без зависимости от nlohmann (требование F6, задача #84).
#include "nlohmann/json.hpp"

// pjson — персистная дискриминантная объединяющая структура (персистный аналог nlohmann::json).
//
// Объекты pjson могут находиться ТОЛЬКО внутри ПАП.
// Для работы с pjson из обычного кода используйте fptr<pjson>.
//
// Требования:
//   - Конструктор и деструктор приватные; создание только через ПАМ (Тр.2, Тр.11).
//   - При загрузке образа ПАП конструкторы не вызываются (Тр.10).
//
// Дизайн: Персистная дискриминантная структура, использующая:
//   - pstring для строковых значений (поле string_val в payload_t)
//   - pvector<pjson> для массивов (поле array_val, совместимая раскладка)
//   - pvector<pjson_kv_entry> для объектов (поле object_val, совместимая раскладка)
//   - pstring для ключей объекта (поле pjson_kv_entry::key)
//   - pallocator<T> для совместимости с STL-аллокаторами
//
// Раскладка payload_t:
//   pstring занимает 2 * sizeof(void*) (length + chars).
//   pvector<T> занимает 3 * sizeof(void*) (size + capacity + data).
//   array_val и object_val имеют совместимую с pvector раскладку (3 поля).
//   Поэтому sizeof(payload_t) >= 3 * sizeof(void*).

// ---------------------------------------------------------------------------
// pjson_type — тег-дискриминант типа значения
// ---------------------------------------------------------------------------
enum class pjson_type : uint32_t
{
    null     = 0,
    boolean  = 1,
    integer  = 2,
    uinteger = 3,
    real     = 4,
    string   = 5,
    array    = 6,
    object   = 7,
};

// ---------------------------------------------------------------------------
// Предварительные объявления
// ---------------------------------------------------------------------------

struct pjson;
struct pjson_kv_entry;

// ---------------------------------------------------------------------------
// pjson — персистная дискриминантная структура JSON-значения.
// Живёт только в ПАП, доступ через fptr<pjson>.
//
// payload_t::string_val — тип pstring (length + chars, 2 * sizeof(void*)).
// payload_t::array_val  — раскладка pvector<pjson> (size + capacity + data, 3 * sizeof(void*)).
// payload_t::object_val — раскладка pvector<pjson_kv_entry> (size + capacity + data).
// ---------------------------------------------------------------------------
struct pjson
{
    pjson_type type; // 4 байта — дискриминант

    union payload_t
    {
        uint32_t boolean_val; // 0 = false, ненулевое = true
        int64_t  int_val;
        uint64_t uint_val;
        double   real_val;

        // Для строки: pstring (length + chars).
        // Раскладка идентична pstring: { uintptr_t length; fptr<char> chars; }.
        pstring string_val;

        // Для массива: раскладка совместима с pvector<pjson>.
        // Используется struct вместо pvector<pjson> напрямую, так как pjson
        // является неполным типом в точке определения union.
        // В методах array_val приводится к pvector<pjson>& через reinterpret_cast.
        struct array_layout
        {
            uintptr_t   size;     ///< Текущее число элементов (pvector::size_)
            uintptr_t   capacity; ///< Выделенная ёмкость (pvector::capacity_)
            fptr<pjson> data;     ///< Смещение массива в ПАП (pvector::data_)
        } array_val;

        // Для объекта: раскладка совместима с pvector<pjson_kv_entry>.
        struct object_layout
        {
            uintptr_t            size;     ///< Текущее число пар (pvector::size_)
            uintptr_t            capacity; ///< Выделенная ёмкость (pvector::capacity_)
            fptr<pjson_kv_entry> data;     ///< Смещение массива пар в ПАП (pvector::data_)
        } object_val;
    } payload;

    // ----- Запросы типа ---------------------------------------------------
    pjson_type type_tag() const { return type; }
    bool       is_null() const { return type == pjson_type::null; }
    bool       is_boolean() const { return type == pjson_type::boolean; }
    bool       is_integer() const { return type == pjson_type::integer; }
    bool       is_uinteger() const { return type == pjson_type::uinteger; }
    bool       is_real() const { return type == pjson_type::real; }
    bool       is_number() const { return is_integer() || is_uinteger() || is_real(); }
    bool       is_string() const { return type == pjson_type::string; }
    bool       is_array() const { return type == pjson_type::array; }
    bool       is_object() const { return type == pjson_type::object; }

    uintptr_t size() const
    {
        if ( type == pjson_type::array )
            return payload.array_val.size;
        if ( type == pjson_type::object )
            return payload.object_val.size;
        if ( type == pjson_type::string )
            return payload.string_val.size();
        return 0;
    }

    bool empty() const { return size() == 0; }

    bool     get_bool() const { return payload.boolean_val != 0; }
    int64_t  get_int() const { return payload.int_val; }
    uint64_t get_uint() const { return payload.uint_val; }
    double   get_real() const { return payload.real_val; }

    // get_string: вернуть raw-указатель на строковые данные через pstring::c_str().
    const char* get_string() const { return payload.string_val.c_str(); }

    // Методы, требующие полного определения pjson_kv_entry,
    // объявлены здесь, а определены ПОСЛЕ pjson_kv_entry.
    void free();
    void set_null();
    void set_bool( bool v );
    void set_int( int64_t v );
    void set_uint( uint64_t v );
    void set_real( double v );
    void set_string( const char* s );
    void set_array();
    void set_object();

    pjson& push_back();

    pjson&       operator[]( uintptr_t idx );
    const pjson& operator[]( uintptr_t idx ) const;

    pjson*       obj_find( const char* key );
    const pjson* obj_find( const char* key ) const;
    pjson&       obj_insert( const char* key );
    bool         obj_erase( const char* key );

    // Сериализация pjson в строку JSON (Фаза 7, оптимизирована в задаче #84).
    // Реализована напрямую без nlohmann::json (требование F6).
    // Возвращает std::string с минимальным JSON-представлением.
    std::string to_string() const;

    // Десериализация строки JSON в pjson по смещению в ПАМ (Фаза 7, оптимизирована в задаче #84).
    // Реализована напрямую без nlohmann::json (требование F6).
    // Принимает смещение (не сырой указатель) для защиты от realloc.
    static void from_string( const char* s, uintptr_t dst_offset );

    // Установить строку с интернированием (дедупликация, требование F3, задача #84).
    // string_table — смещение pjson_string_table в ПАП; 0 = без интернирования.
    void set_string_interned( const char* s, uintptr_t string_table_offset );

  private:
    // Создание pjson на стеке или как статической переменной запрещено.
    // Используйте fptr<pjson>::New() для создания в ПАП (Тр.11).
    pjson()  = default;
    ~pjson() = default;

    // Разрешаем доступ к приватному конструктору только для фабричных методов ПАМ.
    template <class U> friend class AddressManager;
    friend class PersistentAddressSpace;

    // internal helpers — объявлены, определены ниже
    void        _free_impl();
    static void _assign_key( pjson_kv_entry& entry, const char* s );
    uintptr_t   _obj_lower_bound( const char* s ) const;
    // Вспомогательные методы для прямой сериализации (требование F6, задача #84).
    void _serialize_to( std::string& out ) const;
    // Вспомогательные методы для совместимости с тестами, использующими nlohmann.
    nlohmann::json _to_nlohmann() const;
    static void    _from_nlohmann( const nlohmann::json& src, uintptr_t dst_offset );
};

// Проверяем размеры раскладки payload.
static_assert( sizeof( pstring ) == 2 * sizeof( void* ), "pstring должна занимать 2 * sizeof(void*) байт" );
static_assert( sizeof( pjson::payload_t::array_layout ) == 3 * sizeof( void* ),
               "pjson::payload_t::array_layout должна занимать 3 * sizeof(void*) байт" );
static_assert( sizeof( pjson::payload_t::object_layout ) == 3 * sizeof( void* ),
               "pjson::payload_t::object_layout должна занимать 3 * sizeof(void*) байт" );

// ---------------------------------------------------------------------------
// pjson_kv_entry — одна пара (строка-ключ, pjson-значение).
// Определяется ПОСЛЕ pjson, чтобы можно было встроить pjson как поле.
// Ключ хранится как pstring (length + chars в ПАП).
// Используется как тип элемента отсортированного pvector в объектах pjson.
// ---------------------------------------------------------------------------
struct pjson_kv_entry
{
    pstring key;   ///< Ключ (pstring: length + chars в ПАП)
    pjson   value; ///< Значение (pjson)
};

// Проверяем, что раскладка pvector совместима с object_layout (3 * sizeof(void*)).
// Используем pvector<int> (тривиально копируемый тип) вместо pvector<pjson_kv_entry>,
// так как pjson_kv_entry содержит pstring с приватным деструктором и не является
// тривиально копируемым — что запрещено статическим ассертом pvector<T>.
// Размер pvector<T> одинаков для всех T (size_ + capacity_ + fptr<T>).
static_assert( sizeof( pvector<int> ) == 3 * sizeof( void* ), "pvector<T> должна занимать 3 * sizeof(void*) байт" );

// ---------------------------------------------------------------------------
// Подключаем прямой сериализатор (F6), таблицу интернирования строк (F3)
// и пул узлов (F2). Подключаются ПОСЛЕ определений pjson и pjson_kv_entry,
// так как используют их (в том числе sizeof(pjson)).
// ---------------------------------------------------------------------------
#include "pjson_serializer.h"
#include "pjson_interning.h"
#include "pjson_node_pool.h"

// ---------------------------------------------------------------------------
// Определения методов pjson (inline) — после полного определения pjson_kv_entry
// ---------------------------------------------------------------------------

// Вспомогательный метод: освободить все ресурсы pjson рекурсивно.
inline void pjson::_free_impl()
{
    switch ( type )
    {
    case pjson_type::string:
        // Используем pstring::clear() для освобождения символьных данных.
        payload.string_val.clear();
        break;

    case pjson_type::array:
    {
        // Рекурсивно освобождаем каждый элемент массива.
        uintptr_t sz        = payload.array_val.size;
        uintptr_t data_addr = payload.array_val.data.addr();
        if ( data_addr != 0 )
        {
            for ( uintptr_t i = 0; i < sz; i++ )
            {
                pjson& elem = AddressManager<pjson>::GetArrayElement( data_addr, i );
                elem._free_impl();
            }
            // Освобождаем буфер массива через fptr::DeleteArray().
            payload.array_val.data.DeleteArray();
        }
        payload.array_val.size     = 0;
        payload.array_val.capacity = 0;
        break;
    }

    case pjson_type::object:
    {
        // Рекурсивно освобождаем пары ключ-значение.
        uintptr_t sz        = payload.object_val.size;
        uintptr_t data_addr = payload.object_val.data.addr();
        if ( data_addr != 0 )
        {
            for ( uintptr_t i = 0; i < sz; i++ )
            {
                pjson_kv_entry& pair = AddressManager<pjson_kv_entry>::GetArrayElement( data_addr, i );
                // Используем pstring::clear() для освобождения ключа.
                pair.key.clear();
                // Рекурсивно освобождаем значение.
                pair.value._free_impl();
            }
            // Освобождаем буфер массива пар.
            payload.object_val.data.DeleteArray();
        }
        payload.object_val.size     = 0;
        payload.object_val.capacity = 0;
        break;
    }

    default:
        break;
    }
    type = pjson_type::null;
    // Обнуляем весь payload (3 поля объектного/массивного layout).
    payload.object_val.size     = 0;
    payload.object_val.capacity = 0;
    payload.object_val.data.set_addr( 0 );
}

inline void pjson::free()
{
    _free_impl();
}

inline void pjson::set_null()
{
    _free_impl();
}

inline void pjson::set_bool( bool v )
{
    _free_impl();
    type                = pjson_type::boolean;
    payload.boolean_val = v ? 1u : 0u;
}

inline void pjson::set_int( int64_t v )
{
    _free_impl();
    type            = pjson_type::integer;
    payload.int_val = v;
}

inline void pjson::set_uint( uint64_t v )
{
    _free_impl();
    type             = pjson_type::uinteger;
    payload.uint_val = v;
}

inline void pjson::set_real( double v )
{
    _free_impl();
    type             = pjson_type::real;
    payload.real_val = v;
}

inline void pjson::set_string( const char* s )
{
    _free_impl();
    type = pjson_type::string;
    // Инициализируем pstring нулями перед использованием.
    payload.string_val.length = 0;
    payload.string_val.chars.set_addr( 0 );
    if ( s == nullptr || s[0] == '\0' )
        return;

    // Используем pstring::assign() для сохранения строки в ПАП.
    // pstring::assign() сам обрабатывает realloc-безопасность:
    // сохраняет смещение this (адрес pstring в ПАМ) до выделения памяти
    // и повторно разрешает через смещение после возможного realloc.
    payload.string_val.assign( s );
}

inline void pjson::set_array()
{
    _free_impl();
    type = pjson_type::array;
    // Инициализируем поля pvector-совместимой раскладки нулями.
    payload.array_val.size     = 0;
    payload.array_val.capacity = 0;
    payload.array_val.data.set_addr( 0 );
}

inline void pjson::set_object()
{
    _free_impl();
    type = pjson_type::object;
    // Инициализируем поля pvector-совместимой раскладки нулями.
    payload.object_val.size     = 0;
    payload.object_val.capacity = 0;
    payload.object_val.data.set_addr( 0 );
}

inline pjson& pjson::push_back()
{
    // Сохраняем смещение this ДО любого выделения памяти.
    // После realloc буфера ПАМ this может стать недействительным.
    auto&     pam         = PersistentAddressSpace::Get();
    uintptr_t self_offset = pam.PtrToOffset( this );

    uintptr_t old_size = payload.array_val.size;
    uintptr_t new_size = old_size + 1;
    uintptr_t cap      = payload.array_val.capacity;

    if ( new_size > cap )
    {
        // Рост: удваиваем ёмкость (pvector-стратегия).
        uintptr_t new_cap = ( cap == 0 ) ? 4 : cap * 2;
        if ( new_cap < new_size )
            new_cap = new_size;
        uintptr_t old_data_addr = payload.array_val.data.addr();

        fptr<pjson> new_arr;
        new_arr.NewArray( static_cast<unsigned>( new_cap ) ); // Может вызвать realloc!
        // Повторно разрешаем this после возможного realloc.
        pjson* self = pam.Resolve<pjson>( self_offset );

        // Инициализируем новые слоты нулями.
        for ( uintptr_t i = 0; i < new_cap; i++ )
        {
            pjson& e                      = new_arr[static_cast<unsigned>( i )];
            e.type                        = pjson_type::null;
            e.payload.object_val.size     = 0;
            e.payload.object_val.capacity = 0;
            e.payload.object_val.data.set_addr( 0 );
        }

        // Переносим существующие элементы (поверхностная копия — смещения ПАП валидны).
        for ( uintptr_t i = 0; i < old_size; i++ )
            new_arr[static_cast<unsigned>( i )] = AddressManager<pjson>::GetArrayElement( old_data_addr, i );

        if ( old_data_addr != 0 )
        {
            fptr<pjson> old_arr;
            old_arr.set_addr( old_data_addr );
            old_arr.DeleteArray();
        }

        // Повторно разрешаем self (DeleteArray не вызывает realloc, но для единообразия).
        self = pam.Resolve<pjson>( self_offset );
        self->payload.array_val.data.set_addr( new_arr.addr() );
        self->payload.array_val.capacity = new_cap;
        self->payload.array_val.size     = new_size;
        return AddressManager<pjson>::GetArrayElement( self->payload.array_val.data.addr(), old_size );
    }

    // Ёмкость достаточна: инициализируем следующий слот нулями.
    pjson& new_elem = AddressManager<pjson>::GetArrayElement( payload.array_val.data.addr(), old_size );
    new_elem.type   = pjson_type::null;
    new_elem.payload.object_val.size     = 0;
    new_elem.payload.object_val.capacity = 0;
    new_elem.payload.object_val.data.set_addr( 0 );

    payload.array_val.size = new_size;
    return new_elem;
}

inline pjson& pjson::operator[]( uintptr_t idx )
{
    return payload.array_val.data[static_cast<unsigned>( idx )];
}

inline const pjson& pjson::operator[]( uintptr_t idx ) const
{
    return payload.array_val.data[static_cast<unsigned>( idx )];
}

inline uintptr_t pjson::_obj_lower_bound( const char* s ) const
{
    uintptr_t sz = payload.object_val.size;
    uintptr_t lo = 0, hi = sz;
    while ( lo < hi )
    {
        uintptr_t mid = ( lo + hi ) / 2;
        // Используем pstring::c_str() для получения строки ключа.
        const pjson_kv_entry& pair =
            AddressManager<pjson_kv_entry>::GetArrayElement( payload.object_val.data.addr(), mid );
        const char* k = pair.key.c_str();
        if ( std::strcmp( k, s ) < 0 )
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

inline pjson* pjson::obj_find( const char* key )
{
    if ( type != pjson_type::object )
        return nullptr;
    if ( payload.object_val.data.addr() == 0 )
        return nullptr;
    uintptr_t idx = _obj_lower_bound( key );
    uintptr_t sz  = payload.object_val.size;
    if ( idx >= sz )
        return nullptr;
    pjson_kv_entry& pair = AddressManager<pjson_kv_entry>::GetArrayElement( payload.object_val.data.addr(), idx );
    // Используем pstring::c_str() для сравнения ключа.
    if ( std::strcmp( pair.key.c_str(), key ) != 0 )
        return nullptr;
    return &pair.value;
}

inline const pjson* pjson::obj_find( const char* key ) const
{
    if ( type != pjson_type::object )
        return nullptr;
    if ( payload.object_val.data.addr() == 0 )
        return nullptr;
    uintptr_t idx = _obj_lower_bound( key );
    uintptr_t sz  = payload.object_val.size;
    if ( idx >= sz )
        return nullptr;
    const pjson_kv_entry& pair = AddressManager<pjson_kv_entry>::GetArrayElement( payload.object_val.data.addr(), idx );
    // Используем pstring::c_str() для сравнения ключа.
    if ( std::strcmp( pair.key.c_str(), key ) != 0 )
        return nullptr;
    return &pair.value;
}

inline void pjson::_assign_key( pjson_kv_entry& entry, const char* s )
{
    // Используем pstring::assign() для установки ключа в ПАП.
    // Сначала очищаем предыдущее значение через pstring::clear().
    entry.key.clear();
    if ( s != nullptr && s[0] != '\0' )
    {
        // pstring::assign() сам обрабатывает realloc-безопасность.
        entry.key.assign( s );
    }
}

inline pjson& pjson::obj_insert( const char* key )
{
    // Сохраняем смещение this ДО любого выделения памяти.
    auto&     pam         = PersistentAddressSpace::Get();
    uintptr_t self_offset = pam.PtrToOffset( this );

    uintptr_t sz = payload.object_val.size;

    if ( payload.object_val.data.addr() == 0 )
    {
        // Первая запись: выделяем начальную ёмкость — 4 пары (pvector-стратегия).
        fptr<pjson_kv_entry> arr;
        arr.NewArray( 4 ); // Может вызвать realloc!
        // Повторно разрешаем this после возможного realloc.
        pjson* self = pam.Resolve<pjson>( self_offset );
        // Инициализируем слоты нулями.
        for ( unsigned i = 0; i < 4; i++ )
        {
            pjson_kv_entry& p = arr[i];
            // Инициализируем pstring нулями.
            p.key.length = 0;
            p.key.chars.set_addr( 0 );
            // Инициализируем pjson нулями.
            p.value.type                        = pjson_type::null;
            p.value.payload.object_val.size     = 0;
            p.value.payload.object_val.capacity = 0;
            p.value.payload.object_val.data.set_addr( 0 );
        }
        self->payload.object_val.data.set_addr( arr.addr() );
        self->payload.object_val.capacity = 4;
        sz                                = self->payload.object_val.size;
    }

    // Повторно разрешаем this (возможно, после realloc выше).
    pjson*    self = pam.Resolve<pjson>( self_offset );
    uintptr_t idx  = self->_obj_lower_bound( key );

    // Проверяем, существует ли ключ.
    if ( idx < sz )
    {
        pjson_kv_entry& pair =
            AddressManager<pjson_kv_entry>::GetArrayElement( self->payload.object_val.data.addr(), idx );
        // Используем pstring::c_str() для сравнения.
        if ( std::strcmp( pair.key.c_str(), key ) == 0 )
        {
            // Ключ найден — освобождаем старое значение и возвращаем слот.
            pair.value._free_impl();
            return pair.value;
        }
    }

    // Вставляем новую запись в позицию idx, сдвигая вправо.
    uintptr_t new_size = sz + 1;
    uintptr_t cap      = self->payload.object_val.capacity;
    if ( new_size > cap )
    {
        // Рост: удваиваем ёмкость (pvector-стратегия).
        uintptr_t new_cap = ( cap == 0 ) ? 4 : cap * 2;
        if ( new_cap < new_size )
            new_cap = new_size;
        uintptr_t old_data_addr = self->payload.object_val.data.addr();

        fptr<pjson_kv_entry> new_arr;
        new_arr.NewArray( static_cast<unsigned>( new_cap ) ); // Может вызвать realloc!
        // Повторно разрешаем this после возможного realloc.
        self = pam.Resolve<pjson>( self_offset );

        // Инициализируем новые слоты нулями.
        for ( uintptr_t i = 0; i < new_cap; i++ )
        {
            pjson_kv_entry& p = new_arr[static_cast<unsigned>( i )];
            p.key.length      = 0;
            p.key.chars.set_addr( 0 );
            p.value.type                        = pjson_type::null;
            p.value.payload.object_val.size     = 0;
            p.value.payload.object_val.capacity = 0;
            p.value.payload.object_val.data.set_addr( 0 );
        }
        // Копируем существующие записи.
        for ( uintptr_t i = 0; i < sz; i++ )
        {
            new_arr[static_cast<unsigned>( i )] = AddressManager<pjson_kv_entry>::GetArrayElement( old_data_addr, i );
        }
        fptr<pjson_kv_entry> old_arr;
        old_arr.set_addr( old_data_addr );
        old_arr.DeleteArray();
        self->payload.object_val.data.set_addr( new_arr.addr() );
        self->payload.object_val.capacity = new_cap;
    }

    // Сдвигаем элементы вправо, освобождая место в позиции idx.
    for ( uintptr_t i = sz; i > idx; i-- )
    {
        AddressManager<pjson_kv_entry>::GetArrayElement( self->payload.object_val.data.addr(), i ) =
            AddressManager<pjson_kv_entry>::GetArrayElement( self->payload.object_val.data.addr(), i - 1 );
    }

    // Записываем новую пару в позицию idx.
    {
        pjson_kv_entry& new_pair =
            AddressManager<pjson_kv_entry>::GetArrayElement( self->payload.object_val.data.addr(), idx );
        new_pair.value.type                        = pjson_type::null;
        new_pair.value.payload.object_val.size     = 0;
        new_pair.value.payload.object_val.capacity = 0;
        new_pair.value.payload.object_val.data.set_addr( 0 );
        // Обнуляем pstring ДО вызова _assign_key, чтобы не освободить
        // chars, который теперь принадлежит сдвинутому соседу [idx+1].
        new_pair.key.length = 0;
        new_pair.key.chars.set_addr( 0 );
    }

    // _assign_key использует pstring::assign(), которая может вызвать realloc.
    {
        uintptr_t       data_addr_before = self->payload.object_val.data.addr();
        pjson_kv_entry& new_pair_ref     = AddressManager<pjson_kv_entry>::GetArrayElement( data_addr_before, idx );
        _assign_key( new_pair_ref, key );
    }

    // После _assign_key (возможный realloc через pstring::assign): re-resolve self.
    self                          = pam.Resolve<pjson>( self_offset );
    self->payload.object_val.size = new_size;
    // Re-resolve new_pair для возврата.
    return AddressManager<pjson_kv_entry>::GetArrayElement( self->payload.object_val.data.addr(), idx ).value;
}

inline bool pjson::obj_erase( const char* key )
{
    if ( type != pjson_type::object )
        return false;
    if ( payload.object_val.data.addr() == 0 )
        return false;
    uintptr_t sz  = payload.object_val.size;
    uintptr_t idx = _obj_lower_bound( key );
    if ( idx >= sz )
        return false;
    pjson_kv_entry& pair = AddressManager<pjson_kv_entry>::GetArrayElement( payload.object_val.data.addr(), idx );
    // Используем pstring::c_str() для сравнения ключа.
    if ( std::strcmp( pair.key.c_str(), key ) != 0 )
        return false;

    // Используем pstring::clear() для освобождения ключа.
    pair.key.clear();
    // Рекурсивно освобождаем значение.
    pair.value._free_impl();

    // Сдвигаем оставшиеся элементы влево.
    for ( uintptr_t i = idx; i + 1 < sz; i++ )
    {
        AddressManager<pjson_kv_entry>::GetArrayElement( payload.object_val.data.addr(), i ) =
            AddressManager<pjson_kv_entry>::GetArrayElement( payload.object_val.data.addr(), i + 1 );
    }
    payload.object_val.size--;
    return true;
}

// ---------------------------------------------------------------------------
// Реализация методов сериализации (задача #84: F6 — прямая сериализация)
// ---------------------------------------------------------------------------

// Прямая рекурсивная сериализация pjson в std::string без nlohmann::json.
// Заменяет _to_nlohmann().dump() более эффективной реализацией.
inline void pjson::_serialize_to( std::string& out ) const
{
    using namespace pjson_serial_detail;
    switch ( type )
    {
    case pjson_type::null:
        out += "null";
        break;
    case pjson_type::boolean:
        out += ( get_bool() ? "true" : "false" );
        break;
    case pjson_type::integer:
        append_int64( out, get_int() );
        break;
    case pjson_type::uinteger:
        append_uint64( out, get_uint() );
        break;
    case pjson_type::real:
        append_double( out, get_real() );
        break;
    case pjson_type::string:
        append_json_string( out, get_string() );
        break;
    case pjson_type::array:
    {
        out += '[';
        uintptr_t sz        = payload.array_val.size;
        uintptr_t data_addr = payload.array_val.data.addr();
        for ( uintptr_t i = 0; i < sz; i++ )
        {
            if ( i > 0 )
                out += ',';
            const pjson& elem = AddressManager<pjson>::GetArrayElement( data_addr, i );
            elem._serialize_to( out );
        }
        out += ']';
        break;
    }
    case pjson_type::object:
    {
        out += '{';
        uintptr_t sz        = payload.object_val.size;
        uintptr_t data_addr = payload.object_val.data.addr();
        for ( uintptr_t i = 0; i < sz; i++ )
        {
            if ( i > 0 )
                out += ',';
            const pjson_kv_entry& pair = AddressManager<pjson_kv_entry>::GetArrayElement( data_addr, i );
            append_json_string( out, pair.key.c_str() );
            out += ':';
            pair.value._serialize_to( out );
        }
        out += '}';
        break;
    }
    default:
        out += "null";
        break;
    }
}

// Сериализовать pjson в строку JSON (прямая реализация без nlohmann, F6).
inline std::string pjson::to_string() const
{
    std::string out;
    _serialize_to( out );
    return out;
}

// Вспомогательный метод для совместимости с тестами (использует nlohmann).
inline nlohmann::json pjson::_to_nlohmann() const
{
    switch ( type )
    {
    case pjson_type::null:
        return nlohmann::json( nullptr );
    case pjson_type::boolean:
        return nlohmann::json( get_bool() );
    case pjson_type::integer:
        return nlohmann::json( get_int() );
    case pjson_type::uinteger:
        return nlohmann::json( get_uint() );
    case pjson_type::real:
        return nlohmann::json( get_real() );
    case pjson_type::string:
        return nlohmann::json( std::string( get_string() ) );
    case pjson_type::array:
    {
        nlohmann::json arr       = nlohmann::json::array();
        uintptr_t      sz        = payload.array_val.size;
        uintptr_t      data_addr = payload.array_val.data.addr();
        for ( uintptr_t i = 0; i < sz; i++ )
        {
            const pjson& elem = AddressManager<pjson>::GetArrayElement( data_addr, i );
            arr.push_back( elem._to_nlohmann() );
        }
        return arr;
    }
    case pjson_type::object:
    {
        nlohmann::json obj       = nlohmann::json::object();
        uintptr_t      sz        = payload.object_val.size;
        uintptr_t      data_addr = payload.object_val.data.addr();
        for ( uintptr_t i = 0; i < sz; i++ )
        {
            const pjson_kv_entry& pair = AddressManager<pjson_kv_entry>::GetArrayElement( data_addr, i );
            const char*           key  = pair.key.c_str();
            obj[key]                   = pair.value._to_nlohmann();
        }
        return obj;
    }
    default:
        return nlohmann::json( nullptr );
    }
}

// Десериализовать строку JSON в pjson по смещению dst_offset в ПАМ.
// Прямая реализация без nlohmann::json (требование F6, задача #84).
inline void pjson::from_string( const char* s, uintptr_t dst_offset )
{
    if ( s == nullptr || dst_offset == 0 )
        return;
    uintptr_t len = static_cast<uintptr_t>( std::strlen( s ) );
    pjson_parser_detail::ParseState st{ s, s + len };
    pjson_parser_detail::skip_ws( st );
    if ( st.pos >= st.end )
        return; // пустая строка

    bool ok = pjson_parser_detail::parse_value( st, dst_offset );
    if ( !ok )
    {
        // Некорректный JSON: сбрасываем в null.
        PersistentAddressSpace::Get().Resolve<pjson>( dst_offset )->set_null();
    }
}

// Вспомогательный метод для совместимости с тестами (использует nlohmann).
inline void pjson::_from_nlohmann( const nlohmann::json& src, uintptr_t dst_offset )
{
    auto& pam = PersistentAddressSpace::Get();
    pjson* dst = pam.Resolve<pjson>( dst_offset );
    if ( dst == nullptr )
        return;

    switch ( src.type() )
    {
    case nlohmann::json::value_t::null:
        pam.Resolve<pjson>( dst_offset )->set_null();
        break;
    case nlohmann::json::value_t::boolean:
        pam.Resolve<pjson>( dst_offset )->set_bool( src.get<bool>() );
        break;
    case nlohmann::json::value_t::number_integer:
        pam.Resolve<pjson>( dst_offset )->set_int( src.get<int64_t>() );
        break;
    case nlohmann::json::value_t::number_unsigned:
        pam.Resolve<pjson>( dst_offset )->set_uint( src.get<uint64_t>() );
        break;
    case nlohmann::json::value_t::number_float:
        pam.Resolve<pjson>( dst_offset )->set_real( src.get<double>() );
        break;
    case nlohmann::json::value_t::string:
        pam.Resolve<pjson>( dst_offset )->set_string( src.get<std::string>().c_str() );
        break;
    case nlohmann::json::value_t::array:
    {
        pam.Resolve<pjson>( dst_offset )->set_array();
        for ( const auto& elem : src )
        {
            pjson* d              = pam.Resolve<pjson>( dst_offset );
            pjson& new_elem       = d->push_back();
            uintptr_t new_elem_off = pam.PtrToOffset( &new_elem );
            _from_nlohmann( elem, new_elem_off );
        }
        break;
    }
    case nlohmann::json::value_t::object:
    {
        pam.Resolve<pjson>( dst_offset )->set_object();
        for ( const auto& [key, val] : src.items() )
        {
            pjson* d           = pam.Resolve<pjson>( dst_offset );
            pjson& new_val     = d->obj_insert( key.c_str() );
            uintptr_t val_off  = pam.PtrToOffset( &new_val );
            _from_nlohmann( val, val_off );
        }
        break;
    }
    default:
        pam.Resolve<pjson>( dst_offset )->set_null();
        break;
    }
}

// ---------------------------------------------------------------------------
// Реализация set_string_interned (требование F3, задача #84)
// ---------------------------------------------------------------------------

// Установить строковое значение с интернированием.
// string_table_offset — смещение pjson_string_table в ПАП (или 0 = без интернирования).
// При интернировании строка хранится в таблице один раз и повторно используется.
// Безопасна при realloc: сохраняет self_offset ДО вызова intern().
inline void pjson::set_string_interned( const char* s, uintptr_t string_table_offset )
{
    if ( string_table_offset == 0 )
    {
        // Таблица не задана — используем обычную аллокацию.
        set_string( s );
        return;
    }
    if ( s == nullptr )
        s = "";

    auto& pam = PersistentAddressSpace::Get();
    // Сохраняем смещение this ДО любых операций выделения памяти.
    uintptr_t self_offset = pam.PtrToOffset( this );

    // Освобождаем предыдущее значение.
    _free_impl();
    // Повторно разрешаем self (realloc возможен внутри _free_impl у string/array/object).
    pjson* self = pam.Resolve<pjson>( self_offset );
    self->type                         = pjson_type::string;
    self->payload.string_val.length    = 0;
    self->payload.string_val.chars.set_addr( 0 );

    // Интернируем строку — intern() может вызвать realloc через pstring::assign.
    pjson_string_table* tbl     = pam.Resolve<pjson_string_table>( string_table_offset );
    uintptr_t           str_off = tbl->intern( s );

    // Переразрешаем self после возможного realloc внутри intern().
    self = pam.Resolve<pjson>( self_offset );

    // Получаем интернированную pstring и копируем её поля в payload.
    // Важно: payload.string_val.chars будет указывать на те же char-данные,
    // что и интернированная pstring. Это допустимо, поскольку интернированные
    // строки никогда не освобождаются (глобальный пул).
    pstring* ps = pam.Resolve<pstring>( str_off );
    if ( ps != nullptr )
    {
        self->payload.string_val.length = ps->length;
        self->payload.string_val.chars.set_addr( ps->chars.addr() );
    }
}
