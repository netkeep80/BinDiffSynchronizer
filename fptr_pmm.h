#pragma once
/**
 * @file fptr_pmm.h
 * @brief Персистный указатель на базе PMM.
 *
 * Этот файл реализует fptr_pmm<T> — персистный указатель, использующий PMM
 * в качестве бэкенда.
 *
 * Возможности:
 *   - Использует pam_pmm_* функции для управления персистной памятью
 *   - Поддерживает работу с pptr<T> через pjson::pptr_to_offset() / offset_to_pptr()
 *   - sizeof(fptr_pmm<T>) == sizeof(void*)
 *
 * Требования:
 *   Тр.5  — sizeof(fptr<T>) == sizeof(void*)
 *   Тр.9  — не содержит логики с именами файлов
 *   Тр.12 — доступ к персистным объектам только через fptr<T>
 *   Тр.13 — может находиться как в обычной, так и в персистной памяти
 *   Тр.15 — метод find(name) для инициализации по имени объекта через ПАМ
 *
 * @see pam_pmm.h — фасад PMM для управления ПАП
 * @see pam_adapter.h — адаптер pptr<T> <-> uintptr_t
 */

#include "pam_pmm.h"
#include <cstdint>
#include <type_traits>

namespace pjson
{

// ═══════════════════════════════════════════════════════════════════════════
// fptr_pmm<T> — персистный указатель на базе PMM
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Персистный указатель (хранит смещение в образе ПАП).
 *
 * Реализует Тр.5, Тр.9, Тр.12, Тр.13, Тр.15, используя PMM (pam_pmm.h)
 * в качестве бэкенда.
 *
 * @tparam T Тип данных, на который указывает указатель.
 *           Должен быть тривиально копируемым для персистности.
 */
template <typename T> class fptr_pmm
{
    static_assert( std::is_trivially_copyable<T>::value, "fptr_pmm<T> требует, чтобы T был тривиально копируемым" );

    typedef T& Tref;
    typedef T* Tptr;

    /// Смещение объекта в области данных ПАП (или 0 = null).
    /// Размер == sizeof(void*) на целевой платформе (Тр.5).
    uintptr_t __addr;

  public:
    /// Конструктор по умолчанию — нулевой указатель.
    inline fptr_pmm() : __addr( 0 ) {}

    /**
     * @brief Конструктор с инициализацией по строковому имени объекта в ПАМ (Тр.15).
     *
     * Автоматически выполняет поиск по реестру PMM при создании fptr_pmm.
     *
     * @param name Строковое имя объекта (не имя файла).
     */
    inline explicit fptr_pmm( const char* name ) : __addr( 0 )
    {
        if ( name != nullptr && name[0] != '\0' )
            __addr = pam_pmm_find_typed<T>( name );
    }

    /// Конструктор копирования.
    inline fptr_pmm( const fptr_pmm<T>& ) = default;

    /// Оператор присваивания.
    inline fptr_pmm<T>& operator=( const fptr_pmm<T>& ) = default;

    /// Деструктор — ничего не делает (не освобождает ресурсы).
    inline ~fptr_pmm() = default;

    // -------------------------------------------------------------------------
    // Инициализация по имени объекта в ПАМ (Тр.15)
    // -------------------------------------------------------------------------

    /**
     * @brief Найти объект по имени в реестре PMM и установить указатель.
     *
     * @param name Строковое имя объекта (не имя файла).
     */
    void find( const char* name ) { __addr = pam_pmm_find_typed<T>( name ); }

    // -------------------------------------------------------------------------
    // Операции разыменования
    // -------------------------------------------------------------------------

    /// Разыменование — возвращает указатель на объект в ПАП.
    inline operator Tptr() { return pmm_resolve<T>( __addr ); }
    inline operator Tptr() const { return pmm_resolve<T>( __addr ); }

    inline T&       operator*() { return *pmm_resolve<T>( __addr ); }
    inline const T& operator*() const { return *pmm_resolve_const<T>( __addr ); }

    inline T*       operator->() { return pmm_resolve<T>( __addr ); }
    inline const T* operator->() const { return pmm_resolve_const<T>( __addr ); }

    /// Доступ к элементу массива по индексу.
    inline T&       operator[]( unsigned idx ) { return pmm_resolve<T>( __addr )[idx]; }
    inline const T& operator[]( unsigned idx ) const { return pmm_resolve_const<T>( __addr )[idx]; }

    // -------------------------------------------------------------------------
    // Управление объектами через PMM (Тр.2)
    // -------------------------------------------------------------------------

    /**
     * @brief Создать новый объект типа T в ПАП.
     *
     * @param name Необязательное имя объекта.
     */
    void New( const char* name = nullptr ) { __addr = pam_pmm_create<T>( name ); }

    /**
     * @brief Создать массив из count объектов типа T в ПАП.
     *
     * @param count Число элементов.
     * @param name  Необязательное имя массива.
     */
    void NewArray( unsigned count, const char* name = nullptr ) { __addr = pam_pmm_create_array<T>( count, name ); }

    /**
     * @brief Удалить объект из ПАП. Сбрасывает указатель в 0.
     *
     * Конструкторы/деструкторы не вызываются (Тр.10).
     */
    void Delete()
    {
        if ( __addr != 0 )
        {
            // Получаем своё смещение внутри ПАП для безопасного обновления.
            uintptr_t self_off = pam_pmm_ptr_to_offset( this );

            pam_pmm_delete( __addr );

            // Если fptr_pmm находится внутри ПАП, перечитываем this.
            fptr_pmm<T>* self = ( self_off != 0 ) ? pmm_resolve<fptr_pmm<T>>( self_off ) : this;
            self->__addr      = 0;
        }
    }

    /**
     * @brief Удалить массив из ПАП. Сбрасывает указатель в 0.
     */
    void DeleteArray()
    {
        Delete(); // В PMM нет разницы между Delete и DeleteArray.
    }

    // -------------------------------------------------------------------------
    // Вспомогательные методы
    // -------------------------------------------------------------------------

    /// Получить смещение объекта в ПАП.
    inline uintptr_t addr() const { return __addr; }

    /// Установить смещение объекта в ПАП вручную.
    inline void set_addr( uintptr_t a ) { __addr = a; }

    /// Получить число элементов массива (через реестр PMM).
    inline uintptr_t count() const
    {
        if ( __addr == 0 )
            return 0;
        return pam_pmm_get_count( __addr );
    }

    /// Проверка на null.
    inline bool is_null() const { return __addr == 0; }

    /// Сравнение с nullptr.
    inline bool operator==( std::nullptr_t ) const { return __addr == 0; }
    inline bool operator!=( std::nullptr_t ) const { return __addr != 0; }

    // -------------------------------------------------------------------------
    // Сравнение с другими fptr_pmm
    // -------------------------------------------------------------------------

    inline bool operator==( const fptr_pmm<T>& other ) const { return __addr == other.__addr; }
    inline bool operator!=( const fptr_pmm<T>& other ) const { return __addr != other.__addr; }
};

// Проверяем требование Тр.5.
static_assert( sizeof( fptr_pmm<int> ) == sizeof( void* ),
               "sizeof(fptr_pmm<T>) должен быть равен sizeof(void*) (Тр.5)" );
static_assert( sizeof( fptr_pmm<double> ) == sizeof( void* ),
               "sizeof(fptr_pmm<T>) должен быть равен sizeof(void*) (Тр.5)" );

// Проверяем тривиальную копируемость.
static_assert( std::is_trivially_copyable<fptr_pmm<int>>::value, "fptr_pmm<T> должен быть тривиально копируемым" );
static_assert( std::is_trivially_copyable<fptr_pmm<double>>::value, "fptr_pmm<T> должен быть тривиально копируемым" );

// ═══════════════════════════════════════════════════════════════════════════
// АЛИАС fptr ДЛЯ СОВМЕСТИМОСТИ (в пространстве имён pjson)
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Алиас fptr<T> = fptr_pmm<T>.
 *
 * Используйте pjson::fptr<T> для работы с PMM-бэкендом.
 */
template <typename T> using fptr = fptr_pmm<T>;

} // namespace pjson
