#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <type_traits>

#include "persist.h"

// =============================================================================
// Тесты для persist<T>, AddressManager<T> и fptr<T> (фаза 2)
//
// В фазе 2 persist<T> не содержит файловой логики (Тр.7, Тр.8).
// Создание объектов — через AddressManager / fptr (Тр.2, Тр.12).
// Файловая персистность обеспечивается PersistentAddressSpace (pam.h).
// =============================================================================

// ---------------------------------------------------------------------------
// persist<T> — статические проверки размера (Тр.8)
// ---------------------------------------------------------------------------
TEST_CASE("persist<T>: sizeof(persist<T>) == sizeof(T) (Тр.8)",
          "[persist][layout]")
{
    // Требование Тр.8: размер persist<T> должен равняться sizeof(T).
    REQUIRE(sizeof(persist<int>)    == sizeof(int));
    REQUIRE(sizeof(persist<double>) == sizeof(double));
    REQUIRE(sizeof(persist<char>)   == sizeof(char));
    REQUIRE(sizeof(persist<float>)  == sizeof(float));
}

// ---------------------------------------------------------------------------
// fptr<T> — статические проверки размера (Тр.5)
// ---------------------------------------------------------------------------
TEST_CASE("fptr<T>: sizeof(fptr<T>) == sizeof(void*) (Тр.5)",
          "[fptr][layout]")
{
    // Требование Тр.5: размер fptr<T> должен равняться sizeof(void*).
    REQUIRE(sizeof(fptr<int>)    == sizeof(void*));
    REQUIRE(sizeof(fptr<double>) == sizeof(void*));
    REQUIRE(sizeof(fptr<char>)   == sizeof(void*));
}

// ---------------------------------------------------------------------------
// fptr<T> — тривиально копируем (может быть embedded в persist<> структуры)
// ---------------------------------------------------------------------------
TEST_CASE("fptr<T>: тривиально копируем",
          "[fptr][layout]")
{
    REQUIRE(std::is_trivially_copyable<fptr<int>>::value);
    REQUIRE(std::is_trivially_copyable<fptr<double>>::value);
    REQUIRE(std::is_trivially_copyable<fptr<char>>::value);
}

// ---------------------------------------------------------------------------
// fptr<T> — инициализация по умолчанию и set_addr/addr
// ---------------------------------------------------------------------------
TEST_CASE("fptr<int>: set_addr и addr — обход и получение смещения",
          "[fptr]")
{
    fptr<int> p;
    REQUIRE(p.addr() == 0u);

    p.set_addr(7u);
    REQUIRE(p.addr() == 7u);

    p.set_addr(0u);
    REQUIRE(p.addr() == 0u);
}

// ---------------------------------------------------------------------------
// fptr<double> — New / разыменование / Delete
// ---------------------------------------------------------------------------
TEST_CASE("fptr<double>: New / разыменование / Delete",
          "[fptr]")
{
    fptr<double> p;
    REQUIRE(p.addr() == 0u);

    p.New();
    REQUIRE(p.addr() != 0u);

    *p = 2.718;
    REQUIRE(*p == 2.718);

    p.Delete();
    REQUIRE(p.addr() == 0u);
}

// ---------------------------------------------------------------------------
// fptr<int> — NewArray / operator[] / count / DeleteArray
// ---------------------------------------------------------------------------
TEST_CASE("fptr<int>: NewArray / operator[] / count / DeleteArray",
          "[fptr]")
{
    fptr<int> arr;
    arr.NewArray(6);
    REQUIRE(arr.addr() != 0u);
    REQUIRE(arr.count() == 6u);

    for( unsigned i = 0; i < 6; i++ )
        arr[i] = static_cast<int>(i + 1);

    for( unsigned i = 0; i < 6; i++ )
        REQUIRE(arr[i] == static_cast<int>(i + 1));

    arr.DeleteArray();
    REQUIRE(arr.addr() == 0u);
    REQUIRE(arr.count() == 0u);
}

// ---------------------------------------------------------------------------
// AddressManager — Create / доступ / Delete
// ---------------------------------------------------------------------------
TEST_CASE("AddressManager<double>: Create / доступ / Delete",
          "[address_manager]")
{
    // Создаём объект с именем для поиска.
    const char* name = "test_am_create_delete_v2";
    uintptr_t offset = AddressManager<double>::Create(name);
    REQUIRE(offset != 0u);

    // Записываем значение через fptr.
    fptr<double> dp;
    dp.set_addr(offset);
    *dp = 3.14;
    double val = *dp;
    REQUIRE(val == 3.14);

    // Поиск по имени.
    uintptr_t found = AddressManager<double>::Find(name);
    REQUIRE(found == offset);

    // Удаляем и проверяем, что слот освобождён.
    AddressManager<double>::Delete(offset);
    uintptr_t found2 = AddressManager<double>::Find(name);
    REQUIRE(found2 == 0u);
}

// ---------------------------------------------------------------------------
// AddressManager — CreateArray / GetCount / GetArrayElement / DeleteArray
// ---------------------------------------------------------------------------
TEST_CASE("AddressManager<int>: CreateArray / GetArrayElement / DeleteArray",
          "[address_manager]")
{
    const char* name = "test_am_array_v2";
    uintptr_t offset = AddressManager<int>::CreateArray(5, name);
    REQUIRE(offset != 0u);
    REQUIRE(AddressManager<int>::GetCount(offset) == 5u);

    // Записываем элементы.
    for( unsigned i = 0; i < 5; i++ )
        AddressManager<int>::GetArrayElement(offset, i) = static_cast<int>(i * 10);

    // Читаем обратно.
    for( unsigned i = 0; i < 5; i++ )
        REQUIRE(AddressManager<int>::GetArrayElement(offset, i) == static_cast<int>(i * 10));

    AddressManager<int>::DeleteArray(offset);
    REQUIRE(AddressManager<int>::Find(name) == 0u);
}

// ---------------------------------------------------------------------------
// AddressManager — FindByPtr
// ---------------------------------------------------------------------------
TEST_CASE("AddressManager<int>: FindByPtr возвращает смещение по указателю",
          "[address_manager]")
{
    uintptr_t offset = AddressManager<int>::CreateArray(3, nullptr);
    REQUIRE(offset != 0u);

    int* p = &AddressManager<int>::GetArrayElement(offset, 0);
    REQUIRE(p != nullptr);

    uintptr_t found = AddressManager<int>::FindByPtr(p);
    REQUIRE(found == offset);

    AddressManager<int>::DeleteArray(offset);
}

// ---------------------------------------------------------------------------
// Единое ПАП — объекты разных типов в одном адресном пространстве (Тр.4)
// ---------------------------------------------------------------------------
TEST_CASE("AddressManager: объекты int и double в едином ПАП через AddressManager (Тр.4)",
          "[address_manager][unified_space]")
{
    // Создаём объекты разных типов.
    uintptr_t off_i = AddressManager<int>::Create("test_unified_int");
    uintptr_t off_d = AddressManager<double>::Create("test_unified_double");

    REQUIRE(off_i != 0u);
    REQUIRE(off_d != 0u);
    // Смещения должны быть разными.
    REQUIRE(off_i != off_d);

    // Записываем значения.
    fptr<int> pi;    pi.set_addr(off_i);    *pi = 42;
    fptr<double> pd; pd.set_addr(off_d);    *pd = 3.14;

    REQUIRE(*pi == 42);
    REQUIRE(*pd == 3.14);

    // Убираем за собой.
    AddressManager<int>::Delete(off_i);
    AddressManager<double>::Delete(off_d);
}

// ---------------------------------------------------------------------------
// fptr::find — инициализация по имени объекта (Тр.15)
// ---------------------------------------------------------------------------
TEST_CASE("fptr<int>: find() инициализирует по имени объекта (Тр.15)",
          "[fptr][find]")
{
    // Создаём объект с именем.
    uintptr_t offset = AddressManager<int>::Create("find_test_named_int");
    REQUIRE(offset != 0u);

    fptr<int> p1;
    p1.set_addr(offset);
    *p1 = 999;

    // Инициализируем другой fptr по имени.
    fptr<int> p2;
    p2.find("find_test_named_int");
    REQUIRE(p2.addr() != 0u);
    REQUIRE(*p2 == 999);

    AddressManager<int>::Delete(offset);
}
