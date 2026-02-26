#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <cstdio>
#include <filesystem>
#include <type_traits>

#include "pam.h"

// =============================================================================
// Тесты для PersistentAddressSpace (ПАМ — персистный адресный менеджер)
//
// Проверяет:
//   Тр.4  — единое ПАП для объектов разных типов
//   Тр.10 — при загрузке образа конструкторы не вызываются
//   Тр.14 — ПАМ хранит имена объектов
//   Тр.15 — поиск по имени
//   Тр.16 — ПАМ хранит карту объектов
// =============================================================================

namespace {
    /// Вспомогательная функция: удалить временный файл.
    void rm_file(const char* path)
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
} // anonymous namespace

// ---------------------------------------------------------------------------
// Структуры заголовка и дескриптора — тривиально копируемы
// ---------------------------------------------------------------------------
TEST_CASE("slot_descriptor: is trivially copyable", "[pam][layout]")
{
    REQUIRE(std::is_trivially_copyable<slot_descriptor>::value);
}

TEST_CASE("pap_header: is trivially copyable", "[pam][layout]")
{
    REQUIRE(std::is_trivially_copyable<pap_header>::value);
}

// ---------------------------------------------------------------------------
// Init с несуществующим файлом создаёт пустой образ
// ---------------------------------------------------------------------------
TEST_CASE("PersistentAddressSpace: Init() with nonexistent file creates empty image",
          "[pam][init]")
{
    const char* fname = "./test_pam_init_empty.pap";
    rm_file(fname);

    // Init создаёт пустой образ без ошибок.
    REQUIRE_NOTHROW(PersistentAddressSpace::Init(fname));

    auto& pam = PersistentAddressSpace::Get();

    // Поиск несуществующего объекта возвращает 0.
    REQUIRE(pam.Find("nonexistent") == 0u);

    rm_file(fname);
}

// ---------------------------------------------------------------------------
// Create возвращает ненулевое смещение
// ---------------------------------------------------------------------------
TEST_CASE("PersistentAddressSpace: Create<int>() returns nonzero offset",
          "[pam][create]")
{
    auto& pam = PersistentAddressSpace::Get();

    uintptr_t offset = pam.Create<int>();
    REQUIRE(offset != 0u);

    pam.Delete(offset);
}

// ---------------------------------------------------------------------------
// Resolve возвращает указатель; запись и чтение работают
// ---------------------------------------------------------------------------
TEST_CASE("PersistentAddressSpace: Resolve<int>() write and read",
          "[pam][resolve]")
{
    auto& pam = PersistentAddressSpace::Get();

    uintptr_t offset = pam.Create<int>();
    REQUIRE(offset != 0u);

    int* p = pam.Resolve<int>(offset);
    REQUIRE(p != nullptr);

    *p = 42;
    REQUIRE(*pam.Resolve<int>(offset) == 42);

    pam.Delete(offset);
}

// ---------------------------------------------------------------------------
// Поиск по имени (Тр.14, Тр.15, Тр.16)
// ---------------------------------------------------------------------------
TEST_CASE("PersistentAddressSpace: Create with name -- Find returns same offset",
          "[pam][find][named]")
{
    auto& pam = PersistentAddressSpace::Get();

    const char* name = "pam_test_named_counter";
    uintptr_t offset = pam.Create<int>(name);
    REQUIRE(offset != 0u);

    // Find должен вернуть то же смещение.
    uintptr_t found = pam.Find(name);
    REQUIRE(found == offset);

    pam.Delete(offset);

    // После удаления поиск возвращает 0.
    uintptr_t after_del = pam.Find(name);
    REQUIRE(after_del == 0u);
}

// ---------------------------------------------------------------------------
// FindTyped — поиск с проверкой типа
// ---------------------------------------------------------------------------
TEST_CASE("PersistentAddressSpace: FindTyped<T> finds object of correct type",
          "[pam][find_typed]")
{
    auto& pam = PersistentAddressSpace::Get();

    const char* name = "pam_test_typed_double";
    uintptr_t offset = pam.Create<double>(name);
    REQUIRE(offset != 0u);

    // FindTyped с верным типом — находит.
    REQUIRE(pam.FindTyped<double>(name) == offset);
    // FindTyped с неверным типом — не находит.
    REQUIRE(pam.FindTyped<int>(name) == 0u);

    pam.Delete(offset);
}

// ---------------------------------------------------------------------------
// Delete освобождает слот
// ---------------------------------------------------------------------------
TEST_CASE("PersistentAddressSpace: Delete frees slot",
          "[pam][delete]")
{
    auto& pam = PersistentAddressSpace::Get();

    const char* name = "pam_test_delete_me";
    uintptr_t offset = pam.Create<int>(name);
    REQUIRE(offset != 0u);
    REQUIRE(pam.Find(name) != 0u);

    pam.Delete(offset);
    REQUIRE(pam.Find(name) == 0u);
}

// ---------------------------------------------------------------------------
// CreateArray — массив объектов
// ---------------------------------------------------------------------------
TEST_CASE("PersistentAddressSpace: CreateArray<char>(100) creates 100-byte array",
          "[pam][array]")
{
    auto& pam = PersistentAddressSpace::Get();

    uintptr_t offset = pam.CreateArray<char>(100);
    REQUIRE(offset != 0u);
    REQUIRE(pam.GetCount(offset) == 100u);

    char* arr = pam.Resolve<char>(offset);
    REQUIRE(arr != nullptr);

    // Запись и чтение всех элементов.
    for( int i = 0; i < 100; i++ )
        arr[i] = static_cast<char>(i % 127);
    for( int i = 0; i < 100; i++ )
        REQUIRE(arr[i] == static_cast<char>(i % 127));

    pam.Delete(offset);
}

// ---------------------------------------------------------------------------
// ResolveElement — доступ к элементу массива по индексу
// ---------------------------------------------------------------------------
TEST_CASE("PersistentAddressSpace: ResolveElement<int> accesses array elements",
          "[pam][array][resolve_element]")
{
    auto& pam = PersistentAddressSpace::Get();

    uintptr_t offset = pam.CreateArray<int>(5);
    REQUIRE(offset != 0u);

    for( unsigned i = 0; i < 5; i++ )
        pam.ResolveElement<int>(offset, i) = static_cast<int>(i * 10);

    for( unsigned i = 0; i < 5; i++ )
        REQUIRE(pam.ResolveElement<int>(offset, i) == static_cast<int>(i * 10));

    pam.Delete(offset);
}

// ---------------------------------------------------------------------------
// Единое ПАП для разных типов (Тр.4)
// ---------------------------------------------------------------------------
TEST_CASE("PersistentAddressSpace: int and double objects in unified PAP (Tr.4)",
          "[pam][unified_space]")
{
    auto& pam = PersistentAddressSpace::Get();

    uintptr_t off_i = pam.Create<int>();
    uintptr_t off_d = pam.Create<double>();
    uintptr_t off_c = pam.CreateArray<char>(10);

    REQUIRE(off_i != 0u);
    REQUIRE(off_d != 0u);
    REQUIRE(off_c != 0u);

    // Все смещения уникальны.
    REQUIRE(off_i != off_d);
    REQUIRE(off_i != off_c);
    REQUIRE(off_d != off_c);

    // Запись в каждый объект независима.
    *pam.Resolve<int>(off_i)    = 100;
    *pam.Resolve<double>(off_d) = 3.14;
    pam.Resolve<char>(off_c)[0] = 'A';

    REQUIRE(*pam.Resolve<int>(off_i)    == 100);
    REQUIRE(*pam.Resolve<double>(off_d) == 3.14);
    REQUIRE(pam.Resolve<char>(off_c)[0] == 'A');

    pam.Delete(off_i);
    pam.Delete(off_d);
    pam.Delete(off_c);
}

// ---------------------------------------------------------------------------
// FindByPtr — обратный поиск по указателю
// ---------------------------------------------------------------------------
TEST_CASE("PersistentAddressSpace: FindByPtr returns offset by pointer",
          "[pam][find_by_ptr]")
{
    auto& pam = PersistentAddressSpace::Get();

    uintptr_t offset = pam.Create<int>();
    REQUIRE(offset != 0u);

    int* p = pam.Resolve<int>(offset);
    REQUIRE(p != nullptr);

    uintptr_t found = pam.FindByPtr(static_cast<const void*>(p));
    REQUIRE(found == offset);

    pam.Delete(offset);
}

// ---------------------------------------------------------------------------
// Save и повторная загрузка через Init (Тр.10 — без конструкторов)
// ---------------------------------------------------------------------------
TEST_CASE("PersistentAddressSpace: Save and Init -- objects are restored",
          "[pam][save_reload]")
{
    const char* fname = "./test_pam_save_reload.pap";
    rm_file(fname);

    uintptr_t saved_offset = 0;
    {
        PersistentAddressSpace::Init(fname);
        auto& pam = PersistentAddressSpace::Get();

        const char* name = "pam_persist_counter";
        saved_offset = pam.Create<int>(name);
        REQUIRE(saved_offset != 0u);

        *pam.Resolve<int>(saved_offset) = 12345;

        pam.Save();
    }

    // Перезагружаем ПАП из файла. Конструкторы НЕ вызываются (Тр.10).
    PersistentAddressSpace::Init(fname);
    {
        auto& pam = PersistentAddressSpace::Get();

        // Ищем объект по имени.
        uintptr_t offset = pam.Find("pam_persist_counter");
        REQUIRE(offset == saved_offset);

        // Значение должно быть восстановлено.
        int* p = pam.Resolve<int>(offset);
        REQUIRE(p != nullptr);
        REQUIRE(*p == 12345);

        pam.Delete(offset);
    }

    rm_file(fname);
}

// ---------------------------------------------------------------------------
// GetCount возвращает корректное число элементов
// ---------------------------------------------------------------------------
TEST_CASE("PersistentAddressSpace: GetCount returns correct element count",
          "[pam][count]")
{
    auto& pam = PersistentAddressSpace::Get();

    uintptr_t offset = pam.CreateArray<double>(7);
    REQUIRE(offset != 0u);
    REQUIRE(pam.GetCount(offset) == 7u);

    pam.Delete(offset);
    // После удаления GetCount должен вернуть 0.
    REQUIRE(pam.GetCount(offset) == 0u);
}

// ---------------------------------------------------------------------------
// Создание нескольких массивов не приводит к наложению (алиасингу)
// ---------------------------------------------------------------------------
TEST_CASE("PersistentAddressSpace: multiple arrays do not overlap",
          "[pam][array][no_alias]")
{
    auto& pam = PersistentAddressSpace::Get();

    uintptr_t off1 = pam.CreateArray<double>(3);
    uintptr_t off2 = pam.CreateArray<double>(3);

    REQUIRE(off1 != off2);

    pam.ResolveElement<double>(off1, 0) = 1.1;
    pam.ResolveElement<double>(off1, 1) = 2.2;
    pam.ResolveElement<double>(off1, 2) = 3.3;
    pam.ResolveElement<double>(off2, 0) = 4.4;
    pam.ResolveElement<double>(off2, 1) = 5.5;
    pam.ResolveElement<double>(off2, 2) = 6.6;

    REQUIRE(pam.ResolveElement<double>(off1, 0) == 1.1);
    REQUIRE(pam.ResolveElement<double>(off2, 0) == 4.4);

    pam.Delete(off1);
    pam.Delete(off2);
}
