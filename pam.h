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
 * Структура файла ПАМ:
 *   [pam_header]              — заголовок: магия, версия, размер области данных,
 *                               число именованных слотов, ёмкость слотов
 *   [slot_descriptor * cap]   — таблица слотов (дескрипторов именованных объектов),
 *                               ёмкость сохраняется в заголовке
 *   [байты объектов]          — область данных (смежный байтовый пул)
 *
 * Ключевые требования (из задачи #45, #54):
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
 */

// ---------------------------------------------------------------------------
// Константы ПАМ
// ---------------------------------------------------------------------------

/// Магическое число для идентификации файла ПАМ.
constexpr uint32_t PAM_MAGIC   = 0x50414D00u;  // 'PAM\0'

/// Версия формата файла ПАМ.
constexpr uint32_t PAM_VERSION = 2u;

/// Максимальная длина идентификатора типа в дескрипторе слота.
constexpr unsigned PAM_TYPE_ID_SIZE = 64u;

/// Максимальная длина имени объекта в дескрипторе слота.
constexpr unsigned PAM_NAME_SIZE = 64u;

/// Начальный размер области данных ПАМ (байт) — 10 КБ.
constexpr uintptr_t PAM_INITIAL_DATA_SIZE = 10u * 1024u;

/// Начальная ёмкость таблицы именованных слотов (число слотов).
constexpr unsigned PAM_INITIAL_SLOT_CAPACITY = 16u;

// ---------------------------------------------------------------------------
// slot_descriptor — дескриптор одного именованного объекта в таблице ПАМ
// ---------------------------------------------------------------------------

/// Дескриптор именованного объекта в едином ПАП.
/// Все поля фиксированного размера, тривиально копируемы.
/// Слоты создаются ТОЛЬКО для объектов, имеющих строковое имя.
struct slot_descriptor
{
    bool      used;                      ///< Слот занят
    uintptr_t offset;                    ///< Смещение объекта в области данных ПАП
    uintptr_t size;                      ///< Размер объекта в байтах
    uintptr_t count;                     ///< Количество элементов (для массивов)
    char      type_id[PAM_TYPE_ID_SIZE]; ///< Имя типа (из typeid(T).name())
    char      name[PAM_NAME_SIZE];       ///< Имя объекта (не имя файла)
};

static_assert(std::is_trivially_copyable<slot_descriptor>::value,
              "slot_descriptor должен быть тривиально копируемым");

// ---------------------------------------------------------------------------
// pam_header — заголовок файла ПАМ
// ---------------------------------------------------------------------------

/// Заголовок файла персистного адресного пространства.
struct pam_header
{
    uint32_t  magic;          ///< PAM_MAGIC — признак файла ПАМ
    uint32_t  version;        ///< PAM_VERSION — версия формата
    uintptr_t data_area_size; ///< Размер области данных в байтах
    unsigned  slot_count;     ///< Число именованных слотов (только named)
    unsigned  slot_capacity;  ///< Ёмкость таблицы слотов (число записей в файле)
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
 * конструкторов. Хранит динамическую таблицу слотов (_slots) только для
 * ИМЕНОВАННЫХ объектов. Безымянные объекты выделяются из области данных
 * напрямую (bump-аллокатор) без создания слота.
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
     *              Если nullptr — память выделяется без слота.
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
                _slots[i].used = false;
                _slots[i].offset = 0;
                _slots[i].size = 0;
                _slots[i].count = 0;
                _slots[i].type_id[0] = '\0';
                _slots[i].name[0] = '\0';
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
        for( unsigned i = 0; i < _slot_capacity; i++ )
        {
            if( _slots[i].used &&
                std::strncmp(_slots[i].name, name, PAM_NAME_SIZE) == 0 &&
                std::strncmp(_slots[i].type_id, typeid(T).name(), PAM_TYPE_ID_SIZE) == 0 )
                return _slots[i].offset;
        }
        return 0;
    }

    /**
     * Найти смещение по указателю (для именованных объектов).
     * @return Смещение объекта или 0, если указатель не принадлежит именованному объекту ПАМ.
     */
    uintptr_t FindByPtr(const void* p) const
    {
        if( p == nullptr ) return 0;
        const char* ptr = static_cast<const char*>(p);
        const char* base = _data;
        uintptr_t data_size = _header_const().data_area_size;
        if( ptr < base || ptr >= base + data_size ) return 0;
        uintptr_t offset = static_cast<uintptr_t>(ptr - base);
        // Проверяем именованные слоты.
        for( unsigned i = 0; i < _slot_capacity; i++ )
        {
            if( _slots[i].used && _slots[i].offset == offset )
                return offset;
        }
        return 0;
    }

    /**
     * Получить счётчик элементов для именованного слота по смещению.
     * @return Число элементов или 0 (в т.ч. для безымянных объектов).
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
     * Сохранить весь образ ПАМ в файл.
     * Записывает: заголовок, таблицу именованных слотов, область данных.
     */
    void Save()
    {
        if( _filename[0] == '\0' ) return;
        std::FILE* f = std::fopen(_filename, "wb");
        if( f == nullptr ) return;
        std::fwrite(&_header(), sizeof(pam_header), 1, f);
        std::fwrite(_slots, sizeof(slot_descriptor), _slot_capacity, f);
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
    }

private:
    // -----------------------------------------------------------------------
    // Внутреннее состояние
    // -----------------------------------------------------------------------

    char              _filename[256];  ///< Путь к файлу ПАМ
    char*             _data;           ///< Буфер области данных
    slot_descriptor*  _slots;          ///< Динамическая таблица именованных слотов
    unsigned          _slot_capacity;  ///< Текущая ёмкость таблицы слотов

    // Заголовок хранится в начале буфера _data как pam_header.
    // _data[0..sizeof(pam_header)-1] — заголовок.
    // Слоты хранятся отдельно (_slots — динамический буфер).
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

        unsigned cap = hdr.slot_capacity;
        if( cap < PAM_INITIAL_SLOT_CAPACITY )
            cap = PAM_INITIAL_SLOT_CAPACITY;

        // Выделяем таблицу слотов.
        _slots = static_cast<slot_descriptor*>(
            std::malloc(sizeof(slot_descriptor) * cap));
        if( _slots == nullptr )
        {
            std::fclose(f);
            throw std::bad_alloc{};
        }
        std::memset(_slots, 0, sizeof(slot_descriptor) * cap);
        _slot_capacity = cap;

        // Считываем таблицу слотов из файла (только сохранённое количество).
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

        // Обновляем заголовок в буфере данных.
        _header() = hdr;
        _header().slot_capacity = _slot_capacity;

        // Восстанавливаем bump из уже занятых слотов.
        _bump = sizeof(pam_header);
        for( unsigned i = 0; i < _slot_capacity; i++ )
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
        // Выделяем начальную таблицу слотов.
        _slot_capacity = PAM_INITIAL_SLOT_CAPACITY;
        _slots = static_cast<slot_descriptor*>(
            std::malloc(sizeof(slot_descriptor) * _slot_capacity));
        if( _slots == nullptr )
            throw std::bad_alloc{};
        std::memset(_slots, 0, sizeof(slot_descriptor) * _slot_capacity);

        // Выделяем буфер данных.
        uintptr_t data_size = PAM_INITIAL_DATA_SIZE;
        _data = static_cast<char*>(std::malloc(static_cast<std::size_t>(data_size)));
        if( _data == nullptr )
        {
            std::free(_slots);
            _slots = nullptr;
            throw std::bad_alloc{};
        }
        std::memset(_data, 0, static_cast<std::size_t>(data_size));

        // Записываем заголовок в начало буфера данных.
        pam_header& hdr = _header();
        hdr.magic          = PAM_MAGIC;
        hdr.version        = PAM_VERSION;
        hdr.data_area_size = data_size;
        hdr.slot_count     = 0;
        hdr.slot_capacity  = _slot_capacity;

        // Область данных начинается после заголовка.
        _bump = sizeof(pam_header);
    }

    // -----------------------------------------------------------------------
    // Расширение таблицы именованных слотов
    // -----------------------------------------------------------------------

    /**
     * Расширить таблицу слотов, если свободных нет.
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
     * Всегда создаётся запись в таблице слотов (нужна для GetCount/Delete/FindByPtr).
     * Если name задано — слот именованный (для поиска по имени),
     * и увеличивается счётчик slot_count (число ИМЕНОВАННЫХ объектов).
     * Безымянные слоты не увеличивают slot_count в заголовке.
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
            // Пересчитываем aligned_bump после возможного realloc (размер мог измениться).
            data_size = new_size;
            aligned_bump = (_bump + align - 1) & ~(align - 1);
        }

        uintptr_t offset = aligned_bump;
        _bump = aligned_bump + total_size;

        // Всегда создаём запись в таблице слотов (нужна для GetCount/Delete/FindByPtr).
        unsigned slot_idx = _ensure_free_slot();
        if( slot_idx == static_cast<unsigned>(-1) ) return 0;  // Ошибка выделения слота.

        slot_descriptor& sd = _slots[slot_idx];
        sd.used   = true;
        sd.offset = offset;
        sd.size   = elem_size;
        sd.count  = count;
        if( type_id != nullptr )
            std::strncpy(sd.type_id, type_id, PAM_TYPE_ID_SIZE - 1);
        else
            sd.type_id[0] = '\0';
        sd.type_id[PAM_TYPE_ID_SIZE - 1] = '\0';

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
