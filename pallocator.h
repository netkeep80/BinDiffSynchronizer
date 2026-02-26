#pragma once
#include "persist.h"
#include <cstddef>
#include <limits>

// pallocator<T> — персистный STL-совместимый аллокатор.
//
// Реализован на основе AddressManager<T>. Выделяет/освобождает непрерывные массивы T
// в персистном адресном пространстве через CreateArray/DeleteArray.
//
// Ограничения:
//   - T должен быть тривиально копируемым (проверяется AddressManager<T>).
//   - Возвращает raw-указатели C++ (через разрешение смещения ПАП → указатель).
//     Указатель действителен, пока AddressManager<T> жив.
//   - Стандартные STL-контейнеры, использующие этот аллокатор
//     (например, std::vector<T, pallocator<T>>), живут только пока AddressManager<T> жив.
//   - Аллокатор сам по себе НЕ обеспечивает межпроцессную персистность —
//     для этого вызывающий код должен отдельно сохранять смещения (fptr<T>).
//
// Типичное использование:
//   std::vector<int, pallocator<int>> v;
//   v.push_back(42);

template<typename T>
class pallocator
{
public:
    using value_type      = T;
    using pointer         = T*;
    using const_pointer   = const T*;
    using reference       = T&;
    using const_reference = const T&;
    using size_type       = std::size_t;
    using difference_type = std::ptrdiff_t;

    template<typename U>
    struct rebind { using other = pallocator<U>; };

    pallocator() noexcept = default;
    pallocator(const pallocator&) noexcept = default;

    template<typename U>
    explicit pallocator(const pallocator<U>&) noexcept {}

    ~pallocator() noexcept = default;

    // allocate: создать n объектов в персистном адресном пространстве.
    // Возвращает raw-указатель, действительный на время жизни AddressManager<T>.
    pointer allocate(size_type n)
    {
        if( n == 0 ) return nullptr;
        uintptr_t offset = AddressManager<T>::CreateArray(
            static_cast<unsigned>(n), nullptr);
        if( offset == 0 )
            throw std::bad_alloc{};
        return &AddressManager<T>::GetArrayElement(offset, 0);
    }

    // deallocate: освободить массив по указателю.
    // Использует AddressManager<T>::FindByPtr() для обратного поиска смещения по указателю.
    void deallocate(pointer p, size_type /*n*/) noexcept
    {
        if( p == nullptr ) return;
        uintptr_t offset = AddressManager<T>::FindByPtr(p);
        if( offset != 0 )
        {
            AddressManager<T>::DeleteArray(offset);
        }
        else
        {
            // Если указатель не найден в ПАП — удаляем через raw delete[].
            // Это обрабатывает случай, когда указатель не из персистного источника.
            delete[] reinterpret_cast<char*>(p);
        }
    }

    size_type max_size() const noexcept
    {
        return std::numeric_limits<size_type>::max() / sizeof(T);
    }

    template<typename U, typename... Args>
    void construct(U* p, Args&&... args)
    {
        ::new(static_cast<void*>(p)) U(std::forward<Args>(args)...);
    }

    template<typename U>
    void destroy(U* p)
    {
        p->~U();
    }

    bool operator==(const pallocator&) const noexcept { return true; }
    bool operator!=(const pallocator&) const noexcept { return false; }
};
