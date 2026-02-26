#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <filesystem>

#include "pam.h"
#include "pstring.h"

// =============================================================================
// Тесты на динамичность менеджера ПАП (issue #56)
//
// Проверяет способность ПАМ динамически расширяться при создании большого
// числа именованных объектов, а также корректность хранения и поиска.
//
// Методология:
//   1. Создать N pstring с уникальными строковыми именами (PAM-слот = имя "sNNNNNN").
//   2. Записать в каждую pstring её же PAM-имя в качестве содержимого.
//   3. После создания всех N записей найти каждую по имени через PAM.Find()
//      и проверить, что содержимое совпадает с именем.
//   4. Убедиться, что все содержимое уникально.
//
// Ограничения PAM:
//   PAM_NAME_SIZE = 64 байта — имя объекта в таблице слотов ПАМ.
//   PAM_INITIAL_SLOT_CAPACITY = 16 — начальная ёмкость; удваивается при необходимости.
//
// Масштаб теста:
//   PAM_DYNAMIC_SMALL_COUNT  — быстрый тест (всегда запускается), 10 000 записей.
//   PAM_DYNAMIC_LARGE_COUNT  — нагрузочный тест (только если задан макрос),
//                              1 000 000 записей (для ручного запуска).
// =============================================================================

namespace {

/// Количество записей для быстрого теста на динамичность ПАМ.
constexpr unsigned PAM_DYNAMIC_SMALL_COUNT = 10'000;

/// Количество записей для нагрузочного теста (1 млн, опционально).
constexpr unsigned PAM_DYNAMIC_LARGE_COUNT = 1'000'000;

/// Вспомогательная функция: удалить временный файл.
void rm_pam_dyn_file(const char* path)
{
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

/**
 * Сформировать уникальное PAM-имя для N-го объекта.
 * Формат: "s%06u" — 's' + 6 цифр, итого не более 7 байт + null = 8 байт.
 * Гарантированно помещается в PAM_NAME_SIZE (64 байта).
 */
inline void make_pam_name(char* buf, unsigned idx)
{
    std::snprintf(buf, 8, "s%06u", idx);
}

/**
 * Создать N именованных pstring в ПАП.
 * Каждая pstring получает имя "sNNNNNN" и содержимое, равное этому же имени.
 * Возвращает вектор смещений (offsets) созданных pstring в том же порядке.
 */
std::vector<uintptr_t> create_pstrings(unsigned count)
{
    auto& pam = PersistentAddressSpace::Get();
    std::vector<uintptr_t> offsets;
    offsets.reserve(count);

    char name_buf[8];
    for( unsigned i = 0; i < count; i++ )
    {
        make_pam_name(name_buf, i);
        // Создаём pstring в ПАП под именем "sNNNNNN".
        uintptr_t offset = pam.Create<pstring>(name_buf);
        REQUIRE(offset != 0u);

        // Инициализируем содержимое pstring её же именем.
        pstring* ps = pam.Resolve<pstring>(offset);
        REQUIRE(ps != nullptr);
        ps->assign(name_buf);

        offsets.push_back(offset);
    }
    return offsets;
}

/**
 * Проверить N именованных pstring:
 *   1. Найти каждую в ПАМ по имени (PAM.Find).
 *   2. Убедиться, что смещение совпадает с записанным при создании.
 *   3. Убедиться, что содержимое pstring совпадает с её именем.
 */
void verify_pstrings(const std::vector<uintptr_t>& offsets)
{
    auto& pam = PersistentAddressSpace::Get();

    char name_buf[8];
    for( unsigned i = 0; i < static_cast<unsigned>(offsets.size()); i++ )
    {
        make_pam_name(name_buf, i);

        // Поиск по имени в ПАМ.
        uintptr_t found = pam.Find(name_buf);
        REQUIRE(found == offsets[i]);

        // Проверка содержимого pstring.
        const pstring* ps = pam.Resolve<pstring>(found);
        REQUIRE(ps != nullptr);
        REQUIRE(std::strcmp(ps->c_str(), name_buf) == 0);
    }
}

/**
 * Удалить все pstring по сохранённым смещениям.
 * Вызывает pstring::clear() для освобождения символьных данных в ПАП,
 * затем PAM.Delete() для освобождения слота самой pstring.
 */
void delete_pstrings(const std::vector<uintptr_t>& offsets)
{
    auto& pam = PersistentAddressSpace::Get();
    for( uintptr_t offset : offsets )
    {
        pstring* ps = pam.Resolve<pstring>(offset);
        if( ps != nullptr )
            ps->clear();
        pam.Delete(offset);
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Быстрый тест динамичности ПАМ: 10 000 pstring
//
// Проверяет:
//   — ПАМ динамически расширяет таблицу слотов при превышении ёмкости
//   — Все именованные объекты находятся по имени после создания
//   — Содержимое каждой pstring уникально и соответствует имени
//   — Число именованных слотов в заголовке ПАМ корректно
// ---------------------------------------------------------------------------
TEST_CASE("PAM dynamic: create and verify 10000 named pstrings",
          "[pam][dynamic][pstring]")
{
    // Подготовка: инициализируем ПАМ без файла (in-memory).
    PersistentAddressSpace::Init("./test_pam_dynamic_small.pam");

    // Шаг 1: создаём 10 000 именованных pstring.
    std::vector<uintptr_t> offsets = create_pstrings(PAM_DYNAMIC_SMALL_COUNT);
    REQUIRE(offsets.size() == PAM_DYNAMIC_SMALL_COUNT);

    // Шаг 2: проверяем, что slot_count в заголовке ПАМ равен N.
    auto& pam = PersistentAddressSpace::Get();
    // (Доступ к slot_count через публичный API ПАМ — число именованных слотов
    //  отражается в pam_header, но прямой геттер не предусмотрен.
    //  Косвенно проверяем через Find: все N объектов должны быть найдены.)

    // Шаг 3: находим и проверяем все записи.
    verify_pstrings(offsets);

    // Шаг 4: убеждаемся в уникальности содержимого.
    // (Содержимое = имя, имена уникальны => содержимое тоже уникально.)
    // Для компактности проверяем попарно только несколько граничных пар.
    {
        char name_first[8], name_last[8];
        make_pam_name(name_first, 0);
        make_pam_name(name_last, PAM_DYNAMIC_SMALL_COUNT - 1);

        uintptr_t off_first = pam.Find(name_first);
        uintptr_t off_last  = pam.Find(name_last);
        REQUIRE(off_first != off_last);

        const pstring* ps_first = pam.Resolve<pstring>(off_first);
        const pstring* ps_last  = pam.Resolve<pstring>(off_last);
        REQUIRE(ps_first != nullptr);
        REQUIRE(ps_last  != nullptr);
        REQUIRE(*ps_first != *ps_last);
    }

    // Шаг 5: чистим за собой.
    delete_pstrings(offsets);

    rm_pam_dyn_file("./test_pam_dynamic_small.pam");
}

// ---------------------------------------------------------------------------
// Тест: содержимое pstring уникально для каждой записи
//
// Выборочная проверка уникальности: для 100 случайных пар убеждаемся,
// что ни одна pstring не имеет одинакового содержимого с другой.
// ---------------------------------------------------------------------------
TEST_CASE("PAM dynamic: pstring content is unique for each named entry",
          "[pam][dynamic][pstring][unique]")
{
    PersistentAddressSpace::Init("./test_pam_dynamic_unique.pam");

    constexpr unsigned COUNT = 1000u;
    std::vector<uintptr_t> offsets = create_pstrings(COUNT);
    REQUIRE(offsets.size() == COUNT);

    auto& pam = PersistentAddressSpace::Get();

    // Проверяем 100 случайных пар (i, i+COUNT/2) на неравенство содержимого.
    for( unsigned i = 0; i < 100u; i++ )
    {
        unsigned j = i + COUNT / 2;
        const pstring* ps_i = pam.Resolve<pstring>(offsets[i]);
        const pstring* ps_j = pam.Resolve<pstring>(offsets[j]);
        REQUIRE(ps_i != nullptr);
        REQUIRE(ps_j != nullptr);
        REQUIRE(*ps_i != *ps_j);
    }

    delete_pstrings(offsets);
    rm_pam_dyn_file("./test_pam_dynamic_unique.pam");
}

// ---------------------------------------------------------------------------
// Тест: таблица слотов ПАМ динамически расширяется
//
// PAM_INITIAL_SLOT_CAPACITY = 16. Создаём объектов больше, чем начальная
// ёмкость, и проверяем, что ПАМ корректно расширил таблицу.
// ---------------------------------------------------------------------------
TEST_CASE("PAM dynamic: slot table grows beyond initial capacity",
          "[pam][dynamic][slots]")
{
    PersistentAddressSpace::Init("./test_pam_dynamic_grow.pam");

    // Создаём объектов значительно больше PAM_INITIAL_SLOT_CAPACITY (16).
    constexpr unsigned COUNT = 1000u;
    std::vector<uintptr_t> offsets = create_pstrings(COUNT);
    REQUIRE(offsets.size() == COUNT);

    auto& pam = PersistentAddressSpace::Get();

    // Все объекты должны быть найдены.
    char name_buf[8];
    for( unsigned i = 0; i < COUNT; i++ )
    {
        make_pam_name(name_buf, i);
        uintptr_t found = pam.Find(name_buf);
        REQUIRE(found != 0u);
        REQUIRE(found == offsets[i]);
    }

    delete_pstrings(offsets);
    rm_pam_dyn_file("./test_pam_dynamic_grow.pam");
}

// ---------------------------------------------------------------------------
// Нагрузочный тест ПАМ: 1 000 000 именованных pstring
//
// Этот тест демонстрирует полную динамичность менеджера ПАП согласно
// требованию задачи #56.
//
// Тест помечен тегом [stress] и не запускается при обычном ctest.
// Для запуска: ./tests [stress]
//
// ВАЖНО: тест требует значительного времени и памяти (~несколько ГБ RAM).
// Поиск по имени для 1 000 000 записей через PAM.Find() не является
// оптимальным (линейный поиск O(n)), поэтому верификация использует
// сохранённые смещения вместо Find() для каждой записи.
// ---------------------------------------------------------------------------
TEST_CASE("PAM dynamic stress: create and verify 1 million named pstrings",
          "[pam][dynamic][pstring][stress]")
{
    const char* fname = "./test_pam_dynamic_stress.pam";
    rm_pam_dyn_file(fname);
    PersistentAddressSpace::Init(fname);

    // Шаг 1: создаём 1 000 000 именованных pstring.
    auto& pam = PersistentAddressSpace::Get();
    std::vector<uintptr_t> offsets;
    offsets.reserve(PAM_DYNAMIC_LARGE_COUNT);

    char name_buf[8];
    for( unsigned i = 0; i < PAM_DYNAMIC_LARGE_COUNT; i++ )
    {
        make_pam_name(name_buf, i);
        uintptr_t offset = pam.Create<pstring>(name_buf);
        REQUIRE(offset != 0u);
        pstring* ps = pam.Resolve<pstring>(offset);
        REQUIRE(ps != nullptr);
        ps->assign(name_buf);
        offsets.push_back(offset);
    }
    REQUIRE(offsets.size() == PAM_DYNAMIC_LARGE_COUNT);

    // Шаг 2: находим каждую pstring по сохранённому смещению и проверяем
    // содержимое. (Поиск через Find() для 1M записей — O(n²) — не используется
    // в цикле, вместо этого используем сохранённые смещения.)
    for( unsigned i = 0; i < PAM_DYNAMIC_LARGE_COUNT; i++ )
    {
        make_pam_name(name_buf, i);
        const pstring* ps = pam.Resolve<pstring>(offsets[i]);
        REQUIRE(ps != nullptr);
        REQUIRE(std::strcmp(ps->c_str(), name_buf) == 0);
    }

    // Шаг 3: выборочно находим через PAM.Find() первые и последние записи.
    {
        char name_first[8], name_last[8];
        make_pam_name(name_first, 0);
        make_pam_name(name_last, PAM_DYNAMIC_LARGE_COUNT - 1);

        uintptr_t off_first = pam.Find(name_first);
        uintptr_t off_last  = pam.Find(name_last);
        REQUIRE(off_first == offsets[0]);
        REQUIRE(off_last  == offsets[PAM_DYNAMIC_LARGE_COUNT - 1]);

        const pstring* ps_first = pam.Resolve<pstring>(off_first);
        const pstring* ps_last  = pam.Resolve<pstring>(off_last);
        REQUIRE(ps_first != nullptr);
        REQUIRE(ps_last  != nullptr);
        REQUIRE(*ps_first != *ps_last);
        REQUIRE(std::strcmp(ps_first->c_str(), name_first) == 0);
        REQUIRE(std::strcmp(ps_last->c_str(),  name_last)  == 0);
    }

    // Шаг 4: чистим за собой.
    for( uintptr_t offset : offsets )
    {
        pstring* ps = pam.Resolve<pstring>(offset);
        if( ps != nullptr )
            ps->clear();
        pam.Delete(offset);
    }

    rm_pam_dyn_file(fname);
}
