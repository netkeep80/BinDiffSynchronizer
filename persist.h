#ifndef __PERSIST_H__
#define __PERSIST_H__

#include <cstdint>
#include <cstring>
#include <type_traits>
#include "pam.h"

/*
 * persist.h — Персистная инфраструктура фазы 2.
 *
 * Словарь:
 *   АМ  — адресный менеджер
 *   АП  — адресное пространство
 *   ПАП — персистное адресное пространство
 *   ПАМ — персистный адресный менеджер
 *
 * Требования (задача #45):
 *   Тр.1  — персистные объекты используют только персистные указатели
 *   Тр.2  — создание/удаление объектов — через специальные методы аллокатора
 *   Тр.3  — при запуске аллокатор инициализируется именем файла хранилища
 *   Тр.4  — единое ПАП для объектов разных типов
 *   Тр.5  — sizeof(fptr<T>) == sizeof(void*)
 *   Тр.6  — все комментарии в файле — на русском языке
 *   Тр.7  — никакой логики с именами файлов в persist<T>
 *   Тр.8  — sizeof(persist<T>) == sizeof(T)
 *   Тр.9  — никакой логики с именами файлов в fptr<T>
 *   Тр.10 — при загрузке образа ПАП конструкторы/деструкторы не вызываются
 *   Тр.11 — объекты persist<T> живут ТОЛЬКО в образе ПАП
 *   Тр.12 — доступ к персистным объектам — только через fptr<T>
 *   Тр.13 — fptr<T> может находиться как в обычной памяти, так и в ПАП
 *   Тр.14 — ПАМ — персистный объект, хранит имена объектов
 *   Тр.15 — fptr<T> инициализируется строковым именем объекта через ПАМ
 *   Тр.16 — ПАМ хранит карту объектов и их имена
 */

// ---------------------------------------------------------------------------
// Предварительные объявления
// ---------------------------------------------------------------------------

template <class _T> class persist;
template <class _T> class fptr;
template<class _T, unsigned AddressSpace> class AddressManager;

// ---------------------------------------------------------------------------
// persist<T> — обёртка для тривиально копируемого типа T.
//
// Требования:
//   Тр.7  — не содержит логики с именами файлов
//   Тр.8  — sizeof(persist<T>) == sizeof(T)
//   Тр.10 — конструкторы/деструкторы объектов при загрузке не вызываются
//   Тр.11 — объекты persist<T> должны жить только в образе ПАП
//
// Ограничение: T должен быть тривиально копируемым (static_assert ниже).
//
// Создание объектов persist<T> на стеке или как статических переменных
// запрещено: конструктор по умолчанию является private (Тр.11).
// Создание объектов — только через PersistentAddressSpace::Create<T>().
// ---------------------------------------------------------------------------
template <class _T>
class persist
{
    static_assert(std::is_trivially_copyable<_T>::value,
                  "persist<T> требует, чтобы T был тривиально копируемым");

    // Размер persist<T> == sizeof(T) (Тр.8)
    unsigned char _data[sizeof(_T)];

public:
    // Получение ссылки на хранимое значение.
    typedef _T& _Tref;
    typedef _T* _Tptr;

    operator _Tref()       { return *reinterpret_cast<_T*>(_data); }
    operator _Tref() const { return *reinterpret_cast<const _T*>(_data); }
    _T* operator&()        { return reinterpret_cast<_T*>(_data); }
    _Tref operator=(const _T& ref)
    {
        return (*reinterpret_cast<_T*>(_data)) = ref;
    }

private:
    // Создание persist<T> на стеке или как статической переменной запрещено.
    // Используйте PersistentAddressSpace::Create<persist<T>>() (Тр.11).
    persist() = default;

    // Разрешаем доступ к приватному конструктору только для фабричных методов.
    template<class U, unsigned A> friend class AddressManager;
    friend class PersistentAddressSpace;
};

// Проверяем требование Тр.8.
static_assert(sizeof(persist<int>) == sizeof(int),
              "sizeof(persist<T>) должен быть равен sizeof(T) (Тр.8)");
static_assert(sizeof(persist<double>) == sizeof(double),
              "sizeof(persist<T>) должен быть равен sizeof(T) (Тр.8)");

// ---------------------------------------------------------------------------
// fptr<T> — персистный указатель (хранит смещение в образе ПАП).
//
// Требования:
//   Тр.5  — sizeof(fptr<T>) == sizeof(void*)
//   Тр.9  — не содержит логики с именами файлов
//   Тр.12 — доступ к персистным объектам только через fptr<T>
//   Тр.13 — может находиться как в обычной, так и в персистной памяти
//   Тр.15 — метод find(name) для инициализации по имени объекта через ПАМ
// ---------------------------------------------------------------------------
template <class _T>
class fptr
{
    typedef _T& _Tref;
    typedef _T* _Tptr;

    /// Смещение объекта в области данных ПАП (или 0 = null).
    /// Размер == sizeof(void*) на целевой платформе (Тр.5).
    uintptr_t __addr;

public:
    /// Конструктор по умолчанию — нулевой указатель.
    inline fptr() : __addr(0) {}

    /// Конструктор копирования.
    inline fptr(const fptr<_T>&) = default;

    /// Деструктор — ничего не делает (не освобождает ресурсы).
    inline ~fptr() = default;

    // -----------------------------------------------------------------------
    // Инициализация по имени объекта в ПАМ (Тр.15)
    // -----------------------------------------------------------------------

    /**
     * Найти объект по имени в ПАМ и установить указатель.
     * @param name Строковое имя объекта (не имя файла).
     */
    void find(const char* name)
    {
        __addr = static_cast<uintptr_t>(
            PersistentAddressSpace::Get().FindTyped<_T>(name));
    }

    // -----------------------------------------------------------------------
    // Операции разыменования
    // -----------------------------------------------------------------------

    /// Разыменование — возвращает указатель на объект в ПАП.
    inline operator _Tptr()
    {
        return PersistentAddressSpace::Get().Resolve<_T>(__addr);
    }
    inline operator _Tptr() const
    {
        return PersistentAddressSpace::Get().Resolve<_T>(__addr);
    }

    inline _T& operator*()
    {
        return *PersistentAddressSpace::Get().Resolve<_T>(__addr);
    }
    inline const _T& operator*() const
    {
        return *PersistentAddressSpace::Get().Resolve<_T>(__addr);
    }

    inline _T* operator->()
    {
        return PersistentAddressSpace::Get().Resolve<_T>(__addr);
    }
    inline const _T* operator->() const
    {
        return PersistentAddressSpace::Get().Resolve<_T>(__addr);
    }

    /// Доступ к элементу массива по индексу.
    inline _T& operator[](unsigned idx)
    {
        return PersistentAddressSpace::Get().ResolveElement<_T>(__addr, idx);
    }
    inline const _T& operator[](unsigned idx) const
    {
        return PersistentAddressSpace::Get().ResolveElement<_T>(__addr, idx);
    }

    // -----------------------------------------------------------------------
    // Управление объектами через ПАМ (Тр.2)
    // -----------------------------------------------------------------------

    /**
     * Создать новый объект типа T в ПАП.
     * @param name Необязательное имя объекта.
     */
    void New(const char* name = nullptr)
    {
        __addr = static_cast<uintptr_t>(
            PersistentAddressSpace::Get().Create<_T>(name));
    }

    /**
     * Создать массив из count объектов типа T в ПАП.
     * @param count Число элементов.
     * @param name  Необязательное имя массива.
     */
    void NewArray(unsigned count, const char* name = nullptr)
    {
        __addr = static_cast<uintptr_t>(
            PersistentAddressSpace::Get().CreateArray<_T>(count, name));
    }

    /**
     * Удалить объект из ПАП. Сбрасывает указатель в 0.
     * Конструкторы/деструкторы не вызываются (Тр.10).
     */
    void Delete()
    {
        PersistentAddressSpace::Get().Delete(__addr);
        __addr = 0;
    }

    /**
     * Удалить массив из ПАП. Сбрасывает указатель в 0.
     */
    void DeleteArray()
    {
        PersistentAddressSpace::Get().Delete(__addr);
        __addr = 0;
    }

    // -----------------------------------------------------------------------
    // Вспомогательные методы
    // -----------------------------------------------------------------------

    /// Получить смещение объекта в ПАП.
    uintptr_t addr() const { return __addr; }

    /// Установить смещение объекта в ПАП вручную.
    void set_addr(uintptr_t a) { __addr = a; }

    /// Получить число элементов массива (через ПАМ).
    uintptr_t count() const
    {
        if( __addr == 0 ) return 0;
        return PersistentAddressSpace::Get().GetCount(__addr);
    }

    /// Сравнение с nullptr.
    bool operator==(std::nullptr_t) const { return __addr == 0; }
    bool operator!=(std::nullptr_t) const { return __addr != 0; }
};

// Проверяем требование Тр.5.
static_assert(sizeof(fptr<int>) == sizeof(void*),
              "sizeof(fptr<T>) должен быть равен sizeof(void*) (Тр.5)");
static_assert(sizeof(fptr<double>) == sizeof(void*),
              "sizeof(fptr<T>) должен быть равен sizeof(void*) (Тр.5)");

// ---------------------------------------------------------------------------
// Значение по умолчанию для размера адресного пространства (для AddressManager)
// ---------------------------------------------------------------------------

#define ADDRESS_SPACE 1024

// ---------------------------------------------------------------------------
// AddressManager<T, AddressSpace> — тонкий адаптер над PersistentAddressSpace.
//
// Обеспечивает обратную совместимость с кодом фазы 1 (pstring, pvector, pmap,
// pjson), который использует AddressManager<T>::CreateArray() и т.д.
// В фазе 2 вся логика делегируется в PersistentAddressSpace (Тр.4).
//
// Слот 0 зарезервирован как null/недопустимый (совместимость с фазой 1:
// fptr<T>::addr() == 0 означает null).
// ---------------------------------------------------------------------------
template<class _T, unsigned AddressSpace = ADDRESS_SPACE>
class AddressManager
{
    friend class persist<_T>;
    friend class fptr<_T>;

public:
    AddressManager() = default;
    ~AddressManager() = default;

    // -----------------------------------------------------------------------
    // Создание объектов
    // -----------------------------------------------------------------------

    /**
     * Создать один объект типа T в ПАП.
     * @param name Имя объекта (C-строка, может быть nullptr).
     * @return Смещение объекта в ПАП (ненулевое), или 0 при ошибке.
     */
    static uintptr_t Create(const char* name)
    {
        return PersistentAddressSpace::Get().Create<_T>(name);
    }

    /**
     * Создать массив из count объектов типа T в ПАП.
     * @param count Число элементов.
     * @param name  Имя массива (может быть nullptr).
     * @return Смещение первого элемента, или 0 при ошибке.
     */
    static uintptr_t CreateArray(unsigned count, const char* name)
    {
        return PersistentAddressSpace::Get().CreateArray<_T>(count, name);
    }

    // -----------------------------------------------------------------------
    // Удаление объектов
    // -----------------------------------------------------------------------

    /**
     * Освободить слот по смещению.
     */
    static void Delete(uintptr_t offset)
    {
        PersistentAddressSpace::Get().Delete(offset);
    }

    /**
     * Освободить массив по смещению (синоним Delete).
     */
    static void DeleteArray(uintptr_t offset)
    {
        PersistentAddressSpace::Get().Delete(offset);
    }

    // -----------------------------------------------------------------------
    // Поиск
    // -----------------------------------------------------------------------

    /**
     * Найти объект по имени.
     * @return Смещение или 0.
     */
    static uintptr_t Find(const char* name)
    {
        return PersistentAddressSpace::Get().Find(name);
    }

    /**
     * Найти слот по raw-указателю (обратный поиск).
     * @return Смещение или 0.
     */
    static uintptr_t FindByPtr(const _T* p)
    {
        return PersistentAddressSpace::Get().FindByPtr(
            static_cast<const void*>(p));
    }

    // -----------------------------------------------------------------------
    // Доступ к элементам
    // -----------------------------------------------------------------------

    /**
     * Получить счётчик элементов массива по смещению.
     */
    static uintptr_t GetCount(uintptr_t offset)
    {
        return PersistentAddressSpace::Get().GetCount(offset);
    }

    /**
     * Получить ссылку на элемент массива по смещению и индексу.
     */
    static _T& GetArrayElement(uintptr_t offset, uintptr_t elem)
    {
        return PersistentAddressSpace::Get().ResolveElement<_T>(offset, elem);
    }

    /**
     * Получить ссылку на объект (одиночный) по смещению.
     */
    static _T& GetObject(uintptr_t offset)
    {
        return *PersistentAddressSpace::Get().Resolve<_T>(offset);
    }

    /**
     * Получить экземпляр менеджера (для совместимости с фазой 1).
     */
    static AddressManager<_T, AddressSpace>& GetManager()
    {
        static AddressManager<_T, AddressSpace> _mgr;
        return _mgr;
    }

    /**
     * Оператор [] для доступа к объекту по смещению (совместимость).
     */
    _T& operator[](uintptr_t offset)
    {
        return *PersistentAddressSpace::Get().Resolve<_T>(offset);
    }
};

// ---------------------------------------------------------------------------
// Псевдонимы типов (совместимость с фазой 1)
// ---------------------------------------------------------------------------

typedef persist<char>           pchar;
typedef persist<unsigned char>  puchar;
typedef persist<short>          pshort;
typedef persist<unsigned short> pushort;
typedef persist<int>            pint;
typedef persist<unsigned>       punsigned;
typedef persist<long>           plong;
typedef persist<unsigned long>  pulong;
typedef persist<float>          pfloat;
typedef persist<double>         pdouble;

#endif // __PERSIST_H__
