#pragma once
// pjson_db_helpers.h — Вспомогательные функции для pjson_db.
//
// Вынесены из pjson_db.h для соблюдения ограничения в 1500 строк.
// Содержит функции для работы с путями, поиска узлов и подсчёта метрик.
//
// Все комментарии — на русском языке (Тр.6).

#include "pjson_node.h"
#include <string>
#include <cstring>

// ===========================================================================
// Вспомогательные функции: путевая адресация
// ===========================================================================

/// Разбить путь на родительский путь и последний сегмент.
inline void pjson_split_path( const char* path, std::string& parent, std::string& last )
{
    if ( path == nullptr || path[0] == '\0' )
    {
        parent = "";
        last   = "";
        return;
    }

    std::string full( path );
    // Убираем trailing slash.
    while ( full.size() > 1 && full.back() == '/' )
        full.pop_back();

    auto pos = full.rfind( '/' );
    if ( pos == std::string::npos )
    {
        parent = "";
        last   = full;
    }
    else if ( pos == 0 )
    {
        parent = "/";
        last   = full.substr( 1 );
    }
    else
    {
        parent = full.substr( 0, pos );
        last   = full.substr( pos + 1 );
    }
}

/// Проверить, является ли следующий сегмент пути числовым индексом.
inline bool pjson_next_seg_is_numeric( const char* p )
{
    if ( p == nullptr || *p == '\0' )
        return false;
    char c = *p;
    return ( c >= '0' && c <= '9' );
}

// ===========================================================================
// Вспомогательные функции: подсчёт узлов (для метрик)
// ===========================================================================

/// Рекурсивный обход поддерева для подсчёта узлов по типам.
inline void pjson_count_nodes_in_subtree( node_id id, uint64_t& node_cnt, uint64_t& ref_cnt, uint64_t& array_cnt,
                                          uint64_t& object_cnt, uint64_t& binary_bytes )
{
    if ( id == 0 )
        return;
    const node_view v{ id };
    if ( !v.valid() )
        return;

    ++node_cnt;

    switch ( v.tag() )
    {
    case node_tag::array:
        ++array_cnt;
        {
            uintptr_t sz = v.size();
            for ( uintptr_t i = 0; i < sz; ++i )
            {
                node_view elem = v.at( i );
                if ( elem.valid() )
                    pjson_count_nodes_in_subtree( elem.id, node_cnt, ref_cnt, array_cnt, object_cnt, binary_bytes );
            }
        }
        break;
    case node_tag::object:
        ++object_cnt;
        {
            uintptr_t sz = v.size();
            for ( uintptr_t i = 0; i < sz; ++i )
            {
                node_view val = v.value_at( i );
                if ( val.valid() )
                    pjson_count_nodes_in_subtree( val.id, node_cnt, ref_cnt, array_cnt, object_cnt, binary_bytes );
            }
        }
        break;
    case node_tag::ref:
        ++ref_cnt;
        // Не обходим цель ref (избегаем дублирования счёта).
        break;
    case node_tag::binary:
        // Считаем байты из binary_val.size.
        {
            const node* n = pmm_resolve<node>( id );
            if ( n != nullptr )
                binary_bytes += static_cast<uint64_t>( n->binary_val.size );
        }
        break;
    default:
        break;
    }
}

// ===========================================================================
// Вспомогательные функции: поиск по строковым узлам
// ===========================================================================

/// Рекурсивный обход поддерева для поиска string-узлов (pstring),
/// чьё значение содержит подстроку pattern.
inline void pjson_search_node_strings_in_subtree( node_id id, const char* pattern, std::vector<node_id>& results )
{
    if ( id == 0 )
        return;
    const node_view v{ id };
    if ( !v.valid() )
        return;

    switch ( v.tag() )
    {
    case node_tag::string:
    {
        // Проверяем, содержит ли pstring-значение подстроку pattern.
        std::string_view sv = v.as_string();
        if ( pattern[0] == '\0' )
        {
            // Пустой pattern — возвращаем все string-узлы.
            results.push_back( id );
        }
        else if ( !sv.empty() )
        {
            // Поиск подстроки через std::strstr (безопасно: sv нуль-терминирована pstring).
            const char* s = sv.data();
            if ( s != nullptr && std::strstr( s, pattern ) != nullptr )
                results.push_back( id );
        }
        break;
    }
    case node_tag::array:
    {
        uintptr_t sz = v.size();
        for ( uintptr_t i = 0; i < sz; ++i )
        {
            node_view elem = v.at( i );
            if ( elem.valid() )
                pjson_search_node_strings_in_subtree( elem.id, pattern, results );
        }
        break;
    }
    case node_tag::object:
    {
        uintptr_t sz = v.size();
        for ( uintptr_t i = 0; i < sz; ++i )
        {
            node_view val = v.value_at( i );
            if ( val.valid() )
                pjson_search_node_strings_in_subtree( val.id, pattern, results );
        }
        break;
    }
    case node_tag::ref:
        // Не обходим цель ref (избегаем дублирования).
        break;
    default:
        break;
    }
}

// ===========================================================================
// Вспомогательные функции: разрешение ref
// ===========================================================================

// Примечание: _resolve_refs_in_subtree осталась в pjson_db.h, т.к. зависит от
// pjson_db::get() — нельзя вынести как свободную функцию без передачи db.
