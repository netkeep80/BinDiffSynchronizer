#pragma once
/**
 * @file pallocator_pmm.h
 * @brief STL-совместимый аллокатор на базе PMM (Задача 14.7).
 *
 * Этот файл реализует pallocator_pmm<T> — STL-совместимый аллокатор,
 * использующий PMM (pam_pmm.h) в качестве бэкенда для выделения памяти.
 *
 * Ключевые отличия от оригинального pallocator<T>:
 *   - Использует pam_pmm_create_array<T>() вместо PersistentAddressSpace::CreateArray<T>()
 *   - Использует pam_pmm_delete() вместо PersistentAddressSpace::Delete()
 *   - Использует pmm_resolve<T>() вместо Resolve<T>()
 *
 * Ограничения:
 *   - Возвращает raw-указатели C++ (через разрешение смещения ПАП -> указатель).
 *   - Указатель действителен, пока PMM жив и данные не деаллоцированы.
 *   - Стандартные STL-контейнеры с этим аллокатором живут только пока PMM жив.
 *   - Аллокатор сам по себе НЕ обеспечивает межпроцессную персистность —
 *     для этого вызывающий код должен отдельно сохранять смещения (fptr_pmm<T>).
 *
 * Типичное использование:
 *   std::vector<int, pjson::pallocator_pmm<int>> v;
 *   v.push_back(42);
 *
 * @see plan.md Задача 14.7 — Миграция persist<T> и fptr<T>
 * @see pam_pmm.h — фасад PMM для управления ПАП
 */

#include "pam_pmm.h"
#include <cstddef>
#include <limits>
#include <new>
#include <type_traits>

namespace pjson
{

// ═══════════════════════════════════════════════════════════════════════════
// pallocator_pmm<T> — STL-совместимый аллокатор на базе PMM
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief STL-совместимый аллокатор, использующий PMM для выделения памяти.
 *
 * Выделяет/освобождает непрерывные массивы T в персистном адресном
 * пространстве через pam_pmm_create_array()/pam_pmm_delete().
 *
 * @tparam T Тип элементов. Должен быть тривиально копируемым для персистности.
 */
template <typename T> class pallocator_pmm
{
    static_assert( std::is_trivially_copyable<T>::value,
                   "pallocator_pmm<T> требует, чтобы T был тривиально копируемым" );

  public:
    // -------------------------------------------------------------------------
    // Типы, требуемые стандартом C++ для аллокаторов
    // -------------------------------------------------------------------------

    using value_type      = T;
    using pointer         = T*;
    using const_pointer   = const T*;
    using reference       = T&;
    using const_reference = const T&;
    using size_type       = std::size_t;
    using difference_type = std::ptrdiff_t;

    template <typename U> struct rebind
    {
        using other = pallocator_pmm<U>;
    };

    // -------------------------------------------------------------------------
    // Конструкторы и деструктор
    // -------------------------------------------------------------------------

    pallocator_pmm() noexcept                        = default;
    pallocator_pmm( const pallocator_pmm& ) noexcept = default;

    template <typename U> explicit pallocator_pmm( const pallocator_pmm<U>& ) noexcept {}

    ~pallocator_pmm() noexcept = default;

    // -------------------------------------------------------------------------
    // Аллокация и деаллокация
    // -------------------------------------------------------------------------

    /**
     * @brief Выделить память для n объектов типа T.
     *
     * Создаёт массив в PMM и возвращает raw-указатель, действительный
     * на время жизни PMM.
     *
     * @param n Количество объектов.
     * @return Указатель на первый элемент массива.
     * @throw std::bad_alloc Если не удалось выделить память.
     */
    pointer allocate( size_type n )
    {
        if ( n == 0 )
            return nullptr;

        uintptr_t offset = pam_pmm_create_array<T>( static_cast<unsigned>( n ), nullptr );
        if ( offset == 0 )
            throw std::bad_alloc{};

        return pmm_resolve<T>( offset );
    }

    /**
     * @brief Освободить память, выделенную ранее через allocate().
     *
     * Находит смещение по указателю и освобождает слот в PMM.
     *
     * @param p Указатель, полученный от allocate().
     * @param n Количество объектов (игнорируется, PMM знает размер слота).
     */
    void deallocate( pointer p, size_type /*n*/ ) noexcept
    {
        if ( p == nullptr )
            return;

        uintptr_t offset = pam_pmm_ptr_to_offset( static_cast<const void*>( p ) );
        if ( offset != 0 )
        {
            pam_pmm_delete( offset );
        }
        // Если указатель не найден в PMM — ничего не делаем.
        // Это отличие от оригинального pallocator, который вызывал delete[].
    }

    // -------------------------------------------------------------------------
    // Максимальный размер
    // -------------------------------------------------------------------------

    /**
     * @brief Максимальное количество объектов, которое можно выделить.
     */
    size_type max_size() const noexcept { return std::numeric_limits<size_type>::max() / sizeof( T ); }

    // -------------------------------------------------------------------------
    // Конструирование и разрушение объектов
    // -------------------------------------------------------------------------

    /**
     * @brief Конструирование объекта в выделенной памяти.
     *
     * Использует placement new для вызова конструктора.
     */
    template <typename U, typename... Args> void construct( U* p, Args&&... args )
    {
        ::new ( static_cast<void*>( p ) ) U( std::forward<Args>( args )... );
    }

    /**
     * @brief Разрушение объекта.
     *
     * Вызывает деструктор объекта.
     */
    template <typename U> void destroy( U* p ) { p->~U(); }

    // -------------------------------------------------------------------------
    // Сравнение аллокаторов
    // -------------------------------------------------------------------------

    /**
     * @brief Все экземпляры pallocator_pmm считаются равными.
     *
     * Это означает, что память, выделенная одним аллокатором,
     * может быть освобождена другим.
     */
    bool operator==( const pallocator_pmm& ) const noexcept { return true; }
    bool operator!=( const pallocator_pmm& ) const noexcept { return false; }
};

// ═══════════════════════════════════════════════════════════════════════════
// АЛИАС pallocator ДЛЯ СОВМЕСТИМОСТИ (в пространстве имён pjson)
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Алиас pallocator<T> = pallocator_pmm<T> для упрощения миграции.
 *
 * Используйте pjson::pallocator<T> в новом коде для работы с PMM-бэкендом.
 * Оригинальный ::pallocator<T> из pallocator.h использует PersistentAddressSpace.
 */
template <typename T> using pallocator = pallocator_pmm<T>;

} // namespace pjson
