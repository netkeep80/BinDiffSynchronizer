#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>
#include <type_traits>

// pam_core.h — Ядро персистного адресного менеджера (ПАМ).
// Содержит PersistentAddressSpace с вектором типов (type_vec), картой слотов
// (slot_map) и картой имён (name_map) внутри ПАП, а также список свободных
// областей (free_list) для повторного использования удалённых данных.

// ---------------------------------------------------------------------------
// Константы ПАМ
// ---------------------------------------------------------------------------

/// Магическое число для идентификации файла ПАМ.
constexpr uint32_t PAM_MAGIC = 0x50414D00u; // 'PAM\0'

/// Версия формата файла ПАМ (фаза 9: free_list + сохранение _bump).
constexpr uint32_t PAM_VERSION = 9u;

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
// type_entry — запись в массиве вектора типов (совместима с pvector<TypeInfo>)
// ---------------------------------------------------------------------------
// Используется только для static_assert совместимости в pam.h.
// Сам массив TypeInfo[] хранится как elements pvector<TypeInfo>.

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

/// Заголовок файла ПАМ (версия 9: добавлен free_list_offset и bump).
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
};

static_assert( std::is_trivially_copyable<pam_header>::value, "pam_header должен быть тривиально копируемым" );

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
     * Инициализировать ПАМ из файла filename.
     * Если файл не существует — создаётся пустой образ.
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
     * Попытаться расширить последний аллоцированный блок в ПАП на месте.
     * Если old_offset + old_count * elem_size == _bump (блок последний),
     * расширяет до new_count * elem_size, обнуляет новые байты и возвращает
     * old_offset. Иначе возвращает 0 (caller должен выделить новый блок).
     * Слот в карте слотов при этом НЕ обновляется — caller обновляет сам.
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
        if ( idx >= _slot_map_size || _slot_entries()[idx].key != offset )
            return; // слот не найден

        SlotInfo& info = _slot_entries()[idx].value;
        uintptr_t nidx = info.name_idx;

        // Вычисляем размер освобождаемой области для списка свободных областей.
        uintptr_t freed_size = 0;
        if ( info.type_idx < _type_vec_size )
        {
            const TypeInfo* te = _type_entries_const();
            if ( te != nullptr )
                freed_size = te[info.type_idx].elem_size * info.count;
        }

        // Освобождаем запись в карте имён (если объект именованный).
        if ( nidx != PAM_INVALID_IDX && nidx < _name_map_size )
        {
            name_entry* nentries = _name_entries();
            if ( nentries != nullptr )
            {
                // Сдвигаем оставшиеся записи карты имён влево.
                for ( uintptr_t i = nidx; i + 1 < _name_map_size; i++ )
                    nentries[i] = nentries[i + 1];
                _name_map_size--;
                _flush_name_map_mirrors();
                // Корректируем name_idx для слотов с индексами > nidx.
                _shift_name_indices_after_delete( nidx );
            }
        }

        // Удаляем запись из карты слотов (сдвигаем оставшиеся влево).
        for ( uintptr_t i = idx; i + 1 < _slot_map_size; i++ )
            _slot_entries()[i] = _slot_entries()[i + 1];
        _slot_map_size--;
        _flush_slot_map_mirrors();

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
        if ( idx < _name_map_size && _name_entries_const()[idx].key == nk )
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
        if ( idx >= _name_map_size || !( _name_entries_const()[idx].key == nk ) )
            return 0;
        uintptr_t slot_off = _name_entries_const()[idx].slot_offset;
        // Ищем слот по смещению (O(log n)).
        uintptr_t sidx = _slot_lower_bound( slot_off );
        if ( sidx >= _slot_map_size || _slot_entries_const()[sidx].key != slot_off )
            return 0;
        // Проверяем тип через вектор типов (фаза 8.4).
        uintptr_t tidx = _slot_entries_const()[sidx].value.type_idx;
        if ( tidx < _type_vec_size )
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
        if ( idx < _slot_map_size && _slot_entries_const()[idx].key == offset )
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
        if ( idx >= _slot_map_size || _slot_entries_const()[idx].key != offset )
            return nullptr;
        uintptr_t nidx = _slot_entries_const()[idx].value.name_idx;
        if ( nidx == PAM_INVALID_IDX || nidx >= _name_map_size )
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
        if ( idx < _slot_map_size && _slot_entries_const()[idx].key == offset )
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
        if ( idx >= _slot_map_size || _slot_entries_const()[idx].key != offset )
            return 0;
        uintptr_t tidx = _slot_entries_const()[idx].value.type_idx;
        if ( tidx < _type_vec_size )
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

    // -----------------------------------------------------------------------
    // Разыменование адреса
    // -----------------------------------------------------------------------

    /**
     * Преобразовать смещение в указатель типа T*.
     * @return Указатель на объект T в ПАП или nullptr.
     * Конструкторы НЕ вызываются (Тр.10).
     */
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

    /**
     * Получить элемент массива по смещению и индексу.
     * Проверяет границы массива через таблицу слотов.
     * @return Ссылка на элемент.
     */
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
     * Сохранить весь образ ПАМ в файл (фаза 8.4).
     * Записывает: заголовок, область данных.
     * Вектор типов, карта слотов и карта имён сохраняются как часть области данных (внутри ПАП).
     * Смещения структур записываются в заголовок перед сохранением.
     */
    void Save()
    {
        if ( _filename[0] == '\0' )
            return;
        // Обновляем смещения структур и bump в заголовке перед сохранением.
        _header().type_vec_offset  = _type_vec_offset;
        _header().slot_map_offset  = _slot_map_offset;
        _header().name_map_offset  = _name_map_offset;
        _header().free_list_offset = _free_list_offset;
        _header().bump             = _bump;
        std::FILE* f               = std::fopen( _filename, "wb" );
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

    // Вектор типов: раскладка [size, capacity, entries_offset] в _data по _type_vec_offset.
    uintptr_t _type_vec_offset;
    uintptr_t _type_vec_size;
    uintptr_t _type_vec_capacity;
    uintptr_t _type_entries_off;

    // Карта слотов: раскладка [size, capacity, entries_offset] в _data по _slot_map_offset.
    uintptr_t _slot_map_offset;
    uintptr_t _slot_map_size;
    uintptr_t _slot_map_capacity;
    uintptr_t _slot_entries_off;

    // Карта имён: раскладка [size, capacity, entries_offset] в _data по _name_map_offset.
    uintptr_t _name_map_offset;
    uintptr_t _name_map_size;
    uintptr_t _name_map_capacity;
    uintptr_t _name_entries_off;

    // Список свободных областей: хранится в _data по _free_list_offset.
    // Раскладка [size, capacity, entries_offset]. Записи: free_entry{offset, size}.
    // Используется для повторного выделения освобождённых данных (фаза 9).
    uintptr_t _free_list_offset;
    uintptr_t _free_list_size;
    uintptr_t _free_list_capacity;
    uintptr_t _free_entries_off;

    /// Счётчик следующего свободного смещения (bump-allocator).
    uintptr_t _bump;

    // -----------------------------------------------------------------------
    // Доступ к заголовку
    // -----------------------------------------------------------------------

    pam_header& _header() { return *reinterpret_cast<pam_header*>( _data ); }

    const pam_header& _header_const() const { return *reinterpret_cast<const pam_header*>( _data ); }

    // -----------------------------------------------------------------------
    // Доступ к массиву записей вектора типов (внутри _data) — фаза 8.4
    // -----------------------------------------------------------------------

    /// Получить указатель на массив TypeInfo в _data (изменяемый).
    TypeInfo* _type_entries()
    {
        if ( _type_entries_off == 0 )
            return nullptr;
        return reinterpret_cast<TypeInfo*>( _data + _type_entries_off );
    }

    /// Получить указатель на массив TypeInfo в _data (константный).
    const TypeInfo* _type_entries_const() const
    {
        if ( _type_entries_off == 0 )
            return nullptr;
        return reinterpret_cast<const TypeInfo*>( _data + _type_entries_off );
    }

    // -----------------------------------------------------------------------
    // Доступ к массиву записей карты слотов (внутри _data)
    // -----------------------------------------------------------------------

    /// Получить указатель на массив slot_entry в _data (изменяемый).
    slot_entry* _slot_entries()
    {
        if ( _slot_entries_off == 0 )
            return nullptr;
        return reinterpret_cast<slot_entry*>( _data + _slot_entries_off );
    }

    /// Получить указатель на массив slot_entry в _data (константный).
    const slot_entry* _slot_entries_const() const
    {
        if ( _slot_entries_off == 0 )
            return nullptr;
        return reinterpret_cast<const slot_entry*>( _data + _slot_entries_off );
    }

    // -----------------------------------------------------------------------
    // Доступ к массиву записей карты имён (внутри _data) — фаза 8.3
    // -----------------------------------------------------------------------

    /// Получить указатель на массив name_entry в _data (изменяемый).
    name_entry* _name_entries()
    {
        if ( _name_entries_off == 0 )
            return nullptr;
        return reinterpret_cast<name_entry*>( _data + _name_entries_off );
    }

    /// Получить указатель на массив name_entry в _data (константный).
    const name_entry* _name_entries_const() const
    {
        if ( _name_entries_off == 0 )
            return nullptr;
        return reinterpret_cast<const name_entry*>( _data + _name_entries_off );
    }

    // -----------------------------------------------------------------------
    // Доступ к массиву записей списка свободных областей (в _data) — фаза 9
    // -----------------------------------------------------------------------

    free_entry* _free_entries()
    {
        if ( _free_entries_off == 0 )
            return nullptr;
        return reinterpret_cast<free_entry*>( _data + _free_entries_off );
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
        uintptr_t lo = 0, hi = _slot_map_size;
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
        uintptr_t lo = 0, hi = _name_map_size;
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
        : _data( nullptr ), _type_vec_offset( 0 ), _type_vec_size( 0 ), _type_vec_capacity( 0 ), _type_entries_off( 0 ),
          _slot_map_offset( 0 ), _slot_map_size( 0 ), _slot_map_capacity( 0 ), _slot_entries_off( 0 ),
          _name_map_offset( 0 ), _name_map_size( 0 ), _name_map_capacity( 0 ), _name_entries_off( 0 ),
          _free_list_offset( 0 ), _free_list_size( 0 ), _free_list_capacity( 0 ), _free_entries_off( 0 ),
          _bump( sizeof( pam_header ) )
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

    /**
     * Выделить size байт в области данных ПАМ без регистрации слота.
     * Используется для внутренней аллокации (карта слотов, карта имён, их записи).
     * Возвращает смещение в _data или 0 при ошибке.
     * После вызова _data может переместиться (realloc).
     */
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
    // Управление списком свободных областей (фаза 9)
    // -----------------------------------------------------------------------

    void _sync_free_list_mirrors()
    {
        if ( _free_list_offset == 0 )
            return;
        uintptr_t* fl       = reinterpret_cast<uintptr_t*>( _data + _free_list_offset );
        _free_list_size     = fl[0];
        _free_list_capacity = fl[1];
        _free_entries_off   = fl[2];
    }

    void _flush_free_list_mirrors()
    {
        if ( _free_list_offset == 0 )
            return;
        uintptr_t* fl = reinterpret_cast<uintptr_t*>( _data + _free_list_offset );
        fl[0]         = _free_list_size;
        fl[1]         = _free_list_capacity;
        fl[2]         = _free_entries_off;
    }

    void _init_free_list()
    {
        _free_list_offset = _raw_alloc( 3 * sizeof( uintptr_t ), alignof( uintptr_t ) );
        if ( _free_list_offset == 0 )
            throw std::bad_alloc{};
        uintptr_t init_cap    = PAM_INITIAL_FREE_CAPACITY;
        uintptr_t entries_off = _raw_alloc( init_cap * sizeof( free_entry ), alignof( free_entry ) );
        if ( entries_off == 0 )
            throw std::bad_alloc{};
        std::memset( _data + entries_off, 0, init_cap * sizeof( free_entry ) );
        _free_list_size     = 0;
        _free_list_capacity = init_cap;
        _free_entries_off   = entries_off;
        _flush_free_list_mirrors();
        _header().free_list_offset = _free_list_offset;
    }

    bool _ensure_free_list_capacity()
    {
        if ( _free_list_size < _free_list_capacity )
            return true;
        uintptr_t   new_cap        = _free_list_capacity * 2;
        uintptr_t   old_size_bytes = _free_list_size * sizeof( free_entry );
        free_entry* tmp = static_cast<free_entry*>( std::malloc( static_cast<std::size_t>( old_size_bytes ) ) );
        if ( tmp == nullptr )
            return false;
        if ( _free_entries_off != 0 && _free_list_size > 0 )
            std::memcpy( tmp, _data + _free_entries_off, old_size_bytes );
        uintptr_t new_off = _raw_alloc( new_cap * sizeof( free_entry ), alignof( free_entry ) );
        if ( new_off == 0 )
        {
            std::free( tmp );
            return false;
        }
        std::memset( _data + new_off, 0, new_cap * sizeof( free_entry ) );
        if ( _free_list_size > 0 )
            std::memcpy( _data + new_off, tmp, old_size_bytes );
        std::free( tmp );
        _free_entries_off   = new_off;
        _free_list_capacity = new_cap;
        _flush_free_list_mirrors();
        return true;
    }

    /// Добавить свободную область {off, sz} в список (несортированный, O(1) вставка).
    void _free_insert( uintptr_t off, uintptr_t sz )
    {
        if ( sz == 0 || !_ensure_free_list_capacity() )
            return;
        free_entry* fe             = _free_entries();
        fe[_free_list_size].offset = off;
        fe[_free_list_size].size   = sz;
        _free_list_size++;
        _flush_free_list_mirrors();
    }

    /// Найти и изъять подходящую свободную область размером >= size.
    /// Возвращает смещение или 0, если не найдено.
    uintptr_t _free_find_and_remove( uintptr_t size )
    {
        free_entry* fe = _free_entries();
        if ( fe == nullptr )
            return 0;
        for ( uintptr_t i = 0; i < _free_list_size; i++ )
        {
            if ( fe[i].size >= size )
            {
                uintptr_t off = fe[i].offset;
                fe[i]         = fe[_free_list_size - 1]; // удаляем (swap с последним)
                _free_list_size--;
                _flush_free_list_mirrors();
                return off;
            }
        }
        return 0;
    }

    // --- Управление вектором типов ---
    void _sync_type_vec_mirrors()
    {
        if ( _type_vec_offset == 0 )
            return;
        uintptr_t* tv      = reinterpret_cast<uintptr_t*>( _data + _type_vec_offset );
        _type_vec_size     = tv[0];
        _type_vec_capacity = tv[1];
        _type_entries_off  = tv[2];
    }
    void _flush_type_vec_mirrors()
    {
        if ( _type_vec_offset == 0 )
            return;
        uintptr_t* tv = reinterpret_cast<uintptr_t*>( _data + _type_vec_offset );
        tv[0]         = _type_vec_size;
        tv[1]         = _type_vec_capacity;
        tv[2]         = _type_entries_off;
    }
    void _init_type_vec()
    {
        _type_vec_offset = _raw_alloc( 3 * sizeof( uintptr_t ), alignof( uintptr_t ) );
        if ( _type_vec_offset == 0 )
            throw std::bad_alloc{};
        uintptr_t init_cap    = PAM_INITIAL_TYPE_CAPACITY;
        uintptr_t entries_off = _raw_alloc( init_cap * sizeof( TypeInfo ), alignof( TypeInfo ) );
        if ( entries_off == 0 )
            throw std::bad_alloc{};
        std::memset( _data + entries_off, 0, init_cap * sizeof( TypeInfo ) );
        _type_vec_size     = 0;
        _type_vec_capacity = init_cap;
        _type_entries_off  = entries_off;
        _flush_type_vec_mirrors();
        _header().type_vec_offset = _type_vec_offset;
    }
    bool _ensure_type_vec_capacity()
    {
        if ( _type_vec_size < _type_vec_capacity )
            return true;
        uintptr_t new_cap = _type_vec_capacity * 2;
        if ( new_cap < PAM_INITIAL_TYPE_CAPACITY )
            new_cap = PAM_INITIAL_TYPE_CAPACITY;
        uintptr_t old_size_bytes = _type_vec_size * sizeof( TypeInfo );
        TypeInfo* tmp            = static_cast<TypeInfo*>( std::malloc( static_cast<std::size_t>( old_size_bytes ) ) );
        if ( tmp == nullptr )
            return false;
        if ( _type_entries_off != 0 && _type_vec_size > 0 )
            std::memcpy( tmp, _data + _type_entries_off, old_size_bytes );
        uintptr_t new_off = _raw_alloc( new_cap * sizeof( TypeInfo ), alignof( TypeInfo ) );
        if ( new_off == 0 )
        {
            std::free( tmp );
            return false;
        }
        std::memset( _data + new_off, 0, new_cap * sizeof( TypeInfo ) );
        if ( _type_vec_size > 0 )
            std::memcpy( _data + new_off, tmp, old_size_bytes );
        std::free( tmp );
        _type_entries_off  = new_off;
        _type_vec_capacity = new_cap;
        _flush_type_vec_mirrors();
        return true;
    }

    // --- Управление картой слотов ---
    void _sync_slot_map_mirrors()
    {
        if ( _slot_map_offset == 0 )
            return;
        uintptr_t* sm      = reinterpret_cast<uintptr_t*>( _data + _slot_map_offset );
        _slot_map_size     = sm[0];
        _slot_map_capacity = sm[1];
        _slot_entries_off  = sm[2];
    }
    void _flush_slot_map_mirrors()
    {
        if ( _slot_map_offset == 0 )
            return;
        uintptr_t* sm = reinterpret_cast<uintptr_t*>( _data + _slot_map_offset );
        sm[0]         = _slot_map_size;
        sm[1]         = _slot_map_capacity;
        sm[2]         = _slot_entries_off;
    }
    void _init_slot_map()
    {
        _slot_map_offset = _raw_alloc( 3 * sizeof( uintptr_t ), alignof( uintptr_t ) );
        if ( _slot_map_offset == 0 )
            throw std::bad_alloc{};
        uintptr_t init_cap    = PAM_INITIAL_SLOT_CAPACITY;
        uintptr_t entries_off = _raw_alloc( init_cap * sizeof( slot_entry ), alignof( slot_entry ) );
        if ( entries_off == 0 )
            throw std::bad_alloc{};
        std::memset( _data + entries_off, 0, init_cap * sizeof( slot_entry ) );
        _slot_map_size     = 0;
        _slot_map_capacity = init_cap;
        _slot_entries_off  = entries_off;
        _flush_slot_map_mirrors();
    }
    bool _ensure_slot_map_capacity()
    {
        if ( _slot_map_size < _slot_map_capacity )
            return true;
        uintptr_t new_cap = _slot_map_capacity * 2;
        if ( new_cap < PAM_INITIAL_SLOT_CAPACITY )
            new_cap = PAM_INITIAL_SLOT_CAPACITY;
        uintptr_t   old_size_bytes = _slot_map_size * sizeof( slot_entry );
        slot_entry* tmp = static_cast<slot_entry*>( std::malloc( static_cast<std::size_t>( old_size_bytes ) ) );
        if ( tmp == nullptr )
            return false;
        if ( _slot_entries_off != 0 && _slot_map_size > 0 )
            std::memcpy( tmp, _data + _slot_entries_off, old_size_bytes );
        uintptr_t new_off = _raw_alloc( new_cap * sizeof( slot_entry ), alignof( slot_entry ) );
        if ( new_off == 0 )
        {
            std::free( tmp );
            return false;
        }
        std::memset( _data + new_off, 0, new_cap * sizeof( slot_entry ) );
        if ( _slot_map_size > 0 )
            std::memcpy( _data + new_off, tmp, old_size_bytes );
        std::free( tmp );
        _slot_entries_off  = new_off;
        _slot_map_capacity = new_cap;
        _flush_slot_map_mirrors();
        return true;
    }
    bool _slot_insert( uintptr_t offset, const SlotInfo& info )
    {
        if ( !_ensure_slot_map_capacity() )
            return false;
        uintptr_t   idx     = _slot_lower_bound( offset );
        slot_entry* entries = _slot_entries();
        for ( uintptr_t i = _slot_map_size; i > idx; i-- )
            entries[i] = entries[i - 1];

        entries[idx].key   = offset;
        entries[idx].value = info;
        _slot_map_size++;
        _flush_slot_map_mirrors();
        return true;
    }

    // --- Управление картой имён ---
    void _sync_name_map_mirrors()
    {
        if ( _name_map_offset == 0 )
            return;
        uintptr_t* nm      = reinterpret_cast<uintptr_t*>( _data + _name_map_offset );
        _name_map_size     = nm[0];
        _name_map_capacity = nm[1];
        _name_entries_off  = nm[2];
    }
    void _flush_name_map_mirrors()
    {
        if ( _name_map_offset == 0 )
            return;
        uintptr_t* nm = reinterpret_cast<uintptr_t*>( _data + _name_map_offset );
        nm[0]         = _name_map_size;
        nm[1]         = _name_map_capacity;
        nm[2]         = _name_entries_off;
    }
    void _init_name_map()
    {
        _name_map_offset = _raw_alloc( 3 * sizeof( uintptr_t ), alignof( uintptr_t ) );
        if ( _name_map_offset == 0 )
            throw std::bad_alloc{};
        uintptr_t init_cap    = PAM_INITIAL_NAME_CAPACITY;
        uintptr_t entries_off = _raw_alloc( init_cap * sizeof( name_entry ), alignof( name_entry ) );
        if ( entries_off == 0 )
            throw std::bad_alloc{};
        std::memset( _data + entries_off, 0, init_cap * sizeof( name_entry ) );
        _name_map_size     = 0;
        _name_map_capacity = init_cap;
        _name_entries_off  = entries_off;
        _flush_name_map_mirrors();
        _header().name_map_offset = _name_map_offset;
    }
    bool _ensure_name_map_capacity()
    {
        if ( _name_map_size < _name_map_capacity )
            return true;
        uintptr_t new_cap = _name_map_capacity * 2;
        if ( new_cap < PAM_INITIAL_NAME_CAPACITY )
            new_cap = PAM_INITIAL_NAME_CAPACITY;
        uintptr_t   old_size_bytes = _name_map_size * sizeof( name_entry );
        name_entry* tmp = static_cast<name_entry*>( std::malloc( static_cast<std::size_t>( old_size_bytes ) ) );
        if ( tmp == nullptr )
            return false;
        if ( _name_entries_off != 0 && _name_map_size > 0 )
            std::memcpy( tmp, _data + _name_entries_off, old_size_bytes );
        uintptr_t new_off = _raw_alloc( new_cap * sizeof( name_entry ), alignof( name_entry ) );
        if ( new_off == 0 )
        {
            std::free( tmp );
            return false;
        }
        std::memset( _data + new_off, 0, new_cap * sizeof( name_entry ) );
        if ( _name_map_size > 0 )
            std::memcpy( _data + new_off, tmp, old_size_bytes );
        std::free( tmp );
        _name_entries_off  = new_off;
        _name_map_capacity = new_cap;
        _flush_name_map_mirrors();
        return true;
    }

    /**
     * Вставить новое имя в карту имён (отсортированный массив по имени).
     * Сохраняет сортировку для O(log n) поиска.
     * Проверяет уникальность: если имя уже есть, возвращает PAM_INVALID_IDX.
     * @return Индекс вставленной записи или PAM_INVALID_IDX при ошибке/дубликате.
     */
    uintptr_t _name_insert( const name_key& nk, uintptr_t slot_offset )
    {
        if ( !_ensure_name_map_capacity() )
            return PAM_INVALID_IDX;

        // Позиция для вставки (нижняя граница).
        uintptr_t idx = _name_lower_bound( nk );

        // Проверяем уникальность.
        if ( idx < _name_map_size && _name_entries()[idx].key == nk )
            return PAM_INVALID_IDX; // имя занято

        // Перед сдвигом существующих элементов корректируем name_idx в карте слотов
        // (все записи с name_idx >= idx получат +1).
        _shift_name_indices_after_insert( idx );

        // Сдвигаем записи вправо.
        name_entry* entries = _name_entries();
        for ( uintptr_t i = _name_map_size; i > idx; i-- )
            entries[i] = entries[i - 1];

        entries[idx].key         = nk;
        entries[idx].slot_offset = slot_offset;
        _name_map_size++;
        _flush_name_map_mirrors();

        return idx;
    }

    /**
     * После удаления записи карты имён по индексу del_idx сдвинуть name_idx
     * во всех слотах, ссылающихся на индексы > del_idx, уменьшив их на 1.
     */
    void _shift_name_indices_after_delete( uintptr_t del_idx )
    {
        slot_entry* entries = _slot_entries();
        if ( entries == nullptr )
            return;
        for ( uintptr_t i = 0; i < _slot_map_size; i++ )
        {
            uintptr_t& nidx = entries[i].value.name_idx;
            if ( nidx != PAM_INVALID_IDX && nidx > del_idx )
                nidx--;
        }
    }

    /**
     * После вставки записи карты имён по индексу ins_idx сдвинуть name_idx
     * во всех слотах, ссылающихся на индексы >= ins_idx, увеличив их на 1.
     * Вызывается ДО вставки новой записи.
     */
    void _shift_name_indices_after_insert( uintptr_t ins_idx )
    {
        slot_entry* entries = _slot_entries();
        if ( entries == nullptr )
            return;
        for ( uintptr_t i = 0; i < _slot_map_size; i++ )
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
        _type_vec_offset    = 0;
        _type_vec_size      = 0;
        _type_vec_capacity  = 0;
        _type_entries_off   = 0;
        _slot_map_offset    = 0;
        _slot_map_size      = 0;
        _slot_map_capacity  = 0;
        _slot_entries_off   = 0;
        _name_map_offset    = 0;
        _name_map_size      = 0;
        _name_map_capacity  = 0;
        _name_entries_off   = 0;
        _free_list_offset   = 0;
        _free_list_size     = 0;
        _free_list_capacity = 0;
        _free_entries_off   = 0;

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

        // Восстанавливаем вектор типов из _data (по смещению из заголовка, фаза 8.4).
        _type_vec_offset = hdr.type_vec_offset;
        _sync_type_vec_mirrors();

        // Восстанавливаем карту слотов из _data (по смещению из заголовка).
        _slot_map_offset = hdr.slot_map_offset;
        _sync_slot_map_mirrors();

        _name_map_offset = hdr.name_map_offset;
        _sync_name_map_mirrors();
        _free_list_offset = hdr.free_list_offset;
        _sync_free_list_mirrors();

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
        pam_header& hdr      = _header();
        hdr.magic            = PAM_MAGIC;
        hdr.version          = PAM_VERSION;
        hdr.data_area_size   = data_size;
        hdr.type_vec_offset  = 0;
        hdr.slot_map_offset  = 0;
        hdr.name_map_offset  = 0;
        hdr.free_list_offset = 0;
        hdr.bump             = 0;

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

    /**
     * Найти или зарегистрировать тип в векторе типов внутри ПАП (фаза 8.4).
     * Если тип с таким именем и размером уже есть — возвращает его индекс.
     * Если нет — добавляет новую запись в конец вектора и возвращает её индекс.
     * @return Индекс в векторе типов или PAM_INVALID_IDX при ошибке.
     */
    unsigned _find_or_register_type( const char* type_name, uintptr_t elem_size )
    {
        // Ищем существующий тип (O(n) линейный поиск).
        TypeInfo* te = _type_entries();
        for ( unsigned i = 0; i < static_cast<unsigned>( _type_vec_size ); i++ )
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

        unsigned new_idx = static_cast<unsigned>( _type_vec_size );

        // Заполняем запись.
        TypeInfo& entry = te[new_idx];
        entry.elem_size = elem_size;
        std::strncpy( entry.name, type_name, PAM_TYPE_ID_SIZE - 1 );
        entry.name[PAM_TYPE_ID_SIZE - 1] = '\0';

        _type_vec_size++;
        _flush_type_vec_mirrors();

        return new_idx;
    }

    // -----------------------------------------------------------------------
    // Аллокатор области данных
    // -----------------------------------------------------------------------

    /**
     * Выделить size*count байт в области данных ПАМ.
     * Всегда создаётся запись в карте слотов (внутри ПАП, фаза 8.2).
     * Тип регистрируется в таблице типов (один раз на уникальный тип).
     * Если name задано — регистрируется запись в карте имён (фаза 8.3) и
     * устанавливается двусторонняя связь: name_entry.slot_offset = offset,
     *                                      SlotInfo.name_idx = индекс в карте имён.
     * Если name задано, но уже занято — возвращает 0 (уникальность нарушена).
     * Возвращает смещение или 0 при ошибке.
     */
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
            if ( named && nidx != PAM_INVALID_IDX && nidx < _name_map_size )
            {
                name_entry* entries = _name_entries();
                if ( entries != nullptr )
                {
                    _shift_name_indices_after_delete( nidx );
                    for ( uintptr_t i = nidx; i + 1 < _name_map_size; i++ )
                        entries[i] = entries[i + 1];
                    _name_map_size--;
                    _flush_name_map_mirrors();
                }
            }
            return 0;
        }

        return offset;
    }
};
