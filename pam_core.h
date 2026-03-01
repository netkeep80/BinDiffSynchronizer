#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <typeinfo>

// pam_core.h — Ядро персистного адресного менеджера (ПАМ).
// Содержит PersistentAddressSpace с вектором типов (type_vec), картой слотов
// (slot_map) и картой имён (name_map) внутри ПАП, а также список свободных
// областей (free_list) для повторного использования удалённых данных.

// ---------------------------------------------------------------------------
// Константы ПАМ
// ---------------------------------------------------------------------------

/// Магическое число для идентификации файла ПАМ.
constexpr uint32_t PAM_MAGIC = 0x50414D00u; // 'PAM\0'

/// Версия формата файла ПАМ (фаза 10: добавлен string_table_offset для pstringview_table).
constexpr uint32_t PAM_VERSION = 10u;

/// Максимальная длина идентификатора типа в таблице типов (хранится один раз).
constexpr unsigned PAM_TYPE_ID_SIZE = 64u;

/// Максимальная длина имени объекта в карте имён (хранится один раз на объект).
constexpr unsigned PAM_NAME_SIZE = 64u;

/// Специальный индекс, означающий «нет имени» или «нет слота».
constexpr uintptr_t PAM_INVALID_IDX = static_cast<uintptr_t>( -1 );

/// Начальный размер области данных ПАМ (байт) — 10 КБ.
constexpr uintptr_t PAM_INITIAL_DATA_SIZE = 10u * 1024u;

/// Начальная ёмкость карты слотов (число слотов).
constexpr unsigned PAM_INITIAL_SLOT_CAPACITY = 16u;

/// Начальная ёмкость таблицы типов (число уникальных типов).
constexpr unsigned PAM_INITIAL_TYPE_CAPACITY = 16u;

/// Начальная ёмкость карты имён (число уникальных имён объектов).
constexpr unsigned PAM_INITIAL_NAME_CAPACITY = 16u;

/// Начальная ёмкость списка свободных областей (число записей).
constexpr unsigned PAM_INITIAL_FREE_CAPACITY = 16u;

// ---------------------------------------------------------------------------
// free_entry — запись списка свободных областей ПАМ
// ---------------------------------------------------------------------------

/// Одна свободная область данных в ПАП: смещение начала и размер в байтах.
struct free_entry
{
    uintptr_t offset; ///< Смещение свободной области в _data
    uintptr_t size;   ///< Размер свободной области в байтах
};

static_assert( std::is_trivially_copyable<free_entry>::value, "free_entry должен быть тривиально копируемым" );

// ---------------------------------------------------------------------------
// TypeInfo — запись вектора типов ПАМ (хранится внутри ПАП, фаза 8.4)
// ---------------------------------------------------------------------------

/// Информация о типе — элемент вектора типов внутри ПАП.
/// Хранит имя типа и размер элемента один раз для всех слотов этого типа.
/// Все поля фиксированного размера, тривиально копируемы.
struct TypeInfo
{
    uintptr_t elem_size;              ///< Размер одного элемента типа в байтах
    char      name[PAM_TYPE_ID_SIZE]; ///< Имя типа (из typeid(T).name())
};

static_assert( std::is_trivially_copyable<TypeInfo>::value, "TypeInfo должен быть тривиально копируемым" );

// ---------------------------------------------------------------------------
// type_info_entry — запись таблицы типов ПАМ (для совместимости с тестами)
// ---------------------------------------------------------------------------

/// Запись таблицы типов — сохранена для совместимости с тестами фаз 4–8.3.
/// В фазе 8.4 типы хранятся в векторе TypeInfo внутри ПАП.
struct type_info_entry
{
    bool      used;                   ///< Запись занята
    uintptr_t elem_size;              ///< Размер одного элемента типа в байтах
    char      name[PAM_TYPE_ID_SIZE]; ///< Имя типа (из typeid(T).name())
};

static_assert( std::is_trivially_copyable<type_info_entry>::value,
               "type_info_entry должен быть тривиально копируемым" );

// ---------------------------------------------------------------------------
// name_key — ключ карты имён (фиксированный char-массив, тривиально копируемый)
// ---------------------------------------------------------------------------

/// Ключ карты имён — фиксированный массив символов.
/// Тривиально копируем, поддерживает операторы сравнения (< и ==).
struct name_key
{
    char name[PAM_NAME_SIZE]; ///< Имя объекта (нуль-терминированная строка)

    bool operator<( const name_key& other ) const { return std::strncmp( name, other.name, PAM_NAME_SIZE ) < 0; }
    bool operator==( const name_key& other ) const { return std::strncmp( name, other.name, PAM_NAME_SIZE ) == 0; }
};

static_assert( std::is_trivially_copyable<name_key>::value, "name_key должен быть тривиально копируемым" );

// ---------------------------------------------------------------------------
// name_entry — запись в отсортированном массиве карты имён (внутри ПАП)
// ---------------------------------------------------------------------------

/// Одна запись в карте имён — пара (ключ = name_key, значение = slot_offset).
/// Раскладка совместима с pmap_entry<name_key, uintptr_t>.
struct name_entry
{
    name_key  key;         ///< Имя объекта (ключ для бинарного поиска)
    uintptr_t slot_offset; ///< Смещение объекта в ПАП (значение)
};

static_assert( std::is_trivially_copyable<name_entry>::value, "name_entry должен быть тривиально копируемым" );

// ---------------------------------------------------------------------------
// name_info_entry — запись таблицы имён ПАМ (для совместимости с тестами)
// ---------------------------------------------------------------------------

/// Запись таблицы имён — сохранена для совместимости с тестами фаз 5–8.2.
/// В фазе 8.3 имена хранятся в карте имён внутри ПАП (name_entry).
struct name_info_entry
{
    bool      used;           ///< Запись занята
    uintptr_t slot_offset;    ///< Смещение объекта в ПАП (ключ в карте слотов)
    char name[PAM_NAME_SIZE]; ///< Имя объекта (уникально среди занятых записей)
};

static_assert( std::is_trivially_copyable<name_info_entry>::value,
               "name_info_entry должен быть тривиально копируемым" );

// ---------------------------------------------------------------------------
// SlotInfo — значение в карте слотов (pmap<uintptr_t, SlotInfo>)
// ---------------------------------------------------------------------------

/// Информация о слоте — значение в карте слотов.
/// Ключом карты служит смещение объекта в области данных ПАП (offset).
/// Все поля фиксированного размера, тривиально копируемы.
struct SlotInfo
{
    uintptr_t count;    ///< Количество элементов (для массивов)
    uintptr_t type_idx; ///< Индекс типа в таблице type_info_entry
    uintptr_t name_idx; ///< Индекс в карте имён (фаза 8.3); PAM_INVALID_IDX для безымянных
};

static_assert( std::is_trivially_copyable<SlotInfo>::value, "SlotInfo должен быть тривиально копируемым" );

// ---------------------------------------------------------------------------
// slot_descriptor — псевдоним для совместимости и тестов
// ---------------------------------------------------------------------------

/// Дескриптор аллоцированного объекта (для совместимости с тестами фазы 5–8.2).
/// В фазе 8.2–8.3 карта слотов реализована через SlotInfo (ключ = offset).
/// slot_descriptor сохраняется как POD для тестов проверки тривиальной копируемости.
struct slot_descriptor
{
    bool used; ///< Слот занят (не используется в карте 8.2+, сохранён для совместимости)
    uintptr_t offset;   ///< Смещение объекта в области данных ПАП
    uintptr_t count;    ///< Количество элементов (для массивов)
    uintptr_t type_idx; ///< Индекс типа в таблице type_info_entry
    uintptr_t name_idx; ///< Индекс в карте имён; PAM_INVALID_IDX для безымянных
};

static_assert( std::is_trivially_copyable<slot_descriptor>::value,
               "slot_descriptor должен быть тривиально копируемым" );

// ---------------------------------------------------------------------------
// slot_entry — запись в отсортированном массиве карты слотов (внутри ПАП)
// ---------------------------------------------------------------------------

/// Одна запись в карте слотов — пара (ключ = offset, значение = SlotInfo).
/// Раскладка совместима с pmap_entry<uintptr_t, SlotInfo>.
struct slot_entry
{
    uintptr_t key;   ///< Смещение объекта в ПАП (ключ для бинарного поиска)
    SlotInfo  value; ///< Информация о слоте
};

static_assert( std::is_trivially_copyable<slot_entry>::value, "slot_entry должен быть тривиально копируемым" );

// ---------------------------------------------------------------------------
// pam_header — заголовок файла ПАМ
// ---------------------------------------------------------------------------

/// Заголовок файла ПАМ (версия 10: добавлен string_table_offset для pstringview_table).
struct pam_header
{
    uint32_t  magic;            ///< PAM_MAGIC
    uint32_t  version;          ///< PAM_VERSION
    uintptr_t data_area_size;   ///< Размер области данных в байтах
    uintptr_t type_vec_offset;  ///< Смещение вектора типов в данных
    uintptr_t slot_map_offset;  ///< Смещение карты слотов в данных
    uintptr_t name_map_offset;  ///< Смещение карты имён в данных
    uintptr_t free_list_offset; ///< Смещение списка свободных областей в данных
    uintptr_t bump;             ///< Указатель на следующее свободное смещение
    uintptr_t string_table_offset; ///< Смещение pstringview_table в данных; 0 = не создана (фаза 10)
};

static_assert( std::is_trivially_copyable<pam_header>::value, "pam_header должен быть тривиально копируемым" );

// ---------------------------------------------------------------------------
// pam_array_hdr — заголовок внутреннего массива ПАМ (задача 1.5)
// ---------------------------------------------------------------------------
//
// Общий заголовок для всех внутренних массивов ПАМ (type_vec, slot_map,
// name_map, free_list). Раскладка идентична pmem_array_hdr из pmem_array.h:
//   [size | capacity | data_off]  — 3 * sizeof(uintptr_t)
//
// Определяется здесь (а не через #include "pmem_array.h") во избежание
// циклической зависимости: pmem_array.h включает persist.h → pam_core.h.
// Тождественность pam_array_hdr и pmem_array_hdr гарантируется static_assert
// в pmem_array.h (sizeof == 3 * sizeof(void*), тривиально копируем).

struct pam_array_hdr
{
    uintptr_t size;     ///< Текущее количество элементов
    uintptr_t capacity; ///< Ёмкость (число элементов в выделенном буфере)
    uintptr_t data_off; ///< Смещение массива данных в ПАП; 0 = не выделено
};

static_assert( std::is_trivially_copyable<pam_array_hdr>::value, "pam_array_hdr должен быть тривиально копируемым" );
static_assert( sizeof( pam_array_hdr ) == 3 * sizeof( uintptr_t ),
               "pam_array_hdr должен занимать 3 * sizeof(uintptr_t) байт" );

// ---------------------------------------------------------------------------
// PersistentAddressSpace — единый персистный адресный менеджер
// ---------------------------------------------------------------------------

/**
 * Единый менеджер персистного адресного пространства (ПАМ).
 *
 * Управляет плоским байтовым буфером (_data), загружаемым из файла без вызова
 * конструкторов. Хранит вектор типов (_type_vec) ВНУТРИ ПАП — массив TypeInfo,
 * раскладка совместима с pvector<TypeInfo> (фаза 8.4).
 * Хранит карту слотов (_slot_map) ВНУТРИ ПАП — отсортированный
 * массив slot_entry, раскладка совместима с pmap<uintptr_t, SlotInfo> (фаза 8.2).
 * Хранит карту имён (_name_map) ВНУТРИ ПАП — отсортированный массив name_entry,
 * раскладка совместима с pmap<name_key, uintptr_t> (фаза 8.3).
 *
 * Внутренние массивы хранят заголовки как pam_array_hdr [size|capacity|data_off]
 * (задача 1.5). Зеркальных переменных size/capacity/entries_off нет — все поля
 * читаются напрямую из ПАП через _arr_hdr(offset). Это устраняет
 * дублированные _sync_*_mirrors / _flush_*_mirrors функции.
 *
 * Использование:
 *   PersistentAddressSpace::Init("myapp.pam");
 *   auto& pam = PersistentAddressSpace::Get();
 *   uintptr_t off = pam.Create<int>("counter");   // именованный
 *   uintptr_t off2 = pam.Create<int>();           // безымянный
 *   int* p = pam.Resolve<int>(off);
 *   *p = 42;
 *   const char* name = pam.GetName(off);          // "counter"
 *   pam.Save();
 */
class PersistentAddressSpace
{
  public:
    // -----------------------------------------------------------------------
    // Статические методы управления синглтоном
    // -----------------------------------------------------------------------

    /**
     * Инициализировать ПАМ из файла. Если не существует — создаётся пустой образ.
     * Конструкторы объектов при загрузке НЕ вызываются (Тр.10).
     */
    static void Init( const char* filename ) { _instance()._load( filename ); }

    /**
     * Получить ссылку на единственный экземпляр ПАМ.
     * При первом обращении без предварительного Init() автоматически
     * создаётся пустой образ ПАП в оперативной памяти (без файла).
     */
    static PersistentAddressSpace& Get()
    {
        PersistentAddressSpace& pam = _instance();
        if ( pam._data == nullptr )
            pam._init_empty();
        return pam;
    }

    /// Сбросить ПАМ к пустому состоянию за O(1) — для быстрой очистки в тестах.
    void Reset()
    {
        std::free( _data );
        _data                = nullptr;
        _string_table_offset = 0;
        _init_empty();
    }

    // -----------------------------------------------------------------------
    // Создание объектов
    // -----------------------------------------------------------------------

    /**
     * Создать один объект типа T в ПАП.
     * @param name  Имя объекта (необязательно, можно nullptr).
     *              Если имя задано — создаётся запись в карте имён.
     *              Если имя уже занято — возвращает 0 (ошибка: имена уникальны).
     *              Если nullptr — память выделяется без имени.
     * @return Смещение (offset) объекта в области данных ПАП.
     *         0 означает ошибку (нет памяти или имя занято).
     */
    template <class T> uintptr_t Create( const char* name = nullptr )
    {
        return _alloc( sizeof( T ), 1, typeid( T ).name(), name );
    }

    /**
     * Создать массив из count объектов типа T в ПАП.
     * @param count Число элементов.
     * @param name  Имя массива (необязательно, можно nullptr).
     *              Если имя уже занято — возвращает 0 (ошибка: имена уникальны).
     * @return Смещение первого элемента в области данных ПАП.
     */
    template <class T> uintptr_t CreateArray( unsigned count, const char* name = nullptr )
    {
        if ( count == 0 )
            return 0;
        return _alloc( sizeof( T ), count, typeid( T ).name(), name );
    }

    // -----------------------------------------------------------------------
    // Перевыделение памяти (оптимизация pvector/pmap grow)
    // -----------------------------------------------------------------------

    /**
     * Расширить последний блок ПАП на месте (только если он последний в bump).
     * Возвращает old_offset при успехе или 0 (caller выделяет новый блок).
     * Слот в карте слотов НЕ обновляется — caller делает это сам.
     */
    uintptr_t Realloc( uintptr_t old_offset, uintptr_t old_count, uintptr_t new_count, uintptr_t elem_size )
    {
        if ( old_offset == 0 || new_count <= old_count )
            return 0;
        uintptr_t old_size = old_count * elem_size;
        uintptr_t new_size = new_count * elem_size;
        if ( old_offset + old_size != _bump )
            return 0; // не последний блок — нельзя расширить на месте
        uintptr_t data_size = _header().data_area_size;
        if ( _bump + ( new_size - old_size ) > data_size )
        {
            uintptr_t needed = _bump + ( new_size - old_size );
            uintptr_t ns     = data_size * 2;
            while ( ns < needed )
                ns *= 2;
            char* nd = static_cast<char*>( std::realloc( _data, static_cast<std::size_t>( ns ) ) );
            if ( nd == nullptr )
                return 0;
            _data = nd;
            std::memset( _data + data_size, 0, static_cast<std::size_t>( ns - data_size ) );
            _header().data_area_size = ns;
        }
        std::memset( _data + _bump, 0, static_cast<std::size_t>( new_size - old_size ) );
        _bump += new_size - old_size;
        return old_offset;
    }

    // -----------------------------------------------------------------------
    // Удаление объектов
    // -----------------------------------------------------------------------

    /**
     * Освободить слот по смещению offset.
     * Конструкторы/деструкторы НЕ вызываются (Тр.10).
     * Для именованных объектов также освобождается запись в карте имён.
     * Освобождённая область данных добавляется в список свободных областей.
     */
    void Delete( uintptr_t offset )
    {
        if ( offset == 0 )
            return;
        // Ищем слот в карте слотов (O(log n) бинарным поиском).
        uintptr_t idx = _slot_lower_bound( offset );
        if ( idx >= _slot_map_size() || _slot_entries()[idx].key != offset )
            return; // слот не найден

        SlotInfo& info = _slot_entries()[idx].value;
        uintptr_t nidx = info.name_idx;

        // Вычисляем размер освобождаемой области для списка свободных областей.
        uintptr_t freed_size = 0;
        if ( info.type_idx < _type_vec_size() )
        {
            const TypeInfo* te = _type_entries_const();
            if ( te != nullptr )
                freed_size = te[info.type_idx].elem_size * info.count;
        }

        // Освобождаем запись в карте имён (если объект именованный).
        if ( nidx != PAM_INVALID_IDX && nidx < _name_map_size() )
        {
            name_entry* nentries = _name_entries();
            if ( nentries != nullptr )
            {
                // Сдвигаем оставшиеся записи карты имён влево.
                uintptr_t nm_size = _name_map_size();
                for ( uintptr_t i = nidx; i + 1 < nm_size; i++ )
                    nentries[i] = nentries[i + 1];
                _arr_hdr( _name_map_offset )->size--;
                // Корректируем name_idx для слотов с индексами > nidx.
                _shift_name_indices_after_delete( nidx );
            }
        }

        // Удаляем запись из карты слотов (сдвигаем оставшиеся влево).
        uintptr_t sm_size = _slot_map_size();
        for ( uintptr_t i = idx; i + 1 < sm_size; i++ )
            _slot_entries()[i] = _slot_entries()[i + 1];
        _arr_hdr( _slot_map_offset )->size--;

        // Добавляем освобождённую область в список свободных (для повторного использования).
        if ( freed_size > 0 )
            _free_insert( offset, freed_size );
    }

    // -----------------------------------------------------------------------
    // Поиск по имени (только для именованных объектов)
    // -----------------------------------------------------------------------

    /**
     * Найти именованный объект по имени (поиск через карту имён внутри ПАП).
     * Поиск O(log n) бинарным поиском по отсортированной карте имён (фаза 8.3).
     * @return Смещение объекта или 0, если не найден.
     */
    uintptr_t Find( const char* name ) const
    {
        if ( name == nullptr || name[0] == '\0' )
            return 0;
        name_key nk{};
        std::strncpy( nk.name, name, PAM_NAME_SIZE - 1 );
        uintptr_t idx = _name_lower_bound( nk );
        if ( idx < _name_map_size() && _name_entries_const()[idx].key == nk )
            return _name_entries_const()[idx].slot_offset;
        return 0;
    }

    /**
     * Найти именованный объект по имени с проверкой типа.
     * @return Смещение объекта или 0, если не найден или тип не совпадает.
     */
    template <class T> uintptr_t FindTyped( const char* name ) const
    {
        if ( name == nullptr || name[0] == '\0' )
            return 0;
        const char* tname = typeid( T ).name();
        name_key    nk{};
        std::strncpy( nk.name, name, PAM_NAME_SIZE - 1 );
        uintptr_t idx = _name_lower_bound( nk );
        if ( idx >= _name_map_size() || !( _name_entries_const()[idx].key == nk ) )
            return 0;
        uintptr_t slot_off = _name_entries_const()[idx].slot_offset;
        // Ищем слот по смещению (O(log n)).
        uintptr_t sidx = _slot_lower_bound( slot_off );
        if ( sidx >= _slot_map_size() || _slot_entries_const()[sidx].key != slot_off )
            return 0;
        // Проверяем тип через вектор типов (фаза 8.4).
        uintptr_t tidx = _slot_entries_const()[sidx].value.type_idx;
        if ( tidx < _type_vec_size() )
        {
            const TypeInfo* te = _type_entries_const();
            if ( te != nullptr && std::strncmp( te[tidx].name, tname, PAM_TYPE_ID_SIZE ) == 0 )
                return slot_off;
        }
        return 0;
    }

    /**
     * Найти смещение по указателю (для всех аллоцированных объектов).
     * @return Смещение объекта или 0, если указатель не принадлежит ПАМ.
     */
    uintptr_t FindByPtr( const void* p ) const
    {
        if ( p == nullptr )
            return 0;
        const char* ptr       = static_cast<const char*>( p );
        const char* base      = _data;
        uintptr_t   data_size = _header_const().data_area_size;
        if ( ptr < base || ptr >= base + data_size )
            return 0;
        uintptr_t offset = static_cast<uintptr_t>( ptr - base );
        // Ищем смещение в карте слотов (O(log n)).
        uintptr_t idx = _slot_lower_bound( offset );
        if ( idx < _slot_map_size() && _slot_entries_const()[idx].key == offset )
            return offset;
        return 0;
    }

    // -----------------------------------------------------------------------
    // Получение имени объекта по смещению
    // -----------------------------------------------------------------------

    /**
     * Получить имя именованного объекта по его смещению.
     * Двусторонняя связь: SlotInfo.name_idx → индекс в карте имён (фаза 8.3).
     * @return Указатель на строку имени или nullptr, если объект безымянный
     *         или смещение неверно.
     */
    const char* GetName( uintptr_t offset ) const
    {
        if ( offset == 0 )
            return nullptr;
        uintptr_t idx = _slot_lower_bound( offset );
        if ( idx >= _slot_map_size() || _slot_entries_const()[idx].key != offset )
            return nullptr;
        uintptr_t nidx = _slot_entries_const()[idx].value.name_idx;
        if ( nidx == PAM_INVALID_IDX || nidx >= _name_map_size() )
            return nullptr;
        const name_entry* ne = _name_entries_const();
        if ( ne == nullptr )
            return nullptr;
        return ne[nidx].key.name;
    }

    /**
     * Получить счётчик элементов для слота по смещению.
     * @return Число элементов или 0.
     */
    uintptr_t GetCount( uintptr_t offset ) const
    {
        if ( offset == 0 )
            return 0;
        uintptr_t idx = _slot_lower_bound( offset );
        if ( idx < _slot_map_size() && _slot_entries_const()[idx].key == offset )
            return _slot_entries_const()[idx].value.count;
        return 0;
    }

    /**
     * Получить размер элемента для слота по смещению (из таблицы типов).
     * @return Размер одного элемента в байтах или 0.
     */
    uintptr_t GetElemSize( uintptr_t offset ) const
    {
        if ( offset == 0 )
            return 0;
        uintptr_t idx = _slot_lower_bound( offset );
        if ( idx >= _slot_map_size() || _slot_entries_const()[idx].key != offset )
            return 0;
        uintptr_t tidx = _slot_entries_const()[idx].value.type_idx;
        if ( tidx < _type_vec_size() )
        {
            const TypeInfo* te = _type_entries_const();
            if ( te != nullptr )
                return te[tidx].elem_size;
        }
        return 0;
    }

    /**
     * Преобразовать указатель на объект в ПАП в его смещение.
     * Используется для сохранения «текущего положения» перед перераспределением
     * памяти (realloc), после которого указатель становится недействительным.
     * @return Смещение объекта или 0, если указатель вне ПАМ.
     */
    uintptr_t PtrToOffset( const void* p ) const
    {
        if ( p == nullptr || _data == nullptr )
            return 0;
        const char* ptr       = static_cast<const char*>( p );
        uintptr_t   data_size = _header_const().data_area_size;
        if ( ptr < _data || ptr >= _data + data_size )
            return 0;
        return static_cast<uintptr_t>( ptr - _data );
    }

    // ─── Предварительное резервирование и метрики ПАМ ────────────────────────

    /// Зарезервировать ёмкость карты слотов >= min_slots.
    /// Оптимизирует массовое добавление объектов: один вызов вместо многократных
    /// реаллокаций. Если текущая ёмкость уже достаточна — ничего не делает.
    void ReserveSlots( uintptr_t min_slots )
    {
        if ( min_slots > _slot_map_capacity() )
            _reserve_slot_map( min_slots );
    }

    uintptr_t GetSlotCount() const { return _slot_map_size(); }        ///< Аллоцированных слотов.
    uintptr_t GetSlotCapacity() const { return _slot_map_capacity(); } ///< Ёмкость карты слотов.
    uintptr_t GetNamedCount() const { return _name_map_size(); }       ///< Именованных объектов.
    uintptr_t GetTypeCount() const { return _type_vec_size(); }        ///< Уникальных типов.
    uintptr_t GetFreeListSize() const { return _free_list_size(); }    ///< Свободных блоков.
    uintptr_t GetBump() const { return _bump; }                        ///< Позиция bump-указателя.

    /// Получить смещение таблицы интернирования строк (pstringview_table) в ПАП.
    /// 0 = таблица ещё не создана.
    uintptr_t GetStringTableOffset() const { return _string_table_offset; }

    /// Зарегистрировать смещение таблицы интернирования строк в заголовке ПАП.
    /// Вызывается из pstringview_manager при первом создании pstringview_table.
    void SetStringTableOffset( uintptr_t off )
    {
        _string_table_offset          = off;
        _header().string_table_offset = off;
    }

    // ── Словарь строк: интернирование и поиск (фаза 2) ─────────────────────
    // Функции pam_intern_string(), pam_search_strings(), pam_all_strings()
    // определены в pstringview.h после объявления pstringview_table.
    // Доступ к ним возможен только после #include "pstringview.h".

    uintptr_t GetDataSize() const
    {
        if ( _data == nullptr )
            return 0;
        return _header_const().data_area_size;
    }

    /// Полная самодиагностика ПАП: заголовок, структуры, согласованность карт.
    /// Возвращает false при любом нарушении инварианта.
    bool Validate() const
    {
        if ( _data == nullptr )
            return false;
        const pam_header& h = _header_const();
        if ( h.magic != PAM_MAGIC || h.version != PAM_VERSION )
            return false;
        uintptr_t ds = h.data_area_size;
        if ( _bump > ds )
            return false;
        auto in_range = [&]( uintptr_t o, uintptr_t s ) { return o == 0 || o + s <= ds; };
        if ( !in_range( _type_vec_offset, sizeof( pam_array_hdr ) ) ||
             !in_range( _slot_map_offset, sizeof( pam_array_hdr ) ) ||
             !in_range( _name_map_offset, sizeof( pam_array_hdr ) ) ||
             !in_range( _free_list_offset, sizeof( pam_array_hdr ) ) || !in_range( _string_table_offset, 1 ) )
            return false;
        uintptr_t tvsz = _type_vec_size(), tvcp = _type_vec_capacity();
        uintptr_t smsz = _slot_map_size(), smcp = _slot_map_capacity();
        uintptr_t nmsz = _name_map_size(), nmcp = _name_map_capacity();
        if ( tvsz > tvcp || smsz > smcp || nmsz > nmcp )
            return false;
        uintptr_t tv_data = _arr_hdr_const( _type_vec_offset ) ? _arr_hdr_const( _type_vec_offset )->data_off : 0;
        uintptr_t sm_data = _arr_hdr_const( _slot_map_offset ) ? _arr_hdr_const( _slot_map_offset )->data_off : 0;
        uintptr_t nm_data = _arr_hdr_const( _name_map_offset ) ? _arr_hdr_const( _name_map_offset )->data_off : 0;
        if ( tvcp > 0 && !in_range( tv_data, tvcp * sizeof( TypeInfo ) ) )
            return false;
        if ( smcp > 0 && !in_range( sm_data, smcp * sizeof( slot_entry ) ) )
            return false;
        if ( nmcp > 0 && !in_range( nm_data, nmcp * sizeof( name_entry ) ) )
            return false;
        const name_entry* ne = _name_entries_const();
        const slot_entry* se = _slot_entries_const();
        if ( ne && se )
        {
            for ( uintptr_t ni = 0; ni < nmsz; ni++ )
            {
                uintptr_t si = _slot_lower_bound( ne[ni].slot_offset );
                if ( si >= smsz || se[si].key != ne[ni].slot_offset || se[si].value.name_idx != ni )
                    return false;
            }
        }
        return true;
    }

    // -----------------------------------------------------------------------
    // Разыменование адреса
    // -----------------------------------------------------------------------

    /// Преобразовать смещение в T* (конструкторы НЕ вызываются). nullptr при ошибке.
    template <class T> T* Resolve( uintptr_t offset )
    {
        if ( offset == 0 )
            return nullptr;
        uintptr_t data_size = _header_const().data_area_size;
        if ( offset + sizeof( T ) > data_size )
            return nullptr;
        return reinterpret_cast<T*>( _data + offset );
    }

    template <class T> const T* Resolve( uintptr_t offset ) const
    {
        if ( offset == 0 )
            return nullptr;
        uintptr_t data_size = _header_const().data_area_size;
        if ( offset + sizeof( T ) > data_size )
            return nullptr;
        return reinterpret_cast<const T*>( _data + offset );
    }

    /// Получить элемент массива T по смещению и индексу. Проверяет границы.
    template <class T> T& ResolveElement( uintptr_t offset, uintptr_t index )
    {
        uintptr_t data_size   = _header_const().data_area_size;
        uintptr_t byte_offset = offset + index * sizeof( T );
        if ( offset == 0 || byte_offset + sizeof( T ) > data_size )
        {
            // Выход за пределы — неопределённое поведение в любом случае,
            // возвращаем первый байт как «аварийный» адрес (не nullptr).
            return *reinterpret_cast<T*>( _data );
        }
        return reinterpret_cast<T*>( _data + offset )[index];
    }

    template <class T> const T& ResolveElement( uintptr_t offset, uintptr_t index ) const
    {
        uintptr_t data_size   = _header_const().data_area_size;
        uintptr_t byte_offset = offset + index * sizeof( T );
        if ( offset == 0 || byte_offset + sizeof( T ) > data_size )
        {
            return *reinterpret_cast<const T*>( _data );
        }
        return reinterpret_cast<const T*>( _data + offset )[index];
    }

    // -----------------------------------------------------------------------
    // Сохранение
    // -----------------------------------------------------------------------

    /**
     * Сохранить образ ПАМ в файл. Заголовок и область данных (включая карты) записываются вместе.
     */
    void Save()
    {
        if ( _filename[0] == '\0' )
            return;
        // Обновляем смещения структур и bump в заголовке перед сохранением.
        _header().type_vec_offset     = _type_vec_offset;
        _header().slot_map_offset     = _slot_map_offset;
        _header().name_map_offset     = _name_map_offset;
        _header().free_list_offset    = _free_list_offset;
        _header().string_table_offset = _string_table_offset;
        _header().bump                = _bump;
        std::FILE* f                  = std::fopen( _filename, "wb" );
        if ( f == nullptr )
            return;
        std::fwrite( &_header(), sizeof( pam_header ), 1, f );
        std::fwrite( _data, 1, static_cast<std::size_t>( _header().data_area_size ), f );
        std::fclose( f );
    }

    // -----------------------------------------------------------------------
    // Деструктор
    // -----------------------------------------------------------------------

    ~PersistentAddressSpace()
    {
        Save();
        if ( _data != nullptr )
        {
            std::free( _data );
            _data = nullptr;
        }
    }

  private:
    // -----------------------------------------------------------------------
    // Внутреннее состояние
    // -----------------------------------------------------------------------

    char  _filename[256]; ///< Путь к файлу ПАМ
    char* _data;          ///< Буфер области данных

    // Смещения заголовков pam_array_hdr для каждого внутреннего массива.
    // Заголовок хранит [size, capacity, data_off] непосредственно в _data.
    // Зеркальных переменных size/capacity/data_off нет — читаем через _arr_hdr().
    uintptr_t _type_vec_offset;  ///< Смещение pam_array_hdr вектора типов
    uintptr_t _slot_map_offset;  ///< Смещение pam_array_hdr карты слотов
    uintptr_t _name_map_offset;  ///< Смещение pam_array_hdr карты имён
    uintptr_t _free_list_offset; ///< Смещение pam_array_hdr списка свободных областей
    uintptr_t _string_table_offset; ///< Смещение pstringview_table в ПАП; 0 = не создана (фаза 10)

    /// Счётчик следующего свободного смещения (bump-allocator).
    uintptr_t _bump;

    // -----------------------------------------------------------------------
    // Вспомогательные методы доступа к заголовкам внутренних массивов
    // -----------------------------------------------------------------------

    pam_header&       _header() { return *reinterpret_cast<pam_header*>( _data ); }
    const pam_header& _header_const() const { return *reinterpret_cast<const pam_header*>( _data ); }

    /// Получить указатель на pam_array_hdr по смещению в _data.
    pam_array_hdr* _arr_hdr( uintptr_t hdr_off )
    {
        if ( hdr_off == 0 || _data == nullptr )
            return nullptr;
        return reinterpret_cast<pam_array_hdr*>( _data + hdr_off );
    }

    const pam_array_hdr* _arr_hdr_const( uintptr_t hdr_off ) const
    {
        if ( hdr_off == 0 || _data == nullptr )
            return nullptr;
        return reinterpret_cast<const pam_array_hdr*>( _data + hdr_off );
    }

    // -----------------------------------------------------------------------
    // Геттеры для полей внутренних массивов (читают из pam_array_hdr в _data)
    // -----------------------------------------------------------------------

    uintptr_t _type_vec_size() const
    {
        const pam_array_hdr* h = _arr_hdr_const( _type_vec_offset );
        return h ? h->size : 0;
    }
    uintptr_t _type_vec_capacity() const
    {
        const pam_array_hdr* h = _arr_hdr_const( _type_vec_offset );
        return h ? h->capacity : 0;
    }

    uintptr_t _slot_map_size() const
    {
        const pam_array_hdr* h = _arr_hdr_const( _slot_map_offset );
        return h ? h->size : 0;
    }
    uintptr_t _slot_map_capacity() const
    {
        const pam_array_hdr* h = _arr_hdr_const( _slot_map_offset );
        return h ? h->capacity : 0;
    }

    uintptr_t _name_map_size() const
    {
        const pam_array_hdr* h = _arr_hdr_const( _name_map_offset );
        return h ? h->size : 0;
    }
    uintptr_t _name_map_capacity() const
    {
        const pam_array_hdr* h = _arr_hdr_const( _name_map_offset );
        return h ? h->capacity : 0;
    }

    uintptr_t _free_list_size() const
    {
        const pam_array_hdr* h = _arr_hdr_const( _free_list_offset );
        return h ? h->size : 0;
    }

    // -----------------------------------------------------------------------
    // Доступ к данным внутренних массивов (T* по data_off)
    // -----------------------------------------------------------------------

    TypeInfo* _type_entries()
    {
        const pam_array_hdr* h = _arr_hdr_const( _type_vec_offset );
        if ( h == nullptr || h->data_off == 0 )
            return nullptr;
        return reinterpret_cast<TypeInfo*>( _data + h->data_off );
    }
    const TypeInfo* _type_entries_const() const
    {
        const pam_array_hdr* h = _arr_hdr_const( _type_vec_offset );
        if ( h == nullptr || h->data_off == 0 )
            return nullptr;
        return reinterpret_cast<const TypeInfo*>( _data + h->data_off );
    }

    slot_entry* _slot_entries()
    {
        const pam_array_hdr* h = _arr_hdr_const( _slot_map_offset );
        if ( h == nullptr || h->data_off == 0 )
            return nullptr;
        return reinterpret_cast<slot_entry*>( _data + h->data_off );
    }
    const slot_entry* _slot_entries_const() const
    {
        const pam_array_hdr* h = _arr_hdr_const( _slot_map_offset );
        if ( h == nullptr || h->data_off == 0 )
            return nullptr;
        return reinterpret_cast<const slot_entry*>( _data + h->data_off );
    }

    name_entry* _name_entries()
    {
        const pam_array_hdr* h = _arr_hdr_const( _name_map_offset );
        if ( h == nullptr || h->data_off == 0 )
            return nullptr;
        return reinterpret_cast<name_entry*>( _data + h->data_off );
    }
    const name_entry* _name_entries_const() const
    {
        const pam_array_hdr* h = _arr_hdr_const( _name_map_offset );
        if ( h == nullptr || h->data_off == 0 )
            return nullptr;
        return reinterpret_cast<const name_entry*>( _data + h->data_off );
    }

    free_entry* _free_entries()
    {
        const pam_array_hdr* h = _arr_hdr_const( _free_list_offset );
        if ( h == nullptr || h->data_off == 0 )
            return nullptr;
        return reinterpret_cast<free_entry*>( _data + h->data_off );
    }

    // -----------------------------------------------------------------------
    // Бинарный поиск по ключу (offset) в карте слотов (O(log n))
    // -----------------------------------------------------------------------

    /// Найти индекс первой записи с ключом >= offset (lower_bound).
    uintptr_t _slot_lower_bound( uintptr_t offset ) const
    {
        const slot_entry* entries = _slot_entries_const();
        if ( entries == nullptr )
            return 0;
        uintptr_t lo = 0, hi = _slot_map_size();
        while ( lo < hi )
        {
            uintptr_t mid = ( lo + hi ) / 2;
            if ( entries[mid].key < offset )
                lo = mid + 1;
            else
                hi = mid;
        }
        return lo;
    }

    // -----------------------------------------------------------------------
    // Бинарный поиск по имени в карте имён (O(log n)) — фаза 8.3
    // -----------------------------------------------------------------------

    /// Найти индекс первой записи с ключом >= nk (lower_bound) в карте имён.
    uintptr_t _name_lower_bound( const name_key& nk ) const
    {
        const name_entry* entries = _name_entries_const();
        if ( entries == nullptr )
            return 0;
        uintptr_t lo = 0, hi = _name_map_size();
        while ( lo < hi )
        {
            uintptr_t mid = ( lo + hi ) / 2;
            if ( entries[mid].key < nk )
                lo = mid + 1;
            else
                hi = mid;
        }
        return lo;
    }

    // -----------------------------------------------------------------------
    // Конструктор и синглтон
    // -----------------------------------------------------------------------

    PersistentAddressSpace()
        : _data( nullptr ), _type_vec_offset( 0 ), _slot_map_offset( 0 ), _name_map_offset( 0 ), _free_list_offset( 0 ),
          _string_table_offset( 0 ), _bump( sizeof( pam_header ) )
    {
        _filename[0] = '\0';
    }

    static PersistentAddressSpace& _instance()
    {
        static PersistentAddressSpace _pam;
        return _pam;
    }

    // -----------------------------------------------------------------------
    // Низкоуровневое выделение памяти в области данных (без регистрации слота)
    // -----------------------------------------------------------------------

    /// Выделить size байт в области данных ПАМ без регистрации слота. 0 при ошибке.
    uintptr_t _raw_alloc( uintptr_t size, uintptr_t align )
    {
        if ( size == 0 )
            return 0;
        uintptr_t data_size    = _header().data_area_size;
        uintptr_t aligned_bump = ( _bump + align - 1 ) & ~( align - 1 );

        if ( aligned_bump + size > data_size )
        {
            uintptr_t new_size = data_size * 2;
            while ( aligned_bump + size > new_size )
                new_size *= 2;

            char* new_data = static_cast<char*>( std::realloc( _data, static_cast<std::size_t>( new_size ) ) );
            if ( new_data == nullptr )
                return 0;
            _data = new_data;
            std::memset( _data + data_size, 0, static_cast<std::size_t>( new_size - data_size ) );
            _header().data_area_size = new_size;
            data_size                = new_size;
            aligned_bump             = ( _bump + align - 1 ) & ~( align - 1 );
        }

        uintptr_t offset = aligned_bump;
        _bump            = aligned_bump + size;
        return offset;
    }

    // -----------------------------------------------------------------------
    // Унифицированный рост внутреннего массива (задача 1.5)
    // -----------------------------------------------------------------------
    //
    // _raw_grow_array<T>(hdr_off, new_cap) — вырасти буфер данных массива,
    // заголовок которого находится по смещению hdr_off.
    //
    // Алгоритм:
    //   1. Временно сохраняем old_data_off и old_size на стеке.
    //   2. Выделяем новый буфер через _raw_alloc.
    //   3. Копируем old_size элементов из старого буфера во новый.
    //   4. Обновляем hdr->capacity и hdr->data_off.
    //   (Старый буфер не освобождается — он остаётся в ПАП как неиспользуемый;
    //    это приемлемо для внутренних структур, которые никогда не уменьшаются.)
    //
    // Возвращает true при успехе, false при ошибке выделения памяти.

    template <typename T> bool _raw_grow_array( uintptr_t hdr_off, uintptr_t new_cap )
    {
        static_assert( std::is_trivially_copyable<T>::value, "_raw_grow_array<T> требует тривиально копируемый T" );
        if ( hdr_off == 0 )
            return false;

        pam_array_hdr* hdr = _arr_hdr( hdr_off );
        if ( hdr == nullptr )
            return false;
        if ( new_cap <= hdr->capacity )
            return true;

        uintptr_t old_data_off = hdr->data_off;
        uintptr_t old_size     = hdr->size;

        // Сохраняем данные на стеке (временный буфер).
        uintptr_t old_bytes = old_size * sizeof( T );
        void*     tmp       = ( old_bytes > 0 ) ? std::malloc( static_cast<std::size_t>( old_bytes ) ) : nullptr;
        if ( old_bytes > 0 && tmp == nullptr )
            return false;
        if ( tmp != nullptr && old_data_off != 0 )
            std::memcpy( tmp, _data + old_data_off, old_bytes );

        // Выделяем новый буфер в ПАП.
        uintptr_t new_off = _raw_alloc( new_cap * sizeof( T ), alignof( T ) );
        if ( new_off == 0 )
        {
            std::free( tmp );
            return false;
        }
        std::memset( _data + new_off, 0, new_cap * sizeof( T ) );

        // После _raw_alloc _data мог переместиться — повторно получаем hdr.
        hdr = _arr_hdr( hdr_off );

        // Копируем старые данные в новый буфер.
        if ( tmp != nullptr && old_size > 0 )
            std::memcpy( _data + new_off, tmp, old_bytes );
        std::free( tmp );

        hdr->data_off = new_off;
        hdr->capacity = new_cap;
        return true;
    }

    // -----------------------------------------------------------------------
    // Управление списком свободных областей (фаза 9)
    // -----------------------------------------------------------------------

    void _init_free_list()
    {
        _free_list_offset = _raw_alloc( sizeof( pam_array_hdr ), alignof( pam_array_hdr ) );
        if ( _free_list_offset == 0 )
            throw std::bad_alloc{};
        pam_array_hdr* hdr = _arr_hdr( _free_list_offset );
        hdr->size          = 0;
        hdr->capacity      = 0;
        hdr->data_off      = 0;
        uintptr_t init_cap = PAM_INITIAL_FREE_CAPACITY;
        if ( !_raw_grow_array<free_entry>( _free_list_offset, init_cap ) )
            throw std::bad_alloc{};
        _header().free_list_offset = _free_list_offset;
    }

    bool _ensure_free_list_capacity()
    {
        pam_array_hdr* hdr = _arr_hdr( _free_list_offset );
        if ( hdr == nullptr || hdr->size < hdr->capacity )
            return true;
        uintptr_t new_cap = hdr->capacity * 2;
        return _raw_grow_array<free_entry>( _free_list_offset, new_cap );
    }

    /// Добавить свободную область {off, sz} в список (несортированный, O(1) вставка).
    void _free_insert( uintptr_t off, uintptr_t sz )
    {
        if ( sz == 0 || !_ensure_free_list_capacity() )
            return;
        pam_array_hdr* hdr   = _arr_hdr( _free_list_offset );
        free_entry*    fe    = _free_entries();
        fe[hdr->size].offset = off;
        fe[hdr->size].size   = sz;
        hdr->size++;
    }

    /// Найти и изъять подходящую свободную область размером >= size.
    /// Возвращает смещение или 0, если не найдено.
    uintptr_t _free_find_and_remove( uintptr_t size )
    {
        free_entry* fe = _free_entries();
        if ( fe == nullptr )
            return 0;
        uintptr_t fl_size = _free_list_size();
        for ( uintptr_t i = 0; i < fl_size; i++ )
        {
            if ( fe[i].size >= size )
            {
                uintptr_t off = fe[i].offset;
                fe[i]         = fe[fl_size - 1]; // удаляем (swap с последним)
                _arr_hdr( _free_list_offset )->size--;
                return off;
            }
        }
        return 0;
    }

    // --- Управление вектором типов ---

    void _init_type_vec()
    {
        _type_vec_offset = _raw_alloc( sizeof( pam_array_hdr ), alignof( pam_array_hdr ) );
        if ( _type_vec_offset == 0 )
            throw std::bad_alloc{};
        pam_array_hdr* hdr = _arr_hdr( _type_vec_offset );
        hdr->size          = 0;
        hdr->capacity      = 0;
        hdr->data_off      = 0;
        if ( !_raw_grow_array<TypeInfo>( _type_vec_offset, PAM_INITIAL_TYPE_CAPACITY ) )
            throw std::bad_alloc{};
        _header().type_vec_offset = _type_vec_offset;
    }

    bool _ensure_type_vec_capacity()
    {
        pam_array_hdr* hdr = _arr_hdr( _type_vec_offset );
        if ( hdr == nullptr || hdr->size < hdr->capacity )
            return true;
        uintptr_t new_cap = hdr->capacity * 2;
        if ( new_cap < PAM_INITIAL_TYPE_CAPACITY )
            new_cap = PAM_INITIAL_TYPE_CAPACITY;
        return _raw_grow_array<TypeInfo>( _type_vec_offset, new_cap );
    }

    // --- Управление картой слотов ---

    void _init_slot_map()
    {
        _slot_map_offset = _raw_alloc( sizeof( pam_array_hdr ), alignof( pam_array_hdr ) );
        if ( _slot_map_offset == 0 )
            throw std::bad_alloc{};
        pam_array_hdr* hdr = _arr_hdr( _slot_map_offset );
        hdr->size          = 0;
        hdr->capacity      = 0;
        hdr->data_off      = 0;
        if ( !_raw_grow_array<slot_entry>( _slot_map_offset, PAM_INITIAL_SLOT_CAPACITY ) )
            throw std::bad_alloc{};
    }

    /// Зарезервировать ёмкость карты слотов не менее new_cap записей.
    bool _reserve_slot_map( uintptr_t new_cap )
    {
        if ( new_cap <= _slot_map_capacity() )
            return true;
        return _raw_grow_array<slot_entry>( _slot_map_offset, new_cap );
    }

    bool _ensure_slot_map_capacity()
    {
        pam_array_hdr* hdr = _arr_hdr( _slot_map_offset );
        if ( hdr == nullptr || hdr->size < hdr->capacity )
            return true;
        uintptr_t new_cap = hdr->capacity * 2;
        if ( new_cap < PAM_INITIAL_SLOT_CAPACITY )
            new_cap = PAM_INITIAL_SLOT_CAPACITY;
        return _reserve_slot_map( new_cap );
    }

    bool _slot_insert( uintptr_t offset, const SlotInfo& info )
    {
        if ( !_ensure_slot_map_capacity() )
            return false;
        uintptr_t   idx     = _slot_lower_bound( offset );
        slot_entry* entries = _slot_entries();
        uintptr_t   sm_size = _slot_map_size();
        for ( uintptr_t i = sm_size; i > idx; i-- )
            entries[i] = entries[i - 1];

        entries[idx].key   = offset;
        entries[idx].value = info;
        _arr_hdr( _slot_map_offset )->size++;
        return true;
    }

    // --- Управление картой имён ---

    void _init_name_map()
    {
        _name_map_offset = _raw_alloc( sizeof( pam_array_hdr ), alignof( pam_array_hdr ) );
        if ( _name_map_offset == 0 )
            throw std::bad_alloc{};
        pam_array_hdr* hdr = _arr_hdr( _name_map_offset );
        hdr->size          = 0;
        hdr->capacity      = 0;
        hdr->data_off      = 0;
        if ( !_raw_grow_array<name_entry>( _name_map_offset, PAM_INITIAL_NAME_CAPACITY ) )
            throw std::bad_alloc{};
        _header().name_map_offset = _name_map_offset;
    }

    bool _ensure_name_map_capacity()
    {
        pam_array_hdr* hdr = _arr_hdr( _name_map_offset );
        if ( hdr == nullptr || hdr->size < hdr->capacity )
            return true;
        uintptr_t new_cap = hdr->capacity * 2;
        if ( new_cap < PAM_INITIAL_NAME_CAPACITY )
            new_cap = PAM_INITIAL_NAME_CAPACITY;
        return _raw_grow_array<name_entry>( _name_map_offset, new_cap );
    }

    /// Вставить имя в карту имён (отсортировано). PAM_INVALID_IDX при ошибке/дубликате.
    uintptr_t _name_insert( const name_key& nk, uintptr_t slot_offset )
    {
        if ( !_ensure_name_map_capacity() )
            return PAM_INVALID_IDX;

        // Позиция для вставки (нижняя граница).
        uintptr_t idx = _name_lower_bound( nk );

        // Проверяем уникальность.
        if ( idx < _name_map_size() && _name_entries()[idx].key == nk )
            return PAM_INVALID_IDX; // имя занято

        // Перед сдвигом существующих элементов корректируем name_idx в карте слотов
        // (все записи с name_idx >= idx получат +1).
        _shift_name_indices_after_insert( idx );

        // Сдвигаем записи вправо.
        name_entry* entries = _name_entries();
        uintptr_t   nm_size = _name_map_size();
        for ( uintptr_t i = nm_size; i > idx; i-- )
            entries[i] = entries[i - 1];

        entries[idx].key         = nk;
        entries[idx].slot_offset = slot_offset;
        _arr_hdr( _name_map_offset )->size++;

        return idx;
    }

    /// Скорректировать name_idx в слотах (уменьшить на 1 для индексов > del_idx).
    void _shift_name_indices_after_delete( uintptr_t del_idx )
    {
        slot_entry* entries = _slot_entries();
        if ( entries == nullptr )
            return;
        uintptr_t sm_size = _slot_map_size();
        for ( uintptr_t i = 0; i < sm_size; i++ )
        {
            uintptr_t& nidx = entries[i].value.name_idx;
            if ( nidx != PAM_INVALID_IDX && nidx > del_idx )
                nidx--;
        }
    }

    /// Скорректировать name_idx в слотах (увеличить на 1 для индексов >= ins_idx).
    void _shift_name_indices_after_insert( uintptr_t ins_idx )
    {
        slot_entry* entries = _slot_entries();
        if ( entries == nullptr )
            return;
        uintptr_t sm_size = _slot_map_size();
        for ( uintptr_t i = 0; i < sm_size; i++ )
        {
            uintptr_t& nidx = entries[i].value.name_idx;
            if ( nidx != PAM_INVALID_IDX && nidx >= ins_idx )
                nidx++;
        }
    }

    // -----------------------------------------------------------------------
    // Загрузка образа из файла
    // -----------------------------------------------------------------------

    void _load( const char* filename )
    {
        std::strncpy( _filename, filename, sizeof( _filename ) - 1 );
        _filename[sizeof( _filename ) - 1] = '\0';

        if ( _data != nullptr )
        {
            std::free( _data );
            _data = nullptr;
        }
        _type_vec_offset     = 0;
        _slot_map_offset     = 0;
        _name_map_offset     = 0;
        _free_list_offset    = 0;
        _string_table_offset = 0;

        std::FILE* f = std::fopen( filename, "rb" );
        if ( f == nullptr )
        {
            // Файл не существует — создаём пустой образ.
            _init_empty();
            return;
        }

        // Считываем заголовок.
        pam_header hdr{};
        if ( std::fread( &hdr, sizeof( pam_header ), 1, f ) != 1 || hdr.magic != PAM_MAGIC ||
             hdr.version != PAM_VERSION )
        {
            std::fclose( f );
            _init_empty();
            return;
        }

        // ---- Область данных (включает вектор типов, карту слотов и карту имён) ----
        uintptr_t data_size = hdr.data_area_size;
        _data               = static_cast<char*>( std::malloc( static_cast<std::size_t>( data_size ) ) );
        if ( _data == nullptr )
        {
            std::fclose( f );
            throw std::bad_alloc{};
        }

        // Конструкторы объектов НЕ вызываются (Тр.10) — просто fread.
        std::size_t rd = std::fread( _data, 1, static_cast<std::size_t>( data_size ), f );
        if ( rd != static_cast<std::size_t>( data_size ) )
            std::memset( _data + rd, 0, static_cast<std::size_t>( data_size ) - rd );
        std::fclose( f );

        // Обновляем заголовок в буфере данных.
        _header() = hdr;

        // Восстанавливаем смещения внутренних массивов из заголовка.
        // Поля size/capacity/data_off читаются из pam_array_hdr в _data напрямую.
        _type_vec_offset     = hdr.type_vec_offset;
        _slot_map_offset     = hdr.slot_map_offset;
        _name_map_offset     = hdr.name_map_offset;
        _free_list_offset    = hdr.free_list_offset;
        _string_table_offset = hdr.string_table_offset;

        // Восстанавливаем bump из заголовка (сохранён при Save).
        _bump = hdr.bump;
        if ( _bump < sizeof( pam_header ) )
            _bump = sizeof( pam_header );
    }

    // -----------------------------------------------------------------------
    // Инициализация пустого образа
    // -----------------------------------------------------------------------

    void _init_empty()
    {
        // Выделяем буфер данных.
        uintptr_t data_size = PAM_INITIAL_DATA_SIZE;
        _data               = static_cast<char*>( std::malloc( static_cast<std::size_t>( data_size ) ) );
        if ( _data == nullptr )
            throw std::bad_alloc{};
        std::memset( _data, 0, static_cast<std::size_t>( data_size ) );

        // Записываем заголовок в начало буфера данных.
        pam_header& hdr         = _header();
        hdr.magic               = PAM_MAGIC;
        hdr.version             = PAM_VERSION;
        hdr.data_area_size      = data_size;
        hdr.type_vec_offset     = 0;
        hdr.slot_map_offset     = 0;
        hdr.name_map_offset     = 0;
        hdr.free_list_offset    = 0;
        hdr.string_table_offset = 0;
        hdr.bump                = 0;

        // Область данных начинается после заголовка.
        _bump = sizeof( pam_header );

        _init_type_vec();
        _init_slot_map();
        _header().slot_map_offset = _slot_map_offset;
        _init_name_map();
        _init_free_list();
    }

    // -----------------------------------------------------------------------
    // Поиск или регистрация типа в таблице типов
    // -----------------------------------------------------------------------

    /// Найти или зарегистрировать тип в векторе типов. PAM_INVALID_IDX при ошибке.
    unsigned _find_or_register_type( const char* type_name, uintptr_t elem_size )
    {
        // Ищем существующий тип (O(n) линейный поиск).
        TypeInfo* te      = _type_entries();
        uintptr_t tv_size = _type_vec_size();
        for ( unsigned i = 0; i < static_cast<unsigned>( tv_size ); i++ )
        {
            if ( te != nullptr && te[i].elem_size == elem_size &&
                 std::strncmp( te[i].name, type_name, PAM_TYPE_ID_SIZE ) == 0 )
                return i;
        }

        // Не найден — добавляем новую запись в конец вектора.
        if ( !_ensure_type_vec_capacity() )
            return static_cast<unsigned>( -1 );

        // После _ensure_type_vec_capacity() _data мог переместиться — обновляем указатель.
        te = _type_entries();
        if ( te == nullptr )
            return static_cast<unsigned>( -1 );

        unsigned new_idx = static_cast<unsigned>( _type_vec_size() );

        // Заполняем запись.
        TypeInfo& entry = te[new_idx];
        entry.elem_size = elem_size;
        std::strncpy( entry.name, type_name, PAM_TYPE_ID_SIZE - 1 );
        entry.name[PAM_TYPE_ID_SIZE - 1] = '\0';

        _arr_hdr( _type_vec_offset )->size++;

        return new_idx;
    }

    // -----------------------------------------------------------------------
    // Аллокатор области данных
    // -----------------------------------------------------------------------

    /// Выделить elem_size*count байт, зарегистрировать слот и (опционально) имя. 0 при ошибке.
    uintptr_t _alloc( uintptr_t elem_size, uintptr_t count, const char* type_id, const char* name )
    {
        uintptr_t total_size = elem_size * count;

        // Сначала ищем подходящую область в списке свободных (повторное использование).
        uintptr_t offset = _free_find_and_remove( total_size );
        if ( offset != 0 )
        {
            // Обнуляем повторно используемую область.
            std::memset( _data + offset, 0, static_cast<std::size_t>( total_size ) );
        }
        else
        {
            // Не нашли — выделяем bump-allocation.
            uintptr_t data_size    = _header().data_area_size;
            uintptr_t align        = elem_size >= 8 ? 8u : elem_size >= 4 ? 4u : elem_size >= 2 ? 2u : 1u;
            uintptr_t aligned_bump = ( _bump + align - 1 ) & ~( align - 1 );
            if ( aligned_bump + total_size > data_size )
            {
                uintptr_t new_size = data_size * 2;
                while ( aligned_bump + total_size > new_size )
                    new_size *= 2;
                char* nd = static_cast<char*>( std::realloc( _data, static_cast<std::size_t>( new_size ) ) );
                if ( nd == nullptr )
                    return 0;
                _data = nd;
                std::memset( _data + data_size, 0, static_cast<std::size_t>( new_size - data_size ) );
                _header().data_area_size = new_size;
                aligned_bump             = ( _bump + align - 1 ) & ~( align - 1 );
            }
            offset = aligned_bump;
            _bump  = aligned_bump + total_size;
        }

        // Регистрируем тип (один раз для всех слотов этого типа).
        unsigned type_idx = _find_or_register_type( type_id, elem_size );
        if ( type_idx == static_cast<unsigned>( -1 ) )
            return 0;

        bool      named = ( name != nullptr && name[0] != '\0' );
        uintptr_t nidx  = PAM_INVALID_IDX;
        if ( named )
        {
            name_key nk{};
            std::strncpy( nk.name, name, PAM_NAME_SIZE - 1 );
            nidx = _name_insert( nk, offset );
            if ( nidx == PAM_INVALID_IDX )
                return 0; // имя занято
        }

        SlotInfo info;
        info.count    = count;
        info.type_idx = static_cast<uintptr_t>( type_idx );
        info.name_idx = nidx;

        if ( !_slot_insert( offset, info ) )
        {
            if ( named && nidx != PAM_INVALID_IDX && nidx < _name_map_size() )
            {
                name_entry* entries = _name_entries();
                if ( entries != nullptr )
                {
                    _shift_name_indices_after_delete( nidx );
                    uintptr_t nm_size = _name_map_size();
                    for ( uintptr_t i = nidx; i + 1 < nm_size; i++ )
                        entries[i] = entries[i + 1];
                    _arr_hdr( _name_map_offset )->size--;
                }
            }
            return 0;
        }

        return offset;
    }
};
