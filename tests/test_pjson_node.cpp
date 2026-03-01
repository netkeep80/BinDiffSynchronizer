#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>

#include "pjson_node.h"

// Вспомогательная функция: сбросить ПАП перед каждым тестом.
namespace
{
void reset_pam()
{
    pstringview_manager::reset();
    PersistentAddressSpace::Get().Reset();
}
} // anonymous namespace

// =============================================================================
// Tests for Phase 3 — pjson_node: расширенная модель узлов JSON
// =============================================================================

// ---------------------------------------------------------------------------
// node_tag — layout checks (Task 3.1)
// ---------------------------------------------------------------------------
TEST_CASE( "node_tag: values are correct uint32_t discriminants", "[pjson_node][layout][node_tag]" )
{
    REQUIRE( static_cast<uint32_t>( node_tag::null ) == 0u );
    REQUIRE( static_cast<uint32_t>( node_tag::boolean ) == 1u );
    REQUIRE( static_cast<uint32_t>( node_tag::integer ) == 2u );
    REQUIRE( static_cast<uint32_t>( node_tag::uinteger ) == 3u );
    REQUIRE( static_cast<uint32_t>( node_tag::real ) == 4u );
    REQUIRE( static_cast<uint32_t>( node_tag::string ) == 5u );
    REQUIRE( static_cast<uint32_t>( node_tag::binary ) == 6u );
    REQUIRE( static_cast<uint32_t>( node_tag::array ) == 7u );
    REQUIRE( static_cast<uint32_t>( node_tag::object ) == 8u );
    REQUIRE( static_cast<uint32_t>( node_tag::ref ) == 9u );
}

// ---------------------------------------------------------------------------
// node — layout checks (Task 3.2)
// ---------------------------------------------------------------------------
TEST_CASE( "node: trivially copyable POD struct", "[pjson_node][layout][node]" )
{
    REQUIRE( std::is_trivially_copyable<node>::value );
}

TEST_CASE( "node: string_val layout is 2 * sizeof(void*)", "[pjson_node][layout][node]" )
{
    REQUIRE( sizeof( node::string_val ) == 2 * sizeof( void* ) );
}

TEST_CASE( "node: array_val layout is 3 * sizeof(void*)", "[pjson_node][layout][node]" )
{
    REQUIRE( sizeof( node::array_val ) == 3 * sizeof( void* ) );
}

TEST_CASE( "node: object_val layout is 3 * sizeof(void*)", "[pjson_node][layout][node]" )
{
    REQUIRE( sizeof( node::object_val ) == 3 * sizeof( void* ) );
}

TEST_CASE( "node: binary_val layout is 3 * sizeof(void*)", "[pjson_node][layout][node]" )
{
    REQUIRE( sizeof( node::binary_val ) == 3 * sizeof( void* ) );
}

TEST_CASE( "node: ref_val layout is 3 * sizeof(void*)", "[pjson_node][layout][node]" )
{
    REQUIRE( sizeof( node::ref_val ) == 3 * sizeof( void* ) );
}

// ---------------------------------------------------------------------------
// node_id — тип и размер (Task 3.3)
// ---------------------------------------------------------------------------
TEST_CASE( "node_id: is uintptr_t alias", "[pjson_node][layout][node_id]" )
{
    REQUIRE( ( std::is_same<node_id, uintptr_t>::value ) );
    REQUIRE( sizeof( node_id ) == sizeof( void* ) );
}

// ---------------------------------------------------------------------------
// object_entry — layout checks
// ---------------------------------------------------------------------------
TEST_CASE( "object_entry: trivially copyable and 3 * sizeof(void*)", "[pjson_node][layout][object_entry]" )
{
    REQUIRE( std::is_trivially_copyable<object_entry>::value );
    REQUIRE( sizeof( object_entry ) == 3 * sizeof( void* ) );
}

// ---------------------------------------------------------------------------
// node_view — создание и проверка типов (Task 3.4)
// ---------------------------------------------------------------------------
TEST_CASE( "node_view: default construction gives null view", "[pjson_node][node_view]" )
{
    node_view v;
    REQUIRE( v.id == 0 );
    REQUIRE( !v.valid() );
    REQUIRE( v.is_null() );
    REQUIRE( !v.is_boolean() );
    REQUIRE( !v.is_integer() );
    REQUIRE( !v.is_string() );
    REQUIRE( !v.is_array() );
    REQUIRE( !v.is_object() );
    REQUIRE( !v.is_ref() );
}

// ---------------------------------------------------------------------------
// node_set_bool + node_view: boolean nodes (Task 3.5)
// ---------------------------------------------------------------------------
TEST_CASE( "node: boolean node creation and node_view::as_bool", "[pjson_node][boolean]" )
{
    reset_pam();

    fptr<node> fn;
    fn.New();
    uintptr_t off = fn.addr();

    node_set_bool( off, true );
    node_view v{ off };
    REQUIRE( v.is_boolean() );
    REQUIRE( v.as_bool() == true );

    node_set_bool( off, false );
    REQUIRE( v.as_bool() == false );

    fn.Delete();
}

// ---------------------------------------------------------------------------
// node_set_int + node_view: integer nodes (Task 3.5)
// ---------------------------------------------------------------------------
TEST_CASE( "node: integer node creation and node_view::as_int", "[pjson_node][integer]" )
{
    reset_pam();

    fptr<node> fn;
    fn.New();
    uintptr_t off = fn.addr();

    node_set_int( off, -42 );
    node_view v{ off };
    REQUIRE( v.is_integer() );
    REQUIRE( v.as_int() == -42 );
    REQUIRE( v.as_double() == -42.0 );

    node_set_int( off, 1234567890LL );
    REQUIRE( v.as_int() == 1234567890LL );

    fn.Delete();
}

// ---------------------------------------------------------------------------
// node_set_uint + node_view: uinteger nodes (Task 3.5)
// ---------------------------------------------------------------------------
TEST_CASE( "node: uinteger node creation and node_view::as_uint", "[pjson_node][uinteger]" )
{
    reset_pam();

    fptr<node> fn;
    fn.New();
    uintptr_t off = fn.addr();

    node_set_uint( off, 0xDEADBEEFu );
    node_view v{ off };
    REQUIRE( v.is_uinteger() );
    REQUIRE( v.as_uint() == 0xDEADBEEFu );

    fn.Delete();
}

// ---------------------------------------------------------------------------
// node_set_real + node_view: real nodes (Task 3.5)
// ---------------------------------------------------------------------------
TEST_CASE( "node: real node creation and node_view::as_double", "[pjson_node][real]" )
{
    reset_pam();

    fptr<node> fn;
    fn.New();
    uintptr_t off = fn.addr();

    node_set_real( off, 3.14 );
    node_view v{ off };
    REQUIRE( v.is_real() );
    REQUIRE( v.as_double() == Catch::Approx( 3.14 ) );

    node_set_real( off, -1.5e10 );
    REQUIRE( v.as_double() == Catch::Approx( -1.5e10 ) );

    fn.Delete();
}

// ---------------------------------------------------------------------------
// node_set_string + node_view: string nodes — pstring (readwrite) (Task 3.5)
// ---------------------------------------------------------------------------
TEST_CASE( "node: string node (pstring readwrite) creation and node_view::as_string", "[pjson_node][string]" )
{
    reset_pam();

    fptr<node> fn;
    fn.New();
    uintptr_t off = fn.addr();

    node_set_string( off, "hello world" );
    node_view v{ off };
    REQUIRE( v.is_string() );
    REQUIRE( v.as_string() == "hello world" );
    REQUIRE( v.size() == 11u );

    fn.Delete();
}

TEST_CASE( "node: string node reassigning frees old allocation and stores new content", "[pjson_node][string]" )
{
    reset_pam();

    fptr<node> fn;
    fn.New();
    uintptr_t off = fn.addr();

    node_set_string( off, "original" );
    node_view v{ off };
    REQUIRE( v.as_string() == "original" );

    // Переназначаем строку — должно освободить старые данные и выделить новые.
    node_assign_string( off, "replaced" );
    REQUIRE( v.is_string() );
    REQUIRE( v.as_string() == "replaced" );

    // Переназначаем пустой строкой.
    node_assign_string( off, "" );
    REQUIRE( v.is_string() );
    REQUIRE( v.as_string() == "" );

    fn.Delete();
}

TEST_CASE( "node: string node empty string", "[pjson_node][string]" )
{
    reset_pam();

    fptr<node> fn;
    fn.New();
    uintptr_t off = fn.addr();

    node_set_string( off, "" );
    node_view v{ off };
    REQUIRE( v.is_string() );
    REQUIRE( v.as_string() == "" );
    REQUIRE( v.size() == 0u );

    fn.Delete();
}

// ---------------------------------------------------------------------------
// node_set_ref: ref nodes — pstringview path (readonly) (Task 3.5)
// ---------------------------------------------------------------------------
TEST_CASE( "node: ref node creation with path (pstringview readonly)", "[pjson_node][ref]" )
{
    reset_pam();

    fptr<node> fn;
    fn.New();
    uintptr_t off = fn.addr();

    node_set_ref( off, "/users/alice" );
    node_view v{ off };
    REQUIRE( v.is_ref() );
    REQUIRE( v.ref_path() == "/users/alice" );
    REQUIRE( v.ref_target() == 0 ); // не разрешён

    fn.Delete();
}

TEST_CASE( "node: ref node path is interned (pstringview - readonly)", "[pjson_node][ref]" )
{
    reset_pam();

    fptr<node> fn1;
    fn1.New();
    uintptr_t off1 = fn1.addr();
    node_set_ref( off1, "/a/b/c" );

    fptr<node> fn2;
    fn2.New();
    uintptr_t off2 = fn2.addr();
    node_set_ref( off2, "/a/b/c" );

    // Оба ref-узла должны иметь один и тот же path_chars_offset (интернирование).
    auto&       pam = PersistentAddressSpace::Get();
    const node* n1  = pam.Resolve<node>( off1 );
    const node* n2  = pam.Resolve<node>( off2 );
    REQUIRE( n1->ref_val.path_chars_offset == n2->ref_val.path_chars_offset );

    fn1.Delete();
    fn2.Delete();
}

TEST_CASE( "node: ref node target can be set after creation", "[pjson_node][ref]" )
{
    reset_pam();

    fptr<node> fn_target;
    fn_target.New();
    uintptr_t target_off = fn_target.addr();
    node_set_int( target_off, 99 );

    fptr<node> fn_ref;
    fn_ref.New();
    uintptr_t ref_off = fn_ref.addr();
    node_set_ref( ref_off, "/target" );

    REQUIRE( node_view{ ref_off }.ref_target() == 0 );

    // Устанавливаем target.
    node_set_ref_target( ref_off, target_off );
    REQUIRE( node_view{ ref_off }.ref_target() == target_off );

    fn_ref.Delete();
    fn_target.Delete();
}

// ---------------------------------------------------------------------------
// node_view::deref — разыменование ref (Task 3.5)
// ---------------------------------------------------------------------------
TEST_CASE( "node_view::deref: non-recursive dereference of ref node", "[pjson_node][node_view][deref]" )
{
    reset_pam();

    fptr<node> fn_target;
    fn_target.New();
    uintptr_t target_off = fn_target.addr();
    node_set_int( target_off, 42 );

    fptr<node> fn_ref;
    fn_ref.New();
    uintptr_t ref_off = fn_ref.addr();
    node_set_ref( ref_off, "/x" );
    node_set_ref_target( ref_off, target_off );

    node_view ref_view{ ref_off };
    node_view deref_view = ref_view.deref( false );
    REQUIRE( deref_view.id == target_off );
    REQUIRE( deref_view.is_integer() );
    REQUIRE( deref_view.as_int() == 42 );

    fn_ref.Delete();
    fn_target.Delete();
}

TEST_CASE( "node_view::deref: recursive dereference of ref chain", "[pjson_node][node_view][deref]" )
{
    reset_pam();

    // Создаём цепочку ref: ref1 -> ref2 -> integer(100)
    fptr<node> fn_int;
    fn_int.New();
    uintptr_t int_off = fn_int.addr();
    node_set_int( int_off, 100 );

    fptr<node> fn_ref2;
    fn_ref2.New();
    uintptr_t ref2_off = fn_ref2.addr();
    node_set_ref( ref2_off, "/end" );
    node_set_ref_target( ref2_off, int_off );

    fptr<node> fn_ref1;
    fn_ref1.New();
    uintptr_t ref1_off = fn_ref1.addr();
    node_set_ref( ref1_off, "/chain" );
    node_set_ref_target( ref1_off, ref2_off );

    // Нерекурсивное: ref1 -> ref2
    node_view nrec = node_view{ ref1_off }.deref( false );
    REQUIRE( nrec.is_ref() );
    REQUIRE( nrec.id == ref2_off );

    // Рекурсивное: ref1 -> ref2 -> integer(100)
    node_view rec = node_view{ ref1_off }.deref( true );
    REQUIRE( rec.is_integer() );
    REQUIRE( rec.as_int() == 100 );

    fn_ref1.Delete();
    fn_ref2.Delete();
    fn_int.Delete();
}

TEST_CASE( "node_view::deref: non-ref node returns itself", "[pjson_node][node_view][deref]" )
{
    reset_pam();

    fptr<node> fn;
    fn.New();
    uintptr_t off = fn.addr();
    node_set_real( off, 2.71828 );

    node_view v{ off };
    node_view deref_v = v.deref();
    REQUIRE( deref_v.id == v.id );
    REQUIRE( deref_v.is_real() );

    fn.Delete();
}

TEST_CASE( "node_view::deref: unresolved ref returns null view", "[pjson_node][node_view][deref]" )
{
    reset_pam();

    fptr<node> fn;
    fn.New();
    uintptr_t off = fn.addr();
    node_set_ref( off, "/unresolved" );
    // target остаётся 0

    node_view v{ off };
    node_view deref_v = v.deref();
    REQUIRE( !deref_v.valid() );

    fn.Delete();
}

// ---------------------------------------------------------------------------
// node_set_array + node_view: array nodes (Task 3.5)
// ---------------------------------------------------------------------------
TEST_CASE( "node: array node push_back and at", "[pjson_node][array]" )
{
    reset_pam();

    fptr<node> fn_arr;
    fn_arr.New();
    uintptr_t arr_off = fn_arr.addr();
    node_set_array( arr_off );

    // Добавляем три элемента.
    node_id slot0 = node_array_push_back( arr_off );
    node_set_int( slot0, 10 );

    node_id slot1 = node_array_push_back( arr_off );
    node_set_int( slot1, 20 );

    node_id slot2 = node_array_push_back( arr_off );
    node_set_string( slot2, "thirty" );

    node_view arr_view{ arr_off };
    REQUIRE( arr_view.is_array() );
    REQUIRE( arr_view.size() == 3u );

    REQUIRE( arr_view.at( static_cast<uintptr_t>( 0 ) ).as_int() == 10 );
    REQUIRE( arr_view.at( static_cast<uintptr_t>( 1 ) ).as_int() == 20 );
    REQUIRE( arr_view.at( static_cast<uintptr_t>( 2 ) ).as_string() == "thirty" );

    // Выход за границы возвращает null view.
    REQUIRE( !arr_view.at( static_cast<uintptr_t>( 3 ) ).valid() );

    fn_arr.Delete();
}

TEST_CASE( "node: array node operator[] works like at(idx)", "[pjson_node][array]" )
{
    reset_pam();

    fptr<node> fn_arr;
    fn_arr.New();
    uintptr_t arr_off = fn_arr.addr();
    node_set_array( arr_off );

    node_id s0 = node_array_push_back( arr_off );
    node_set_bool( s0, true );

    node_view arr_view{ arr_off };
    REQUIRE( arr_view[static_cast<uintptr_t>( 0 )].as_bool() == true );

    fn_arr.Delete();
}

TEST_CASE( "node: empty array has size 0", "[pjson_node][array]" )
{
    reset_pam();

    fptr<node> fn_arr;
    fn_arr.New();
    uintptr_t arr_off = fn_arr.addr();
    node_set_array( arr_off );

    node_view v{ arr_off };
    REQUIRE( v.is_array() );
    REQUIRE( v.size() == 0u );
    REQUIRE( v.empty() );

    fn_arr.Delete();
}

// ---------------------------------------------------------------------------
// node_set_object + node_view: object nodes — pmap<pstringview, node_id> (Task 3.5)
// ---------------------------------------------------------------------------
TEST_CASE( "node: object node insert and at with pstringview keys (readonly)", "[pjson_node][object]" )
{
    reset_pam();

    fptr<node> fn_obj;
    fn_obj.New();
    uintptr_t obj_off = fn_obj.addr();
    node_set_object( obj_off );

    // Вставляем три поля с ключами-pstringview (readonly, интернированными).
    node_id name_slot   = node_object_insert( obj_off, "name" );
    node_id age_slot    = node_object_insert( obj_off, "age" );
    node_id active_slot = node_object_insert( obj_off, "active" );

    node_set_string( name_slot, "Alice" );
    node_set_int( age_slot, 30 );
    node_set_bool( active_slot, true );

    node_view obj_view{ obj_off };
    REQUIRE( obj_view.is_object() );
    REQUIRE( obj_view.size() == 3u );

    // Доступ по ключу.
    REQUIRE( obj_view.at( "name" ).as_string() == "Alice" );
    REQUIRE( obj_view.at( "age" ).as_int() == 30 );
    REQUIRE( obj_view.at( "active" ).as_bool() == true );

    // Отсутствующий ключ возвращает null view.
    REQUIRE( !obj_view.at( "nonexistent" ).valid() );

    fn_obj.Delete();
}

TEST_CASE( "node: object node operator[] works like at(key)", "[pjson_node][object]" )
{
    reset_pam();

    fptr<node> fn_obj;
    fn_obj.New();
    uintptr_t obj_off = fn_obj.addr();
    node_set_object( obj_off );

    node_id slot = node_object_insert( obj_off, "score" );
    node_set_uint( slot, 100u );

    node_view obj_view{ obj_off };
    REQUIRE( obj_view["score"].as_uint() == 100u );

    fn_obj.Delete();
}

TEST_CASE( "node: object keys are readonly pstringview (same key returns existing slot)", "[pjson_node][object]" )
{
    reset_pam();

    fptr<node> fn_obj;
    fn_obj.New();
    uintptr_t obj_off = fn_obj.addr();
    node_set_object( obj_off );

    // Первая вставка.
    node_id slot1 = node_object_insert( obj_off, "key" );
    node_set_int( slot1, 1 );

    // Повторная вставка того же ключа возвращает тот же slot.
    node_id slot2 = node_object_insert( obj_off, "key" );
    REQUIRE( slot1 == slot2 );

    // Значение не изменилось.
    node_view obj_view{ obj_off };
    REQUIRE( obj_view.at( "key" ).as_int() == 1 );
    REQUIRE( obj_view.size() == 1u );

    fn_obj.Delete();
}

TEST_CASE( "node: object keys are interned (pstringview) - same chars_offset", "[pjson_node][object]" )
{
    reset_pam();

    fptr<node> fn_obj1;
    fn_obj1.New();
    uintptr_t obj1_off = fn_obj1.addr();
    node_set_object( obj1_off );

    fptr<node> fn_obj2;
    fn_obj2.New();
    uintptr_t obj2_off = fn_obj2.addr();
    node_set_object( obj2_off );

    // Вставляем один и тот же ключ в два разных объекта.
    node_object_insert( obj1_off, "shared_key" );
    node_object_insert( obj2_off, "shared_key" );

    // Оба объекта должны использовать один и тот же chars_offset для ключа.
    auto& pam = PersistentAddressSpace::Get();

    const node* n1 = pam.Resolve<node>( obj1_off );
    const node* n2 = pam.Resolve<node>( obj2_off );

    // Доступ к first entry каждого object_val.
    REQUIRE( n1->object_val.size == 1u );
    REQUIRE( n2->object_val.size == 1u );

    const object_entry* e1 = pam.Resolve<object_entry>( n1->object_val.data_off );
    const object_entry* e2 = pam.Resolve<object_entry>( n2->object_val.data_off );

    REQUIRE( e1 != nullptr );
    REQUIRE( e2 != nullptr );
    REQUIRE( e1->key_chars_offset == e2->key_chars_offset ); // интернирование

    fn_obj1.Delete();
    fn_obj2.Delete();
}

TEST_CASE( "node: object iteration via key_at and value_at", "[pjson_node][object]" )
{
    reset_pam();

    fptr<node> fn_obj;
    fn_obj.New();
    uintptr_t obj_off = fn_obj.addr();
    node_set_object( obj_off );

    // Вставляем в алфавитном порядке для детерминированного порядка.
    node_id a_slot = node_object_insert( obj_off, "a" );
    node_id b_slot = node_object_insert( obj_off, "b" );
    node_id c_slot = node_object_insert( obj_off, "c" );
    node_set_int( a_slot, 1 );
    node_set_int( b_slot, 2 );
    node_set_int( c_slot, 3 );

    node_view obj_view{ obj_off };
    REQUIRE( obj_view.size() == 3u );

    // Объект отсортирован по ключу (pmap<pstringview, node_id>).
    REQUIRE( obj_view.key_at( 0 ) == "a" );
    REQUIRE( obj_view.key_at( 1 ) == "b" );
    REQUIRE( obj_view.key_at( 2 ) == "c" );
    REQUIRE( obj_view.value_at( 0 ).as_int() == 1 );
    REQUIRE( obj_view.value_at( 1 ).as_int() == 2 );
    REQUIRE( obj_view.value_at( 2 ).as_int() == 3 );

    fn_obj.Delete();
}

TEST_CASE( "node: empty object has size 0", "[pjson_node][object]" )
{
    reset_pam();

    fptr<node> fn_obj;
    fn_obj.New();
    uintptr_t obj_off = fn_obj.addr();
    node_set_object( obj_off );

    node_view v{ obj_off };
    REQUIRE( v.is_object() );
    REQUIRE( v.size() == 0u );
    REQUIRE( v.empty() );

    fn_obj.Delete();
}

// ---------------------------------------------------------------------------
// Two string types: string_val (pstring, readwrite) vs ref_val.path (pstringview, readonly)
// (Task 3.5: проверка двух типов строк)
// ---------------------------------------------------------------------------
TEST_CASE( "node: string_val uses pstring (readwrite) - assign modifies in place", "[pjson_node][string][pstring]" )
{
    reset_pam();

    fptr<node> fn;
    fn.New();
    uintptr_t off = fn.addr();

    node_set_string( off, "initial" );
    node_view v{ off };
    REQUIRE( v.as_string() == "initial" );

    // Переназначаем в ту же ячейку — старые данные освобождены, новые выделены.
    node_assign_string( off, "modified" );
    REQUIRE( v.as_string() == "modified" );

    fn.Delete();
}

TEST_CASE( "node: ref_val.path uses pstringview (readonly, interned) - same offset for same path",
           "[pjson_node][ref][pstringview]" )
{
    reset_pam();

    fptr<node> fn1;
    fn1.New();
    uintptr_t off1 = fn1.addr();
    node_set_ref( off1, "/same/path" );

    fptr<node> fn2;
    fn2.New();
    uintptr_t off2 = fn2.addr();
    node_set_ref( off2, "/same/path" );

    auto& pam = PersistentAddressSpace::Get();

    // Оба ref-узла должны иметь одинаковый path_chars_offset (интернирование).
    const node* n1 = pam.Resolve<node>( off1 );
    const node* n2 = pam.Resolve<node>( off2 );
    REQUIRE( n1->ref_val.path_chars_offset != 0 );
    REQUIRE( n1->ref_val.path_chars_offset == n2->ref_val.path_chars_offset );

    fn1.Delete();
    fn2.Delete();
}

TEST_CASE( "node: object keys are pstringview (readonly) - not pstring (readwrite)",
           "[pjson_node][object][pstringview]" )
{
    reset_pam();

    fptr<node> fn_obj;
    fn_obj.New();
    uintptr_t obj_off = fn_obj.addr();
    node_set_object( obj_off );

    // Ключи объекта — pstringview (readonly, интернированные) — interned.
    auto r1 = pam_intern_string( "my_key" );
    node_object_insert( obj_off, "my_key" );

    // После вставки ключ должен иметь тот же chars_offset что pam_intern_string.
    auto&       pam = PersistentAddressSpace::Get();
    const node* n   = pam.Resolve<node>( obj_off );
    REQUIRE( n->object_val.size == 1u );
    const object_entry* e = pam.Resolve<object_entry>( n->object_val.data_off );
    REQUIRE( e->key_chars_offset == r1.chars_offset ); // тот же интернированный offset

    fn_obj.Delete();
}

// ---------------------------------------------------------------------------
// node_set_binary: binary nodes (Task 3.5)
// ---------------------------------------------------------------------------
TEST_CASE( "node: binary node push_back bytes", "[pjson_node][binary]" )
{
    reset_pam();

    fptr<node> fn;
    fn.New();
    uintptr_t off = fn.addr();
    node_set_binary( off );

    node_binary_push_back( off, 0xABu );
    node_binary_push_back( off, 0xCDu );
    node_binary_push_back( off, 0xEFu );

    node_view v{ off };
    REQUIRE( v.is_binary() );
    REQUIRE( v.size() == 3u );

    fn.Delete();
}

// ---------------------------------------------------------------------------
// node_view: size and empty on различных типах
// ---------------------------------------------------------------------------
TEST_CASE( "node_view: size and empty on null, string, array, object", "[pjson_node][node_view]" )
{
    reset_pam();

    // null
    {
        node_view null_v;
        REQUIRE( null_v.size() == 0u );
        REQUIRE( null_v.empty() );
    }

    // string
    {
        fptr<node> fn;
        fn.New();
        uintptr_t off = fn.addr();
        node_set_string( off, "abc" );
        node_view v{ off };
        REQUIRE( v.size() == 3u );
        REQUIRE( !v.empty() );
        fn.Delete();
    }

    // пустой массив
    {
        reset_pam();
        fptr<node> fn;
        fn.New();
        uintptr_t off = fn.addr();
        node_set_array( off );
        node_view v{ off };
        REQUIRE( v.size() == 0u );
        REQUIRE( v.empty() );
        fn.Delete();
    }
}

// ---------------------------------------------------------------------------
// node_view: type queries for all types
// ---------------------------------------------------------------------------
TEST_CASE( "node_view: is_number returns true for integer, uinteger, real", "[pjson_node][node_view]" )
{
    reset_pam();

    // integer
    {
        fptr<node> fn;
        fn.New();
        uintptr_t off = fn.addr();
        node_set_int( off, 1 );
        REQUIRE( node_view{ off }.is_number() );
        fn.Delete();
    }

    // uinteger
    {
        reset_pam();
        fptr<node> fn;
        fn.New();
        uintptr_t off = fn.addr();
        node_set_uint( off, 2u );
        REQUIRE( node_view{ off }.is_number() );
        fn.Delete();
    }

    // real
    {
        reset_pam();
        fptr<node> fn;
        fn.New();
        uintptr_t off = fn.addr();
        node_set_real( off, 3.0 );
        REQUIRE( node_view{ off }.is_number() );
        fn.Delete();
    }
}

// ---------------------------------------------------------------------------
// node_view: as_* type coercions
// ---------------------------------------------------------------------------
TEST_CASE( "node_view: as_int coerces from uinteger and real", "[pjson_node][node_view]" )
{
    reset_pam();

    // Из uinteger.
    {
        fptr<node> fn;
        fn.New();
        uintptr_t off = fn.addr();
        node_set_uint( off, 55u );
        REQUIRE( node_view{ off }.as_int() == 55 );
        fn.Delete();
    }

    // Из real.
    {
        reset_pam();
        fptr<node> fn;
        fn.New();
        uintptr_t off = fn.addr();
        node_set_real( off, 7.9 );
        REQUIRE( node_view{ off }.as_int() == 7 ); // усечение
        fn.Delete();
    }
}

TEST_CASE( "node_view: as_uint coerces from integer", "[pjson_node][node_view]" )
{
    reset_pam();

    fptr<node> fn;
    fn.New();
    uintptr_t off = fn.addr();
    node_set_int( off, 42 );
    REQUIRE( node_view{ off }.as_uint() == 42u );

    fn.Delete();
}

// ---------------------------------------------------------------------------
// node_view: operator== and operator!=
// ---------------------------------------------------------------------------
TEST_CASE( "node_view: equality comparison by id", "[pjson_node][node_view]" )
{
    reset_pam();

    fptr<node> fn1;
    fn1.New();
    uintptr_t off1 = fn1.addr();
    node_set_int( off1, 1 );

    fptr<node> fn2;
    fn2.New();
    uintptr_t off2 = fn2.addr();
    node_set_int( off2, 1 );

    node_view v1{ off1 };
    node_view v2{ off2 };
    node_view v1b{ off1 };

    REQUIRE( v1 == v1b );
    REQUIRE( v1 != v2 );

    fn1.Delete();
    fn2.Delete();
}

// ---------------------------------------------------------------------------
// Комплексный тест: вложенный объект с массивом и ref (Task 3.5)
// ---------------------------------------------------------------------------
TEST_CASE( "node: nested object with array and ref - complex scenario", "[pjson_node][complex]" )
{
    reset_pam();

    // Строим:
    // {
    //   "user": {
    //     "name": "Bob",
    //     "scores": [10, 20, 30],
    //     "profile": { "$ref": "/profiles/bob" }
    //   }
    // }

    fptr<node> fn_root;
    fn_root.New();
    uintptr_t root_off = fn_root.addr();
    node_set_object( root_off );

    // user объект
    node_id user_slot = node_object_insert( root_off, "user" );
    node_set_object( user_slot );

    // name
    node_id name_slot = node_object_insert( user_slot, "name" );
    node_set_string( name_slot, "Bob" );

    // scores массив
    node_id scores_slot = node_object_insert( user_slot, "scores" );
    node_set_array( scores_slot );
    node_id s0 = node_array_push_back( scores_slot );
    node_id s1 = node_array_push_back( scores_slot );
    node_id s2 = node_array_push_back( scores_slot );
    node_set_int( s0, 10 );
    node_set_int( s1, 20 );
    node_set_int( s2, 30 );

    // profile ref
    node_id profile_slot = node_object_insert( user_slot, "profile" );
    node_set_ref( profile_slot, "/profiles/bob" );

    // Проверяем.
    node_view root_view{ root_off };
    REQUIRE( root_view.is_object() );

    node_view user_view = root_view.at( "user" );
    REQUIRE( user_view.is_object() );

    node_view name_view = user_view.at( "name" );
    REQUIRE( name_view.is_string() );
    REQUIRE( name_view.as_string() == "Bob" );

    node_view scores_view = user_view.at( "scores" );
    REQUIRE( scores_view.is_array() );
    REQUIRE( scores_view.size() == 3u );
    REQUIRE( scores_view.at( static_cast<uintptr_t>( 0 ) ).as_int() == 10 );
    REQUIRE( scores_view.at( static_cast<uintptr_t>( 1 ) ).as_int() == 20 );
    REQUIRE( scores_view.at( static_cast<uintptr_t>( 2 ) ).as_int() == 30 );

    node_view profile_view = user_view.at( "profile" );
    REQUIRE( profile_view.is_ref() );
    REQUIRE( profile_view.ref_path() == "/profiles/bob" );
    REQUIRE( profile_view.ref_target() == 0 ); // не разрешён

    fn_root.Delete();
}
