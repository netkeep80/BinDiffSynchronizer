#pragma once
// pjson_interning.h — Таблица интернирования строк для pjson (F3 из задачи #84).
//
// String interning (дедупликация строк) позволяет хранить каждую уникальную строку
// в ПАП ровно один раз и повторно использовать её адрес при повторных вставках.
//
// Реализация:
//   pjson_string_table — персистная хэш-таблица (открытая адресация, linear probing)
//     в ПАП, отображающая C-строку → смещение pstring в ПАП.
//
// Преимущества:
//   - Дедупликация одинаковых ключей JSON-объектов: экономия до 40% памяти
//     при типичных JSON-документах со множеством одинаковых ключей.
//   - Быстрое сравнение строк: по смещению в ПАП (O(1)) вместо strcmp (O(n)).
//
// Ограничения:
//   - Интернированные строки никогда не освобождаются (глобальный пул).
//   - Таблица хранится в ПАП: выживает между запусками (если PAM сохраняется).
//
// Все комментарии — на русском языке (Тр.6).

#include "pstring.h"
#include "pvector.h"
#include <cstring>
#include <cstdint>

// ===========================================================================
// Запись в хэш-таблице интернирования
// ===========================================================================

/// Одна запись открытой адресации: хэш, смещение pstring в ПАП.
/// Смещение 0 означает пустую ячейку. Смещение PJSON_INTERNING_DELETED — удалённую.
constexpr uintptr_t PJSON_INTERNING_EMPTY   = 0u;
constexpr uintptr_t PJSON_INTERNING_DELETED = static_cast<uintptr_t>( -1 );

struct pjson_intern_entry
{
    uint64_t  hash;       ///< Хэш строки (FNV-1a)
    uintptr_t str_offset; ///< Смещение pstring в ПАП, или EMPTY/DELETED
};

static_assert( std::is_trivially_copyable<pjson_intern_entry>::value,
               "pjson_intern_entry должен быть тривиально копируемым" );

// ===========================================================================
// pjson_string_table — таблица интернирования строк
// ===========================================================================

/// Персистная таблица интернирования строк.
/// Живёт только в ПАП; создаётся через fptr<pjson_string_table>::New().
/// Хранит хэш-таблицу с открытой адресацией и linear probing.
struct pjson_string_table
{
    uintptr_t                count_;    ///< Число занятых ячеек
    uintptr_t                capacity_; ///< Ёмкость таблицы (число ячеек)
    fptr<pjson_intern_entry> buckets_;  ///< Массив ячеек в ПАП

    // Вычислить FNV-1a хэш строки.
    static uint64_t fnv1a( const char* s )
    {
        uint64_t h = 14695981039346656037ULL;
        while ( *s != '\0' )
        {
            h ^= static_cast<uint8_t>( *s++ );
            h *= 1099511628211ULL;
        }
        return h;
    }

    /// Интернировать строку s: вернуть смещение существующей или новой pstring в ПАП.
    /// Если строка уже есть в таблице — вернуть её смещение.
    /// Если нет — создать новую pstring в ПАП, добавить в таблицу, вернуть смещение.
    uintptr_t intern( const char* s )
    {
        if ( s == nullptr )
            s = "";

        uint64_t  hash     = fnv1a( s );
        auto&     pam      = PersistentAddressSpace::Get();
        uintptr_t self_off = pam.PtrToOffset( this );

        // Убеждаемся, что таблица инициализирована.
        if ( capacity_ == 0 )
            _init_buckets( self_off, 16u );

        // Перехэшируем, если load factor > 0.5.
        {
            pjson_string_table* self = pam.Resolve<pjson_string_table>( self_off );
            if ( self->count_ * 2 >= self->capacity_ )
            {
                self->_rehash( self_off, self->capacity_ * 2 );
            }
        }

        // Ищем ячейку.
        pjson_string_table* self      = pam.Resolve<pjson_string_table>( self_off );
        uintptr_t           cap       = self->capacity_;
        uintptr_t           idx       = static_cast<uintptr_t>( hash % cap );
        uintptr_t           first_del = static_cast<uintptr_t>( -1 );

        for ( uintptr_t probe = 0; probe < cap; probe++ )
        {
            // Переразрешаем self после каждой итерации (вставка pstring могла вызвать realloc).
            self                     = pam.Resolve<pjson_string_table>( self_off );
            cap                      = self->capacity_;
            pjson_intern_entry& cell = self->_bucket( idx );

            if ( cell.str_offset == PJSON_INTERNING_EMPTY )
            {
                // Пустая ячейка: строки нет — создаём новую pstring.
                uintptr_t new_str_off = _create_pstring( self_off, s );
                // Переразрешаем self и ячейку после возможного realloc.
                self = pam.Resolve<pjson_string_table>( self_off );
                cap  = self->capacity_;
                // Пересчитываем idx для нашего хэша (capacity могла вырасти).
                idx = static_cast<uintptr_t>( hash % cap );
                // Повторно ищем нужную пустую ячейку (новая capacity → другая позиция).
                // Поскольку _rehash был вызван ДО вставки, просто линейно ищем.
                uintptr_t ins_idx = idx;
                for ( uintptr_t p2 = 0; p2 < cap; p2++ )
                {
                    pjson_intern_entry& c2 = self->_bucket( ins_idx );
                    if ( c2.str_offset == PJSON_INTERNING_EMPTY || c2.str_offset == PJSON_INTERNING_DELETED )
                    {
                        c2.hash       = hash;
                        c2.str_offset = new_str_off;
                        self->count_++;
                        return new_str_off;
                    }
                    ins_idx = ( ins_idx + 1 ) % cap;
                }
                // Не должно произойти: таблица должна иметь свободное место.
                return new_str_off;
            }
            if ( cell.str_offset == PJSON_INTERNING_DELETED )
            {
                if ( first_del == static_cast<uintptr_t>( -1 ) )
                    first_del = idx;
                idx = ( idx + 1 ) % cap;
                continue;
            }
            // Занятая ячейка: сравниваем строку.
            if ( cell.hash == hash )
            {
                uintptr_t str_off = cell.str_offset;
                pstring*  ps      = pam.Resolve<pstring>( str_off );
                if ( ps != nullptr && std::strcmp( ps->c_str(), s ) == 0 )
                    return str_off; // найдено!
            }
            idx = ( idx + 1 ) % cap;
        }

        // Таблица переполнена (не должно происходить при load factor < 0.5).
        return _create_pstring( self_off, s );
    }

    /// Получить строку по смещению pstring. Возвращает "" при str_offset == 0.
    static const char* get( uintptr_t str_offset )
    {
        if ( str_offset == 0 )
            return "";
        auto&    pam = PersistentAddressSpace::Get();
        pstring* ps  = pam.Resolve<pstring>( str_offset );
        return ( ps != nullptr ) ? ps->c_str() : "";
    }

  private:
    /// Доступ к ячейке хэш-таблицы по индексу.
    pjson_intern_entry& _bucket( uintptr_t idx )
    {
        auto& pam = PersistentAddressSpace::Get();
        return *( pam.Resolve<pjson_intern_entry>( buckets_.addr() ) + idx );
    }

    /// Инициализировать массив ячеек нулями (initial_cap ячеек).
    void _init_buckets( uintptr_t self_off, uintptr_t initial_cap )
    {
        auto&                    pam = PersistentAddressSpace::Get();
        fptr<pjson_intern_entry> arr;
        arr.NewArray( static_cast<unsigned>( initial_cap ) );
        // Инициализируем нулями (EMPTY).
        auto* raw = pam.Resolve<pjson_intern_entry>( arr.addr() );
        for ( uintptr_t i = 0; i < initial_cap; i++ )
        {
            raw[i].hash       = 0;
            raw[i].str_offset = PJSON_INTERNING_EMPTY;
        }
        pjson_string_table* self = pam.Resolve<pjson_string_table>( self_off );
        self->buckets_.set_addr( arr.addr() );
        self->capacity_ = initial_cap;
        self->count_    = 0;
    }

    /// Создать новую pstring в ПАП со строкой s. Возвращает смещение.
    static uintptr_t _create_pstring( uintptr_t /*self_off*/, const char* s )
    {
        auto&         pam = PersistentAddressSpace::Get();
        fptr<pstring> fp;
        fp.New();
        uintptr_t off = fp.addr();
        pstring*  ps  = pam.Resolve<pstring>( off );
        if ( ps != nullptr )
            ps->assign( s );
        return off;
    }

    /// Перехэшировать таблицу в новую ёмкость new_cap.
    void _rehash( uintptr_t self_off, uintptr_t new_cap )
    {
        auto& pam = PersistentAddressSpace::Get();

        pjson_string_table* self    = pam.Resolve<pjson_string_table>( self_off );
        uintptr_t           old_cap = self->capacity_;
        uintptr_t           old_bkt = self->buckets_.addr();

        // Выделяем новый массив ячеек.
        fptr<pjson_intern_entry> new_arr;
        new_arr.NewArray( static_cast<unsigned>( new_cap ) );
        auto* new_raw = pam.Resolve<pjson_intern_entry>( new_arr.addr() );
        for ( uintptr_t i = 0; i < new_cap; i++ )
        {
            new_raw[i].hash       = 0;
            new_raw[i].str_offset = PJSON_INTERNING_EMPTY;
        }

        // Переносим все существующие записи.
        auto* old_raw = pam.Resolve<pjson_intern_entry>( old_bkt );
        for ( uintptr_t i = 0; i < old_cap; i++ )
        {
            if ( old_raw[i].str_offset == PJSON_INTERNING_EMPTY || old_raw[i].str_offset == PJSON_INTERNING_DELETED )
                continue;
            uint64_t  h   = old_raw[i].hash;
            uintptr_t idx = static_cast<uintptr_t>( h % new_cap );
            // Линейный зондирование.
            for ( uintptr_t p = 0; p < new_cap; p++ )
            {
                // Переразрешаем (после NewArray возможен realloc).
                new_raw = pam.Resolve<pjson_intern_entry>( new_arr.addr() );
                if ( new_raw[idx].str_offset == PJSON_INTERNING_EMPTY )
                {
                    new_raw[idx].hash       = h;
                    new_raw[idx].str_offset = old_raw[i].str_offset;
                    break;
                }
                idx = ( idx + 1 ) % new_cap;
            }
        }

        // Освобождаем старый массив.
        if ( old_bkt != 0 )
        {
            fptr<pjson_intern_entry> old_fptr;
            old_fptr.set_addr( old_bkt );
            old_fptr.DeleteArray();
        }

        // Обновляем self.
        self = pam.Resolve<pjson_string_table>( self_off );
        self->buckets_.set_addr( new_arr.addr() );
        self->capacity_ = new_cap;
        // count_ не изменяется (deleted записи выброшены).
    }

    // Создание pjson_string_table на стеке запрещено.
    pjson_string_table()  = default;
    ~pjson_string_table() = default;

    template <class U> friend class AddressManager;
    friend class PersistentAddressSpace;
};
