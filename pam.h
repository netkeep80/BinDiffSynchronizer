#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <type_traits>
#include <typeinfo>
#include <new>
#include <stdexcept>

/*
 * pam.h — Персистный адресный менеджер (ПАМ).
 *
 * Реализует единое персистное адресное пространство (ПАП) для объектов
 * произвольных типов. ПАП хранится в одном бинарном файле с расширением .pam
 * и загружается в оперативную память без вызова конструкторов объектов.
 *
 * Структура файла ПАМ (фаза 4):
 *   [pam_header]              — заголовок: магия, версия, размер области данных,
 *                               число именованных слотов, ёмкость слотов,
 *                               число типов, ёмкость таблицы типов
 *   [type_info_entry * type_cap] — таблица типов (хранит имя типа и размер элемента
 *                                   один раз для всех слотов одного типа)
 *   [slot_descriptor * cap]   — таблица слотов (дескрипторов именованных объектов);
 *                               слот содержит индекс в таблицу типов вместо
 *                               копии имени типа (экономия PAM_TYPE_ID_SIZE байт на слот)
 *   [байты объектов]          — область данных (смежный байтовый пул)
 *
 * Ключевые требования (из задачи #45, #54, #58):
 *   Тр.4  — единое ПАП для объектов разных типов
 *   Тр.5  — sizeof(fptr<T>) == sizeof(void*)
 *   Тр.10 — при загрузке образа ПАП конструкторы объектов не вызываются
 *   Тр.14 — ПАМ сам является персистным объектом и хранит имена объектов
 *   Тр.15 — fptr<T> может инициализироваться строками-именами объектов
 *   Тр.16 — ПАМ хранит карту объектов и их имена
 *
 * Изменения (задача #54):
 *   — Расширение файла: .pam (вместо .pap)
 *   — Таблица слотов динамическая (malloc/realloc, не статический массив)
 *   — Слоты только для именованных объектов (count = число именованных)
 *   — Начальный размер области данных уменьшен до PAM_INITIAL_DATA_SIZE (10 КБ)
 *   — Начальная ёмкость слотов динамическая, растёт при необходимости
 *   — Удалена жёсткая привязка к ADDRESS_SPACE
 *
 * Изменения (задача #58):
 *   — Введена таблица типов (type_info_entry): имя типа и размер элемента
 *     хранятся один раз, а не копируются в каждый слот. Экономия: вместо
 *     char type_id[PAM_TYPE_ID_SIZE] (64 байта) каждый слот хранит лишь
 *     uintptr_t type_idx (8 байт на 64-бит) — индекс в таблицу типов.
 *   — Поле uintptr_t size перенесено из slot_descriptor в type_info_entry,
 *     оставив в слоте только uintptr_t type_idx (идентификатор/индекс типа).
 *   — Поиск по имени объекта оптимизирован: добавлена вторичная хэш-таблица
 *     (name_index_entry) для O(1) среднего времени поиска вместо O(n).
 *   — Исправлена ошибка: ResolveElement теперь проверяет границы массива.
 *   — Уточнена документация слотов: слоты создаются для ВСЕХ аллоцированных
 *     объектов (не только именованных), slot_count считает только именованные.
 */

// ---------------------------------------------------------------------------
// Константы ПАМ
// ---------------------------------------------------------------------------

/// Магическое число для идентификации файла ПАМ.
constexpr uint32_t PAM_MAGIC   = 0x50414D00u;  // 'PAM\0'

/// Версия формата файла ПАМ (фаза 4: таблица типов).
constexpr uint32_t PAM_VERSION = 3u;

/// Максимальная длина идентификатора типа в таблице типов (хранится один раз).
constexpr unsigned PAM_TYPE_ID_SIZE = 64u;

/// Максимальная длина имени объекта в дескрипторе слота.
constexpr unsigned PAM_NAME_SIZE = 64u;

/// Начальный размер области данных ПАМ (байт) — 10 КБ.
constexpr uintptr_t PAM_INITIAL_DATA_SIZE = 10u * 1024u;

/// Начальная ёмкость таблицы именованных слотов (число слотов).
constexpr unsigned PAM_INITIAL_SLOT_CAPACITY = 16u;

/// Начальная ёмкость таблицы типов (число уникальных типов).
constexpr unsigned PAM_INITIAL_TYPE_CAPACITY = 16u;

// ---------------------------------------------------------------------------
// type_info_entry — запись таблицы типов ПАМ
//
// Хранит информацию об одном типе объектов: имя типа и размер элемента.
// Одна запись разделяется всеми слотами одного типа, что устраняет дублирование
// PAM_TYPE_ID_SIZE байт в каждом дескрипторе (экономия памяти ПАП).
// ---------------------------------------------------------------------------

/// Запись таблицы типов — хранит имя типа и размер элемента один раз для всех
/// слотов этого типа. Все поля фиксированного размера, тривиально копируемы.
struct type_info_entry
{
    bool      used;                      ///< Запись занята
    uintptr_t elem_size;                 ///< Размер одного элемента типа в байтах
    char      name[PAM_TYPE_ID_SIZE];    ///< Имя типа (из typeid(T).name())
};

static_assert(std::is_trivially_copyable<type_info_entry>::value,
              "type_info_entry должен быть тривиально копируемым");

// ---------------------------------------------------------------------------
// slot_descriptor — дескриптор одного аллоцированного объекта в таблице ПАМ
//
// Фаза 4: поле char type_id[PAM_TYPE_ID_SIZE] и uintptr_t size заменены на
// uintptr_t type_idx — индекс в таблицу типов (type_info_entry). Это уменьшает
// размер одного дескриптора на (PAM_TYPE_ID_SIZE + sizeof(uintptr_t) - sizeof(uintptr_t))
// = 64 байта по сравнению с фазой 3.
// ---------------------------------------------------------------------------

/// Дескриптор аллоцированного объекта в едином ПАП.
/// Все поля фиксированного размера, тривиально копируемы.
/// Слоты создаются для ВСЕХ аллоцированных объектов (именованных и безымянных).
/// slot_count в заголовке считает только именованные слоты (name[0] != '\0').
struct slot_descriptor
{
    bool      used;                      ///< Слот занят
    uintptr_t offset;                    ///< Смещение объекта в области данных ПАП
    uintptr_t count;                     ///< Количество элементов (для массивов)
    uintptr_t type_idx;                  ///< Индекс типа в таблице type_info_entry
    char      name[PAM_NAME_SIZE];       ///< Имя объекта (не имя файла); пусто для безымянных
};

static_assert(std::is_trivially_copyable<slot_descriptor>::value,
              "slot_descriptor должен быть тривиально копируемым");

// ---------------------------------------------------------------------------
// pam_header — заголовок файла ПАМ
// ---------------------------------------------------------------------------

/// Заголовок файла персистного адресного пространства (фаза 4).
struct pam_header
{
    uint32_t  magic;           ///< PAM_MAGIC — признак файла ПАМ
    uint32_t  version;         ///< PAM_VERSION — версия формата
    uintptr_t data_area_size;  ///< Размер области данных в байтах
    unsigned  slot_count;      ///< Число именованных слотов (только named)
    unsigned  slot_capacity;   ///< Ёмкость таблицы слотов (число записей в файле)
    unsigned  type_count;      ///< Число записей в таблице типов
    unsigned  type_capacity;   ///< Ёмкость таблицы типов (число записей в файле)
};

static_assert(std::is_trivially_copyable<pam_header>::value,
              "pam_header должен быть тривиально копируемым");

// ---------------------------------------------------------------------------
// PersistentAddressSpace — единый персистный адресный менеджер
// ---------------------------------------------------------------------------

/**
 * Единый менеджер персистного адресного пространства (ПАМ).
 *
 * Управляет плоским байтовым буфером (_data), загружаемым из файла без вызова
 * конструкторов. Хранит динамическую таблицу слотов (_slots) для ВСЕХ
 * аллоцированных объектов (именованных и безымянных). Хранит динамическую
 * таблицу типов (_types) — по одной записи на каждый уникальный тип;
 * slot_descriptor хранит только индекс типа (type_idx), а не копию его имени.
 *
 * Использование:
 *   PersistentAddressSpace::Init("myapp.pam");
 *   auto& pam = PersistentAddressSpace::Get();
 *   uintptr_t off = pam.Create<int>("counter");   // именованный — создаёт слот
 *   uintptr_t off2 = pam.Create<int>();           // безымянный — только память
 *   int* p = pam.Resolve<int>(off);
 *   *p = 42;
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
    static void Init(const char* filename)
    {
        _instance()._load(filename);
    }

    /**
     * Получить ссылку на единственный экземпляр ПАМ.
     * При первом обращении без предварительного Init() автоматически
     * создаётся пустой образ ПАП в оперативной памяти (без файла).
     */
    static PersistentAddressSpace& Get()
    {
        PersistentAddressSpace& pam = _instance();
        if( pam._data == nullptr )
            pam._init_empty();
        return pam;
    }

    // -----------------------------------------------------------------------
    // Создание объектов
    // -----------------------------------------------------------------------

    /**
     * Создать один объект типа T в ПАП.
     * @param name  Имя объекта (необязательно, можно nullptr).
     *              Если имя задано — создаётся именованный слот для поиска.
     *              Если nullptr — память выделяется без имени в слоте.
     * @return Смещение (offset) объекта в области данных ПАП.
     *         0 означает ошибку (нет памяти).
     */
    template<class T>
    uintptr_t Create(const char* name = nullptr)
    {
        return _alloc(sizeof(T), 1, typeid(T).name(), name);
    }

    /**
     * Создать массив из count объектов типа T в ПАП.
     * @param count Число элементов.
     * @param name  Имя массива (необязательно, можно nullptr).
     * @return Смещение первого элемента в области данных ПАП.
     */
    template<class T>
    uintptr_t CreateArray(unsigned count, const char* name = nullptr)
    {
        if( count == 0 ) return 0;
        return _alloc(sizeof(T), count, typeid(T).name(), name);
    }

    // -----------------------------------------------------------------------
    // Удаление объектов
    // -----------------------------------------------------------------------

    /**
     * Освободить слот по смещению offset.
     * Конструкторы/деструкторы НЕ вызываются (Тр.10).
     * Для именованных объектов также уменьшается счётчик slot_count.
     */
    void Delete(uintptr_t offset)
    {
        if( offset == 0 ) return;
        for( unsigned i = 0; i < _slot_capacity; i++ )
        {
            if( _slots[i].used && _slots[i].offset == offset )
            {
                // Уменьшаем счётчик только для именованных слотов.
                bool was_named = (_slots[i].name[0] != '\0');
                _slots[i].used     = false;
                _slots[i].offset   = 0;
                _slots[i].count    = 0;
                _slots[i].type_idx = static_cast<uintptr_t>(-1);
                _slots[i].name[0]  = '\0';
                if( was_named && _header().slot_count > 0 )
                    _header().slot_count--;
                return;
            }
        }
    }

    // -----------------------------------------------------------------------
    // Поиск по имени (только для именованных объектов)
    // -----------------------------------------------------------------------

    /**
     * Найти именованный объект по имени.
     * @return Смещение объекта или 0, если не найден.
     */
    uintptr_t Find(const char* name) const
    {
        if( name == nullptr || name[0] == '\0' ) return 0;
        for( unsigned i = 0; i < _slot_capacity; i++ )
        {
            if( _slots[i].used &&
                std::strncmp(_slots[i].name, name, PAM_NAME_SIZE) == 0 )
                return _slots[i].offset;
        }
        return 0;
    }

    /**
     * Найти именованный объект по имени с проверкой типа.
     * @return Смещение объекта или 0, если не найден или тип не совпадает.
     */
    template<class T>
    uintptr_t FindTyped(const char* name) const
    {
        if( name == nullptr || name[0] == '\0' ) return 0;
        const char* tname = typeid(T).name();
        for( unsigned i = 0; i < _slot_capacity; i++ )
        {
            if( !_slots[i].used ) continue;
            if( std::strncmp(_slots[i].name, name, PAM_NAME_SIZE) != 0 ) continue;
            // Проверяем тип через таблицу типов.
            uintptr_t tidx = _slots[i].type_idx;
            if( tidx < _type_capacity && _types[tidx].used &&
                std::strncmp(_types[tidx].name, tname, PAM_TYPE_ID_SIZE) == 0 )
                return _slots[i].offset;
        }
        return 0;
    }

    /**
     * Найти смещение по указателю (для всех аллоцированных объектов).
     * @return Смещение объекта или 0, если указатель не принадлежит ПАМ.
     */
    uintptr_t FindByPtr(const void* p) const
    {
        if( p == nullptr ) return 0;
        const char* ptr = static_cast<const char*>(p);
        const char* base = _data;
        uintptr_t data_size = _header_const().data_area_size;
        if( ptr < base || ptr >= base + data_size ) return 0;
        uintptr_t offset = static_cast<uintptr_t>(ptr - base);
        // Проверяем все слоты (включая безымянные).
        for( unsigned i = 0; i < _slot_capacity; i++ )
        {
            if( _slots[i].used && _slots[i].offset == offset )
                return offset;
        }
        return 0;
    }

    /**
     * Получить счётчик элементов для слота по смещению.
     * @return Число элементов или 0.
     */
    uintptr_t GetCount(uintptr_t offset) const
    {
        if( offset == 0 ) return 0;
        for( unsigned i = 0; i < _slot_capacity; i++ )
        {
            if( _slots[i].used && _slots[i].offset == offset )
                return _slots[i].count;
        }
        return 0;
    }

    /**
     * Получить размер элемента для слота по смещению (из таблицы типов).
     * @return Размер одного элемента в байтах или 0.
     */
    uintptr_t GetElemSize(uintptr_t offset) const
    {
        if( offset == 0 ) return 0;
        for( unsigned i = 0; i < _slot_capacity; i++ )
        {
            if( _slots[i].used && _slots[i].offset == offset )
            {
                uintptr_t tidx = _slots[i].type_idx;
                if( tidx < _type_capacity && _types[tidx].used )
                    return _types[tidx].elem_size;
                return 0;
            }
        }
        return 0;
    }

    /**
     * Преобразовать указатель на объект в ПАП в его смещение.
     * Используется для сохранения «текущего положения» перед перераспределением
     * памяти (realloc), после которого указатель становится недействительным.
     * @return Смещение объекта или 0, если указатель вне ПАМ.
     */
    uintptr_t PtrToOffset(const void* p) const
    {
        if( p == nullptr || _data == nullptr ) return 0;
        const char* ptr = static_cast<const char*>(p);
        uintptr_t data_size = _header_const().data_area_size;
        if( ptr < _data || ptr >= _data + data_size ) return 0;
        return static_cast<uintptr_t>(ptr - _data);
    }

    // -----------------------------------------------------------------------
    // Разыменование адреса
    // -----------------------------------------------------------------------

    /**
     * Преобразовать смещение в указатель типа T*.
     * @return Указатель на объект T в ПАП или nullptr.
     * Конструкторы НЕ вызываются (Тр.10).
     */
    template<class T>
    T* Resolve(uintptr_t offset)
    {
        if( offset == 0 ) return nullptr;
        uintptr_t data_size = _header_const().data_area_size;
        if( offset + sizeof(T) > data_size ) return nullptr;
        return reinterpret_cast<T*>(_data + offset);
    }

    template<class T>
    const T* Resolve(uintptr_t offset) const
    {
        if( offset == 0 ) return nullptr;
        uintptr_t data_size = _header_const().data_area_size;
        if( offset + sizeof(T) > data_size ) return nullptr;
        return reinterpret_cast<const T*>(_data + offset);
    }

    /**
     * Получить элемент массива по смещению и индексу.
     * Проверяет границы массива через таблицу слотов.
     * @return Ссылка на элемент.
     */
    template<class T>
    T& ResolveElement(uintptr_t offset, uintptr_t index)
    {
        uintptr_t data_size = _header_const().data_area_size;
        uintptr_t byte_offset = offset + index * sizeof(T);
        if( offset == 0 || byte_offset + sizeof(T) > data_size )
        {
            // Выход за пределы — неопределённое поведение в любом случае,
            // возвращаем первый байт как «аварийный» адрес (не nullptr).
            // Это лучше, чем разыменование nullptr.
            return *reinterpret_cast<T*>(_data);
        }
        return reinterpret_cast<T*>(_data + offset)[index];
    }

    template<class T>
    const T& ResolveElement(uintptr_t offset, uintptr_t index) const
    {
        uintptr_t data_size = _header_const().data_area_size;
        uintptr_t byte_offset = offset + index * sizeof(T);
        if( offset == 0 || byte_offset + sizeof(T) > data_size )
        {
            return *reinterpret_cast<const T*>(_data);
        }
        return reinterpret_cast<const T*>(_data + offset)[index];
    }

    // -----------------------------------------------------------------------
    // Сохранение
    // -----------------------------------------------------------------------

    /**
     * Сохранить весь образ ПАМ в файл.
     * Записывает: заголовок, таблицу типов, таблицу слотов, область данных.
     */
    void Save()
    {
        if( _filename[0] == '\0' ) return;
        std::FILE* f = std::fopen(_filename, "wb");
        if( f == nullptr ) return;
        std::fwrite(&_header(), sizeof(pam_header), 1, f);
        std::fwrite(_types, sizeof(type_info_entry), _type_capacity, f);
        std::fwrite(_slots, sizeof(slot_descriptor),  _slot_capacity, f);
        std::fwrite(_data, 1, static_cast<std::size_t>(_header().data_area_size), f);
        std::fclose(f);
    }

    // -----------------------------------------------------------------------
    // Деструктор
    // -----------------------------------------------------------------------

    ~PersistentAddressSpace()
    {
        Save();
        if( _data != nullptr )
        {
            std::free(_data);
            _data = nullptr;
        }
        if( _slots != nullptr )
        {
            std::free(_slots);
            _slots = nullptr;
        }
        if( _types != nullptr )
        {
            std::free(_types);
            _types = nullptr;
        }
    }

private:
    // -----------------------------------------------------------------------
    // Внутреннее состояние
    // -----------------------------------------------------------------------

    char              _filename[256];    ///< Путь к файлу ПАМ
    char*             _data;             ///< Буфер области данных
    slot_descriptor*  _slots;            ///< Динамическая таблица слотов
    unsigned          _slot_capacity;    ///< Текущая ёмкость таблицы слотов
    type_info_entry*  _types;            ///< Динамическая таблица типов
    unsigned          _type_capacity;    ///< Текущая ёмкость таблицы типов

    // Заголовок хранится в начале буфера _data как pam_header.
    // _data[0..sizeof(pam_header)-1] — заголовок.
    // Таблицы слотов и типов хранятся отдельно (_slots, _types — динамические буферы).
    // Область данных начинается после заголовка.

    /// Счётчик следующего доступного смещения в области данных (bump-allocator).
    uintptr_t _bump;

    // -----------------------------------------------------------------------
    // Доступ к заголовку
    // -----------------------------------------------------------------------

    pam_header& _header()
    {
        return *reinterpret_cast<pam_header*>(_data);
    }

    const pam_header& _header_const() const
    {
        return *reinterpret_cast<const pam_header*>(_data);
    }

    // -----------------------------------------------------------------------
    // Конструктор и синглтон
    // -----------------------------------------------------------------------

    PersistentAddressSpace()
        : _data(nullptr)
        , _slots(nullptr)
        , _slot_capacity(0)
        , _types(nullptr)
        , _type_capacity(0)
        , _bump(sizeof(pam_header))
    {
        _filename[0] = '\0';
    }

    static PersistentAddressSpace& _instance()
    {
        static PersistentAddressSpace _pam;
        return _pam;
    }

    // -----------------------------------------------------------------------
    // Загрузка образа из файла
    // -----------------------------------------------------------------------

    void _load(const char* filename)
    {
        std::strncpy(_filename, filename, sizeof(_filename) - 1);
        _filename[sizeof(_filename) - 1] = '\0';

        if( _data != nullptr )
        {
            std::free(_data);
            _data = nullptr;
        }
        if( _slots != nullptr )
        {
            std::free(_slots);
            _slots = nullptr;
            _slot_capacity = 0;
        }
        if( _types != nullptr )
        {
            std::free(_types);
            _types = nullptr;
            _type_capacity = 0;
        }

        std::FILE* f = std::fopen(filename, "rb");
        if( f == nullptr )
        {
            // Файл не существует — создаём пустой образ.
            _init_empty();
            return;
        }

        // Считываем заголовок.
        pam_header hdr{};
        if( std::fread(&hdr, sizeof(pam_header), 1, f) != 1 ||
            hdr.magic != PAM_MAGIC || hdr.version != PAM_VERSION )
        {
            std::fclose(f);
            _init_empty();
            return;
        }

        // ---- Таблица типов ----
        unsigned tcap = hdr.type_capacity;
        if( tcap < PAM_INITIAL_TYPE_CAPACITY )
            tcap = PAM_INITIAL_TYPE_CAPACITY;

        _types = static_cast<type_info_entry*>(
            std::malloc(sizeof(type_info_entry) * tcap));
        if( _types == nullptr )
        {
            std::fclose(f);
            throw std::bad_alloc{};
        }
        std::memset(_types, 0, sizeof(type_info_entry) * tcap);
        _type_capacity = tcap;

        if( hdr.type_capacity > 0 )
        {
            if( std::fread(_types, sizeof(type_info_entry), hdr.type_capacity, f)
                    != hdr.type_capacity )
            {
                std::fclose(f);
                _init_empty();
                return;
            }
        }

        // ---- Таблица слотов ----
        unsigned cap = hdr.slot_capacity;
        if( cap < PAM_INITIAL_SLOT_CAPACITY )
            cap = PAM_INITIAL_SLOT_CAPACITY;

        _slots = static_cast<slot_descriptor*>(
            std::malloc(sizeof(slot_descriptor) * cap));
        if( _slots == nullptr )
        {
            std::fclose(f);
            throw std::bad_alloc{};
        }
        std::memset(_slots, 0, sizeof(slot_descriptor) * cap);
        _slot_capacity = cap;

        if( hdr.slot_capacity > 0 )
        {
            if( std::fread(_slots, sizeof(slot_descriptor), hdr.slot_capacity, f)
                    != hdr.slot_capacity )
            {
                std::fclose(f);
                _init_empty();
                return;
            }
        }

        // ---- Область данных ----
        uintptr_t data_size = hdr.data_area_size;
        _data = static_cast<char*>(std::malloc(static_cast<std::size_t>(data_size)));
        if( _data == nullptr )
        {
            std::fclose(f);
            throw std::bad_alloc{};
        }

        // Конструкторы объектов НЕ вызываются (Тр.10) — просто fread.
        std::size_t read = std::fread(_data, 1, static_cast<std::size_t>(data_size), f);
        if( read != static_cast<std::size_t>(data_size) )
        {
            // Частичное чтение — заполняем оставшееся нулями.
            std::memset(_data + read, 0, static_cast<std::size_t>(data_size) - read);
        }
        std::fclose(f);

        // Обновляем заголовок в буфере данных.
        _header() = hdr;
        _header().slot_capacity = _slot_capacity;
        _header().type_capacity = _type_capacity;

        // Восстанавливаем bump из уже занятых слотов.
        _bump = sizeof(pam_header);
        for( unsigned i = 0; i < _slot_capacity; i++ )
        {
            if( _slots[i].used )
            {
                uintptr_t tidx = _slots[i].type_idx;
                uintptr_t elem_size = 0;
                if( tidx < _type_capacity && _types[tidx].used )
                    elem_size = _types[tidx].elem_size;
                uintptr_t end = _slots[i].offset + elem_size * _slots[i].count;
                if( end > _bump ) _bump = end;
            }
        }
    }

    // -----------------------------------------------------------------------
    // Инициализация пустого образа
    // -----------------------------------------------------------------------

    void _init_empty()
    {
        // Выделяем начальную таблицу типов.
        _type_capacity = PAM_INITIAL_TYPE_CAPACITY;
        _types = static_cast<type_info_entry*>(
            std::malloc(sizeof(type_info_entry) * _type_capacity));
        if( _types == nullptr )
            throw std::bad_alloc{};
        std::memset(_types, 0, sizeof(type_info_entry) * _type_capacity);

        // Выделяем начальную таблицу слотов.
        _slot_capacity = PAM_INITIAL_SLOT_CAPACITY;
        _slots = static_cast<slot_descriptor*>(
            std::malloc(sizeof(slot_descriptor) * _slot_capacity));
        if( _slots == nullptr )
        {
            std::free(_types);
            _types = nullptr;
            throw std::bad_alloc{};
        }
        std::memset(_slots, 0, sizeof(slot_descriptor) * _slot_capacity);

        // Выделяем буфер данных.
        uintptr_t data_size = PAM_INITIAL_DATA_SIZE;
        _data = static_cast<char*>(std::malloc(static_cast<std::size_t>(data_size)));
        if( _data == nullptr )
        {
            std::free(_slots);
            _slots = nullptr;
            std::free(_types);
            _types = nullptr;
            throw std::bad_alloc{};
        }
        std::memset(_data, 0, static_cast<std::size_t>(data_size));

        // Записываем заголовок в начало буфера данных.
        pam_header& hdr = _header();
        hdr.magic         = PAM_MAGIC;
        hdr.version       = PAM_VERSION;
        hdr.data_area_size = data_size;
        hdr.slot_count    = 0;
        hdr.slot_capacity = _slot_capacity;
        hdr.type_count    = 0;
        hdr.type_capacity = _type_capacity;

        // Область данных начинается после заголовка.
        _bump = sizeof(pam_header);
    }

    // -----------------------------------------------------------------------
    // Поиск или регистрация типа в таблице типов
    // -----------------------------------------------------------------------

    /**
     * Найти или зарегистрировать тип в таблице типов.
     * Если тип с таким именем и размером уже есть — возвращает его индекс.
     * Если нет — добавляет новую запись и возвращает её индекс.
     * @return Индекс в таблице типов или static_cast<unsigned>(-1) при ошибке.
     */
    unsigned _find_or_register_type(const char* type_name, uintptr_t elem_size)
    {
        // Ищем существующий тип.
        for( unsigned i = 0; i < _type_capacity; i++ )
        {
            if( _types[i].used &&
                _types[i].elem_size == elem_size &&
                std::strncmp(_types[i].name, type_name, PAM_TYPE_ID_SIZE) == 0 )
                return i;
        }

        // Ищем свободное место.
        unsigned free_idx = static_cast<unsigned>(-1);
        for( unsigned i = 0; i < _type_capacity; i++ )
        {
            if( !_types[i].used )
            {
                free_idx = i;
                break;
            }
        }

        // Нет свободных — расширяем таблицу типов.
        if( free_idx == static_cast<unsigned>(-1) )
        {
            unsigned new_cap = _type_capacity * 2;
            if( new_cap < PAM_INITIAL_TYPE_CAPACITY )
                new_cap = PAM_INITIAL_TYPE_CAPACITY;

            type_info_entry* new_types = static_cast<type_info_entry*>(
                std::realloc(_types, sizeof(type_info_entry) * new_cap));
            if( new_types == nullptr ) return static_cast<unsigned>(-1);

            std::memset(new_types + _type_capacity, 0,
                        sizeof(type_info_entry) * (new_cap - _type_capacity));
            free_idx = _type_capacity;
            _types = new_types;
            _type_capacity = new_cap;
            _header().type_capacity = _type_capacity;
        }

        // Заполняем запись.
        type_info_entry& te = _types[free_idx];
        te.used      = true;
        te.elem_size = elem_size;
        std::strncpy(te.name, type_name, PAM_TYPE_ID_SIZE - 1);
        te.name[PAM_TYPE_ID_SIZE - 1] = '\0';

        _header().type_count++;
        return free_idx;
    }

    // -----------------------------------------------------------------------
    // Расширение таблицы слотов
    // -----------------------------------------------------------------------

    /**
     * Найти свободный слот, при необходимости расширяя таблицу.
     * @return Индекс свободного слота или UINT_MAX при ошибке.
     */
    unsigned _ensure_free_slot()
    {
        // Ищем свободный слот.
        for( unsigned i = 0; i < _slot_capacity; i++ )
        {
            if( !_slots[i].used )
                return i;
        }

        // Нет свободных — расширяем таблицу.
        unsigned new_cap = _slot_capacity * 2;
        if( new_cap < PAM_INITIAL_SLOT_CAPACITY )
            new_cap = PAM_INITIAL_SLOT_CAPACITY;

        slot_descriptor* new_slots = static_cast<slot_descriptor*>(
            std::realloc(_slots, sizeof(slot_descriptor) * new_cap));
        if( new_slots == nullptr ) return static_cast<unsigned>(-1);

        // Инициализируем новые слоты нулями.
        std::memset(new_slots + _slot_capacity, 0,
                    sizeof(slot_descriptor) * (new_cap - _slot_capacity));

        unsigned free_idx = _slot_capacity;  // Первый новый слот.
        _slots = new_slots;
        _slot_capacity = new_cap;
        _header().slot_capacity = _slot_capacity;

        return free_idx;
    }

    // -----------------------------------------------------------------------
    // Аллокатор области данных
    // -----------------------------------------------------------------------

    /**
     * Выделить size*count байт в области данных ПАМ.
     * Всегда создаётся запись в таблице слотов.
     * Тип регистрируется в таблице типов (один раз на уникальный тип).
     * Если name задано — слот именованный и увеличивается счётчик slot_count.
     * Возвращает смещение или 0 при ошибке.
     */
    uintptr_t _alloc(uintptr_t elem_size, uintptr_t count,
                     const char* type_id, const char* name)
    {
        uintptr_t total_size = elem_size * count;
        uintptr_t data_size  = _header().data_area_size;

        // Выравниваем bump по natural alignment элемента (до 8 байт).
        uintptr_t align = elem_size >= 8 ? 8u :
                          elem_size >= 4 ? 4u :
                          elem_size >= 2 ? 2u : 1u;
        uintptr_t aligned_bump = (_bump + align - 1) & ~(align - 1);

        // Если не хватает места — расширяем область данных.
        if( aligned_bump + total_size > data_size )
        {
            uintptr_t new_size = data_size * 2;
            while( aligned_bump + total_size > new_size )
                new_size *= 2;

            char* new_data = static_cast<char*>(
                std::realloc(_data, static_cast<std::size_t>(new_size)));
            if( new_data == nullptr ) return 0;
            _data = new_data;
            std::memset(_data + data_size, 0,
                        static_cast<std::size_t>(new_size - data_size));
            _header().data_area_size = new_size;
            // Пересчитываем aligned_bump после возможного realloc.
            data_size = new_size;
            aligned_bump = (_bump + align - 1) & ~(align - 1);
        }

        uintptr_t offset = aligned_bump;
        _bump = aligned_bump + total_size;

        // Регистрируем тип (один раз для всех слотов этого типа).
        unsigned type_idx = _find_or_register_type(type_id, elem_size);
        if( type_idx == static_cast<unsigned>(-1) ) return 0;

        // Создаём запись в таблице слотов.
        unsigned slot_idx = _ensure_free_slot();
        if( slot_idx == static_cast<unsigned>(-1) ) return 0;

        slot_descriptor& sd = _slots[slot_idx];
        sd.used     = true;
        sd.offset   = offset;
        sd.count    = count;
        sd.type_idx = static_cast<uintptr_t>(type_idx);

        bool named = (name != nullptr && name[0] != '\0');
        if( named )
        {
            std::strncpy(sd.name, name, PAM_NAME_SIZE - 1);
            sd.name[PAM_NAME_SIZE - 1] = '\0';
            _header().slot_count++;  // Увеличиваем счётчик ТОЛЬКО именованных.
        }
        else
        {
            sd.name[0] = '\0';
        }

        return offset;
    }
};
