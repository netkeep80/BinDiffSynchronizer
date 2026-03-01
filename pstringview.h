#pragma once
#include "pstring.h"
#include "pmap.h"
#include <cstring>
#include <string>
#include <vector>

// pstringview — персистная строка только для чтения с интернированием (аналог string_view).
//
// Объекты pstringview могут находиться ТОЛЬКО внутри ПАП.
// Для работы с pstringview из обычного кода используйте fptr<pstringview>.
//
// Особенности:
//   - Read-only: строковые данные никогда не изменяются после создания.
//   - Интернирование: одна и та же строка всегда хранится в ПАП ровно один раз.
//     Два pstringview с одинаковым содержимым всегда указывают на один chars_offset.
//   - Персистный указатель: внутренний chars_offset никогда не освобождается,
//     т.е. он действителен на протяжении всего времени жизни ПАП.
//   - Деструктор не освобождает chars: строки живут вечно в ПАП.
//   - Использование в pmap: pstringview тривиально копируем, поддерживает < и ==.
//
// Использование:
//   fptr<pstringview> fps;
//   fps.New();
//   fps->intern("hello");    // сохраняет или находит интернированную строку
//   fps->c_str();            // вернуть указатель на символьные данные
//   fps.Delete();            // освобождает только pstringview, NOT символьные данные
//
// Phase 1: pstringview хранит chars_offset (смещение массива char в ПАП) и length.
//          Интернирующая таблица — pstringview_table — отдельный персистный объект.

// ---------------------------------------------------------------------------
// pstringview — интернированная read-only строка в ПАП
// ---------------------------------------------------------------------------

struct pstringview
{
    uintptr_t length;       ///< Длина строки (без нулевого терминатора)
    uintptr_t chars_offset; ///< Смещение массива char в ПАП; 0 = пустая строка

    // intern: интернировать строку s.
    // Ищет s в глобальной таблице интернирования ПАП.
    // Если найдена — устанавливает chars_offset на существующий массив.
    // Если нет — создаёт новый массив char в ПАП, добавляет в таблицу.
    // Символьные данные никогда не освобождаются.
    void intern( const char* s );

    // c_str: вернуть raw-указатель на символьные данные (нуль-терминированные).
    // Действителен, пока ПАМ жив.
    const char* c_str() const
    {
        if ( chars_offset == 0 )
            return "";
        auto& pam = PersistentAddressSpace::Get();
        return pam.Resolve<char>( chars_offset );
    }

    uintptr_t size() const { return length; }
    bool      empty() const { return length == 0; }

    bool operator==( const char* s ) const
    {
        if ( s == nullptr )
            return length == 0;
        return std::strcmp( c_str(), s ) == 0;
    }

    bool operator==( const pstringview& other ) const
    {
        // Интернирование гарантирует: одинаковые строки → один chars_offset.
        return chars_offset == other.chars_offset;
    }

    bool operator!=( const char* s ) const { return !( *this == s ); }
    bool operator!=( const pstringview& other ) const { return !( *this == other ); }

    bool operator<( const pstringview& other ) const { return std::strcmp( c_str(), other.c_str() ) < 0; }

  private:
    // Создание pstringview на стеке или как статической переменной запрещено.
    // Используйте fptr<pstringview>::New() для создания в ПАП (Тр.11).
    // Исключение: pam_intern_string() возвращает pstringview по значению
    // (только для чтения полей chars_offset/length — не для хранения в ПАП).
    pstringview()  = default;
    ~pstringview() = default;

    // Разрешаем доступ к приватному конструктору только для фабричных методов ПАМ.
    template <class U> friend class AddressManager;
    friend class PersistentAddressSpace;
};

static_assert( sizeof( pstringview ) == 2 * sizeof( void* ), "pstringview должна занимать 2 * sizeof(void*) байт" );

// ---------------------------------------------------------------------------
// pstringview_table — таблица интернирования строк (хранится в ПАП)
// ---------------------------------------------------------------------------
//
// Хэш-таблица (открытая адресация, linear probing) в ПАП.
// Ключ: FNV-1a хэш + смещение массива char в ПАП.
// Строки добавляются, но НИКОГДА не удаляются.

/// Одна запись открытой адресации для pstringview_table.
struct pstringview_entry
{
    uint64_t  hash;         ///< FNV-1a хэш строки
    uintptr_t chars_offset; ///< Смещение массива char в ПАП; 0 = пустая ячейка
    uintptr_t length;       ///< Длина строки (кэшируется)
};

static_assert( std::is_trivially_copyable<pstringview_entry>::value,
               "pstringview_entry должен быть тривиально копируемым" );

/// Персистная таблица интернирования для pstringview.
/// Живёт только в ПАП; создаётся автоматически при первом обращении.
struct pstringview_table
{
    uintptr_t               count_;    ///< Число занятых ячеек
    uintptr_t               capacity_; ///< Ёмкость таблицы (число ячеек)
    fptr<pstringview_entry> buckets_;  ///< Массив ячеек в ПАП

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

    /// Интернировать строку s: вернуть (chars_offset, length) для строки.
    /// Если строка уже есть — вернуть смещение существующего массива.
    /// Если нет — создать новый массив char в ПАП, добавить в таблицу.
    /// Символьные данные НИКОГДА не освобождаются.
    struct InternResult
    {
        uintptr_t chars_offset;
        uintptr_t length;
    };

    InternResult intern( const char* s )
    {
        if ( s == nullptr )
            s = "";

        uintptr_t len      = static_cast<uintptr_t>( std::strlen( s ) );
        uint64_t  hash     = fnv1a( s );
        auto&     pam      = PersistentAddressSpace::Get();
        uintptr_t self_off = pam.PtrToOffset( this );

        // Убеждаемся, что таблица инициализирована.
        if ( capacity_ == 0 )
            _init_buckets( self_off, 16u );

        // Перехэшируем, если load factor > 0.5.
        {
            pstringview_table* self = pam.Resolve<pstringview_table>( self_off );
            if ( self->count_ * 2 >= self->capacity_ )
                self->_rehash( self_off, self->capacity_ * 2 );
        }

        // Ищем ячейку.
        pstringview_table* self = pam.Resolve<pstringview_table>( self_off );
        uintptr_t          cap  = self->capacity_;
        uintptr_t          idx  = static_cast<uintptr_t>( hash % cap );

        for ( uintptr_t probe = 0; probe < cap; probe++ )
        {
            self                    = pam.Resolve<pstringview_table>( self_off );
            cap                     = self->capacity_;
            pstringview_entry& cell = self->_bucket( idx );

            if ( cell.chars_offset == 0 )
            {
                // Пустая ячейка — строки нет, создаём новый массив char в ПАП.
                uintptr_t new_chars = _create_chars( self_off, s, len );
                // Переразрешаем self и ячейку после возможного realloc.
                self = pam.Resolve<pstringview_table>( self_off );
                cap  = self->capacity_;
                // Пересчитываем idx (capacity могла вырасти при _rehash).
                idx = static_cast<uintptr_t>( hash % cap );
                // Находим первую пустую ячейку для вставки.
                for ( uintptr_t p2 = 0; p2 < cap; p2++ )
                {
                    pstringview_entry& c2 = self->_bucket( idx );
                    if ( c2.chars_offset == 0 )
                    {
                        c2.hash         = hash;
                        c2.chars_offset = new_chars;
                        c2.length       = len;
                        self->count_++;
                        return { new_chars, len };
                    }
                    idx = ( idx + 1 ) % cap;
                }
                // Не должно произойти.
                return { new_chars, len };
            }

            // Занятая ячейка: сравниваем хэш и строку.
            if ( cell.hash == hash && cell.length == len )
            {
                uintptr_t   co = cell.chars_offset;
                const char* cs = pam.Resolve<char>( co );
                if ( cs != nullptr && std::strcmp( cs, s ) == 0 )
                    return { co, len }; // найдено — возвращаем существующее смещение
            }
            idx = ( idx + 1 ) % cap;
        }

        // Таблица переполнена (не должно происходить при load factor < 0.5).
        uintptr_t new_chars = _create_chars( self_off, s, len );
        return { new_chars, len };
    }

  private:
    /// Доступ к ячейке хэш-таблицы по индексу.
    pstringview_entry& _bucket( uintptr_t idx )
    {
        auto& pam = PersistentAddressSpace::Get();
        return *( pam.Resolve<pstringview_entry>( buckets_.addr() ) + idx );
    }

    /// Инициализировать массив ячеек нулями (initial_cap ячеек).
    void _init_buckets( uintptr_t self_off, uintptr_t initial_cap )
    {
        auto&                   pam = PersistentAddressSpace::Get();
        fptr<pstringview_entry> arr;
        arr.NewArray( static_cast<unsigned>( initial_cap ) );
        // Инициализируем нулями (пустые ячейки).
        auto* raw = pam.Resolve<pstringview_entry>( arr.addr() );
        for ( uintptr_t i = 0; i < initial_cap; i++ )
        {
            raw[i].hash         = 0;
            raw[i].chars_offset = 0;
            raw[i].length       = 0;
        }
        pstringview_table* self = pam.Resolve<pstringview_table>( self_off );
        self->buckets_.set_addr( arr.addr() );
        self->capacity_ = initial_cap;
        self->count_    = 0;
    }

    /// Создать новый массив char в ПАП со строкой s длиной len. Возвращает смещение.
    static uintptr_t _create_chars( uintptr_t /*self_off*/, const char* s, uintptr_t len )
    {
        auto&      pam = PersistentAddressSpace::Get();
        fptr<char> arr;
        arr.NewArray( static_cast<unsigned>( len + 1 ) );
        uintptr_t off = arr.addr();
        char*     dst = pam.Resolve<char>( off );
        if ( dst != nullptr )
            std::memcpy( dst, s, static_cast<std::size_t>( len + 1 ) );
        return off;
    }

    /// Перехэшировать таблицу в новую ёмкость new_cap.
    void _rehash( uintptr_t self_off, uintptr_t new_cap )
    {
        auto& pam = PersistentAddressSpace::Get();

        pstringview_table* self    = pam.Resolve<pstringview_table>( self_off );
        uintptr_t          old_cap = self->capacity_;
        uintptr_t          old_bkt = self->buckets_.addr();

        // Выделяем новый массив ячеек.
        fptr<pstringview_entry> new_arr;
        new_arr.NewArray( static_cast<unsigned>( new_cap ) );
        auto* new_raw = pam.Resolve<pstringview_entry>( new_arr.addr() );
        for ( uintptr_t i = 0; i < new_cap; i++ )
        {
            new_raw[i].hash         = 0;
            new_raw[i].chars_offset = 0;
            new_raw[i].length       = 0;
        }

        // Переносим все существующие записи.
        auto* old_raw = pam.Resolve<pstringview_entry>( old_bkt );
        for ( uintptr_t i = 0; i < old_cap; i++ )
        {
            if ( old_raw[i].chars_offset == 0 )
                continue;
            uint64_t  h   = old_raw[i].hash;
            uintptr_t ins = static_cast<uintptr_t>( h % new_cap );
            for ( uintptr_t p = 0; p < new_cap; p++ )
            {
                // Переразрешаем (после NewArray возможен realloc).
                new_raw = pam.Resolve<pstringview_entry>( new_arr.addr() );
                if ( new_raw[ins].chars_offset == 0 )
                {
                    new_raw[ins] = old_raw[i];
                    break;
                }
                ins = ( ins + 1 ) % new_cap;
            }
        }

        // Освобождаем старый массив.
        if ( old_bkt != 0 )
        {
            fptr<pstringview_entry> old_fptr;
            old_fptr.set_addr( old_bkt );
            old_fptr.DeleteArray();
        }

        // Обновляем self.
        self = pam.Resolve<pstringview_table>( self_off );
        self->buckets_.set_addr( new_arr.addr() );
        self->capacity_ = new_cap;
        // count_ не изменяется (deleted записи не используются: строки бессмертны).
    }

    // Создание pstringview_table на стеке запрещено.
    pstringview_table()  = default;
    ~pstringview_table() = default;

    template <class U> friend class AddressManager;
    friend class PersistentAddressSpace;
};

static_assert( sizeof( pstringview_table ) == 3 * sizeof( void* ),
               "pstringview_table должна занимать 3 * sizeof(void*) байт" );

// ---------------------------------------------------------------------------
// pstringview_manager — синглтон менеджера таблицы интернирования
// ---------------------------------------------------------------------------
//
// Хранит смещение pstringview_table в ПАП в статической переменной.
// При первом обращении создаёт таблицу в ПАП.

struct pstringview_manager
{
    // Получить или создать таблицу интернирования.
    // При первом вызове после загрузки образа восстанавливает смещение из заголовка ПАП.
    // При создании новой таблицы регистрирует смещение в заголовке ПАП.
    static pstringview_table* get_table()
    {
        auto& pam = PersistentAddressSpace::Get();
        // Синхронизируем _table_offset с заголовком ПАП (восстановление после Load).
        if ( _table_offset == 0 )
        {
            uintptr_t stored = pam.GetStringTableOffset();
            if ( stored != 0 )
            {
                _table_offset = stored;
            }
            else
            {
                // Создаём новую таблицу и регистрируем её в заголовке ПАП.
                fptr<pstringview_table> ft;
                ft.New();
                _table_offset = ft.addr();
                pam.SetStringTableOffset( _table_offset );
            }
        }
        return pam.Resolve<pstringview_table>( _table_offset );
    }

    // Сбросить синглтон (для тестов).
    static void reset() { _table_offset = 0; }

    static uintptr_t _table_offset; ///< Смещение таблицы интернирования в ПАП; 0 = не инициализировано
};

// Определение статической переменной (в заголовочном файле через inline — C++17).
inline uintptr_t pstringview_manager::_table_offset = 0;

// ---------------------------------------------------------------------------
// pstringview::intern — реализация (после определения pstringview_table)
// ---------------------------------------------------------------------------

inline void pstringview::intern( const char* s )
{
    if ( s == nullptr )
        s = "";

    auto& pam = PersistentAddressSpace::Get();

    // Сохраняем собственное смещение до любых аллокаций.
    uintptr_t self_off = pam.PtrToOffset( this );

    // Получаем (или создаём) таблицу интернирования через менеджер.
    pstringview_table* tbl = pstringview_manager::get_table();
    // Сохраняем смещение таблицы (get_table мог создать новую таблицу → realloc).
    uintptr_t tbl_off = pstringview_manager::_table_offset;

    // Интернируем строку.
    tbl         = pam.Resolve<pstringview_table>( tbl_off );
    auto result = tbl->intern( s );

    // Переразрешаем себя после возможного realloc.
    pstringview* self  = ( self_off != 0 ) ? pam.Resolve<pstringview>( self_off ) : this;
    self->chars_offset = result.chars_offset;
    self->length       = result.length;
}

// ---------------------------------------------------------------------------
// PersistentAddressSpace — расширение API словаря строк (фаза 2)
//
// Эти методы определяются здесь (в pstringview.h), а не в pam_core.h,
// поскольку они зависят от pstringview_table, которая не доступна в pam_core.h
// из-за запрета на циклические включения.
//
// Задача 2.2: InternString — интернирование строки через ПАМ.
// Задача 2.5: SearchStrings, AllStrings — поиск и перебор строк словаря.
// ---------------------------------------------------------------------------

/// Результат поиска строки в словаре ПАП.
struct pstringview_search_result
{
    std::string value;        ///< Найденная строка
    uintptr_t   chars_offset; ///< Смещение символьных данных в ПАП
    uintptr_t   length;       ///< Длина строки
};

/// Интернировать строку s через ПАМ: вернуть {chars_offset, length} для строки.
/// Это обёртка над pstringview_manager::get_table()->intern(s).
///
/// Задача 2.2: метод уровня ПАМ для интернирования строк.
/// Строка будет занесена в персистный словарь pstringview_table
/// (или возвращено смещение уже существующей записи).
///
/// Возвращает pstringview_table::InternResult с заполненными chars_offset и length.
/// Для хранения pstringview в ПАП создайте его через fptr<pstringview>::New()
/// и вызовите intern().
///
/// Использование:
///   auto r = pam_intern_string("hello");
///   // r.chars_offset != 0, r.length == 5
///   // PersistentAddressSpace::Get().Resolve<char>(r.chars_offset) == "hello"
inline pstringview_table::InternResult pam_intern_string( const char* s )
{
    if ( s == nullptr )
        s = "";
    pstringview_table* tbl     = pstringview_manager::get_table();
    uintptr_t          tbl_off = pstringview_manager::_table_offset;
    auto&              pam     = PersistentAddressSpace::Get();
    tbl                        = pam.Resolve<pstringview_table>( tbl_off );
    return tbl->intern( s );
}

/// Найти все интернированные строки в словаре ПАП, содержащие подстроку pattern.
/// Возвращает вектор результатов поиска (pstringview_search_result).
/// Поиск по substr (O(n*m) scan по всем строкам словаря).
///
/// Задача 2.5: поддержка полнотекстового поиска по словарю pstringview.
inline std::vector<pstringview_search_result> pam_search_strings( const char* pattern )
{
    std::vector<pstringview_search_result> results;
    if ( pattern == nullptr )
        pattern = "";

    pstringview_table* tbl     = pstringview_manager::get_table();
    uintptr_t          tbl_off = pstringview_manager::_table_offset;
    auto&              pam     = PersistentAddressSpace::Get();
    tbl                        = pam.Resolve<pstringview_table>( tbl_off );
    if ( tbl == nullptr )
        return results;

    uintptr_t cap = tbl->capacity_;
    if ( cap == 0 )
        return results;

    for ( uintptr_t i = 0; i < cap; i++ )
    {
        // Переразрешаем tbl на каждой итерации (нет аллокаций — оптимизация не нужна).
        tbl = pam.Resolve<pstringview_table>( tbl_off );
        if ( tbl == nullptr )
            break;
        const pstringview_entry* raw = pam.Resolve<pstringview_entry>( tbl->buckets_.addr() );
        if ( raw == nullptr )
            break;
        const pstringview_entry& cell = raw[i];
        if ( cell.chars_offset == 0 )
            continue; // пустая ячейка
        const char* str = pam.Resolve<char>( cell.chars_offset );
        if ( str == nullptr )
            continue;
        if ( std::strstr( str, pattern ) != nullptr )
        {
            pstringview_search_result r;
            r.value        = str;
            r.chars_offset = cell.chars_offset;
            r.length       = cell.length;
            results.push_back( std::move( r ) );
        }
    }
    return results;
}

/// Вернуть все интернированные строки из словаря ПАП.
/// Удобно для полного перебора словаря (итерация по всем ключам объектов).
///
/// Задача 2.5: PersistentAddressSpace::AllStrings() — перебор всех pstringview в словаре.
inline std::vector<pstringview_search_result> pam_all_strings()
{
    return pam_search_strings( "" );
}
