/*
 * main.cpp — Демонстрационный пример использования персистной инфраструктуры (фаза 2).
 *
 * Показывает базовые операции с ПАМ (персистным адресным менеджером):
 *   - Инициализация ПАП из файла
 *   - Создание объектов по имени
 *   - Запись и чтение значений
 *   - Сохранение ПАП в файл
 */

#include <cstdio>
#include "persist.h"
#include "pjson.h"

// ---------------------------------------------------------------------------
// Пример структуры страницы
// ---------------------------------------------------------------------------
struct page
{
    char ptr[1024];
};

static_assert(std::is_trivially_copyable<page>::value,
              "page должна быть тривиально копируемой");

int main()
{
    // Инициализируем персистное адресное пространство из файла (Тр.3).
    PersistentAddressSpace::Init("demo.pap");

    auto& pam = PersistentAddressSpace::Get();

    // Ищем или создаём объект "main.counter" в ПАП.
    uintptr_t counter_off = pam.Find("main.counter");
    if( counter_off == 0 )
    {
        counter_off = pam.Create<double>("main.counter");
        *pam.Resolve<double>(counter_off) = 0.0;
    }

    double* counter = pam.Resolve<double>(counter_off);
    *counter += 1.0;

    std::printf("main.counter = %.1f\n", *counter);

    // Сохраняем ПАП в файл для следующего запуска.
    pam.Save();

    return 0;
}
