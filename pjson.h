#pragma once
#include "pstring.h"
#include "pvector.h"
#include "pmap.h"
#include <cstring>
#include <cstdint>
#include <type_traits>

// pjson — персистная дискриминантная объединяющая структура (персистный аналог nlohmann::json).
//
// Дизайн: Вариант Б из DEVELOPMENT_PLAN.md — пользовательская персистная
// дискриминантная структура, построенная напрямую на fptr<T> / pstring / pvector / pmap.
//
// Почему вариант Б, а не вариант А (инстанцирование nlohmann::basic_json)?
//   nlohmann::basic_json внутренне разыменовывает raw-указатели и предполагает
//   выделение памяти из кучи. AddressManager использует смещения в ПАП, а не
//   raw C++ указатели, поэтому вариант А потребовал бы нетривиального перевода
//   указателей при каждом обращении. Вариант Б даёт полный контроль и
//   согласуется с остальной персистной инфраструктурой.
//
// Структура:
//   pjson_data тривиально копируем и может быть сохранён через persist<pjson_data>
//   или встроен в pvector<pjson_data> / pmap<K, pjson_data>.
//
//   Дискриминант (pjson_type) занимает 4 байта. Payload — объединение
//   примитивных значений и смещений в ПАП для типов с выделенной памятью.
//
// Типы значений:
//   null    — пустой payload.
//   boolean — bool (хранится как uint32_t для выравнивания).
//   integer — int64_t.
//   uinteger— uint64_t.
//   real    — double.
//   string  — fptr<char> (указывает на массив символов в AddressManager<char>).
//   array   — fptr<pjson_data> (указывает на массив элементов pjson_data).
//   object  — отсортированный массив пар (pstring_data ключ, pjson_data значение).
//             Для фазы 1: объекты хранятся как отсортированные массивы записей
//             (pstring_data key, pjson_data value) — см. pjson_kv_pair.
//
// Смещения fptr хранятся как uintptr_t (sizeof(void*)). pjson_data — 16 байт.

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
// pjson_obj_entry — одна пара ключ-значение внутри объекта pjson.
// Оба поля тривиально копируемы.
// ---------------------------------------------------------------------------
struct pjson_obj_entry
{
    pstring_data key;    // Заголовок персистной строки (length + fptr<char>)
    // Значение хранится отдельно в параллельном pvector_data<pjson_data>.
    // Для простоты в фазе 1 объект хранит (key, pjson_data) как единую пару
    // (см. pjson_kv_pair ниже).
};

// Предварительное объявление, чтобы pjson_data мог хранить смещения на самого себя.
struct pjson_data;

// pjson_kv_entry — одна пара (pstring_data ключ, pjson_data значение).
// Используется как тип элемента отсортированного массива, лежащего в основе объектов pjson.
// Оба компонента тривиально копируемы, значит и вся структура тривиально копируема
// (проверяется static_assert ниже).
struct pjson_kv_entry
{
    pstring_data key;    // 8 байт: unsigned length + fptr<char>
    pjson_data*  _pad;   // Заглушка, заменяемая на pjson_data ниже
};
// (Нельзя встроить pjson_data в pjson_kv_entry до его определения, поэтому используем
//  двухуровневый трюк: сначала определяем pjson_data, затем финальную структуру pjson_kv_pair.)

// ---------------------------------------------------------------------------
// pjson_data — тривиально копируемый заголовок персистного JSON-значения.
// ---------------------------------------------------------------------------
struct pjson_data
{
    pjson_type type;    // 4 байта — дискриминант

    union payload_t
    {
        uint32_t    boolean_val;  // 0 = false, ненулевое = true
        int64_t     int_val;
        uint64_t    uint_val;
        double      real_val;

        // Для строки: смещение в ПАП для массива символов.
        // (Хранится как raw unsigned вместо fptr<char>, чтобы избежать
        //  ограничения C++ на нетривиальные типы в union.
        //  fptr<char> хранит только uintptr_t — представление идентично.)
        struct { unsigned length; unsigned chars_slot; } string_val;

        // Для массива: размер и смещение в ПАП для массива pjson_data.
        struct { unsigned size; unsigned data_slot; } array_val;

        // Для объекта: размер и смещение в ПАП для массива pjson_kv_pair.
        struct { unsigned size; unsigned pairs_slot; } object_val;
    } payload;

    // Выравнивание до 16 байт (4 + 4 + 8 = 16).
    // Наибольший элемент payload_t — int64_t/uint64_t/double (8 байт);
    // варианты string/array/object — 2×unsigned = 8 байт.
    // Итого: 4 (type) + 4 (выравнивание в union) + 8 (значение) = 16 байт.
};

static_assert(std::is_trivially_copyable<pjson_data>::value,
              "pjson_data должна быть тривиально копируемой для использования с persist<T>");

// ---------------------------------------------------------------------------
// pjson_kv_pair — одна пара (pstring_data ключ, pjson_data значение).
// Оба компонента тривиально копируемы, значит и пара тривиально копируема.
// Объекты pjson хранятся в pvector<pjson_kv_pair> (отсортированном по ключу, O(log n) поиск).
// ---------------------------------------------------------------------------
struct pjson_kv_pair
{
    pstring_data key;    // 8 байт
    pjson_data   value;  // 16 байт
};

static_assert(std::is_trivially_copyable<pjson_kv_pair>::value,
              "pjson_kv_pair должна быть тривиально копируемой");
static_assert(std::is_trivially_copyable<pvector_data<pjson_kv_pair>>::value,
              "pvector_data<pjson_kv_pair> должна быть тривиально копируемой");

// ---------------------------------------------------------------------------
// pjson — тонкая не-владеющая обёртка над ссылкой pjson_data.
// ---------------------------------------------------------------------------
class pjson
{
    pjson_data& _d;

    // ----- internal helpers -----------------------------------------------

    // Free any heap-allocated children of _d, then reset to null.
    void _free()
    {
        switch( _d.type )
        {
        case pjson_type::string:
            if( _d.payload.string_val.chars_slot != 0 )
            {
                fptr<char> tmp;
                tmp.set_addr( _d.payload.string_val.chars_slot );
                tmp.DeleteArray();
                _d.payload.string_val.chars_slot = 0;
                _d.payload.string_val.length = 0;
            }
            break;

        case pjson_type::array:
            if( _d.payload.array_val.data_slot != 0 )
            {
                // Recursively free each element.
                unsigned sz = _d.payload.array_val.size;
                for( unsigned i = 0; i < sz; i++ )
                {
                    pjson_data& elem =
                        AddressManager<pjson_data>::GetArrayElement(
                            _d.payload.array_val.data_slot, i );
                    pjson(elem)._free();
                }
                fptr<pjson_data> tmp;
                tmp.set_addr( _d.payload.array_val.data_slot );
                tmp.DeleteArray();
                _d.payload.array_val.data_slot = 0;
                _d.payload.array_val.size = 0;
            }
            break;

        case pjson_type::object:
            if( _d.payload.object_val.pairs_slot != 0 )
            {
                unsigned sz = _d.payload.object_val.size;
                for( unsigned i = 0; i < sz; i++ )
                {
                    pjson_kv_pair& pair =
                        AddressManager<pjson_kv_pair>::GetArrayElement(
                            _d.payload.object_val.pairs_slot, i );
                    // Free the key string.
                    if( pair.key.chars.addr() != 0 )
                        pair.key.chars.DeleteArray();
                    // Free the value recursively.
                    pjson(pair.value)._free();
                }
                fptr<pjson_kv_pair> tmp;
                tmp.set_addr( _d.payload.object_val.pairs_slot );
                tmp.DeleteArray();
                _d.payload.object_val.pairs_slot = 0;
                _d.payload.object_val.size = 0;
            }
            break;

        default:
            break;
        }
        _d.type = pjson_type::null;
        _d.payload.uint_val = 0;
    }

    // Assign pstring_data by copying a C-string into persistent storage.
    static void _assign_key( pstring_data& sd, const char* s )
    {
        if( sd.chars.addr() != 0 )
            sd.chars.DeleteArray();
        if( s == nullptr || s[0] == '\0' )
        {
            sd.length = 0;
            return;
        }
        unsigned len = static_cast<unsigned>(std::strlen(s));
        sd.length = len;
        sd.chars.NewArray(len + 1);
        for( unsigned i = 0; i <= len; i++ )
            sd.chars[i] = s[i];
    }

    // Binary search for a key in the object's sorted pairs array.
    // Returns the index of the first pair whose key >= s, or size if none.
    unsigned _obj_lower_bound( const char* s ) const
    {
        unsigned sz = _d.payload.object_val.size;
        unsigned lo = 0, hi = sz;
        while( lo < hi )
        {
            unsigned mid = (lo + hi) / 2;
            pjson_kv_pair& pair =
                AddressManager<pjson_kv_pair>::GetArrayElement(
                    _d.payload.object_val.pairs_slot, mid );
            const char* k = (pair.key.chars.addr() != 0)
                            ? &pair.key.chars[0]
                            : "";
            if( std::strcmp(k, s) < 0 )
                lo = mid + 1;
            else
                hi = mid;
        }
        return lo;
    }

public:
    // Оборачивает существующую ссылку pjson_data. НЕ принимает владение.
    explicit pjson(pjson_data& data) : _d(data) {}

    // ----- Запросы типа ---------------------------------------------------
    pjson_type type() const { return _d.type; }
    bool is_null()     const { return _d.type == pjson_type::null; }
    bool is_boolean()  const { return _d.type == pjson_type::boolean; }
    bool is_integer()  const { return _d.type == pjson_type::integer; }
    bool is_uinteger() const { return _d.type == pjson_type::uinteger; }
    bool is_real()     const { return _d.type == pjson_type::real; }
    bool is_number()   const { return is_integer() || is_uinteger() || is_real(); }
    bool is_string()   const { return _d.type == pjson_type::string; }
    bool is_array()    const { return _d.type == pjson_type::array; }
    bool is_object()   const { return _d.type == pjson_type::object; }

    // ----- Установка значений ---------------------------------------------

    void set_null()
    {
        _free();
    }

    void set_bool(bool v)
    {
        _free();
        _d.type = pjson_type::boolean;
        _d.payload.boolean_val = v ? 1u : 0u;
    }

    void set_int(int64_t v)
    {
        _free();
        _d.type = pjson_type::integer;
        _d.payload.int_val = v;
    }

    void set_uint(uint64_t v)
    {
        _free();
        _d.type = pjson_type::uinteger;
        _d.payload.uint_val = v;
    }

    void set_real(double v)
    {
        _free();
        _d.type = pjson_type::real;
        _d.payload.real_val = v;
    }

    void set_string(const char* s)
    {
        _free();
        _d.type = pjson_type::string;
        _d.payload.string_val.chars_slot = 0;
        _d.payload.string_val.length = 0;
        if( s == nullptr || s[0] == '\0' ) return;

        unsigned len = static_cast<unsigned>(std::strlen(s));
        _d.payload.string_val.length = len;
        fptr<char> chars;
        chars.NewArray(len + 1);
        for( unsigned i = 0; i <= len; i++ )
            chars[i] = s[i];
        _d.payload.string_val.chars_slot = chars.addr();
    }

    // set_array: сбросить до пустого персистного массива.
    void set_array()
    {
        _free();
        _d.type = pjson_type::array;
        _d.payload.array_val.size = 0;
        _d.payload.array_val.data_slot = 0;
    }

    // set_object: сбросить до пустого персистного объекта.
    void set_object()
    {
        _free();
        _d.type = pjson_type::object;
        _d.payload.object_val.size = 0;
        _d.payload.object_val.pairs_slot = 0;
    }

    // ----- Получение значений ---------------------------------------------

    bool get_bool() const
    {
        return _d.payload.boolean_val != 0;
    }

    int64_t get_int() const
    {
        return _d.payload.int_val;
    }

    uint64_t get_uint() const
    {
        return _d.payload.uint_val;
    }

    double get_real() const
    {
        return _d.payload.real_val;
    }

    const char* get_string() const
    {
        if( _d.payload.string_val.chars_slot == 0 ) return "";
        return &AddressManager<char>::GetArrayElement(
                    _d.payload.string_val.chars_slot, 0);
    }

    // ----- Операции с массивами -------------------------------------------

    unsigned size() const
    {
        if( _d.type == pjson_type::array )
            return _d.payload.array_val.size;
        if( _d.type == pjson_type::object )
            return _d.payload.object_val.size;
        if( _d.type == pjson_type::string )
            return _d.payload.string_val.length;
        return 0;
    }

    bool empty() const { return size() == 0; }

    // push_back: добавить нулевой элемент в массив и вернуть ссылку на него.
    pjson_data& push_back()
    {
        unsigned old_size = _d.payload.array_val.size;
        unsigned new_size = old_size + 1;

        if( _d.payload.array_val.data_slot == 0 )
        {
            // Первый элемент: выделяем начальную ёмкость.
            fptr<pjson_data> arr;
            arr.NewArray(4);
            // Инициализируем все слоты нулями.
            for( unsigned i = 0; i < 4; i++ )
            {
                pjson_data& e = arr[i];
                e.type = pjson_type::null;
                e.payload.uint_val = 0;
            }
            _d.payload.array_val.data_slot = arr.addr();
        }
        else if( new_size > AddressManager<pjson_data>::GetCount(
                                _d.payload.array_val.data_slot ) )
        {
            // Рост: удваиваем ёмкость.
            unsigned old_cap = AddressManager<pjson_data>::GetCount(
                                   _d.payload.array_val.data_slot);
            unsigned new_cap = old_cap * 2;
            if( new_cap < new_size ) new_cap = new_size;

            fptr<pjson_data> new_arr;
            new_arr.NewArray(new_cap);
            for( unsigned i = 0; i < new_cap; i++ )
            {
                pjson_data& e = new_arr[i];
                e.type = pjson_type::null;
                e.payload.uint_val = 0;
            }
            // Переносим существующие элементы (поверхностная копия — только примитивы;
            // вложенные объекты сохраняют свои смещения в ПАП).
            for( unsigned i = 0; i < old_size; i++ )
                new_arr[i] = AddressManager<pjson_data>::GetArrayElement(
                                 _d.payload.array_val.data_slot, i);

            fptr<pjson_data> old_arr;
            old_arr.set_addr(_d.payload.array_val.data_slot);
            old_arr.DeleteArray();
            _d.payload.array_val.data_slot = new_arr.addr();
        }

        _d.payload.array_val.size = new_size;
        return AddressManager<pjson_data>::GetArrayElement(
                   _d.payload.array_val.data_slot, old_size);
    }

    // operator[](idx): доступ к элементу массива по индексу.
    pjson_data& operator[](unsigned idx)
    {
        return AddressManager<pjson_data>::GetArrayElement(
                   _d.payload.array_val.data_slot, idx);
    }

    const pjson_data& operator[](unsigned idx) const
    {
        return AddressManager<pjson_data>::GetArrayElement(
                   _d.payload.array_val.data_slot, idx);
    }

    // ----- Операции с объектами -------------------------------------------

    // obj_find: найти ключ в объекте; возвращает указатель на значение или nullptr.
    pjson_data* obj_find(const char* key)
    {
        if( _d.type != pjson_type::object ) return nullptr;
        if( _d.payload.object_val.pairs_slot == 0 ) return nullptr;
        unsigned idx = _obj_lower_bound(key);
        unsigned sz  = _d.payload.object_val.size;
        if( idx >= sz ) return nullptr;
        pjson_kv_pair& pair =
            AddressManager<pjson_kv_pair>::GetArrayElement(
                _d.payload.object_val.pairs_slot, idx);
        const char* k = (pair.key.chars.addr() != 0) ? &pair.key.chars[0] : "";
        if( std::strcmp(k, key) != 0 ) return nullptr;
        return &pair.value;
    }

    const pjson_data* obj_find(const char* key) const
    {
        if( _d.type != pjson_type::object ) return nullptr;
        if( _d.payload.object_val.pairs_slot == 0 ) return nullptr;
        unsigned idx = _obj_lower_bound(key);
        unsigned sz  = _d.payload.object_val.size;
        if( idx >= sz ) return nullptr;
        const pjson_kv_pair& pair =
            AddressManager<pjson_kv_pair>::GetArrayElement(
                _d.payload.object_val.pairs_slot, idx);
        const char* k = (pair.key.chars.addr() != 0) ? &pair.key.chars[0] : "";
        if( std::strcmp(k, key) != 0 ) return nullptr;
        return &pair.value;
    }

    // obj_insert: вставить или заменить ключ в объекте.
    // Возвращает ссылку на слот значения для данного ключа.
    pjson_data& obj_insert(const char* key)
    {
        unsigned sz = _d.payload.object_val.size;

        if( _d.payload.object_val.pairs_slot == 0 )
        {
            // Первая запись: выделяем начальную ёмкость — 4 пары.
            fptr<pjson_kv_pair> arr;
            arr.NewArray(4);
            for( unsigned i = 0; i < 4; i++ )
            {
                pjson_kv_pair& p = arr[i];
                p.key.length = 0;
                p.key.chars.set_addr(0);
                p.value.type = pjson_type::null;
                p.value.payload.uint_val = 0;
            }
            _d.payload.object_val.pairs_slot = arr.addr();
        }

        unsigned idx = _obj_lower_bound(key);

        // Проверяем, существует ли ключ.
        if( idx < sz )
        {
            pjson_kv_pair& pair =
                AddressManager<pjson_kv_pair>::GetArrayElement(
                    _d.payload.object_val.pairs_slot, idx);
            const char* k = (pair.key.chars.addr() != 0) ? &pair.key.chars[0] : "";
            if( std::strcmp(k, key) == 0 )
            {
                // Ключ найден — освобождаем старое значение и возвращаем слот.
                pjson(pair.value)._free();
                return pair.value;
            }
        }

        // Вставляем новую запись в позицию idx, сдвигая вправо.
        unsigned new_size = sz + 1;
        unsigned cap = AddressManager<pjson_kv_pair>::GetCount(
                           _d.payload.object_val.pairs_slot);
        if( new_size > cap )
        {
            unsigned new_cap = cap * 2;
            if( new_cap < new_size ) new_cap = new_size;
            fptr<pjson_kv_pair> new_arr;
            new_arr.NewArray(new_cap);
            for( unsigned i = 0; i < new_cap; i++ )
            {
                pjson_kv_pair& p = new_arr[i];
                p.key.length = 0;
                p.key.chars.set_addr(0);
                p.value.type = pjson_type::null;
                p.value.payload.uint_val = 0;
            }
            for( unsigned i = 0; i < sz; i++ )
                new_arr[i] = AddressManager<pjson_kv_pair>::GetArrayElement(
                                 _d.payload.object_val.pairs_slot, i);
            fptr<pjson_kv_pair> old_arr;
            old_arr.set_addr(_d.payload.object_val.pairs_slot);
            old_arr.DeleteArray();
            _d.payload.object_val.pairs_slot = new_arr.addr();
        }

        // Сдвигаем элементы вправо, освобождая место в позиции idx.
        for( unsigned i = sz; i > idx; i-- )
        {
            AddressManager<pjson_kv_pair>::GetArrayElement(
                _d.payload.object_val.pairs_slot, i) =
            AddressManager<pjson_kv_pair>::GetArrayElement(
                _d.payload.object_val.pairs_slot, i - 1);
        }

        // Записываем новую пару в позицию idx.
        pjson_kv_pair& new_pair =
            AddressManager<pjson_kv_pair>::GetArrayElement(
                _d.payload.object_val.pairs_slot, idx);
        new_pair.value.type = pjson_type::null;
        new_pair.value.payload.uint_val = 0;
        // Обнуляем слот ключа ДО вызова _assign_key, чтобы не освободить
        // chars_slot, который теперь принадлежит сдвинутому соседу [idx+1].
        // После цикла сдвига и [idx], и [idx+1] содержат одно и то же смещение
        // (поверхностная копия); сброс [idx].key помечает его как «не владелец»
        // и предотвращает двойное освобождение / использование после освобождения.
        new_pair.key.chars.set_addr(0);
        new_pair.key.length = 0;
        _assign_key(new_pair.key, key);

        _d.payload.object_val.size = new_size;
        return new_pair.value;
    }

    // obj_erase: удалить ключ из объекта. Возвращает true, если найден.
    bool obj_erase(const char* key)
    {
        if( _d.type != pjson_type::object ) return false;
        if( _d.payload.object_val.pairs_slot == 0 ) return false;
        unsigned sz  = _d.payload.object_val.size;
        unsigned idx = _obj_lower_bound(key);
        if( idx >= sz ) return false;
        pjson_kv_pair& pair =
            AddressManager<pjson_kv_pair>::GetArrayElement(
                _d.payload.object_val.pairs_slot, idx);
        const char* k = (pair.key.chars.addr() != 0) ? &pair.key.chars[0] : "";
        if( std::strcmp(k, key) != 0 ) return false;

        // Освобождаем ключ-строку и значение.
        if( pair.key.chars.addr() != 0 )
            pair.key.chars.DeleteArray();
        pjson(pair.value)._free();

        // Сдвигаем оставшиеся элементы влево.
        for( unsigned i = idx; i + 1 < sz; i++ )
        {
            AddressManager<pjson_kv_pair>::GetArrayElement(
                _d.payload.object_val.pairs_slot, i) =
            AddressManager<pjson_kv_pair>::GetArrayElement(
                _d.payload.object_val.pairs_slot, i + 1);
        }
        _d.payload.object_val.size--;
        return true;
    }

    // ----- Освобождение ресурсов ------------------------------------------

    // free: освободить все выделенные ресурсы и сбросить в null.
    void free() { _free(); }
};
