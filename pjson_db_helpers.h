#pragma once
// pjson_db_helpers.h — Вспомогательные функции для pjson_db.
//
// Вынесены из pjson_db_pmm.h для соблюдения ограничения в 1500 строк.
// Содержит функции для работы с путями, поиска узлов и подсчёта метрик.
//
// Все комментарии — на русском языке.

#include "pjson_node.h"
#include <string>
#include <cstring>

// ===========================================================================
// Вспомогательные функции: путевая адресация
// ===========================================================================

/// Декодировать сегмент пути по RFC 6901 (JSON Pointer):
///   ~1 → /
///   ~0 → ~
/// Порядок декодирования важен: сначала ~1, затем ~0.
inline std::string pjson_decode_rfc6901_segment( const char* data, uintptr_t len )
{
    std::string result;
    result.reserve( len );
    for ( uintptr_t i = 0; i < len; ++i )
    {
        if ( data[i] == '~' && i + 1 < len )
        {
            if ( data[i + 1] == '1' )
            {
                result += '/';
                ++i;
                continue;
            }
            else if ( data[i + 1] == '0' )
            {
                result += '~';
                ++i;
                continue;
            }
        }
        result += data[i];
    }
    return result;
}

/// Декодировать сегмент пути по RFC 6901 (JSON Pointer) из std::string.
inline std::string pjson_decode_rfc6901_segment( const std::string& seg )
{
    return pjson_decode_rfc6901_segment( seg.c_str(), static_cast<uintptr_t>( seg.size() ) );
}

/// Разбить путь на родительский путь и последний сегмент.
/// Последний сегмент декодируется по RFC 6901.
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
        last   = pjson_decode_rfc6901_segment( full );
    }
    else if ( pos == 0 )
    {
        parent = "/";
        last   = pjson_decode_rfc6901_segment( full.substr( 1 ) );
    }
    else
    {
        parent = full.substr( 0, pos );
        last   = pjson_decode_rfc6901_segment( full.substr( pos + 1 ) );
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
// Обобщённый обход поддерева (visitor pattern)
// ===========================================================================

/// Рекурсивный обход поддерева JSON-дерева с visitor-паттерном.
/// Visitor должен реализовать метод visit(node_id, const node_view&).
/// Рекурсия выполняется для array (по элементам) и object (по значениям).
/// ref-узлы не рекурсируются (избегаем дублирования и циклов).
template <typename Visitor> inline void pjson_traverse_subtree( node_id id, Visitor&& vis )
{
    if ( id == 0 )
        return;
    const node_view v{ id };
    if ( !v.valid() )
        return;

    vis.visit( id, v );

    if ( v.is_array() )
    {
        uintptr_t sz = v.size();
        for ( uintptr_t i = 0; i < sz; ++i )
        {
            node_view elem = v.at( i );
            if ( elem.valid() )
                pjson_traverse_subtree( elem.id, vis );
        }
    }
    else if ( v.is_object() )
    {
        uintptr_t sz = v.size();
        for ( uintptr_t i = 0; i < sz; ++i )
        {
            node_view val = v.value_at( i );
            if ( val.valid() )
                pjson_traverse_subtree( val.id, vis );
        }
    }
}

/// Рекурсивный обход поддерева JSON-дерева в post-order (дети → родитель).
/// Visitor должен реализовать метод visit(node_id, const node_view&).
/// Используется для операций, требующих обработки детей до родителя (например, освобождение).
template <typename Visitor> inline void pjson_traverse_subtree_postorder( node_id id, Visitor&& vis )
{
    if ( id == 0 )
        return;
    const node_view v{ id };
    if ( !v.valid() )
        return;

    if ( v.is_array() )
    {
        uintptr_t sz = v.size();
        for ( uintptr_t i = 0; i < sz; ++i )
        {
            node_view elem = v.at( i );
            if ( elem.valid() )
                pjson_traverse_subtree_postorder( elem.id, vis );
        }
    }
    else if ( v.is_object() )
    {
        uintptr_t sz = v.size();
        for ( uintptr_t i = 0; i < sz; ++i )
        {
            node_view val = v.value_at( i );
            if ( val.valid() )
                pjson_traverse_subtree_postorder( val.id, vis );
        }
    }

    vis.visit( id, v );
}

// ===========================================================================
// Visitor для освобождения поддерева узлов (post-order)
// ===========================================================================

/// Visitor для post-order обхода: освобождает ресурсы узла и удаляет его из ПАП.
/// Должен использоваться с pjson_traverse_subtree_postorder(), чтобы дети
/// были освобождены раньше родителей.
struct pjson_free_node_visitor
{
    void visit( node_id id, const node_view& v )
    {
        node* n = pmm_resolve<node>( id );
        switch ( v.tag() )
        {
        case node_tag::array:
            if ( n != nullptr )
                n->array_val.free_data();
            break;
        case node_tag::object:
            if ( n != nullptr )
                n->object_val.free_data();
            break;
        case node_tag::string:
            if ( n != nullptr )
                n->string_val.free_data();
            break;
        case node_tag::binary:
            if ( n != nullptr )
                n->binary_val.free_data();
            break;
        default:
            break;
        }
        pam_pmm_delete( id );
    }
};

/// Освободить поддерево JSON-узлов: рекурсивно удаляет все дочерние узлы и сам узел.
inline void pjson_free_node_tree( node_id id )
{
    pjson_free_node_visitor vis;
    pjson_traverse_subtree_postorder( id, vis );
}

// ===========================================================================
// Вспомогательные функции: подсчёт узлов (для метрик)
// ===========================================================================

/// Visitor для подсчёта узлов по типам.
struct pjson_count_visitor
{
    uint64_t& node_cnt;
    uint64_t& ref_cnt;
    uint64_t& array_cnt;
    uint64_t& object_cnt;
    uint64_t& binary_bytes;

    void visit( node_id id, const node_view& v )
    {
        ++node_cnt;
        switch ( v.tag() )
        {
        case node_tag::array:
            ++array_cnt;
            break;
        case node_tag::object:
            ++object_cnt;
            break;
        case node_tag::ref:
            ++ref_cnt;
            break;
        case node_tag::binary:
        {
            const node* n = pmm_resolve<node>( id );
            if ( n != nullptr )
                binary_bytes += static_cast<uint64_t>( n->binary_val.size() );
            break;
        }
        default:
            break;
        }
    }
};

/// Рекурсивный обход поддерева для подсчёта узлов по типам.
inline void pjson_count_nodes_in_subtree( node_id id, uint64_t& node_cnt, uint64_t& ref_cnt, uint64_t& array_cnt,
                                          uint64_t& object_cnt, uint64_t& binary_bytes )
{
    pjson_count_visitor vis{ node_cnt, ref_cnt, array_cnt, object_cnt, binary_bytes };
    pjson_traverse_subtree( id, vis );
}

// ===========================================================================
// Вспомогательные функции: поиск по строковым узлам
// ===========================================================================

/// Visitor для поиска string-узлов, чьё значение содержит подстроку pattern.
struct pjson_search_strings_visitor
{
    const char*           pattern;
    std::vector<node_id>& results;

    void visit( node_id id, const node_view& v )
    {
        if ( v.tag() != node_tag::string )
            return;

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
    }
};

/// Рекурсивный обход поддерева для поиска string-узлов (pstring),
/// чьё значение содержит подстроку pattern.
inline void pjson_search_node_strings_in_subtree( node_id id, const char* pattern, std::vector<node_id>& results )
{
    pjson_search_strings_visitor vis{ pattern, results };
    pjson_traverse_subtree( id, vis );
}

// ===========================================================================
// Вспомогательные функции: разрешение ref
// ===========================================================================

// Примечание: _resolve_refs_in_subtree реализована в pjson_db_pmm.h через
// pjson_traverse_subtree с visitor-функтором _resolve_refs_visitor,
// т.к. зависит от pjson_db_pmm::get().
