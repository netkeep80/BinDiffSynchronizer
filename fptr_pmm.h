#pragma once
/**
 * @file fptr_pmm.h
 * @brief Персистный указатель на базе PMM — тонкая обёртка над pptr<T>.
 *
 * Этот файл реализует fptr_pmm<T> — персистный указатель, использующий PMM
 * в качестве бэкенда. Внутренне хранит pptr<T> (гранульный индекс) и делегирует
 * разыменование встроенным операторам pptr<T>.
 *
 * Возможности:
 *   - Тонкая обёртка над PamManager::pptr<T>
 *   - Удобные методы New/Delete/find для управления объектами в ПАП
 *   - Обратная совместимость через addr()/set_addr() (байтовые смещения)
 *   - sizeof(fptr_pmm<T>) == sizeof(pptr<T>) (гранульный индекс)
 *
 * Требования:
 *   Тр.9  — не содержит логики с именами файлов
 *   Тр.12 — доступ к персистным объектам только через fptr<T>
 *   Тр.15 — метод find(name) для инициализации по имени объекта через ПАМ
 *
 * Изменения (Issue #143, План 1.4):
 *   - Внутреннее хранение заменено с uintptr_t на pptr<T>
 *   - Разыменование делегировано операторам pptr<T> (operator*, operator->)
 *   - Удалена зависимость от pmm_resolve/pmm_resolve_const
 *   - sizeof(fptr_pmm<T>) == sizeof(pptr<T>) вместо sizeof(void*)
 *
 * @see pam_pmm.h — фасад PMM для управления ПАП
 * @see pam_pmm.h — фасад ПАМ на PMM (pmm_resolve, pptr::byte_offset, pptr_from_byte_offset)
 */

#include "pam_pmm.h"
#include <cstdint>
#include <type_traits>

namespace pjson
{

// ═══════════════════════════════════════════════════════════════════════════
// fptr_pmm<T> — тонкая обёртка над pptr<T>
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Персистный указатель (обёртка над pptr<T> с удобными методами).
 *
 * Внутренне хранит PamManager::pptr<T> (гранульный индекс PMM).
 * Предоставляет удобные методы New/Delete/find для управления объектами в ПАП.
 * Обратная совместимость: addr()/set_addr() работают с байтовыми смещениями.
 *
 * @tparam T Тип данных, на который указывает указатель.
 *           Должен быть тривиально копируемым для персистности.
 */
template <typename T> class fptr_pmm
{
    static_assert( std::is_trivially_copyable<T>::value, "fptr_pmm<T> требует, чтобы T был тривиально копируемым" );

    /// pptr<T> — гранульный индекс в PMM (0 = null).
    typename PamManager::template pptr<T> _p;

  public:
    /// Конструктор по умолчанию — нулевой указатель.
    constexpr fptr_pmm() noexcept : _p{} {}

    /**
     * @brief Конструктор с инициализацией по строковому имени объекта в ПАМ (Тр.15).
     *
     * Автоматически выполняет поиск по реестру PMM при создании fptr_pmm.
     *
     * @param name Строковое имя объекта (не имя файла).
     */
    explicit fptr_pmm( const char* name ) noexcept : _p{}
    {
        if ( name != nullptr && name[0] != '\0' )
        {
            uintptr_t off = pam_pmm_find_typed<T>( name );
            _p            = PamManager::pptr_from_byte_offset<T>( off );
        }
    }

    /// Конструктор копирования.
    constexpr fptr_pmm( const fptr_pmm<T>& ) noexcept = default;

    /// Оператор присваивания.
    constexpr fptr_pmm<T>& operator=( const fptr_pmm<T>& ) noexcept = default;

    /// Деструктор — ничего не делает (не освобождает ресурсы).
    ~fptr_pmm() noexcept = default;

    // -------------------------------------------------------------------------
    // Доступ к внутреннему pptr<T>
    // -------------------------------------------------------------------------

    /// Получить внутренний pptr<T>.
    constexpr typename PamManager::template pptr<T> pptr() const noexcept { return _p; }

    /// Установить внутренний pptr<T>.
    constexpr void set_pptr( typename PamManager::template pptr<T> p ) noexcept { _p = p; }

    // -------------------------------------------------------------------------
    // Инициализация по имени объекта в ПАМ (Тр.15)
    // -------------------------------------------------------------------------

    /**
     * @brief Найти объект по имени в реестре PMM и установить указатель.
     *
     * @param name Строковое имя объекта (не имя файла).
     */
    void find( const char* name )
    {
        uintptr_t off = pam_pmm_find_typed<T>( name );
        _p            = PamManager::pptr_from_byte_offset<T>( off );
    }

    // -------------------------------------------------------------------------
    // Операции разыменования (делегируются pptr<T>)
    // -------------------------------------------------------------------------

    /// Неявное преобразование в T* — для совместимости с существующим кодом.
    operator T*() { return _p.resolve(); }
    operator T*() const { return _p.resolve(); }

    T&       operator*() { return *_p; }
    const T& operator*() const { return *_p; }

    T*       operator->() { return _p.resolve(); }
    const T* operator->() const { return _p.resolve(); }

    /// Доступ к элементу массива по индексу.
    T&       operator[]( unsigned idx ) { return _p.resolve()[idx]; }
    const T& operator[]( unsigned idx ) const { return _p.resolve()[idx]; }

    // -------------------------------------------------------------------------
    // Управление объектами через PMM
    // -------------------------------------------------------------------------

    /**
     * @brief Создать новый объект типа T в ПАП.
     *
     * @param name Необязательное имя объекта.
     */
    void New( const char* name = nullptr )
    {
        uintptr_t off = pam_pmm_create<T>( name );
        _p            = PamManager::pptr_from_byte_offset<T>( off );
    }

    /**
     * @brief Создать массив из count объектов типа T в ПАП.
     *
     * @param count Число элементов.
     * @param name  Необязательное имя массива.
     */
    void NewArray( unsigned count, const char* name = nullptr )
    {
        uintptr_t off = pam_pmm_create_array<T>( count, name );
        _p            = PamManager::pptr_from_byte_offset<T>( off );
    }

    /**
     * @brief Удалить объект из ПАП. Сбрасывает указатель в null.
     *
     * Конструкторы/деструкторы не вызываются.
     */
    void Delete()
    {
        if ( !_p.is_null() )
        {
            uintptr_t obj_off = _p.byte_offset();

            // Получаем своё смещение внутри ПАП для безопасного обновления.
            uintptr_t self_off = pam_pmm_ptr_to_offset( this );

            pam_pmm_delete( obj_off );

            // Если fptr_pmm находится внутри ПАП, перечитываем this после возможной реаллокации.
            fptr_pmm<T>* self = ( self_off != 0 ) ? pmm_resolve<fptr_pmm<T>>( self_off ) : this;
            self->_p          = typename PamManager::template pptr<T>{};
        }
    }

    /**
     * @brief Удалить массив из ПАП. Сбрасывает указатель в null.
     */
    void DeleteArray()
    {
        Delete(); // В PMM нет разницы между Delete и DeleteArray.
    }

    // -------------------------------------------------------------------------
    // Обратная совместимость: байтовые смещения
    // -------------------------------------------------------------------------

    /// Получить байтовое смещение объекта в ПАП (через pptr::byte_offset).
    uintptr_t addr() const noexcept { return _p.byte_offset(); }

    /// Установить указатель по байтовому смещению (через pptr_from_byte_offset).
    void set_addr( uintptr_t a ) noexcept { _p = PamManager::pptr_from_byte_offset<T>( a ); }

    /// Получить число элементов массива (через реестр PMM).
    uintptr_t count() const
    {
        if ( _p.is_null() )
            return 0;
        return pam_pmm_get_count( _p.byte_offset() );
    }

    /// Проверка на null.
    constexpr bool is_null() const noexcept { return _p.is_null(); }

    /// Сравнение с nullptr.
    constexpr bool operator==( std::nullptr_t ) const noexcept { return _p.is_null(); }
    constexpr bool operator!=( std::nullptr_t ) const noexcept { return !_p.is_null(); }

    // -------------------------------------------------------------------------
    // Сравнение с другими fptr_pmm
    // -------------------------------------------------------------------------

    constexpr bool operator==( const fptr_pmm<T>& other ) const noexcept { return _p == other._p; }
    constexpr bool operator!=( const fptr_pmm<T>& other ) const noexcept { return _p != other._p; }
};

// Проверяем, что fptr_pmm<T> имеет размер pptr<T> (гранульный индекс).
static_assert( sizeof( fptr_pmm<int> ) == sizeof( PamManager::pptr<int> ),
               "sizeof(fptr_pmm<T>) должен быть равен sizeof(pptr<T>)" );
static_assert( sizeof( fptr_pmm<double> ) == sizeof( PamManager::pptr<double> ),
               "sizeof(fptr_pmm<T>) должен быть равен sizeof(pptr<T>)" );

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
