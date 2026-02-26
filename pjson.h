#pragma once
#include "pstring.h"
#include <cstring>
#include <cstdint>
#include <type_traits>

// pjson — персистная дискриминантная объединяющая структура (персистный аналог nlohmann::json).
//
// Объекты pjson могут находиться ТОЛЬКО внутри ПАП.
// Для работы с pjson из обычного кода используйте fptr<pjson>.
//
// Требования:
//   - Конструктор и деструктор приватные; создание только через ПАМ (Тр.2, Тр.11).
//   - При загрузке образа ПАП конструкторы не вызываются (Тр.10).
//
// Дизайн: Пользовательская персистная дискриминантная структура,
// построенная напрямую на fptr<T>, с ключами в виде C-строк в ПАП.
//
// Phase 3: поля chars_slot, data_slot, pairs_slot имеют тип uintptr_t.

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
// ---------------------------------------------------------------------------
struct pjson
{
    pjson_type type;    // 4 байта — дискриминант

    union payload_t
    {
        uint32_t    boolean_val;  // 0 = false, ненулевое = true
        int64_t     int_val;
        uint64_t    uint_val;
        double      real_val;

        // Для строки: длина строки и смещение в ПАП для массива символов.
        // Phase 3: оба поля имеют тип uintptr_t для корректного хранения смещений ПАМ.
        struct { uintptr_t length; uintptr_t chars_slot; } string_val;

        // Для массива: размер и смещение в ПАП для массива pjson.
        struct { uintptr_t size; uintptr_t data_slot; } array_val;

        // Для объекта: размер и смещение в ПАП для массива pjson_kv_entry.
        struct { uintptr_t size; uintptr_t pairs_slot; } object_val;
    } payload;

    // ----- Запросы типа ---------------------------------------------------
    pjson_type type_tag() const { return type; }
    bool is_null()     const { return type == pjson_type::null; }
    bool is_boolean()  const { return type == pjson_type::boolean; }
    bool is_integer()  const { return type == pjson_type::integer; }
    bool is_uinteger() const { return type == pjson_type::uinteger; }
    bool is_real()     const { return type == pjson_type::real; }
    bool is_number()   const { return is_integer() || is_uinteger() || is_real(); }
    bool is_string()   const { return type == pjson_type::string; }
    bool is_array()    const { return type == pjson_type::array; }
    bool is_object()   const { return type == pjson_type::object; }

    uintptr_t size() const
    {
        if( type == pjson_type::array )
            return payload.array_val.size;
        if( type == pjson_type::object )
            return payload.object_val.size;
        if( type == pjson_type::string )
            return payload.string_val.length;
        return 0;
    }

    bool empty() const { return size() == 0; }

    bool get_bool() const { return payload.boolean_val != 0; }
    int64_t  get_int()  const { return payload.int_val; }
    uint64_t get_uint() const { return payload.uint_val; }
    double   get_real() const { return payload.real_val; }

    const char* get_string() const
    {
        if( payload.string_val.chars_slot == 0 ) return "";
        return &AddressManager<char>::GetArrayElement(
                    payload.string_val.chars_slot, 0);
    }

    // Методы, требующие полного определения pjson_kv_entry,
    // объявлены здесь, а определены ПОСЛЕ pjson_kv_entry.
    void free();
    void set_null();
    void set_bool(bool v);
    void set_int(int64_t v);
    void set_uint(uint64_t v);
    void set_real(double v);
    void set_string(const char* s);
    void set_array();
    void set_object();

    pjson& push_back();

    pjson& operator[](uintptr_t idx);
    const pjson& operator[](uintptr_t idx) const;

    pjson* obj_find(const char* key);
    const pjson* obj_find(const char* key) const;
    pjson& obj_insert(const char* key);
    bool obj_erase(const char* key);

private:
    // Создание pjson на стеке или как статической переменной запрещено.
    // Используйте fptr<pjson>::New() для создания в ПАП (Тр.11).
    pjson() = default;
    ~pjson() = default;

    // Разрешаем доступ к приватному конструктору только для фабричных методов ПАМ.
    template<class U> friend class AddressManager;
    friend class PersistentAddressSpace;

    // internal helpers — объявлены, определены ниже
    void _free_impl();
    static void _assign_key( pjson_kv_entry& entry, const char* s );
    uintptr_t _obj_lower_bound( const char* s ) const;
};

// Phase 3: проверяем размеры полей payload.
static_assert(sizeof(pjson::payload_t::string_val.chars_slot) == sizeof(void*),
              "pjson::payload.string_val.chars_slot должен иметь размер void* (Phase 3)");
static_assert(sizeof(pjson::payload_t::array_val.data_slot) == sizeof(void*),
              "pjson::payload.array_val.data_slot должен иметь размер void* (Phase 3)");
static_assert(sizeof(pjson::payload_t::object_val.pairs_slot) == sizeof(void*),
              "pjson::payload.object_val.pairs_slot должен иметь размер void* (Phase 3)");

// ---------------------------------------------------------------------------
// pjson_kv_entry — одна пара (строка-ключ, pjson-значение).
// Определяется ПОСЛЕ pjson, чтобы можно было встроить pjson как поле.
// Используется как тип элемента отсортированного массива в объектах pjson.
// ---------------------------------------------------------------------------
struct pjson_kv_entry
{
    uintptr_t  key_length;  ///< Длина строки-ключа (без нулевого терминатора)
    fptr<char> key_chars;   ///< Смещение в ПАП для массива символов ключа; 0 = пусто
    pjson      value;       ///< Значение (pjson)
};

// ---------------------------------------------------------------------------
// Определения методов pjson (inline) — после полного определения pjson_kv_entry
// ---------------------------------------------------------------------------

inline void pjson::_free_impl()
{
    switch( type )
    {
    case pjson_type::string:
        if( payload.string_val.chars_slot != 0 )
        {
            fptr<char> tmp;
            tmp.set_addr( payload.string_val.chars_slot );
            tmp.DeleteArray();
            payload.string_val.chars_slot = 0;
            payload.string_val.length = 0;
        }
        break;

    case pjson_type::array:
        if( payload.array_val.data_slot != 0 )
        {
            // Рекурсивно освобождаем каждый элемент.
            uintptr_t sz = payload.array_val.size;
            for( uintptr_t i = 0; i < sz; i++ )
            {
                pjson& elem =
                    AddressManager<pjson>::GetArrayElement(
                        payload.array_val.data_slot, i );
                elem._free_impl();
            }
            fptr<pjson> tmp;
            tmp.set_addr( payload.array_val.data_slot );
            tmp.DeleteArray();
            payload.array_val.data_slot = 0;
            payload.array_val.size = 0;
        }
        break;

    case pjson_type::object:
        if( payload.object_val.pairs_slot != 0 )
        {
            uintptr_t sz = payload.object_val.size;
            for( uintptr_t i = 0; i < sz; i++ )
            {
                pjson_kv_entry& pair =
                    AddressManager<pjson_kv_entry>::GetArrayElement(
                        payload.object_val.pairs_slot, i );
                // Освобождаем строку-ключ.
                if( pair.key_chars.addr() != 0 )
                    pair.key_chars.DeleteArray();
                // Рекурсивно освобождаем значение.
                pair.value._free_impl();
            }
            fptr<pjson_kv_entry> tmp;
            tmp.set_addr( payload.object_val.pairs_slot );
            tmp.DeleteArray();
            payload.object_val.pairs_slot = 0;
            payload.object_val.size = 0;
        }
        break;

    default:
        break;
    }
    type = pjson_type::null;
    payload.uint_val = 0;
}

inline void pjson::free() { _free_impl(); }

inline void pjson::set_null() { _free_impl(); }

inline void pjson::set_bool(bool v)
{
    _free_impl();
    type = pjson_type::boolean;
    payload.boolean_val = v ? 1u : 0u;
}

inline void pjson::set_int(int64_t v)
{
    _free_impl();
    type = pjson_type::integer;
    payload.int_val = v;
}

inline void pjson::set_uint(uint64_t v)
{
    _free_impl();
    type = pjson_type::uinteger;
    payload.uint_val = v;
}

inline void pjson::set_real(double v)
{
    _free_impl();
    type = pjson_type::real;
    payload.real_val = v;
}

inline void pjson::set_string(const char* s)
{
    _free_impl();
    type = pjson_type::string;
    payload.string_val.chars_slot = 0;
    payload.string_val.length = 0;
    if( s == nullptr || s[0] == '\0' ) return;

    uintptr_t len = static_cast<uintptr_t>(std::strlen(s));
    // Сохраняем смещение `this` перед выделением памяти (возможный realloc).
    auto& pam = PersistentAddressSpace::Get();
    uintptr_t self_offset = pam.PtrToOffset(this);
    fptr<char> chars;
    chars.NewArray(static_cast<unsigned>(len + 1));  // Может вызвать realloc!
    for( uintptr_t i = 0; i <= len; i++ )
        chars[static_cast<unsigned>(i)] = s[i];
    // Повторно разрешаем `this` после возможного realloc.
    pjson* self = pam.Resolve<pjson>(self_offset);
    self->payload.string_val.length = len;
    self->payload.string_val.chars_slot = chars.addr();
}

inline void pjson::set_array()
{
    _free_impl();
    type = pjson_type::array;
    payload.array_val.size = 0;
    payload.array_val.data_slot = 0;
}

inline void pjson::set_object()
{
    _free_impl();
    type = pjson_type::object;
    payload.object_val.size = 0;
    payload.object_val.pairs_slot = 0;
}

inline pjson& pjson::push_back()
{
    // Сохраняем смещение `this` ДО любого выделения памяти.
    // После realloc буфера ПАМ `this` может стать недействительным указателем.
    // После выделения повторно разрешаем указатель через смещение.
    auto& pam = PersistentAddressSpace::Get();
    uintptr_t self_offset = pam.PtrToOffset(this);

    uintptr_t old_size = payload.array_val.size;
    uintptr_t new_size = old_size + 1;

    if( payload.array_val.data_slot == 0 )
    {
        // Первый элемент: выделяем начальную ёмкость.
        fptr<pjson> arr;
        arr.NewArray(4);  // Может вызвать realloc!
        // Повторно разрешаем `this` после возможного realloc.
        pjson* self = pam.Resolve<pjson>(self_offset);
        // Инициализируем все слоты нулями.
        for( unsigned i = 0; i < 4; i++ )
        {
            pjson& e = arr[i];
            e.type = pjson_type::null;
            e.payload.uint_val = 0;
        }
        self->payload.array_val.data_slot = arr.addr();
        self->payload.array_val.size = new_size;
        return AddressManager<pjson>::GetArrayElement(
                   self->payload.array_val.data_slot, old_size);
    }
    else if( new_size > AddressManager<pjson>::GetCount(
                            payload.array_val.data_slot ) )
    {
        // Рост: удваиваем ёмкость.
        uintptr_t old_cap = AddressManager<pjson>::GetCount(
                                payload.array_val.data_slot);
        uintptr_t new_cap = old_cap * 2;
        if( new_cap < new_size ) new_cap = new_size;
        uintptr_t old_data_slot = payload.array_val.data_slot;

        fptr<pjson> new_arr;
        new_arr.NewArray(static_cast<unsigned>(new_cap));  // Может вызвать realloc!
        // Повторно разрешаем `this` после возможного realloc.
        pjson* self = pam.Resolve<pjson>(self_offset);

        for( uintptr_t i = 0; i < new_cap; i++ )
        {
            pjson& e = new_arr[static_cast<unsigned>(i)];
            e.type = pjson_type::null;
            e.payload.uint_val = 0;
        }
        // Переносим существующие элементы (поверхностная копия — только примитивы;
        // вложенные объекты сохраняют свои смещения в ПАП).
        for( uintptr_t i = 0; i < old_size; i++ )
            new_arr[static_cast<unsigned>(i)] =
                AddressManager<pjson>::GetArrayElement(old_data_slot, i);

        fptr<pjson> old_arr;
        old_arr.set_addr(old_data_slot);
        old_arr.DeleteArray();
        self->payload.array_val.data_slot = new_arr.addr();
        self->payload.array_val.size = new_size;
        return AddressManager<pjson>::GetArrayElement(
                   self->payload.array_val.data_slot, old_size);
    }

    payload.array_val.size = new_size;
    return AddressManager<pjson>::GetArrayElement(
               payload.array_val.data_slot, old_size);
}

inline pjson& pjson::operator[](uintptr_t idx)
{
    return AddressManager<pjson>::GetArrayElement(
               payload.array_val.data_slot, idx);
}

inline const pjson& pjson::operator[](uintptr_t idx) const
{
    return AddressManager<pjson>::GetArrayElement(
               payload.array_val.data_slot, idx);
}

inline uintptr_t pjson::_obj_lower_bound( const char* s ) const
{
    uintptr_t sz = payload.object_val.size;
    uintptr_t lo = 0, hi = sz;
    while( lo < hi )
    {
        uintptr_t mid = (lo + hi) / 2;
        pjson_kv_entry& pair =
            AddressManager<pjson_kv_entry>::GetArrayElement(
                payload.object_val.pairs_slot, mid );
        const char* k = (pair.key_chars.addr() != 0)
                        ? &pair.key_chars[0]
                        : "";
        if( std::strcmp(k, s) < 0 )
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

inline pjson* pjson::obj_find(const char* key)
{
    if( type != pjson_type::object ) return nullptr;
    if( payload.object_val.pairs_slot == 0 ) return nullptr;
    uintptr_t idx = _obj_lower_bound(key);
    uintptr_t sz  = payload.object_val.size;
    if( idx >= sz ) return nullptr;
    pjson_kv_entry& pair =
        AddressManager<pjson_kv_entry>::GetArrayElement(
            payload.object_val.pairs_slot, idx);
    const char* k = (pair.key_chars.addr() != 0) ? &pair.key_chars[0] : "";
    if( std::strcmp(k, key) != 0 ) return nullptr;
    return &pair.value;
}

inline const pjson* pjson::obj_find(const char* key) const
{
    if( type != pjson_type::object ) return nullptr;
    if( payload.object_val.pairs_slot == 0 ) return nullptr;
    uintptr_t idx = _obj_lower_bound(key);
    uintptr_t sz  = payload.object_val.size;
    if( idx >= sz ) return nullptr;
    const pjson_kv_entry& pair =
        AddressManager<pjson_kv_entry>::GetArrayElement(
            payload.object_val.pairs_slot, idx);
    const char* k = (pair.key_chars.addr() != 0) ? &pair.key_chars[0] : "";
    if( std::strcmp(k, key) != 0 ) return nullptr;
    return &pair.value;
}

inline void pjson::_assign_key( pjson_kv_entry& entry, const char* s )
{
    if( entry.key_chars.addr() != 0 )
        entry.key_chars.DeleteArray();
    if( s == nullptr || s[0] == '\0' )
    {
        entry.key_length = 0;
        return;
    }
    uintptr_t len = static_cast<uintptr_t>(std::strlen(s));
    // Сохраняем смещение `entry` перед выделением памяти (возможный realloc).
    auto& pam = PersistentAddressSpace::Get();
    uintptr_t entry_offset = pam.PtrToOffset(&entry);
    // NewArray может вызвать realloc, делая `entry` недействительным.
    fptr<char> chars;
    chars.NewArray(static_cast<unsigned>(len + 1));
    for( uintptr_t i = 0; i <= len; i++ )
        chars[static_cast<unsigned>(i)] = s[i];
    // Повторно разрешаем `entry` после возможного realloc.
    pjson_kv_entry* ep = pam.Resolve<pjson_kv_entry>(entry_offset);
    ep->key_length = len;
    ep->key_chars.set_addr(chars.addr());
}

inline pjson& pjson::obj_insert(const char* key)
{
    // Сохраняем смещение `this` ДО любого выделения памяти.
    // После realloc буфера ПАМ `this` может стать недействительным указателем.
    auto& pam = PersistentAddressSpace::Get();
    uintptr_t self_offset = pam.PtrToOffset(this);

    uintptr_t sz = payload.object_val.size;

    if( payload.object_val.pairs_slot == 0 )
    {
        // Первая запись: выделяем начальную ёмкость — 4 пары.
        fptr<pjson_kv_entry> arr;
        arr.NewArray(4);  // Может вызвать realloc!
        // Повторно разрешаем `this` после возможного realloc.
        pjson* self = pam.Resolve<pjson>(self_offset);
        for( unsigned i = 0; i < 4; i++ )
        {
            pjson_kv_entry& p = arr[i];
            p.key_length = 0;
            p.key_chars.set_addr(0);
            p.value.type = pjson_type::null;
            p.value.payload.uint_val = 0;
        }
        self->payload.object_val.pairs_slot = arr.addr();
        // Обновляем локальные переменные из свежего self.
        sz = self->payload.object_val.size;
    }

    // Повторно разрешаем `this` (возможно, после realloc выше).
    pjson* self = pam.Resolve<pjson>(self_offset);
    uintptr_t idx = self->_obj_lower_bound(key);

    // Проверяем, существует ли ключ.
    if( idx < sz )
    {
        pjson_kv_entry& pair =
            AddressManager<pjson_kv_entry>::GetArrayElement(
                self->payload.object_val.pairs_slot, idx);
        const char* k = (pair.key_chars.addr() != 0) ? &pair.key_chars[0] : "";
        if( std::strcmp(k, key) == 0 )
        {
            // Ключ найден — освобождаем старое значение и возвращаем слот.
            pair.value._free_impl();
            return pair.value;
        }
    }

    // Вставляем новую запись в позицию idx, сдвигая вправо.
    uintptr_t new_size = sz + 1;
    uintptr_t cap = AddressManager<pjson_kv_entry>::GetCount(
                        self->payload.object_val.pairs_slot);
    if( new_size > cap )
    {
        uintptr_t new_cap = cap * 2;
        if( new_cap < new_size ) new_cap = new_size;
        uintptr_t old_pairs_slot = self->payload.object_val.pairs_slot;
        fptr<pjson_kv_entry> new_arr;
        new_arr.NewArray(static_cast<unsigned>(new_cap));  // Может вызвать realloc!
        // Повторно разрешаем `this` после возможного realloc.
        self = pam.Resolve<pjson>(self_offset);
        for( uintptr_t i = 0; i < new_cap; i++ )
        {
            pjson_kv_entry& p = new_arr[static_cast<unsigned>(i)];
            p.key_length = 0;
            p.key_chars.set_addr(0);
            p.value.type = pjson_type::null;
            p.value.payload.uint_val = 0;
        }
        for( uintptr_t i = 0; i < sz; i++ )
            new_arr[static_cast<unsigned>(i)] =
                AddressManager<pjson_kv_entry>::GetArrayElement(
                    old_pairs_slot, i);
        fptr<pjson_kv_entry> old_arr;
        old_arr.set_addr(old_pairs_slot);
        old_arr.DeleteArray();
        self->payload.object_val.pairs_slot = new_arr.addr();
    }

    // Сдвигаем элементы вправо, освобождая место в позиции idx.
    for( uintptr_t i = sz; i > idx; i-- )
    {
        AddressManager<pjson_kv_entry>::GetArrayElement(
            self->payload.object_val.pairs_slot, i) =
        AddressManager<pjson_kv_entry>::GetArrayElement(
            self->payload.object_val.pairs_slot, i - 1);
    }

    // Записываем новую пару в позицию idx.
    {
        pjson_kv_entry& new_pair =
            AddressManager<pjson_kv_entry>::GetArrayElement(
                self->payload.object_val.pairs_slot, idx);
        new_pair.value.type = pjson_type::null;
        new_pair.value.payload.uint_val = 0;
        // Обнуляем слот ключа ДО вызова _assign_key, чтобы не освободить
        // chars_slot, который теперь принадлежит сдвинутому соседу [idx+1].
        new_pair.key_chars.set_addr(0);
        new_pair.key_length = 0;
    }
    // _assign_key может вызвать realloc: передаём ссылку, которая обновится внутри.
    // После _assign_key re-resolve self и new_pair.
    {
        uintptr_t pairs_slot_before = self->payload.object_val.pairs_slot;
        pjson_kv_entry& new_pair_ref =
            AddressManager<pjson_kv_entry>::GetArrayElement(pairs_slot_before, idx);
        _assign_key(new_pair_ref, key);
    }

    // После _assign_key (возможный realloc): re-resolve self.
    self = pam.Resolve<pjson>(self_offset);
    self->payload.object_val.size = new_size;
    // Re-resolve new_pair для возврата.
    return AddressManager<pjson_kv_entry>::GetArrayElement(
               self->payload.object_val.pairs_slot, idx).value;
}

inline bool pjson::obj_erase(const char* key)
{
    if( type != pjson_type::object ) return false;
    if( payload.object_val.pairs_slot == 0 ) return false;
    uintptr_t sz  = payload.object_val.size;
    uintptr_t idx = _obj_lower_bound(key);
    if( idx >= sz ) return false;
    pjson_kv_entry& pair =
        AddressManager<pjson_kv_entry>::GetArrayElement(
            payload.object_val.pairs_slot, idx);
    const char* k = (pair.key_chars.addr() != 0) ? &pair.key_chars[0] : "";
    if( std::strcmp(k, key) != 0 ) return false;

    // Освобождаем ключ-строку и значение.
    if( pair.key_chars.addr() != 0 )
        pair.key_chars.DeleteArray();
    pair.value._free_impl();

    // Сдвигаем оставшиеся элементы влево.
    for( uintptr_t i = idx; i + 1 < sz; i++ )
    {
        AddressManager<pjson_kv_entry>::GetArrayElement(
            payload.object_val.pairs_slot, i) =
        AddressManager<pjson_kv_entry>::GetArrayElement(
            payload.object_val.pairs_slot, i + 1);
    }
    payload.object_val.size--;
    return true;
}
