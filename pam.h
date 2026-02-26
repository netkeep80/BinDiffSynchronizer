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
 * произвольных типов. ПАП хранится в одном бинарном файле и загружается
 * в оперативную память без вызова конструкторов объектов.
 *
 * Структура файла ПАП:
 *   [pap_header]              — заголовок: магия, версия, размер области данных
 *   [slot_descriptor * N]     — таблица слотов (дескрипторов объектов)
 *   [байты объектов]          — область данных (смежный байтовый пул)
 *
 * Ключевые требования (из задачи #45):
 *   Тр.4  — единое ПАП для объектов разных типов
 *   Тр.5  — sizeof(fptr<T>) == sizeof(void*)
 *   Тр.10 — при загрузке образа ПАП конструкторы объектов не вызываются
 *   Тр.14 — ПАМ сам является персистным объектом и хранит имена объектов
 *   Тр.15 — fptr<T> может инициализироваться строками-именами объектов
 *   Тр.16 — ПАМ хранит карту объектов и их имена
 */

// ---------------------------------------------------------------------------
// Константы ПАП
// ---------------------------------------------------------------------------

/// Магическое число для идентификации файла ПАП.
constexpr uint32_t PAP_MAGIC   = 0x50415000u;  // 'PAP\0'

/// Версия формата файла ПАП.
constexpr uint32_t PAP_VERSION = 1u;

/// Максимальное число слотов в таблице дескрипторов.
constexpr unsigned PAP_MAX_SLOTS = 4096u;

/// Максимальная длина идентификатора типа в дескрипторе слота.
constexpr unsigned PAP_TYPE_ID_SIZE = 64u;

/// Максимальная длина имени объекта в дескрипторе слота.
constexpr unsigned PAP_NAME_SIZE = 64u;

/// Начальный размер области данных ПАП (байт).
constexpr uintptr_t PAP_INITIAL_DATA_SIZE = 1024u * 1024u;  // 1 МБ

// ---------------------------------------------------------------------------
// slot_descriptor — дескриптор одного слота в таблице ПАМ
// ---------------------------------------------------------------------------

/// Дескриптор одного объекта в едином ПАП.
/// Все поля фиксированного размера, тривиально копируемы.
struct slot_descriptor
{
    bool      used;                      ///< Слот занят
    uintptr_t offset;                    ///< Смещение объекта в области данных ПАП
    uintptr_t size;                      ///< Размер объекта в байтах
    uintptr_t count;                     ///< Количество элементов (для массивов)
    char      type_id[PAP_TYPE_ID_SIZE]; ///< Имя типа (из typeid(T).name())
    char      name[PAP_NAME_SIZE];       ///< Имя объекта (не имя файла)
};

static_assert(std::is_trivially_copyable<slot_descriptor>::value,
              "slot_descriptor должен быть тривиально копируемым");

// ---------------------------------------------------------------------------
// pap_header — заголовок файла ПАП
// ---------------------------------------------------------------------------

/// Заголовок файла персистного адресного пространства.
struct pap_header
{
    uint32_t  magic;          ///< PAP_MAGIC — признак файла ПАП
    uint32_t  version;        ///< PAP_VERSION — версия формата
    uintptr_t data_area_size; ///< Размер области данных в байтах
    unsigned  slot_count;     ///< Число использованных слотов
};

static_assert(std::is_trivially_copyable<pap_header>::value,
              "pap_header должен быть тривиально копируемым");

// ---------------------------------------------------------------------------
// PersistentAddressSpace — единый персистный адресный менеджер
// ---------------------------------------------------------------------------

/**
 * Единый менеджер персистного адресного пространства (ПАМ).
 *
 * Управляет плоским байтовым буфером (_data), загружаемым из файла без вызова
 * конструкторов. Хранит таблицу слотов (_slots) с описанием каждого объекта.
 *
 * Использование:
 *   PersistentAddressSpace::Init("myapp.pap");
 *   auto& pam = PersistentAddressSpace::Get();
 *   uintptr_t off = pam.Create<int>("counter");
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
     * @return Смещение (offset) объекта в области данных ПАП.
     *         0 означает ошибку (нет свободных слотов или памяти).
     */
    template<class T>
    uintptr_t Create(const char* name = nullptr)
    {
        return _alloc_slot(sizeof(T), 1, typeid(T).name(), name);
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
        return _alloc_slot(sizeof(T), count, typeid(T).name(), name);
    }

    // -----------------------------------------------------------------------
    // Удаление объектов
    // -----------------------------------------------------------------------

    /**
     * Освободить слот, занятый объектом по смещению offset.
     * Конструкторы/деструкторы НЕ вызываются (Тр.10).
     */
    void Delete(uintptr_t offset)
    {
        if( offset == 0 ) return;
        for( unsigned i = 1; i < PAP_MAX_SLOTS; i++ )
        {
            if( _slots[i].used && _slots[i].offset == offset )
            {
                _slots[i].used = false;
                _slots[i].offset = 0;
                _slots[i].size = 0;
                _slots[i].count = 0;
                _slots[i].type_id[0] = '\0';
                _slots[i].name[0] = '\0';
                if( _header().slot_count > 0 )
                    _header().slot_count--;
                return;
            }
        }
    }

    // -----------------------------------------------------------------------
    // Поиск по имени
    // -----------------------------------------------------------------------

    /**
     * Найти объект по имени.
     * @return Смещение объекта или 0, если не найден.
     */
    uintptr_t Find(const char* name) const
    {
        if( name == nullptr || name[0] == '\0' ) return 0;
        for( unsigned i = 1; i < PAP_MAX_SLOTS; i++ )
        {
            if( _slots[i].used &&
                std::strncmp(_slots[i].name, name, PAP_NAME_SIZE) == 0 )
                return _slots[i].offset;
        }
        return 0;
    }

    /**
     * Найти объект по имени с проверкой типа.
     * @return Смещение объекта или 0, если не найден или тип не совпадает.
     */
    template<class T>
    uintptr_t FindTyped(const char* name) const
    {
        if( name == nullptr || name[0] == '\0' ) return 0;
        for( unsigned i = 1; i < PAP_MAX_SLOTS; i++ )
        {
            if( _slots[i].used &&
                std::strncmp(_slots[i].name, name, PAP_NAME_SIZE) == 0 &&
                std::strncmp(_slots[i].type_id, typeid(T).name(), PAP_TYPE_ID_SIZE) == 0 )
                return _slots[i].offset;
        }
        return 0;
    }

    /**
     * Найти смещение по указателю (для обратного поиска слота из raw-указателя).
     * @return Смещение объекта или 0, если указатель не принадлежит ПАП.
     */
    uintptr_t FindByPtr(const void* p) const
    {
        if( p == nullptr ) return 0;
        const char* ptr = static_cast<const char*>(p);
        const char* base = _data;
        uintptr_t data_size = _header_const().data_area_size;
        if( ptr < base || ptr >= base + data_size ) return 0;
        uintptr_t offset = static_cast<uintptr_t>(ptr - base);
        // Проверяем, совпадает ли смещение с началом какого-либо слота.
        for( unsigned i = 1; i < PAP_MAX_SLOTS; i++ )
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
        for( unsigned i = 1; i < PAP_MAX_SLOTS; i++ )
        {
            if( _slots[i].used && _slots[i].offset == offset )
                return _slots[i].count;
        }
        return 0;
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
     * @return Ссылка на элемент.
     */
    template<class T>
    T& ResolveElement(uintptr_t offset, uintptr_t index)
    {
        return reinterpret_cast<T*>(_data + offset)[index];
    }

    template<class T>
    const T& ResolveElement(uintptr_t offset, uintptr_t index) const
    {
        return reinterpret_cast<const T*>(_data + offset)[index];
    }

    // -----------------------------------------------------------------------
    // Сохранение
    // -----------------------------------------------------------------------

    /**
     * Сохранить весь образ ПАП в файл.
     * Записывает: заголовок, таблицу слотов, область данных.
     */
    void Save()
    {
        if( _filename[0] == '\0' ) return;
        std::FILE* f = std::fopen(_filename, "wb");
        if( f == nullptr ) return;
        std::fwrite(&_header(), sizeof(pap_header), 1, f);
        std::fwrite(_slots, sizeof(slot_descriptor), PAP_MAX_SLOTS, f);
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
    }

private:
    // -----------------------------------------------------------------------
    // Внутреннее состояние
    // -----------------------------------------------------------------------

    char            _filename[256];           ///< Путь к файлу ПАП
    char*           _data;                    ///< Буфер области данных
    slot_descriptor _slots[PAP_MAX_SLOTS];    ///< Таблица слотов

    // Заголовок хранится в начале буфера _data как pap_header.
    // _data[0..sizeof(pap_header)-1] — заголовок.
    // Область данных начинается сразу после заголовка + таблицы слотов?
    // Нет — для простоты заголовок и слоты хранятся отдельно (_slots[]),
    // _data хранит только объекты (смещения отсчитываются от 0 внутри _data).

    /// Счётчик следующего доступного смещения в области данных (bump-allocator).
    uintptr_t _bump;

    // -----------------------------------------------------------------------
    // Доступ к заголовку
    // -----------------------------------------------------------------------

    pap_header& _header()
    {
        return *reinterpret_cast<pap_header*>(_data);
    }

    const pap_header& _header_const() const
    {
        return *reinterpret_cast<const pap_header*>(_data);
    }

    // -----------------------------------------------------------------------
    // Конструктор и синглтон
    // -----------------------------------------------------------------------

    PersistentAddressSpace()
        : _data(nullptr)
        , _bump(sizeof(pap_header))
    {
        _filename[0] = '\0';
        std::memset(_slots, 0, sizeof(_slots));
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

        std::FILE* f = std::fopen(filename, "rb");
        if( f == nullptr )
        {
            // Файл не существует — создаём пустой образ.
            _init_empty();
            return;
        }

        // Считываем заголовок.
        pap_header hdr{};
        if( std::fread(&hdr, sizeof(pap_header), 1, f) != 1 ||
            hdr.magic != PAP_MAGIC || hdr.version != PAP_VERSION )
        {
            std::fclose(f);
            _init_empty();
            return;
        }

        // Считываем таблицу слотов.
        if( std::fread(_slots, sizeof(slot_descriptor), PAP_MAX_SLOTS, f)
                != PAP_MAX_SLOTS )
        {
            std::fclose(f);
            _init_empty();
            return;
        }

        // Выделяем буфер и считываем область данных.
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

        // Восстанавливаем bump из уже занятых слотов.
        _bump = sizeof(pap_header);
        for( unsigned i = 1; i < PAP_MAX_SLOTS; i++ )
        {
            if( _slots[i].used )
            {
                uintptr_t end = _slots[i].offset + _slots[i].size * _slots[i].count;
                if( end > _bump ) _bump = end;
            }
        }
    }

    // -----------------------------------------------------------------------
    // Инициализация пустого образа
    // -----------------------------------------------------------------------

    void _init_empty()
    {
        uintptr_t data_size = PAP_INITIAL_DATA_SIZE;
        _data = static_cast<char*>(std::malloc(static_cast<std::size_t>(data_size)));
        if( _data == nullptr )
            throw std::bad_alloc{};
        std::memset(_data, 0, static_cast<std::size_t>(data_size));
        std::memset(_slots, 0, sizeof(_slots));

        // Записываем заголовок в начало буфера данных.
        pap_header& hdr = _header();
        hdr.magic          = PAP_MAGIC;
        hdr.version        = PAP_VERSION;
        hdr.data_area_size = data_size;
        hdr.slot_count     = 0;

        // Область данных начинается после заголовка.
        _bump = sizeof(pap_header);
    }

    // -----------------------------------------------------------------------
    // Bump-аллокатор области данных
    // -----------------------------------------------------------------------

    /**
     * Выделить size*count байт в области данных ПАП.
     * Записывает дескриптор слота и возвращает смещение.
     * Возвращает 0 при ошибке (нет слотов или памяти).
     */
    uintptr_t _alloc_slot(uintptr_t elem_size, uintptr_t count,
                          const char* type_id, const char* name)
    {
        // Найти свободный слот (слот 0 зарезервирован как null).
        unsigned slot_idx = 0;
        for( unsigned i = 1; i < PAP_MAX_SLOTS; i++ )
        {
            if( !_slots[i].used )
            {
                slot_idx = i;
                break;
            }
        }
        if( slot_idx == 0 ) return 0;  // Нет свободных слотов.

        uintptr_t total_size = elem_size * count;
        uintptr_t data_size  = _header().data_area_size;

        // Если не хватает места — расширяем область данных.
        if( _bump + total_size > data_size )
        {
            uintptr_t new_size = data_size * 2;
            while( _bump + total_size > new_size )
                new_size *= 2;

            char* new_data = static_cast<char*>(
                std::realloc(_data, static_cast<std::size_t>(new_size)));
            if( new_data == nullptr ) return 0;
            _data = new_data;
            std::memset(_data + data_size, 0,
                        static_cast<std::size_t>(new_size - data_size));
            _header().data_area_size = new_size;
        }

        uintptr_t offset = _bump;
        _bump += total_size;

        // Заполняем дескриптор слота.
        slot_descriptor& sd = _slots[slot_idx];
        sd.used   = true;
        sd.offset = offset;
        sd.size   = elem_size;
        sd.count  = count;
        if( type_id != nullptr )
            std::strncpy(sd.type_id, type_id, PAP_TYPE_ID_SIZE - 1);
        else
            sd.type_id[0] = '\0';
        sd.type_id[PAP_TYPE_ID_SIZE - 1] = '\0';
        if( name != nullptr )
            std::strncpy(sd.name, name, PAP_NAME_SIZE - 1);
        else
            sd.name[0] = '\0';
        sd.name[PAP_NAME_SIZE - 1] = '\0';

        _header().slot_count++;
        return offset;
    }
};
