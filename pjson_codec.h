#pragma once
// pjson_codec.h — Сериализация и десериализация для новой модели узлов (Фаза 5).
//
// Реализует парсер и сериализатор, работающие с node_id-адресацией и pjson_pool.
// Поддерживает расширения:
//   - $ref: объект вида {"$ref": "<path>"} -> node_tag::ref
//   - $base64: объект вида {"$base64": "<base64>"} -> node_tag::binary
//
// Ключевые архитектурные решения:
//   - Ключи объектов интернируются через pam_intern_string -> pstringview (readonly)
//   - Строковые значения JSON создаются как pstring-узлы (readwrite)
//   - Сегменты путей $ref интернируются как pstringview (readonly)
//
// Все комментарии — на русском языке (Тр.6).

#include "pjson_node.h"
#include "pjson_pool.h"
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <string>
#include <stdexcept>
#include <charconv>
#include <vector>
#include <algorithm>

// ===========================================================================
// Base64 кодек (для $base64)
// ===========================================================================

namespace pjson_codec_detail
{

/// Таблица декодирования Base64.
/// -1 = недопустимый символ, -2 = padding ('=').
inline int base64_decode_char( char c )
{
    if ( c >= 'A' && c <= 'Z' )
        return c - 'A';
    if ( c >= 'a' && c <= 'z' )
        return c - 'a' + 26;
    if ( c >= '0' && c <= '9' )
        return c - '0' + 52;
    if ( c == '+' )
        return 62;
    if ( c == '/' )
        return 63;
    if ( c == '=' )
        return -2; // padding
    return -1;     // invalid
}

/// Декодировать Base64 строку в вектор байт.
/// Возвращает true при успехе, false при ошибке.
inline bool base64_decode( const char* input, uintptr_t len, std::vector<uint8_t>& out )
{
    out.clear();
    if ( len == 0 )
        return true;

    // Пропускаем пробельные символы в конце.
    while ( len > 0 &&
            ( input[len - 1] == ' ' || input[len - 1] == '\n' || input[len - 1] == '\r' || input[len - 1] == '\t' ) )
        --len;

    // Длина должна быть кратна 4.
    if ( len % 4 != 0 )
        return false;

    out.reserve( ( len / 4 ) * 3 );

    for ( uintptr_t i = 0; i < len; i += 4 )
    {
        int c0 = base64_decode_char( input[i] );
        int c1 = base64_decode_char( input[i + 1] );
        int c2 = base64_decode_char( input[i + 2] );
        int c3 = base64_decode_char( input[i + 3] );

        if ( c0 < 0 || c1 < 0 )
            return false; // первые два символа не могут быть padding

        // Первый байт.
        out.push_back( static_cast<uint8_t>( ( c0 << 2 ) | ( c1 >> 4 ) ) );

        // Второй байт (если не padding).
        if ( c2 == -2 )
        {
            // Один padding: ожидаем c3 тоже padding.
            if ( c3 != -2 )
                return false;
            break;
        }
        if ( c2 < 0 )
            return false;
        out.push_back( static_cast<uint8_t>( ( ( c1 & 0x0F ) << 4 ) | ( c2 >> 2 ) ) );

        // Третий байт (если не padding).
        if ( c3 == -2 )
            break;
        if ( c3 < 0 )
            return false;
        out.push_back( static_cast<uint8_t>( ( ( c2 & 0x03 ) << 6 ) | c3 ) );
    }

    return true;
}

/// Таблица кодирования Base64.
inline const char* base64_encode_table()
{
    return "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

/// Кодировать вектор байт в Base64 строку.
inline std::string base64_encode( const uint8_t* data, uintptr_t len )
{
    const char* table = base64_encode_table();
    std::string out;
    out.reserve( ( ( len + 2 ) / 3 ) * 4 );

    for ( uintptr_t i = 0; i < len; i += 3 )
    {
        uint8_t b0 = data[i];
        uint8_t b1 = ( i + 1 < len ) ? data[i + 1] : 0;
        uint8_t b2 = ( i + 2 < len ) ? data[i + 2] : 0;

        out += table[b0 >> 2];
        out += table[( ( b0 & 0x03 ) << 4 ) | ( b1 >> 4 )];

        if ( i + 1 < len )
            out += table[( ( b1 & 0x0F ) << 2 ) | ( b2 >> 6 )];
        else
            out += '=';

        if ( i + 2 < len )
            out += table[b2 & 0x3F];
        else
            out += '=';
    }

    return out;
}

// ===========================================================================
// Вспомогательные функции сериализации
// ===========================================================================

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
inline void append_json_string( std::string& out, const char* s, uintptr_t len )
{
    out += '"';
    if ( s != nullptr )
    {
        for ( uintptr_t i = 0; i < len; ++i )
            append_char_escaped( out, s[i] );
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
/// Использует std::to_chars (C++17) для кратчайшего представления.
/// Гарантирует наличие десятичной точки: 100.0 -> "100.0".
inline void append_double( std::string& out, double v )
{
    if ( !std::isfinite( v ) )
    {
        // JSON не поддерживает бесконечность и NaN — выводим null.
        out += "null";
        return;
    }
    char                 buf[64];
    std::to_chars_result res = std::to_chars( buf, buf + sizeof( buf ), v );
    std::size_t          len = static_cast<std::size_t>( res.ptr - buf );

    // Проверяем наличие десятичной точки или экспоненты.
    bool has_dot_or_exp = false;
    for ( std::size_t i = 0; i < len; ++i )
    {
        char c = buf[i];
        if ( c == '.' || c == 'e' || c == 'E' )
        {
            has_dot_or_exp = true;
            break;
        }
    }
    out.append( buf, len );
    if ( !has_dot_or_exp )
        out += ".0";
}

// ===========================================================================
// Парсер JSON -> node_id (Задача 5.1)
// ===========================================================================

/// Внутреннее состояние парсера.
struct ParseState
{
    const char* pos; ///< Текущая позиция
    const char* end; ///< Конец строки
};

/// Пропустить пробельные символы.
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

/// Прочитать символ без продвижения.
inline char peek( const ParseState& st )
{
    if ( st.pos >= st.end )
        return '\0';
    return *st.pos;
}

/// Продвинуть позицию.
inline void advance( ParseState& st )
{
    if ( st.pos < st.end )
        ++st.pos;
}

/// Проверить и поглотить ожидаемый символ.
inline bool expect( ParseState& st, char c )
{
    if ( st.pos >= st.end || *st.pos != c )
        return false;
    ++st.pos;
    return true;
}

/// Прочитать 4 hex-цифры.
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

/// Декодировать Unicode code point в UTF-8.
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
    else if ( cp < 0x10000u )
    {
        out += static_cast<char>( 0xE0u | ( cp >> 12 ) );
        out += static_cast<char>( 0x80u | ( ( cp >> 6 ) & 0x3Fu ) );
        out += static_cast<char>( 0x80u | ( cp & 0x3Fu ) );
    }
    else
    {
        out += static_cast<char>( 0xF0u | ( cp >> 18 ) );
        out += static_cast<char>( 0x80u | ( ( cp >> 12 ) & 0x3Fu ) );
        out += static_cast<char>( 0x80u | ( ( cp >> 6 ) & 0x3Fu ) );
        out += static_cast<char>( 0x80u | ( cp & 0x3Fu ) );
    }
}

/// Разобрать JSON-строку (позиция уже ПОСЛЕ открывающей ").
inline bool parse_string( ParseState& st, std::string& out )
{
    out.clear();
    while ( st.pos < st.end )
    {
        char c = *st.pos++;
        if ( c == '"' )
            return true;
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
    return false;
}

/// Разобрать числовой токен и записать в node.
inline bool parse_number( ParseState& st, uintptr_t node_off )
{
    const char* start = st.pos;
    bool        neg   = false;
    if ( peek( st ) == '-' )
    {
        neg = true;
        advance( st );
    }

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
    if ( st.pos < st.end && *st.pos == '.' )
    {
        is_float = true;
        advance( st );
        if ( st.pos >= st.end || *st.pos < '0' || *st.pos > '9' )
            return false;
        while ( st.pos < st.end && *st.pos >= '0' && *st.pos <= '9' )
            advance( st );
    }
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

    if ( is_float )
    {
        double val = std::strtod( start, nullptr );
        node_set_real( node_off, val );
    }
    else if ( neg )
    {
        long long val = std::strtoll( start, nullptr, 10 );
        node_set_int( node_off, static_cast<int64_t>( val ) );
    }
    else
    {
        unsigned long long val = std::strtoull( start, nullptr, 10 );
        node_set_uint( node_off, static_cast<uint64_t>( val ) );
    }
    return true;
}

// Предварительное объявление.
bool parse_value( ParseState& st, uintptr_t node_off );

/// Разобрать JSON-массив (позиция уже ПОСЛЕ '[').
inline bool parse_array( ParseState& st, uintptr_t node_off )
{
    node_set_array( node_off );

    skip_ws( st );
    if ( peek( st ) == ']' )
    {
        advance( st );
        return true;
    }

    for ( ;; )
    {
        skip_ws( st );
        // Добавляем новый элемент.
        node_id elem_off = node_array_push_back( node_off );

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
        advance( st );
    }
}

/// Разобрать JSON-объект (позиция уже ПОСЛЕ '{').
/// Распознаёт специальные объекты $ref и $base64 (Задачи 5.2, 5.3).
inline bool parse_object( ParseState& st, uintptr_t node_off )
{
    skip_ws( st );
    if ( peek( st ) == '}' )
    {
        advance( st );
        node_set_object( node_off );
        return true;
    }

    // Временно собираем пары ключ-значение.
    struct KV
    {
        std::string key;
        std::string value_str; // Только для $ref и $base64
        bool        is_string;
    };
    std::vector<KV> pairs;

    std::string key_buf;
    std::string val_buf;

    for ( ;; )
    {
        skip_ws( st );
        if ( !expect( st, '"' ) )
            return false;
        if ( !parse_string( st, key_buf ) )
            return false;

        skip_ws( st );
        if ( !expect( st, ':' ) )
            return false;
        skip_ws( st );

        // Для $ref и $base64 нужно проверить, является ли значение строкой.
        bool is_string_val = ( peek( st ) == '"' );
        if ( is_string_val )
        {
            advance( st );
            if ( !parse_string( st, val_buf ) )
                return false;
            pairs.push_back( { key_buf, val_buf, true } );
        }
        else
        {
            pairs.push_back( { key_buf, "", false } );
        }

        skip_ws( st );
        char c = peek( st );
        if ( c == '}' )
        {
            advance( st );
            break;
        }
        if ( c != ',' )
            return false;
        advance( st );
    }

    // Проверяем на специальные объекты: ровно 1 ключ.
    if ( pairs.size() == 1 && pairs[0].is_string )
    {
        // Задача 5.2: $ref
        if ( pairs[0].key == "$ref" )
        {
            node_set_ref( node_off, pairs[0].value_str.c_str() );
            return true;
        }

        // Задача 5.3: $base64
        if ( pairs[0].key == "$base64" )
        {
            std::vector<uint8_t> bytes;
            if ( !base64_decode( pairs[0].value_str.c_str(), static_cast<uintptr_t>( pairs[0].value_str.size() ),
                                 bytes ) )
            {
                // Некорректный base64 — создаём пустой binary.
                node_set_binary( node_off );
                return true;
            }
            node_set_binary( node_off );
            for ( uint8_t b : bytes )
                node_binary_push_back( node_off, b );
            return true;
        }
    }

    // Обычный объект.
    node_set_object( node_off );

    // Теперь нужно повторно распарсить значения, которые не были строками.
    // Для этого восстанавливаем позицию и парсим заново.
    // Это неоптимально, но простой подход для первой реализации.

    // Альтернатива: создаём узлы сразу.
    // Поскольку у нас уже есть pairs с ключами, а нестроковые значения
    // не были сохранены, нам нужен другой подход.

    // Пересоздаём парсер с начала объекта — это неэффективно.
    // Лучше: сразу парсить в узлы и проверять на $ref/$base64 после.

    // Для простоты реализации: перепарсим объект целиком.
    // Сначала освободим node_off и пересоздадим.
    return false; // Заглушка — перепишем логику ниже.
}

/// Улучшенная версия parse_object с корректной обработкой $ref/$base64.
inline bool parse_object_v2( ParseState& st, uintptr_t node_off )
{
    skip_ws( st );
    if ( peek( st ) == '}' )
    {
        advance( st );
        node_set_object( node_off );
        return true;
    }

    // Первый проход: подсчитываем ключи и проверяем на $ref/$base64.
    std::string first_key;
    std::string first_val;
    bool        first_val_is_string = false;
    int         key_count           = 0;
    const char* reparse_pos         = st.pos;

    // Парсим только первый ключ-значение для проверки $ref/$base64.
    {
        skip_ws( st );
        if ( !expect( st, '"' ) )
            return false;
        if ( !parse_string( st, first_key ) )
            return false;

        skip_ws( st );
        if ( !expect( st, ':' ) )
            return false;
        skip_ws( st );

        first_val_is_string = ( peek( st ) == '"' );
        if ( first_val_is_string )
        {
            advance( st );
            if ( !parse_string( st, first_val ) )
                return false;
        }
        else
        {
            // Пропускаем значение.
            int depth = 0;
            while ( st.pos < st.end )
            {
                char c = *st.pos;
                if ( c == '{' || c == '[' )
                {
                    depth++;
                    advance( st );
                }
                else if ( c == '}' || c == ']' )
                {
                    if ( depth == 0 )
                        break;
                    depth--;
                    advance( st );
                }
                else if ( c == ',' && depth == 0 )
                {
                    break;
                }
                else if ( c == '"' )
                {
                    advance( st );
                    std::string dummy;
                    if ( !parse_string( st, dummy ) )
                        return false;
                }
                else
                {
                    advance( st );
                }
            }
        }
        key_count = 1;

        skip_ws( st );
        char c = peek( st );
        if ( c == ',' )
        {
            // Есть ещё ключи — не $ref/$base64.
            key_count = 2;
        }
        else if ( c != '}' )
        {
            return false;
        }
    }

    // Проверяем на $ref/$base64: ровно 1 ключ и строковое значение.
    if ( key_count == 1 && first_val_is_string )
    {
        // Проглатываем закрывающую скобку.
        skip_ws( st );
        if ( !expect( st, '}' ) )
            return false;

        if ( first_key == "$ref" )
        {
            node_set_ref( node_off, first_val.c_str() );
            return true;
        }

        if ( first_key == "$base64" )
        {
            std::vector<uint8_t> bytes;
            if ( !base64_decode( first_val.c_str(), static_cast<uintptr_t>( first_val.size() ), bytes ) )
            {
                node_set_binary( node_off );
                return true;
            }
            node_set_binary( node_off );
            for ( uint8_t b : bytes )
                node_binary_push_back( node_off, b );
            return true;
        }
    }

    // Обычный объект — перепарсиваем с начала.
    st.pos = reparse_pos;
    node_set_object( node_off );

    std::string key_buf;
    for ( ;; )
    {
        skip_ws( st );
        if ( !expect( st, '"' ) )
            return false;
        if ( !parse_string( st, key_buf ) )
            return false;

        skip_ws( st );
        if ( !expect( st, ':' ) )
            return false;
        skip_ws( st );

        // Вставляем ключ и получаем node_id для значения.
        node_id val_off = node_object_insert( node_off, key_buf.c_str() );

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
        advance( st );
    }
}

/// Разобрать одно JSON-значение и записать в node по смещению node_off.
inline bool parse_value( ParseState& st, uintptr_t node_off )
{
    skip_ws( st );
    char c = peek( st );

    if ( c == 'n' )
    {
        if ( st.end - st.pos >= 4 && std::strncmp( st.pos, "null", 4 ) == 0 )
        {
            st.pos += 4;
            node_init_null( node_off );
            return true;
        }
        return false;
    }
    if ( c == 't' )
    {
        if ( st.end - st.pos >= 4 && std::strncmp( st.pos, "true", 4 ) == 0 )
        {
            st.pos += 4;
            node_set_bool( node_off, true );
            return true;
        }
        return false;
    }
    if ( c == 'f' )
    {
        if ( st.end - st.pos >= 5 && std::strncmp( st.pos, "false", 5 ) == 0 )
        {
            st.pos += 5;
            node_set_bool( node_off, false );
            return true;
        }
        return false;
    }
    if ( c == '"' )
    {
        advance( st );
        std::string str_buf;
        if ( !parse_string( st, str_buf ) )
            return false;
        node_set_string( node_off, str_buf.c_str() );
        return true;
    }
    if ( c == '[' )
    {
        advance( st );
        return parse_array( st, node_off );
    }
    if ( c == '{' )
    {
        advance( st );
        return parse_object_v2( st, node_off );
    }
    if ( c == '-' || ( c >= '0' && c <= '9' ) )
    {
        return parse_number( st, node_off );
    }
    return false;
}

} // namespace pjson_codec_detail

// ===========================================================================
// Публичный API pjson_codec (Фаза 5)
// ===========================================================================

/// Сериализовать node в JSON-строку.
/// Для ref-узлов выводит {"$ref": "<path>"}.
/// Для binary-узлов выводит {"$base64": "<base64>"}.
inline void node_serialize_to( node_id id, std::string& out )
{
    using namespace pjson_codec_detail;

    node_view v{ id };
    if ( !v.valid() )
    {
        out += "null";
        return;
    }

    switch ( v.tag() )
    {
    case node_tag::null:
        out += "null";
        break;

    case node_tag::boolean:
        out += v.as_bool() ? "true" : "false";
        break;

    case node_tag::integer:
        append_int64( out, v.as_int() );
        break;

    case node_tag::uinteger:
        append_uint64( out, v.as_uint() );
        break;

    case node_tag::real:
        append_double( out, v.as_double() );
        break;

    case node_tag::string:
    {
        std::string_view sv = v.as_string();
        append_json_string( out, sv.data(), static_cast<uintptr_t>( sv.size() ) );
        break;
    }

    case node_tag::binary:
    {
        // Задача 5.4: binary -> {"$base64": "..."}
        auto&       pam = PersistentAddressSpace::Get();
        const node* n   = pam.Resolve<node>( id );
        if ( n == nullptr || n->binary_val.size == 0 )
        {
            out += "{\"$base64\":\"\"}";
        }
        else
        {
            const uint8_t* data = pam.Resolve<uint8_t>( n->binary_val.data_off );
            std::string    b64  = base64_encode( data, n->binary_val.size );
            out += "{\"$base64\":\"";
            out += b64;
            out += "\"}";
        }
        break;
    }

    case node_tag::array:
    {
        out += '[';
        uintptr_t sz = v.size();
        for ( uintptr_t i = 0; i < sz; ++i )
        {
            if ( i > 0 )
                out += ',';
            node_serialize_to( v.at( i ).id, out );
        }
        out += ']';
        break;
    }

    case node_tag::object:
    {
        out += '{';
        uintptr_t sz = v.size();
        for ( uintptr_t i = 0; i < sz; ++i )
        {
            if ( i > 0 )
                out += ',';
            std::string_view key = v.key_at( i );
            append_json_string( out, key.data(), static_cast<uintptr_t>( key.size() ) );
            out += ':';
            node_serialize_to( v.value_at( i ).id, out );
        }
        out += '}';
        break;
    }

    case node_tag::ref:
    {
        // Задача 5.4: ref -> {"$ref": "<path>"}
        std::string_view path = v.ref_path();
        out += "{\"$ref\":\"";
        if ( path.data() != nullptr )
        {
            for ( char c : path )
                append_char_escaped( out, c );
        }
        out += "\"}";
        break;
    }

    default:
        out += "null";
        break;
    }
}

/// Сериализовать node в JSON-строку (возвращает std::string).
inline std::string node_to_string( node_id id )
{
    std::string result;
    node_serialize_to( id, result );
    return result;
}

/// Десериализовать JSON-строку в node по смещению node_off.
/// Поддерживает $ref и $base64.
inline bool node_from_string( const char* json, uintptr_t node_off )
{
    if ( json == nullptr )
    {
        node_init_null( node_off );
        return false;
    }

    pjson_codec_detail::ParseState st;
    st.pos = json;
    st.end = json + std::strlen( json );

    bool ok = pjson_codec_detail::parse_value( st, node_off );
    if ( !ok )
    {
        node_init_null( node_off );
    }
    return ok;
}

/// Десериализовать JSON-строку, аллоцируя новый node.
/// Возвращает node_id (0 при ошибке).
inline node_id node_parse( const char* json )
{
    // Аллоцируем новый node.
    fptr<node> fv;
    fv.New();
    uintptr_t node_off = fv.addr();

    if ( !node_from_string( json, node_off ) )
        return 0;

    return node_off;
}
