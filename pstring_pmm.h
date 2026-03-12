#pragma once
/**
 * @file pstring_pmm.h
 * @brief Реализация pstring на базе PersistMemoryManager (Задача 14.4.2).
 *
 * Этот файл содержит PMM-версию персистной изменяемой строки,
 * совместимую по API с оригинальным pstring.h.
 *
 * pstring_pmm — персистная readwrite строка для JSON string-value узлов.
 * Поддерживает изменение значения на месте через метод assign().
 *
 * Ключевые отличия от оригинальной реализации:
 *   - Использует PamManager::allocate_typed<char>() вместо fptr<char>::NewArray()
 *   - Использует pjson::pmm_resolve<char>() вместо PersistentAddressSpace::Resolve<char>()
 *   - Все смещения кратны размеру гранулы PMM (16 байт)
 *   - Нет SSO: все строки хранятся в ПАП через смещение
 *
 * @see plan.md Задача 14.4.2 — Миграция pstring на PMM
 * @see pstring.h — оригинальная реализация
 * @see pam_adapter.h — адаптер pptr<T> <-> uintptr_t
 */

#include "pam_pmm_config.h"
#include "pam_adapter.h"
#include "pmem_array_pmm.h" // Для pmm_resolve и pmm_resolve_const
#include <cstring>
#include <algorithm>

namespace pjson
{

/**
 * @brief Персистная изменяемая строка на базе PMM.
 *
 * Аналог std::string, но все данные хранятся в персистном адресном
 * пространстве, управляемом PersistMemoryManager.
 *
 * Объекты pstring_pmm предназначены для хранения в ПАП.
 * Создание на стеке допустимо только для временных операций.
 *
 * Примечание: pstring_pmm НЕ использует SSO (Small String Optimization).
 * Все строки хранятся в ПАП через смещение, что необходимо для
 * сквозного поиска по строковым значениям.
 */
struct pstring_pmm
{
    uintptr_t length;    ///< Число символов (без учёта нулевого терминатора)
    uintptr_t chars_off; ///< Байтовое смещение массива char в ПАП; 0 = пустая строка

    /**
     * @brief Присвоить новое значение строке.
     *
     * Освобождает предыдущий массив символов (если есть), затем выделяет новый.
     *
     * @param s C-строка для присваивания. nullptr или "" приводят к пустой строке.
     * @param self_off Байтовое смещение этого pstring_pmm в ПАП (для realloc-безопасности).
     *                 Если pstring_pmm находится на стеке (временный объект), передайте 0.
     */
    void assign( const char* s, uintptr_t self_off = 0 )
    {
        // Освобождаем старые данные
        if ( chars_off != 0 )
        {
            auto old_pptr = offset_to_pptr<char>( chars_off );
            PamManager::template deallocate_typed<char>( old_pptr );
        }

        if ( s == nullptr || s[0] == '\0' )
        {
            // Переразрешаем self после деаллокации (если в ПАП)
            pstring_pmm* self = ( self_off != 0 ) ? pmm_resolve<pstring_pmm>( self_off ) : this;
            self->chars_off   = 0;
            self->length      = 0;
            return;
        }

        uintptr_t len = static_cast<uintptr_t>( std::strlen( s ) );

        // Выделяем len+1 символов (включая нулевой терминатор)
        auto new_chars_pptr = PamManager::template allocate_typed<char>( len + 1 );
        if ( new_chars_pptr.is_null() )
        {
            // Ошибка аллокации — обнуляем строку
            pstring_pmm* self = ( self_off != 0 ) ? pmm_resolve<pstring_pmm>( self_off ) : this;
            self->chars_off   = 0;
            self->length      = 0;
            return;
        }

        uintptr_t new_off = pptr_to_offset( new_chars_pptr );

        // Записываем символы
        char* dst = new_chars_pptr.resolve();
        std::memcpy( dst, s, static_cast<std::size_t>( len + 1 ) );

        // Обновляем поля (переразрешаем self после аллокации)
        pstring_pmm* self = ( self_off != 0 ) ? pmm_resolve<pstring_pmm>( self_off ) : this;
        self->chars_off   = new_off;
        self->length      = len;
    }

    /**
     * @brief Получить C-строку (нуль-терминированную).
     *
     * @return Указатель на символьные данные или "" для пустой строки.
     */
    const char* c_str() const
    {
        if ( chars_off == 0 )
            return "";
        const char* ptr = pmm_resolve_const<char>( chars_off );
        return ( ptr != nullptr ) ? ptr : "";
    }

    /**
     * @brief Получить длину строки (без нулевого терминатора).
     */
    uintptr_t size() const { return length; }

    /**
     * @brief Проверить, пустая ли строка.
     */
    bool empty() const { return length == 0; }

    /**
     * @brief Очистить строку (освободить память).
     *
     * @param self_off Байтовое смещение этого pstring_pmm в ПАП (для realloc-безопасности).
     */
    void clear( uintptr_t self_off = 0 )
    {
        if ( chars_off != 0 )
        {
            auto pptr = offset_to_pptr<char>( chars_off );
            PamManager::template deallocate_typed<char>( pptr );
        }

        pstring_pmm* self = ( self_off != 0 ) ? pmm_resolve<pstring_pmm>( self_off ) : this;
        self->chars_off   = 0;
        self->length      = 0;
    }

    /**
     * @brief Доступ к символу по индексу (без проверки границ).
     */
    char& operator[]( uintptr_t idx )
    {
        char* ptr = pmm_resolve<char>( chars_off );
        return ptr[idx];
    }

    const char& operator[]( uintptr_t idx ) const
    {
        const char* ptr = pmm_resolve_const<char>( chars_off );
        return ptr[idx];
    }

    /**
     * @brief Сравнение с C-строкой.
     */
    bool operator==( const char* s ) const
    {
        if ( s == nullptr )
            return length == 0;
        return std::strcmp( c_str(), s ) == 0;
    }

    /**
     * @brief Сравнение двух pstring_pmm.
     */
    bool operator==( const pstring_pmm& other ) const
    {
        if ( length != other.length )
            return false;
        if ( length == 0 )
            return true;
        return std::strcmp( c_str(), other.c_str() ) == 0;
    }

    bool operator!=( const char* s ) const { return !( *this == s ); }
    bool operator!=( const pstring_pmm& other ) const { return !( *this == other ); }

    /**
     * @brief Лексикографическое сравнение.
     */
    bool operator<( const pstring_pmm& other ) const { return std::strcmp( c_str(), other.c_str() ) < 0; }
};

static_assert( sizeof( pstring_pmm ) == 2 * sizeof( void* ),
               "pstring_pmm должна занимать 2 * sizeof(void*) байт" );
static_assert( std::is_trivially_copyable<pstring_pmm>::value,
               "pstring_pmm должен быть тривиально копируемым" );

// ═══════════════════════════════════════════════════════════════════════════
// Вспомогательные функции для работы с pstring_pmm
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Создать pstring_pmm в ПАП с заданным значением.
 *
 * @param s C-строка для инициализации.
 * @return Байтовое смещение созданного pstring_pmm в ПАП.
 */
inline uintptr_t pstring_pmm_create( const char* s )
{
    auto pptr = PamManager::template allocate_typed<pstring_pmm>();
    if ( pptr.is_null() )
        return 0;

    uintptr_t off = pptr_to_offset( pptr );

    // Инициализируем структуру нулями
    pstring_pmm* ps = pptr.resolve();
    ps->length      = 0;
    ps->chars_off   = 0;

    // Присваиваем значение
    if ( s != nullptr && s[0] != '\0' )
        ps->assign( s, off );

    return off;
}

/**
 * @brief Уничтожить pstring_pmm в ПАП.
 *
 * Освобождает как символьные данные, так и сам объект pstring_pmm.
 *
 * @param off Байтовое смещение pstring_pmm в ПАП.
 */
inline void pstring_pmm_destroy( uintptr_t off )
{
    if ( off == 0 )
        return;

    pstring_pmm* ps = pmm_resolve<pstring_pmm>( off );
    if ( ps == nullptr )
        return;

    // Освобождаем символьные данные
    if ( ps->chars_off != 0 )
    {
        auto chars_pptr = offset_to_pptr<char>( ps->chars_off );
        PamManager::template deallocate_typed<char>( chars_pptr );
    }

    // Освобождаем сам объект pstring_pmm
    auto pptr = offset_to_pptr<pstring_pmm>( off );
    PamManager::template deallocate_typed<pstring_pmm>( pptr );
}

/**
 * @brief Получить указатель на pstring_pmm по смещению.
 *
 * @param off Байтовое смещение pstring_pmm в ПАП.
 * @return Указатель на pstring_pmm или nullptr.
 */
inline pstring_pmm* pstring_pmm_get( uintptr_t off )
{
    return pmm_resolve<pstring_pmm>( off );
}

/**
 * @brief Получить указатель на pstring_pmm по смещению (const версия).
 */
inline const pstring_pmm* pstring_pmm_get_const( uintptr_t off )
{
    return pmm_resolve_const<pstring_pmm>( off );
}

} // namespace pjson
