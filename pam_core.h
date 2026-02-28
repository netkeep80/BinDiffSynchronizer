#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>
#include <type_traits>

/*
 * pam_core.h — Ядро персистного адресного менеджера (ПАМ), фаза 8.2.
 *
 * Содержит полный публичный API PersistentAddressSpace (Create, Delete, Find,
 * Resolve, PtrToOffset, Save, ...) с реализацией таблицы слотов внутри ПАП
 * в виде персистной отсортированной карты (pmap-совместимая раскладка, фаза 8.2).
 *
 * Разделение pam.h → pam_core.h + pam.h устраняет циклическую зависимость:
 *
 *   Прежняя цепочка (цикл!):
 *     pam.h ← persist.h ← pvector.h ← pmap.h ← (хотим использовать в pam.h)
 *
 *   Новая цепочка (без цикла):
 *     pam_core.h ← persist.h ← pvector.h ← pmap.h ← pam.h (включает оба)
 *
 * Типы top-level (используются в тестах и коде):
 *   type_info_entry, name_info_entry, slot_descriptor, SlotInfo, pam_header
 *
 * Константы top-level:
 *   PAM_MAGIC, PAM_VERSION, PAM_INVALID_IDX, PAM_INITIAL_DATA_SIZE,
 *   PAM_INITIAL_SLOT_CAPACITY, PAM_INITIAL_TYPE_CAPACITY,
 *   PAM_INITIAL_NAME_CAPACITY, PAM_TYPE_ID_SIZE, PAM_NAME_SIZE
 *
 * Структура файла ПАМ (фаза 8.2, версия 5):
 *   [pam_header]                  — заголовок
 *   [type_info_entry * type_cap]  — таблица типов
 *   [name_info_entry * name_cap]  — таблица имён
 *   [байты объектов]              — область данных (включает карту слотов)
 *
 * Карта слотов (slot_map) хранится ВНУТРИ области данных ПАП:
 *   — раскладка совместима с pmap<uintptr_t, SlotInfo> (фаза 8.2)
 *   — объект карты: [size, capacity, entries_offset] по _slot_map_offset
 *   — записи карты: отсортированный массив slot_entry{offset, SlotInfo}
 *   — поиск O(log n) бинарным поиском
 */

// ---------------------------------------------------------------------------
// Константы ПАМ
// ---------------------------------------------------------------------------

/// Магическое число для идентификации файла ПАМ.
constexpr uint32_t PAM_MAGIC = 0x50414D00u; // 'PAM\0'

/// Версия формата файла ПАМ (фаза 8.2: карта слотов внутри ПАП).
constexpr uint32_t PAM_VERSION = 5u;

/// Максимальная длина идентификатора типа в таблице типов (хранится один раз).
constexpr unsigned PAM_TYPE_ID_SIZE = 64u;

/// Максимальная длина имени объекта в таблице имён (хранится один раз на объект).
constexpr unsigned PAM_NAME_SIZE = 64u;

/// Специальный индекс, означающий «нет имени» или «нет слота».
constexpr uintptr_t PAM_INVALID_IDX = static_cast<uintptr_t>( -1 );

/// Начальный размер области данных ПАМ (байт) — 10 КБ.
constexpr uintptr_t PAM_INITIAL_DATA_SIZE = 10u * 1024u;

/// Начальная ёмкость таблицы именованных слотов (число слотов).
constexpr unsigned PAM_INITIAL_SLOT_CAPACITY = 16u;

/// Начальная ёмкость таблицы типов (число уникальных типов).
constexpr unsigned PAM_INITIAL_TYPE_CAPACITY = 16u;

/// Начальная ёмкость таблицы имён (число уникальных имён объектов).
constexpr unsigned PAM_INITIAL_NAME_CAPACITY = 16u;

// ---------------------------------------------------------------------------
// type_info_entry — запись таблицы типов ПАМ
// ---------------------------------------------------------------------------

/// Запись таблицы типов — хранит имя типа и размер элемента один раз для всех
/// слотов этого типа. Все поля фиксированного размера, тривиально копируемы.
struct type_info_entry
{
    bool      used;                   ///< Запись занята
    uintptr_t elem_size;              ///< Размер одного элемента типа в байтах
    char      name[PAM_TYPE_ID_SIZE]; ///< Имя типа (из typeid(T).name())
};

static_assert( std::is_trivially_copyable<type_info_entry>::value,
               "type_info_entry должен быть тривиально копируемым" );

// ---------------------------------------------------------------------------
// name_info_entry — запись таблицы имён ПАМ
// ---------------------------------------------------------------------------

/// Запись таблицы имён — хранит имя объекта и смещение его слота в ПАП.
/// Все поля фиксированного размера, тривиально копируемы.
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
    uintptr_t name_idx; ///< Индекс в таблице name_info_entry; PAM_INVALID_IDX для безымянных
};

static_assert( std::is_trivially_copyable<SlotInfo>::value, "SlotInfo должен быть тривиально копируемым" );

// ---------------------------------------------------------------------------
// slot_descriptor — псевдоним для совместимости и тестов
// ---------------------------------------------------------------------------

/// Дескриптор аллоцированного объекта (для совместимости с тестами фазы 5–8.1).
/// В фазе 8.2 карта слотов реализована через SlotInfo (ключ = offset).
/// slot_descriptor сохраняется как POD для тестов проверки тривиальной копируемости.
struct slot_descriptor
{
    bool used; ///< Слот занят (не используется в карте 8.2, сохранён для совместимости)
    uintptr_t offset;   ///< Смещение объекта в области данных ПАП
    uintptr_t count;    ///< Количество элементов (для массивов)
    uintptr_t type_idx; ///< Индекс типа в таблице type_info_entry
    uintptr_t name_idx; ///< Индекс в таблице name_info_entry; PAM_INVALID_IDX для безымянных
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

/// Заголовок файла персистного адресного пространства (фаза 8.2, версия 5).
/// Карта слотов хранится внутри области данных ПАП по slot_map_offset.
struct pam_header
{
    uint32_t  magic;          ///< PAM_MAGIC — признак файла ПАМ
    uint32_t  version;        ///< PAM_VERSION — версия формата
    uintptr_t data_area_size; ///< Размер области данных в байтах
    unsigned  type_count;     ///< Число записей в таблице типов
    unsigned  type_capacity;  ///< Ёмкость таблицы типов (число записей в файле)
    unsigned  name_count;     ///< Число записей в таблице имён
    unsigned  name_capacity;  ///< Ёмкость таблицы имён (число записей в файле)
};

static_assert( std::is_trivially_copyable<pam_header>::value, "pam_header должен быть тривиально копируемым" );

// ---------------------------------------------------------------------------
// PersistentAddressSpace — единый персистный адресный менеджер
// ---------------------------------------------------------------------------

/**
 * Единый менеджер персистного адресного пространства (ПАМ).
 *
 * Управляет плоским байтовым буфером (_data), загружаемым из файла без вызова
 * конструкторов. Хранит карту слотов (_slot_map) ВНУТРИ ПАП — отсортированный
 * массив slot_entry, раскладка совместима с pmap<uintptr_t, SlotInfo> (фаза 8.2).
 * Хранит динамическую таблицу типов (_types) — по одной записи на каждый
 * уникальный тип. Хранит динамическую таблицу имён (_names) — по одной записи
 * на каждое именованное объекта; обеспечивает уникальность имён и двустороннюю
 * связь «имя ↔ слот»: name_info_entry.slot_offset → offset (ключ в карте слотов),
 * SlotInfo.name_idx → индекс в таблице имён.
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
     *              Если имя задано — создаётся запись в таблице имён.
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
    // Удаление объектов
    // -----------------------------------------------------------------------

    /**
     * Освободить слот по смещению offset.
     * Конструкторы/деструкторы НЕ вызываются (Тр.10).
     * Для именованных объектов также освобождается запись в таблице имён.
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

        // Освобождаем запись в таблице имён (если объект именованный).
        if ( nidx != PAM_INVALID_IDX && nidx < _name_capacity && _names[nidx].used )
        {
            _names[nidx].used        = false;
            _names[nidx].slot_offset = 0;
            _names[nidx].name[0]     = '\0';
            if ( _header().name_count > 0 )
                _header().name_count--;
        }

        // Удаляем запись из карты слотов (сдвигаем оставшиеся влево).
        for ( uintptr_t i = idx; i + 1 < _slot_map_size; i++ )
            _slot_entries()[i] = _slot_entries()[i + 1];
        _slot_map_size--;
    }

    // -----------------------------------------------------------------------
    // Поиск по имени (только для именованных объектов)
    // -----------------------------------------------------------------------

    /**
     * Найти именованный объект по имени (поиск через таблицу имён).
     * @return Смещение объекта или 0, если не найден.
     */
    uintptr_t Find( const char* name ) const
    {
        if ( name == nullptr || name[0] == '\0' )
            return 0;
        for ( unsigned i = 0; i < _name_capacity; i++ )
        {
            if ( _names[i].used && std::strncmp( _names[i].name, name, PAM_NAME_SIZE ) == 0 )
                return _names[i].slot_offset;
        }
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
        for ( unsigned i = 0; i < _name_capacity; i++ )
        {
            if ( !_names[i].used )
                continue;
            if ( std::strncmp( _names[i].name, name, PAM_NAME_SIZE ) != 0 )
                continue;
            uintptr_t slot_off = _names[i].slot_offset;
            // Ищем слот по смещению (O(log n)).
            uintptr_t sidx = _slot_lower_bound( slot_off );
            if ( sidx >= _slot_map_size || _slot_entries_const()[sidx].key != slot_off )
                continue;
            // Проверяем тип через таблицу типов.
            uintptr_t tidx = _slot_entries_const()[sidx].value.type_idx;
            if ( tidx < _type_capacity && _types[tidx].used &&
                 std::strncmp( _types[tidx].name, tname, PAM_TYPE_ID_SIZE ) == 0 )
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
     * Двусторонняя связь: SlotInfo.name_idx → name_info_entry.name.
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
        if ( nidx != PAM_INVALID_IDX && nidx < _name_capacity && _names[nidx].used )
            return _names[nidx].name;
        return nullptr;
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
        if ( tidx < _type_capacity && _types[tidx].used )
            return _types[tidx].elem_size;
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
     * Сохранить весь образ ПАМ в файл (фаза 8.2).
     * Записывает: заголовок, таблицу типов, таблицу имён, область данных.
     * Карта слотов сохраняется как часть области данных (внутри ПАП).
     */
    void Save()
    {
        if ( _filename[0] == '\0' )
            return;
        std::FILE* f = std::fopen( _filename, "wb" );
        if ( f == nullptr )
            return;
        std::fwrite( &_header(), sizeof( pam_header ), 1, f );
        std::fwrite( _types, sizeof( type_info_entry ), _type_capacity, f );
        std::fwrite( _names, sizeof( name_info_entry ), _name_capacity, f );
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
        if ( _types != nullptr )
        {
            std::free( _types );
            _types = nullptr;
        }
        if ( _names != nullptr )
        {
            std::free( _names );
            _names = nullptr;
        }
    }

  private:
    // -----------------------------------------------------------------------
    // Внутреннее состояние
    // -----------------------------------------------------------------------

    char _filename[256]; ///< Путь к файлу ПАМ
    char* _data; ///< Буфер области данных (включает заголовок и карту слотов)

    /// Карта слотов хранится ВНУТРИ _data по смещению _slot_map_offset.
    /// Раскладка (фаза 8.2, совместима с pmap<uintptr_t, SlotInfo>):
    ///   [uintptr_t size][uintptr_t capacity][uintptr_t entries_offset]
    /// Массив записей slot_entry[] расположен по entries_offset в _data.
    uintptr_t _slot_map_offset;   ///< Смещение объекта карты слотов в _data
    uintptr_t _slot_map_size;     ///< Текущее число слотов (зеркало size_ в _data)
    uintptr_t _slot_map_capacity; ///< Текущая ёмкость карты (зеркало capacity_ в _data)
    uintptr_t _slot_entries_off; ///< Смещение массива slot_entry[] в _data (зеркало entries_offset)

    type_info_entry* _types;         ///< Динамическая таблица типов
    unsigned         _type_capacity; ///< Текущая ёмкость таблицы типов
    name_info_entry* _names;         ///< Динамическая таблица имён объектов
    unsigned         _name_capacity; ///< Текущая ёмкость таблицы имён

    // Заголовок хранится в начале буфера _data как pam_header.
    // _data[0..sizeof(pam_header)-1] — заголовок.
    // Область данных начинается после заголовка.

    /// Счётчик следующего доступного смещения в области данных (bump-allocator).
    uintptr_t _bump;

    // -----------------------------------------------------------------------
    // Доступ к заголовку
    // -----------------------------------------------------------------------

    pam_header& _header() { return *reinterpret_cast<pam_header*>( _data ); }

    const pam_header& _header_const() const { return *reinterpret_cast<const pam_header*>( _data ); }

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
    // Конструктор и синглтон
    // -----------------------------------------------------------------------

    PersistentAddressSpace()
        : _data( nullptr ), _slot_map_offset( 0 ), _slot_map_size( 0 ), _slot_map_capacity( 0 ), _slot_entries_off( 0 ),
          _types( nullptr ), _type_capacity( 0 ), _names( nullptr ), _name_capacity( 0 ), _bump( sizeof( pam_header ) )
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
     * Используется для внутренней аллокации (карта слотов, её записи).
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
    // Управление картой слотов (внутри ПАП)
    // -----------------------------------------------------------------------

    /**
     * Синхронизировать зеркальные поля карты слотов из _data.
     * Вызывается после realloc (когда _data мог переместиться).
     */
    void _sync_slot_map_mirrors()
    {
        if ( _slot_map_offset == 0 )
            return;
        uintptr_t* sm      = reinterpret_cast<uintptr_t*>( _data + _slot_map_offset );
        _slot_map_size     = sm[0];
        _slot_map_capacity = sm[1];
        _slot_entries_off  = sm[2];
    }

    /**
     * Сохранить зеркальные поля карты слотов в _data.
     */
    void _flush_slot_map_mirrors()
    {
        if ( _slot_map_offset == 0 )
            return;
        uintptr_t* sm = reinterpret_cast<uintptr_t*>( _data + _slot_map_offset );
        sm[0]         = _slot_map_size;
        sm[1]         = _slot_map_capacity;
        sm[2]         = _slot_entries_off;
    }

    /**
     * Инициализировать карту слотов: выделить объект и начальный массив внутри ПАП.
     * Вызывается при создании пустого образа и при загрузке нового формата.
     */
    void _init_slot_map()
    {
        // Выделяем объект карты (3 × uintptr_t: size, capacity, entries_offset).
        uintptr_t sm_obj_align = alignof( uintptr_t );
        _slot_map_offset       = _raw_alloc( 3 * sizeof( uintptr_t ), sm_obj_align );
        if ( _slot_map_offset == 0 )
            throw std::bad_alloc{};

        // Выделяем начальный массив записей.
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

    /**
     * Убедиться, что карта слотов имеет достаточную ёмкость для ещё одной записи.
     * При необходимости расширяет массив записей (с копированием).
     * Возвращает false при ошибке выделения памяти.
     */
    bool _ensure_slot_map_capacity()
    {
        if ( _slot_map_size < _slot_map_capacity )
            return true;

        uintptr_t new_cap = _slot_map_capacity * 2;
        if ( new_cap < PAM_INITIAL_SLOT_CAPACITY )
            new_cap = PAM_INITIAL_SLOT_CAPACITY;

        // Копируем старые записи перед realloc (они в _data, который может переместиться).
        uintptr_t   old_size_bytes = _slot_map_size * sizeof( slot_entry );
        slot_entry* tmp = static_cast<slot_entry*>( std::malloc( static_cast<std::size_t>( old_size_bytes ) ) );
        if ( tmp == nullptr )
            return false;
        if ( _slot_entries_off != 0 && _slot_map_size > 0 )
            std::memcpy( tmp, _data + _slot_entries_off, old_size_bytes );

        // Выделяем новый массив в ПАП (raw_alloc — без регистрации слота).
        uintptr_t new_off = _raw_alloc( new_cap * sizeof( slot_entry ), alignof( slot_entry ) );
        if ( new_off == 0 )
        {
            std::free( tmp );
            return false;
        }
        std::memset( _data + new_off, 0, new_cap * sizeof( slot_entry ) );

        // Копируем старые записи в новый массив.
        if ( _slot_map_size > 0 )
            std::memcpy( _data + new_off, tmp, old_size_bytes );
        std::free( tmp );

        _slot_entries_off  = new_off;
        _slot_map_capacity = new_cap;
        _flush_slot_map_mirrors();
        return true;
    }

    /**
     * Вставить новый слот в карту слотов (отсортированный массив).
     * offset является ключом; info — значением.
     * Сохраняет сортировку для O(log n) поиска.
     * Возвращает true при успехе.
     */
    bool _slot_insert( uintptr_t offset, const SlotInfo& info )
    {
        if ( !_ensure_slot_map_capacity() )
            return false;

        // Позиция для вставки (нижняя граница).
        uintptr_t idx = _slot_lower_bound( offset );

        // Сдвигаем записи вправо.
        slot_entry* entries = _slot_entries();
        for ( uintptr_t i = _slot_map_size; i > idx; i-- )
            entries[i] = entries[i - 1];

        entries[idx].key   = offset;
        entries[idx].value = info;
        _slot_map_size++;
        _flush_slot_map_mirrors();
        return true;
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
        if ( _types != nullptr )
        {
            std::free( _types );
            _types         = nullptr;
            _type_capacity = 0;
        }
        if ( _names != nullptr )
        {
            std::free( _names );
            _names         = nullptr;
            _name_capacity = 0;
        }
        _slot_map_offset   = 0;
        _slot_map_size     = 0;
        _slot_map_capacity = 0;
        _slot_entries_off  = 0;

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

        // ---- Таблица типов ----
        unsigned tcap = hdr.type_capacity;
        if ( tcap < PAM_INITIAL_TYPE_CAPACITY )
            tcap = PAM_INITIAL_TYPE_CAPACITY;

        _types = static_cast<type_info_entry*>( std::malloc( sizeof( type_info_entry ) * tcap ) );
        if ( _types == nullptr )
        {
            std::fclose( f );
            throw std::bad_alloc{};
        }
        std::memset( _types, 0, sizeof( type_info_entry ) * tcap );
        _type_capacity = tcap;

        if ( hdr.type_capacity > 0 )
        {
            if ( std::fread( _types, sizeof( type_info_entry ), hdr.type_capacity, f ) != hdr.type_capacity )
            {
                std::fclose( f );
                _init_empty();
                return;
            }
        }

        // ---- Таблица имён ----
        unsigned ncap = hdr.name_capacity;
        if ( ncap < PAM_INITIAL_NAME_CAPACITY )
            ncap = PAM_INITIAL_NAME_CAPACITY;

        _names = static_cast<name_info_entry*>( std::malloc( sizeof( name_info_entry ) * ncap ) );
        if ( _names == nullptr )
        {
            std::fclose( f );
            throw std::bad_alloc{};
        }
        std::memset( _names, 0, sizeof( name_info_entry ) * ncap );
        _name_capacity = ncap;

        if ( hdr.name_capacity > 0 )
        {
            if ( std::fread( _names, sizeof( name_info_entry ), hdr.name_capacity, f ) != hdr.name_capacity )
            {
                std::fclose( f );
                _init_empty();
                return;
            }
        }

        // ---- Область данных (включает карту слотов) ----
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
        _header()               = hdr;
        _header().type_capacity = _type_capacity;
        _header().name_capacity = _name_capacity;

        // Восстанавливаем карту слотов из _data.
        // Объект карты сохранён по _slot_map_offset (первые байты после заголовка).
        _slot_map_offset = sizeof( pam_header );
        _sync_slot_map_mirrors();

        // Восстанавливаем bump: находим максимальный конец среди всех слотов.
        _bump = _slot_entries_off + _slot_map_capacity * sizeof( slot_entry );
        if ( _bump < sizeof( pam_header ) )
            _bump = sizeof( pam_header );
    }

    // -----------------------------------------------------------------------
    // Инициализация пустого образа
    // -----------------------------------------------------------------------

    void _init_empty()
    {
        // Выделяем начальную таблицу типов.
        _type_capacity = PAM_INITIAL_TYPE_CAPACITY;
        _types         = static_cast<type_info_entry*>( std::malloc( sizeof( type_info_entry ) * _type_capacity ) );
        if ( _types == nullptr )
            throw std::bad_alloc{};
        std::memset( _types, 0, sizeof( type_info_entry ) * _type_capacity );

        // Выделяем начальную таблицу имён.
        _name_capacity = PAM_INITIAL_NAME_CAPACITY;
        _names         = static_cast<name_info_entry*>( std::malloc( sizeof( name_info_entry ) * _name_capacity ) );
        if ( _names == nullptr )
        {
            std::free( _types );
            _types = nullptr;
            throw std::bad_alloc{};
        }
        std::memset( _names, 0, sizeof( name_info_entry ) * _name_capacity );

        // Выделяем буфер данных.
        uintptr_t data_size = PAM_INITIAL_DATA_SIZE;
        _data               = static_cast<char*>( std::malloc( static_cast<std::size_t>( data_size ) ) );
        if ( _data == nullptr )
        {
            std::free( _names );
            _names = nullptr;
            std::free( _types );
            _types = nullptr;
            throw std::bad_alloc{};
        }
        std::memset( _data, 0, static_cast<std::size_t>( data_size ) );

        // Записываем заголовок в начало буфера данных.
        pam_header& hdr    = _header();
        hdr.magic          = PAM_MAGIC;
        hdr.version        = PAM_VERSION;
        hdr.data_area_size = data_size;
        hdr.type_count     = 0;
        hdr.type_capacity  = _type_capacity;
        hdr.name_count     = 0;
        hdr.name_capacity  = _name_capacity;

        // Область данных начинается после заголовка.
        _bump = sizeof( pam_header );

        // Инициализируем карту слотов внутри области данных ПАП.
        _init_slot_map();
    }

    // -----------------------------------------------------------------------
    // Поиск или регистрация типа в таблице типов
    // -----------------------------------------------------------------------

    /**
     * Найти или зарегистрировать тип в таблице типов.
     * Если тип с таким именем и размером уже есть — возвращает его индекс.
     * Если нет — добавляет новую запись и возвращает её индекс.
     * @return Индекс в таблице типов или PAM_INVALID_IDX при ошибке.
     */
    unsigned _find_or_register_type( const char* type_name, uintptr_t elem_size )
    {
        // Ищем существующий тип.
        for ( unsigned i = 0; i < _type_capacity; i++ )
        {
            if ( _types[i].used && _types[i].elem_size == elem_size &&
                 std::strncmp( _types[i].name, type_name, PAM_TYPE_ID_SIZE ) == 0 )
                return i;
        }

        // Ищем свободное место.
        unsigned free_idx = static_cast<unsigned>( -1 );
        for ( unsigned i = 0; i < _type_capacity; i++ )
        {
            if ( !_types[i].used )
            {
                free_idx = i;
                break;
            }
        }

        // Нет свободных — расширяем таблицу типов.
        if ( free_idx == static_cast<unsigned>( -1 ) )
        {
            unsigned new_cap = _type_capacity * 2;
            if ( new_cap < PAM_INITIAL_TYPE_CAPACITY )
                new_cap = PAM_INITIAL_TYPE_CAPACITY;

            type_info_entry* new_types =
                static_cast<type_info_entry*>( std::realloc( _types, sizeof( type_info_entry ) * new_cap ) );
            if ( new_types == nullptr )
                return static_cast<unsigned>( -1 );

            std::memset( new_types + _type_capacity, 0, sizeof( type_info_entry ) * ( new_cap - _type_capacity ) );
            free_idx                = _type_capacity;
            _types                  = new_types;
            _type_capacity          = new_cap;
            _header().type_capacity = _type_capacity;
        }

        // Заполняем запись.
        type_info_entry& te = _types[free_idx];
        te.used             = true;
        te.elem_size        = elem_size;
        std::strncpy( te.name, type_name, PAM_TYPE_ID_SIZE - 1 );
        te.name[PAM_TYPE_ID_SIZE - 1] = '\0';

        _header().type_count++;
        return free_idx;
    }

    // -----------------------------------------------------------------------
    // Регистрация имени в таблице имён
    // -----------------------------------------------------------------------

    /**
     * Зарегистрировать новое имя в таблице имён (связать с offset объекта).
     * Если имя уже занято — возвращает PAM_INVALID_IDX (уникальность нарушена).
     * Добавляет новую запись в свободное место, при необходимости расширяя таблицу.
     * @return Индекс в таблице имён или PAM_INVALID_IDX при ошибке/дубликате.
     */
    unsigned _register_name( const char* name, uintptr_t slot_offset )
    {
        // Проверяем уникальность: имя не должно уже присутствовать.
        for ( unsigned i = 0; i < _name_capacity; i++ )
        {
            if ( _names[i].used && std::strncmp( _names[i].name, name, PAM_NAME_SIZE ) == 0 )
                return static_cast<unsigned>( -1 ); // имя занято
        }

        // Ищем свободное место.
        unsigned free_idx = static_cast<unsigned>( -1 );
        for ( unsigned i = 0; i < _name_capacity; i++ )
        {
            if ( !_names[i].used )
            {
                free_idx = i;
                break;
            }
        }

        // Нет свободных — расширяем таблицу имён.
        if ( free_idx == static_cast<unsigned>( -1 ) )
        {
            unsigned new_cap = _name_capacity * 2;
            if ( new_cap < PAM_INITIAL_NAME_CAPACITY )
                new_cap = PAM_INITIAL_NAME_CAPACITY;

            name_info_entry* new_names =
                static_cast<name_info_entry*>( std::realloc( _names, sizeof( name_info_entry ) * new_cap ) );
            if ( new_names == nullptr )
                return static_cast<unsigned>( -1 );

            std::memset( new_names + _name_capacity, 0, sizeof( name_info_entry ) * ( new_cap - _name_capacity ) );
            free_idx                = _name_capacity;
            _names                  = new_names;
            _name_capacity          = new_cap;
            _header().name_capacity = _name_capacity;
        }

        // Заполняем запись.
        name_info_entry& ne = _names[free_idx];
        ne.used             = true;
        ne.slot_offset      = slot_offset;
        std::strncpy( ne.name, name, PAM_NAME_SIZE - 1 );
        ne.name[PAM_NAME_SIZE - 1] = '\0';

        _header().name_count++;
        return free_idx;
    }

    // -----------------------------------------------------------------------
    // Аллокатор области данных
    // -----------------------------------------------------------------------

    /**
     * Выделить size*count байт в области данных ПАМ.
     * Всегда создаётся запись в карте слотов (внутри ПАП, фаза 8.2).
     * Тип регистрируется в таблице типов (один раз на уникальный тип).
     * Если name задано — регистрируется запись в таблице имён и устанавливается
     * двусторонняя связь: name_info_entry.slot_offset = offset,
     *                     SlotInfo.name_idx = индекс в таблице имён.
     * Если name задано, но уже занято — возвращает 0 (уникальность нарушена).
     * Возвращает смещение или 0 при ошибке.
     */
    uintptr_t _alloc( uintptr_t elem_size, uintptr_t count, const char* type_id, const char* name )
    {
        uintptr_t total_size = elem_size * count;
        uintptr_t data_size  = _header().data_area_size;

        // Выравниваем bump по natural alignment элемента (до 8 байт).
        uintptr_t align        = elem_size >= 8 ? 8u : elem_size >= 4 ? 4u : elem_size >= 2 ? 2u : 1u;
        uintptr_t aligned_bump = ( _bump + align - 1 ) & ~( align - 1 );

        // Если не хватает места — расширяем область данных.
        if ( aligned_bump + total_size > data_size )
        {
            uintptr_t new_size = data_size * 2;
            while ( aligned_bump + total_size > new_size )
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
        _bump            = aligned_bump + total_size;

        // Регистрируем тип (один раз для всех слотов этого типа).
        unsigned type_idx = _find_or_register_type( type_id, elem_size );
        if ( type_idx == static_cast<unsigned>( -1 ) )
            return 0;

        // Регистрируем имя (если задано), проверяя уникальность.
        bool     named = ( name != nullptr && name[0] != '\0' );
        unsigned nidx  = static_cast<unsigned>( PAM_INVALID_IDX );
        if ( named )
        {
            nidx = _register_name( name, offset );
            if ( nidx == static_cast<unsigned>( -1 ) )
                return 0; // имя занято
        }

        // Вставляем запись в карту слотов (внутри ПАП, O(log n) вставка).
        SlotInfo info;
        info.count    = count;
        info.type_idx = static_cast<uintptr_t>( type_idx );
        info.name_idx = named ? static_cast<uintptr_t>( nidx ) : PAM_INVALID_IDX;

        if ( !_slot_insert( offset, info ) )
        {
            // Откатываем регистрацию имени при ошибке вставки в карту.
            if ( named && nidx != static_cast<unsigned>( PAM_INVALID_IDX ) )
            {
                _names[nidx].used        = false;
                _names[nidx].slot_offset = 0;
                _names[nidx].name[0]     = '\0';
                if ( _header().name_count > 0 )
                    _header().name_count--;
            }
            return 0;
        }

        return offset;
    }
};
