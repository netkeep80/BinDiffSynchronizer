#pragma once
// pjson_serializer.h — Прямая сериализация и десериализация pjson без nlohmann::json.
//
// Реализует требование F6 из задачи #84:
//   «Реализовать прямую сериализацию без nlohmann::json».
//
// Включает:
//   pjson_writer  — рекурсивная запись pjson в std::string (замена _to_nlohmann().dump())
//   pjson_parser  — рекурсивный нисходящий парсер JSON → pjson (замена nlohmann::json::parse + _from_nlohmann)
//
// Для форматирования вещественных чисел использует алгоритм Grisu2 из nlohmann/json.hpp
// (через nlohmann::detail::to_chars), что обеспечивает идентичный вывод с nlohmann::json::dump().
//
// Все комментарии — на русском языке (Тр.6).

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <string>
#include <stdexcept>
// Используем алгоритм Grisu2 из nlohmann для форматирования double.
#include "nlohmann/json.hpp"

// Предварительные объявления — pjson.h подключается ПОСЛЕ этого файла.
struct pjson;
struct pjson_kv_entry;

// ===========================================================================
// pjson_writer — прямая сериализация pjson в std::string
// ===========================================================================

// Вспомогательные функции: экранирование символов в JSON-строке.
namespace pjson_serial_detail
{

/// Добавить символ c в строку out с учётом JSON-экранирования.
inline void append_char_escaped( std::string& out, char c )
{
    switch ( c )
    {
    case '"':
        out += "\\\"";
        break;
    case '\\':
        out += "\\\\";
        break;
    case '\b':
        out += "\\b";
        break;
    case '\f':
        out += "\\f";
        break;
    case '\n':
        out += "\\n";
        break;
    case '\r':
        out += "\\r";
        break;
    case '\t':
        out += "\\t";
        break;
    default:
        if ( static_cast<unsigned char>( c ) < 0x20u )
        {
            // Управляющий символ: \uXXXX
            char buf[8];
            std::snprintf( buf, sizeof( buf ), "\\u%04X", static_cast<unsigned char>( c ) );
            out += buf;
        }
        else
        {
            out += c;
        }
        break;
    }
}

/// Добавить JSON-строку (с кавычками и экранированием) в out.
inline void append_json_string( std::string& out, const char* s )
{
    out += '"';
    if ( s != nullptr )
    {
        while ( *s != '\0' )
        {
            append_char_escaped( out, *s );
            ++s;
        }
    }
    out += '"';
}

/// Добавить представление int64_t в out.
inline void append_int64( std::string& out, int64_t v )
{
    char buf[32];
    std::snprintf( buf, sizeof( buf ), "%lld", static_cast<long long>( v ) );
    out += buf;
}

/// Добавить представление uint64_t в out.
inline void append_uint64( std::string& out, uint64_t v )
{
    char buf[32];
    std::snprintf( buf, sizeof( buf ), "%llu", static_cast<unsigned long long>( v ) );
    out += buf;
}

/// Добавить представление double в out.
/// Использует алгоритм Grisu2 из nlohmann/json.hpp через nlohmann::detail::to_chars,
/// что обеспечивает идентичный вывод с nlohmann::json::dump().
/// Например: 100.0 → "100.0", 3.14 → "3.14", 1e100 → "1e+100".
inline void append_double( std::string& out, double v )
{
    if ( !std::isfinite( v ) )
    {
        // JSON не поддерживает бесконечность и NaN — выводим null для совместимости.
        out += "null";
        return;
    }
    // Используем nlohmann::detail::to_chars (алгоритм Grisu2) для кратчайшего представления.
    // Размер буфера: достаточно для максимального представления double (64 символа).
    char  buf[64];
    char* end = ::nlohmann::detail::to_chars( buf, buf + sizeof( buf ), v );
    out.append( buf, static_cast<std::size_t>( end - buf ) );
}

} // namespace pjson_serial_detail

// ===========================================================================
// pjson_parser — рекурсивный нисходящий парсер JSON → pjson
// ===========================================================================

namespace pjson_parser_detail
{

/// Внутреннее состояние парсера: указатель на текущую позицию и конец строки.
struct ParseState
{
    const char* pos; ///< Текущая позиция
    const char* end; ///< Конец строки (за последним символом)
};

/// Пропустить пробельные символы (space, tab, \r, \n).
inline void skip_ws( ParseState& st )
{
    while ( st.pos < st.end )
    {
        char c = *st.pos;
        if ( c == ' ' || c == '\t' || c == '\r' || c == '\n' )
            ++st.pos;
        else
            break;
    }
}

/// Прочитать один символ, не продвигая позицию. Возвращает '\0' при конце.
inline char peek( const ParseState& st )
{
    if ( st.pos >= st.end )
        return '\0';
    return *st.pos;
}

/// Продвинуть позицию на 1.
inline void advance( ParseState& st )
{
    if ( st.pos < st.end )
        ++st.pos;
}

/// Проверить и поглотить ожидаемый символ. Возвращает false при несовпадении.
inline bool expect( ParseState& st, char c )
{
    if ( st.pos >= st.end || *st.pos != c )
        return false;
    ++st.pos;
    return true;
}

/// Прочитать 4 hex-цифры и вернуть кодовую точку (0–0xFFFF).
/// Возвращает -1 при ошибке.
inline int parse_hex4( ParseState& st )
{
    if ( st.end - st.pos < 4 )
        return -1;
    unsigned val = 0;
    for ( int i = 0; i < 4; i++ )
    {
        char     c = *st.pos++;
        unsigned digit;
        if ( c >= '0' && c <= '9' )
            digit = static_cast<unsigned>( c - '0' );
        else if ( c >= 'a' && c <= 'f' )
            digit = static_cast<unsigned>( c - 'a' ) + 10u;
        else if ( c >= 'A' && c <= 'F' )
            digit = static_cast<unsigned>( c - 'A' ) + 10u;
        else
            return -1;
        val = ( val << 4 ) | digit;
    }
    return static_cast<int>( val );
}

/// Декодировать Unicode code point (U+XXXX) в UTF-8 и дописать в out.
inline void encode_utf8( std::string& out, unsigned cp )
{
    if ( cp < 0x80u )
    {
        out += static_cast<char>( cp );
    }
    else if ( cp < 0x800u )
    {
        out += static_cast<char>( 0xC0u | ( cp >> 6 ) );
        out += static_cast<char>( 0x80u | ( cp & 0x3Fu ) );
    }
    else
    {
        out += static_cast<char>( 0xE0u | ( cp >> 12 ) );
        out += static_cast<char>( 0x80u | ( ( cp >> 6 ) & 0x3Fu ) );
        out += static_cast<char>( 0x80u | ( cp & 0x3Fu ) );
    }
}

/// Разобрать JSON-строку (позиция уже ПОСЛЕ открывающей ") в std::string.
/// Возвращает false при ошибке.
inline bool parse_string( ParseState& st, std::string& out )
{
    out.clear();
    while ( st.pos < st.end )
    {
        char c = *st.pos++;
        if ( c == '"' )
            return true; // конец строки
        if ( c == '\\' )
        {
            if ( st.pos >= st.end )
                return false;
            char esc = *st.pos++;
            switch ( esc )
            {
            case '"':
                out += '"';
                break;
            case '\\':
                out += '\\';
                break;
            case '/':
                out += '/';
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u':
            {
                int cp = parse_hex4( st );
                if ( cp < 0 )
                    return false;
                // Суррогатная пара UTF-16?
                if ( cp >= 0xD800 && cp <= 0xDBFF )
                {
                    // Ожидаем \uXXXX для младшего суррогата.
                    if ( st.pos + 1 < st.end && *st.pos == '\\' && *( st.pos + 1 ) == 'u' )
                    {
                        st.pos += 2;
                        int lo = parse_hex4( st );
                        if ( lo >= 0xDC00 && lo <= 0xDFFF )
                        {
                            unsigned full = 0x10000u + ( static_cast<unsigned>( cp - 0xD800 ) << 10 ) +
                                            static_cast<unsigned>( lo - 0xDC00 );
                            encode_utf8( out, full );
                            break;
                        }
                    }
                    // Не удалось разобрать суррогат — вставляем как есть (UTF-8 BMP).
                }
                encode_utf8( out, static_cast<unsigned>( cp ) );
                break;
            }
            default:
                return false;
            }
        }
        else
        {
            out += c;
        }
    }
    return false; // не встретили закрывающую "
}

/// Разобрать числовой токен: integer, uinteger или real.
/// Записывает результат в pjson (по смещению dst_offset).
/// Возвращает false при ошибке.
inline bool parse_number( ParseState& st, uintptr_t dst_offset )
{
    const char* start = st.pos;
    bool        neg   = false;
    if ( peek( st ) == '-' )
    {
        neg = true;
        advance( st );
    }

    // Целая часть.
    if ( st.pos >= st.end )
        return false;
    if ( *st.pos == '0' )
    {
        advance( st );
    }
    else if ( *st.pos >= '1' && *st.pos <= '9' )
    {
        while ( st.pos < st.end && *st.pos >= '0' && *st.pos <= '9' )
            advance( st );
    }
    else
    {
        return false;
    }

    bool is_float = false;
    // Дробная часть.
    if ( st.pos < st.end && *st.pos == '.' )
    {
        is_float = true;
        advance( st );
        if ( st.pos >= st.end || *st.pos < '0' || *st.pos > '9' )
            return false;
        while ( st.pos < st.end && *st.pos >= '0' && *st.pos <= '9' )
            advance( st );
    }
    // Экспоненциальная часть.
    if ( st.pos < st.end && ( *st.pos == 'e' || *st.pos == 'E' ) )
    {
        is_float = true;
        advance( st );
        if ( st.pos < st.end && ( *st.pos == '+' || *st.pos == '-' ) )
            advance( st );
        if ( st.pos >= st.end || *st.pos < '0' || *st.pos > '9' )
            return false;
        while ( st.pos < st.end && *st.pos >= '0' && *st.pos <= '9' )
            advance( st );
    }

    // Преобразуем в значение.
    auto& pam = PersistentAddressSpace::Get();
    if ( is_float )
    {
        double val = std::strtod( start, nullptr );
        pam.Resolve<pjson>( dst_offset )->set_real( val );
    }
    else if ( neg )
    {
        long long val = std::strtoll( start, nullptr, 10 );
        pam.Resolve<pjson>( dst_offset )->set_int( static_cast<int64_t>( val ) );
    }
    else
    {
        unsigned long long val = std::strtoull( start, nullptr, 10 );
        pam.Resolve<pjson>( dst_offset )->set_uint( static_cast<uint64_t>( val ) );
    }
    return true;
}

// Предварительное объявление рекурсивного парсера значения.
bool parse_value( ParseState& st, uintptr_t dst_offset );

/// Разобрать JSON-массив (позиция уже ПОСЛЕ '[').
inline bool parse_array( ParseState& st, uintptr_t dst_offset )
{
    auto& pam = PersistentAddressSpace::Get();
    pam.Resolve<pjson>( dst_offset )->set_array();

    skip_ws( st );
    if ( peek( st ) == ']' )
    {
        advance( st );
        return true; // пустой массив
    }

    for ( ;; )
    {
        skip_ws( st );
        // Добавляем новый элемент в конец.
        pjson*    d        = pam.Resolve<pjson>( dst_offset );
        pjson&    new_elem = d->push_back();
        uintptr_t elem_off = pam.PtrToOffset( &new_elem );

        if ( !parse_value( st, elem_off ) )
            return false;

        skip_ws( st );
        char c = peek( st );
        if ( c == ']' )
        {
            advance( st );
            return true;
        }
        if ( c != ',' )
            return false;
        advance( st ); // поглощаем ','
    }
}

/// Разобрать JSON-объект (позиция уже ПОСЛЕ '{').
inline bool parse_object( ParseState& st, uintptr_t dst_offset )
{
    auto& pam = PersistentAddressSpace::Get();
    pam.Resolve<pjson>( dst_offset )->set_object();

    skip_ws( st );
    if ( peek( st ) == '}' )
    {
        advance( st );
        return true; // пустой объект
    }

    std::string key_buf; // буфер для декодирования ключа (в обычной памяти)
    for ( ;; )
    {
        skip_ws( st );
        // Ожидаем строку-ключ.
        if ( !expect( st, '"' ) )
            return false;
        if ( !parse_string( st, key_buf ) )
            return false;

        skip_ws( st );
        if ( !expect( st, ':' ) )
            return false;
        skip_ws( st );

        // Вставляем ключ в объект и рекурсивно разбираем значение.
        pjson*    d       = pam.Resolve<pjson>( dst_offset );
        pjson&    new_val = d->obj_insert( key_buf.c_str() );
        uintptr_t val_off = pam.PtrToOffset( &new_val );

        if ( !parse_value( st, val_off ) )
            return false;

        skip_ws( st );
        char c = peek( st );
        if ( c == '}' )
        {
            advance( st );
            return true;
        }
        if ( c != ',' )
            return false;
        advance( st ); // поглощаем ','
    }
}

/// Разобрать одно JSON-значение и записать в pjson по смещению dst_offset.
inline bool parse_value( ParseState& st, uintptr_t dst_offset )
{
    skip_ws( st );
    char  c   = peek( st );
    auto& pam = PersistentAddressSpace::Get();

    if ( c == 'n' )
    {
        // null
        if ( st.end - st.pos >= 4 && std::strncmp( st.pos, "null", 4 ) == 0 )
        {
            st.pos += 4;
            pam.Resolve<pjson>( dst_offset )->set_null();
            return true;
        }
        return false;
    }
    if ( c == 't' )
    {
        // true
        if ( st.end - st.pos >= 4 && std::strncmp( st.pos, "true", 4 ) == 0 )
        {
            st.pos += 4;
            pam.Resolve<pjson>( dst_offset )->set_bool( true );
            return true;
        }
        return false;
    }
    if ( c == 'f' )
    {
        // false
        if ( st.end - st.pos >= 5 && std::strncmp( st.pos, "false", 5 ) == 0 )
        {
            st.pos += 5;
            pam.Resolve<pjson>( dst_offset )->set_bool( false );
            return true;
        }
        return false;
    }
    if ( c == '"' )
    {
        advance( st ); // поглощаем '"'
        std::string str_buf;
        if ( !parse_string( st, str_buf ) )
            return false;
        // После parse_string буфер ПАМ мог переместиться (set_string вызывает alloc).
        pam.Resolve<pjson>( dst_offset )->set_string( str_buf.c_str() );
        return true;
    }
    if ( c == '[' )
    {
        advance( st );
        return parse_array( st, dst_offset );
    }
    if ( c == '{' )
    {
        advance( st );
        return parse_object( st, dst_offset );
    }
    if ( c == '-' || ( c >= '0' && c <= '9' ) )
    {
        return parse_number( st, dst_offset );
    }
    return false; // неизвестный токен
}

} // namespace pjson_parser_detail
