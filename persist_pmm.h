#pragma once
/**
 * @file persist_pmm.h
 * @brief Обёртка persist<T> на базе PMM (Задача 14.7).
 *
 * Этот файл реализует persist_pmm<T> — обёртку для тривиально копируемых типов,
 * совместимую по API с оригинальным persist<T> из persist.h.
 *
 * В контексте PMM класс persist<T> фактически не нужен:
 *   - PMM сам гарантирует работу с тривиально копируемыми типами
 *   - pptr<T>::resolve() возвращает корректный указатель на данные
 *   - Тип T аллоцируется через PamManager::allocate_typed<T>()
 *
 * Этот файл предоставляется для обратной совместимости с кодом,
 * который использует persist<T>.
 *
 * Требования (из persist.h):
 *   Тр.7  — не содержит логики с именами файлов
 *   Тр.8  — sizeof(persist<T>) == sizeof(T)
 *   Тр.10 — конструкторы/деструкторы объектов при загрузке не вызываются
 *   Тр.11 — объекты persist<T> должны жить только в образе ПАП
 *
 * @see plan.md Задача 14.7 — Миграция persist<T> и fptr<T>
 * @see fptr_pmm.h — персистный указатель на базе PMM
 */

#include "pam_pmm_config.h"
#include <cstdint>
#include <type_traits>

namespace pjson
{

// ═══════════════════════════════════════════════════════════════════════════
// persist_pmm<T> — обёртка для тривиально копируемого типа T
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Обёртка для тривиально копируемого типа T.
 *
 * В PMM эта обёртка технически избыточна, так как PMM напрямую работает
 * с тривиально копируемыми типами. Однако она сохранена для обратной
 * совместимости с существующим кодом.
 *
 * @tparam T Тип данных. Должен быть тривиально копируемым.
 *
 * @note Объекты persist_pmm<T> должны создаваться только через
 *       pam_pmm_create<persist_pmm<T>>() или fptr_pmm<persist_pmm<T>>::New().
 */
template <typename T> class persist_pmm
{
    static_assert( std::is_trivially_copyable<T>::value, "persist_pmm<T> требует, чтобы T был тривиально копируемым" );

    // Размер persist_pmm<T> == sizeof(T) (Тр.8)
    unsigned char _data[sizeof( T )];

  public:
    // -------------------------------------------------------------------------
    // Типы
    // -------------------------------------------------------------------------
    typedef T& Tref;
    typedef T* Tptr;

    // -------------------------------------------------------------------------
    // Доступ к значению
    // -------------------------------------------------------------------------

    /// Приведение к ссылке на хранимое значение.
    operator Tref() { return *reinterpret_cast<T*>( _data ); }
    operator Tref() const { return *reinterpret_cast<const T*>( _data ); }

    /// Получение адреса хранимого значения.
    T* operator&() { return reinterpret_cast<T*>( _data ); }

    /// Присваивание значения.
    Tref operator=( const T& ref ) { return ( *reinterpret_cast<T*>( _data ) ) = ref; }

  private:
    // Создание persist_pmm<T> на стеке или как статической переменной запрещено.
    // Используйте pam_pmm_create<persist_pmm<T>>() (Тр.11).
    //
    // Примечание: В PMM аллоцированная память инициализируется memset(0),
    // поэтому PMM не вызывает конструктор напрямую. Но чтобы pam_pmm_create<>
    // мог работать с persist_pmm, конструктор должен быть доступен.
    persist_pmm() = default;

    // Разрешаем доступ к приватному конструктору для фабричных методов.
    template <typename U> friend class fptr_pmm;

    // Разрешаем функциям pam_pmm_* создавать persist_pmm<T>.
    template <typename U> friend uintptr_t pam_pmm_create( const char* name );
    template <typename U> friend uintptr_t pam_pmm_create_array( unsigned count, const char* name );
};

// Проверяем требование Тр.8.
static_assert( sizeof( persist_pmm<int> ) == sizeof( int ),
               "sizeof(persist_pmm<T>) должен быть равен sizeof(T) (Тр.8)" );
static_assert( sizeof( persist_pmm<double> ) == sizeof( double ),
               "sizeof(persist_pmm<T>) должен быть равен sizeof(T) (Тр.8)" );

// Проверяем тривиальную копируемость.
static_assert( std::is_trivially_copyable<persist_pmm<int>>::value,
               "persist_pmm<T> должен быть тривиально копируемым" );
static_assert( std::is_trivially_copyable<persist_pmm<double>>::value,
               "persist_pmm<T> должен быть тривиально копируемым" );

// ═══════════════════════════════════════════════════════════════════════════
// АЛИАС persist ДЛЯ СОВМЕСТИМОСТИ (в пространстве имён pjson)
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Алиас persist<T> = persist_pmm<T> для упрощения миграции.
 *
 * Используйте pjson::persist<T> в новом коде для работы с PMM-бэкендом.
 * Оригинальный ::persist<T> из persist.h использует PersistentAddressSpace.
 *
 * @note В большинстве случаев при работе с PMM можно использовать
 *       непосредственно T без обёртки persist<T>, так как PMM гарантирует
 *       корректную работу с тривиально копируемыми типами.
 */
template <typename T> using persist = persist_pmm<T>;

// ═══════════════════════════════════════════════════════════════════════════
// Псевдонимы типов (совместимость с persist.h)
// ═══════════════════════════════════════════════════════════════════════════

typedef persist_pmm<char>           pchar_pmm;
typedef persist_pmm<unsigned char>  puchar_pmm;
typedef persist_pmm<short>          pshort_pmm;
typedef persist_pmm<unsigned short> pushort_pmm;
typedef persist_pmm<int>            pint_pmm;
typedef persist_pmm<unsigned>       punsigned_pmm;
typedef persist_pmm<long>           plong_pmm;
typedef persist_pmm<unsigned long>  pulong_pmm;
typedef persist_pmm<float>          pfloat_pmm;
typedef persist_pmm<double>         pdouble_pmm;

} // namespace pjson
