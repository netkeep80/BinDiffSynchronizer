#pragma once
#include "persist.h"

// pvector<T> — персистный динамический массив, аналог std::vector<T>.
//
// Объекты pvector<T> могут находиться ТОЛЬКО внутри ПАП.
// Для работы с pvector<T> из обычного кода используйте fptr<pvector<T>>.
//
// Требования:
//   - Конструктор и деструктор приватные; создание только через ПАМ (Тр.2, Тр.11).
//   - При загрузке образа ПАП конструкторы не вызываются (Тр.10).
//   - T должен быть тривиально копируемым (static_assert ниже).
//   - Стратегия роста: удвоение ёмкости при заполнении (начальная ёмкость: 4).
//   - Пустой pvector имеет data.addr() == 0, size == 0, capacity == 0.
//
// Phase 3: поля size и capacity имеют тип uintptr_t для полной совместимости
//   с Phase 2 PAM API (PersistentAddressSpace использует uintptr_t).
//
// Использование:
//   fptr<pvector<int>> fv;
//   fv.New();          // выделяем pvector<int> в ПАП (нулевая инициализация)
//   fv->push_back(1);
//   fv->push_back(2);
//   // fv->size() == 2, (*fv)[0] == 1
//   fv->free();
//   fv.Delete();

template<typename T>
class pvector
{
    static_assert(std::is_trivially_copyable<T>::value,
                  "pvector<T> требует, чтобы T был тривиально копируемым");

    uintptr_t size_;      ///< Текущее число элементов; uintptr_t для совместимости с Phase 2
    uintptr_t capacity_;  ///< Выделенная ёмкость; uintptr_t для совместимости с Phase 2
    fptr<T>   data_;      ///< Смещение в ПАП для массива элементов; 0 = не выделено

    // grow: обеспечить ёмкость >= needed, при необходимости перевыделить память.
    void grow(uintptr_t needed)
    {
        if( needed <= capacity_ ) return;

        uintptr_t new_cap = (capacity_ == 0) ? 4 : capacity_ * 2;
        while( new_cap < needed ) new_cap *= 2;

        fptr<T> new_data;
        new_data.NewArray(static_cast<unsigned>(new_cap));

        // Копируем существующие элементы в новый буфер.
        for( uintptr_t i = 0; i < size_; i++ )
            new_data[static_cast<unsigned>(i)] = data_[static_cast<unsigned>(i)];

        // Освобождаем старый буфер.
        if( data_.addr() != 0 )
            data_.DeleteArray();

        data_     = new_data;
        capacity_ = new_cap;
    }

public:
    uintptr_t size()     const { return size_; }
    uintptr_t capacity() const { return capacity_; }
    bool      empty()    const { return size_ == 0; }

    void push_back(const T& val)
    {
        grow(size_ + 1);
        data_[static_cast<unsigned>(size_)] = val;
        size_++;
    }

    void pop_back()
    {
        if( size_ > 0 ) size_--;
    }

    T& operator[](uintptr_t idx)       { return data_[static_cast<unsigned>(idx)]; }
    const T& operator[](uintptr_t idx) const { return data_[static_cast<unsigned>(idx)]; }

    T& front()       { return data_[0]; }
    const T& front() const { return data_[0]; }

    T& back()       { return data_[static_cast<unsigned>(size_ - 1)]; }
    const T& back() const { return data_[static_cast<unsigned>(size_ - 1)]; }

    // clear: обнулить размер. НЕ освобождает выделенный буфер.
    void clear() { size_ = 0; }

    // free: полностью освободить выделенный буфер.
    void free()
    {
        if( data_.addr() != 0 )
            data_.DeleteArray();
        size_     = 0;
        capacity_ = 0;
    }

    // Простой итератор в стиле указателя.
    // Действителен, пока ПАМ жив и слот не освобождён.
    class iterator
    {
        pvector<T>* _pv;
        uintptr_t   _idx;
    public:
        iterator(pvector<T>* pv, uintptr_t idx) : _pv(pv), _idx(idx) {}
        T& operator*()  { return (*_pv)[_idx]; }
        T* operator->() { return &(*_pv)[_idx]; }
        iterator& operator++() { ++_idx; return *this; }
        iterator  operator++(int) { iterator tmp = *this; ++_idx; return tmp; }
        bool operator==(const iterator& o) const { return _idx == o._idx; }
        bool operator!=(const iterator& o) const { return _idx != o._idx; }
    };

    class const_iterator
    {
        const pvector<T>* _pv;
        uintptr_t         _idx;
    public:
        const_iterator(const pvector<T>* pv, uintptr_t idx) : _pv(pv), _idx(idx) {}
        const T& operator*()  const { return (*_pv)[_idx]; }
        const T* operator->() const { return &(*_pv)[_idx]; }
        const_iterator& operator++() { ++_idx; return *this; }
        const_iterator  operator++(int) { const_iterator tmp = *this; ++_idx; return tmp; }
        bool operator==(const const_iterator& o) const { return _idx == o._idx; }
        bool operator!=(const const_iterator& o) const { return _idx != o._idx; }
    };

    iterator begin() { return iterator(this, 0); }
    iterator end()   { return iterator(this, size_); }
    const_iterator begin() const { return const_iterator(this, 0); }
    const_iterator end()   const { return const_iterator(this, size_); }
    const_iterator cbegin() const { return const_iterator(this, 0); }
    const_iterator cend()   const { return const_iterator(this, size_); }

private:
    // Создание pvector<T> на стеке или как статической переменной запрещено.
    // Используйте fptr<pvector<T>>::New() для создания в ПАП (Тр.11).
    pvector() = default;
    ~pvector() = default;

    // Разрешаем доступ к приватному конструктору только для фабричных методов ПАМ.
    template<class U> friend class AddressManager;
    friend class PersistentAddressSpace;
};

// Phase 3: проверяем, что поля size_ и capacity_ имеют размер void*.
template<typename T>
struct pvector_size_check
{
    static_assert(sizeof(pvector<T>) == 3 * sizeof(void*),
                  "pvector<T> должен занимать 3 * sizeof(void*) байт (Phase 3)");
};

// Проверяем для конкретных типов.
static_assert(sizeof(pvector<int>) == 3 * sizeof(void*),
              "pvector<int> должен занимать 3 * sizeof(void*) байт (Phase 3)");
static_assert(sizeof(pvector<double>) == 3 * sizeof(void*),
              "pvector<double> должен занимать 3 * sizeof(void*) байт (Phase 3)");

// Примечание: pvector<T> НЕ является тривиально копируемым (private конструктор/деструктор),
// но это допустимо, поскольку ПАМ выделяет сырую память без вызова конструкторов (Тр.10).
// pvector<T> хранится в ПАП как сырые байты, без вызова конструктора/деструктора.
