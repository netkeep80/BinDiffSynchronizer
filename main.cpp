/*
 * main.cpp — Демонстрационный пример использования персистной инфраструктуры.
 *
 * Показывает основные персистные классы:
 *   - PersistentAddressSpace (ПАП) — единое персистное адресное пространство
 *   - fptr<T>    — персистный указатель (смещение в ПАП)
 *   - pstring    — персистная строка
 *   - pvector<T> — персистный динамический массив
 *   - pmap<K,V>  — персистная карта (ключ-значение)
 *   - pjson      — персистный JSON
 *   - pallocator — персистный STL-совместимый аллокатор
 *
 * При каждом запуске программа:
 *   1. Загружает ПАП из файла demo.pap (или создаёт новый).
 *   2. Увеличивает счётчик запусков.
 *   3. Добавляет имя очередного пользователя в персистный список.
 *   4. Обновляет конфигурацию в виде pjson-объекта.
 *   5. Сохраняет ПАП обратно в файл.
 *
 * Запустите программу несколько раз, чтобы увидеть накопленные данные.
 */

#include <cstdio>
#include <vector>
#include "persist.h"
#include "pstring.h"
#include "pvector.h"
#include "pmap.h"
#include "pjson.h"
#include "pallocator.h"

// ---------------------------------------------------------------------------
// Вспомогательные функции для вывода
// ---------------------------------------------------------------------------

static void print_separator(const char* title)
{
    std::printf("\n--- %s ---\n", title);
}

static void print_pstring(const fptr<pstring>& fps)
{
    if( fps == nullptr )
        std::printf("(null)");
    else
        std::printf("\"%s\"", fps->c_str());
}

static void print_pjson(const fptr<pjson>& fpj, int indent = 0);

static void print_indent(int indent)
{
    for( int i = 0; i < indent * 2; i++ )
        std::putchar(' ');
}

static void print_pjson(const fptr<pjson>& fpj, int indent)
{
    if( fpj == nullptr ) { std::printf("null"); return; }
    const pjson& j = *fpj;
    switch( j.type_tag() )
    {
    case pjson_type::null:
        std::printf("null");
        break;
    case pjson_type::boolean:
        std::printf("%s", j.get_bool() ? "true" : "false");
        break;
    case pjson_type::integer:
        std::printf("%lld", static_cast<long long>(j.get_int()));
        break;
    case pjson_type::uinteger:
        std::printf("%llu", static_cast<unsigned long long>(j.get_uint()));
        break;
    case pjson_type::real:
        std::printf("%g", j.get_real());
        break;
    case pjson_type::string:
        std::printf("\"%s\"", j.get_string());
        break;
    case pjson_type::array:
    {
        std::printf("[\n");
        uintptr_t sz = j.size();
        for( uintptr_t i = 0; i < sz; i++ )
        {
            print_indent(indent + 1);
            // Получаем временный fptr для вывода элемента массива
            fptr<pjson> elem;
            elem.set_addr(j.payload.array_val.data_slot);
            // Обращаемся напрямую к PersistentAddressSpace для получения элемента
            pjson* ep = PersistentAddressSpace::Get().Resolve<pjson>(
                j.payload.array_val.data_slot) + i;
            if( ep )
            {
                fptr<pjson> fe;
                fe.set_addr(static_cast<uintptr_t>(
                    reinterpret_cast<char*>(ep) -
                    reinterpret_cast<char*>(PersistentAddressSpace::Get().Resolve<char>(0) + 0)
                ));
                // Вывод непосредственно через указатель
                switch( ep->type_tag() )
                {
                case pjson_type::null:    std::printf("null"); break;
                case pjson_type::boolean: std::printf("%s", ep->get_bool() ? "true" : "false"); break;
                case pjson_type::integer: std::printf("%lld", static_cast<long long>(ep->get_int())); break;
                case pjson_type::uinteger: std::printf("%llu", static_cast<unsigned long long>(ep->get_uint())); break;
                case pjson_type::real:    std::printf("%g", ep->get_real()); break;
                case pjson_type::string:  std::printf("\"%s\"", ep->get_string()); break;
                default:                  std::printf("..."); break;
                }
            }
            if( i + 1 < sz ) std::printf(",");
            std::printf("\n");
        }
        print_indent(indent);
        std::printf("]");
        break;
    }
    case pjson_type::object:
    {
        std::printf("{\n");
        uintptr_t sz = j.size();
        for( uintptr_t i = 0; i < sz; i++ )
        {
            pjson_kv_entry* pair = PersistentAddressSpace::Get().Resolve<pjson_kv_entry>(
                j.payload.object_val.pairs_slot) + i;
            if( pair )
            {
                print_indent(indent + 1);
                // Выводим ключ
                const char* key = (pair->key_chars.addr() != 0)
                    ? &(pair->key_chars[0]) : "";
                std::printf("\"%s\": ", key);
                // Выводим значение
                switch( pair->value.type_tag() )
                {
                case pjson_type::null:    std::printf("null"); break;
                case pjson_type::boolean: std::printf("%s", pair->value.get_bool() ? "true" : "false"); break;
                case pjson_type::integer: std::printf("%lld", static_cast<long long>(pair->value.get_int())); break;
                case pjson_type::uinteger: std::printf("%llu", static_cast<unsigned long long>(pair->value.get_uint())); break;
                case pjson_type::real:    std::printf("%g", pair->value.get_real()); break;
                case pjson_type::string:  std::printf("\"%s\"", pair->value.get_string()); break;
                case pjson_type::array:   std::printf("[...(%llu)]", static_cast<unsigned long long>(pair->value.size())); break;
                case pjson_type::object:  std::printf("{...(%llu)}", static_cast<unsigned long long>(pair->value.size())); break;
                }
                if( i + 1 < sz ) std::printf(",");
                std::printf("\n");
            }
        }
        print_indent(indent);
        std::printf("}");
        break;
    }
    }
}

// ---------------------------------------------------------------------------
// Демо 1: счётчик запусков через fptr<double>
// ---------------------------------------------------------------------------

static void demo_counter(PersistentAddressSpace& pam)
{
    print_separator("Демо 1: персистный счётчик запусков");

    uintptr_t off = pam.Find("demo.counter");
    if( off == 0 )
    {
        off = pam.Create<double>("demo.counter");
        *pam.Resolve<double>(off) = 0.0;
    }

    double* counter = pam.Resolve<double>(off);
    *counter += 1.0;

    std::printf("Запуск №%.0f\n", *counter);
}

// ---------------------------------------------------------------------------
// Демо 2: персистная строка через fptr<pstring>
// ---------------------------------------------------------------------------

static void demo_pstring(PersistentAddressSpace& pam)
{
    print_separator("Демо 2: pstring — персистная строка");

    // Ищем или создаём строку в ПАП
    uintptr_t off = pam.FindTyped<pstring>("demo.greeting");
    fptr<pstring> fps;
    if( off == 0 )
    {
        fps.New("demo.greeting");
        fps->assign("Привет, персистный мир!");
    }
    else
    {
        fps.set_addr(off);
    }

    std::printf("Строка: ");
    print_pstring(fps);
    std::printf(" (длина: %llu)\n",
        static_cast<unsigned long long>(fps->size()));
}

// ---------------------------------------------------------------------------
// Демо 3: персистный вектор через fptr<pvector<int>>
// ---------------------------------------------------------------------------

static void demo_pvector(PersistentAddressSpace& pam)
{
    print_separator("Демо 3: pvector<int> — персистный массив");

    uintptr_t off = pam.FindTyped<pvector<int>>("demo.numbers");
    fptr<pvector<int>> fv;

    if( off == 0 )
    {
        fv.New("demo.numbers");
        // Начальные значения
        for( int i = 1; i <= 5; i++ )
            fv->push_back(i * 10);
    }
    else
    {
        fv.set_addr(off);
        // Добавляем элемент при каждом запуске
        int next = static_cast<int>(fv->size() + 1) * 10;
        fv->push_back(next);
    }

    std::printf("Элементы (%llu): [",
        static_cast<unsigned long long>(fv->size()));
    for( uintptr_t i = 0; i < fv->size(); i++ )
    {
        if( i > 0 ) std::printf(", ");
        std::printf("%d", (*fv)[i]);
    }
    std::printf("]\n");
}

// ---------------------------------------------------------------------------
// Демо 4: персистная карта через fptr<pmap<int, double>>
// ---------------------------------------------------------------------------

static void demo_pmap(PersistentAddressSpace& pam)
{
    print_separator("Демо 4: pmap<int, double> — персистная карта");

    uintptr_t off = pam.FindTyped<pmap<int, double>>("demo.scores");
    fptr<pmap<int, double>> fm;

    if( off == 0 )
    {
        fm.New("demo.scores");
        fm->insert(1, 9.5);
        fm->insert(2, 7.3);
        fm->insert(3, 8.8);
    }
    else
    {
        fm.set_addr(off);
        // Обновляем оценку при каждом запуске
        double* v = fm->find(1);
        if( v ) *v += 0.1;
    }

    std::printf("Карта (%llu записей):\n",
        static_cast<unsigned long long>(fm->size()));
    for( auto& entry : *fm )
    {
        std::printf("  %d -> %.2f\n", entry.key, entry.value);
    }
}

// ---------------------------------------------------------------------------
// Демо 5: pjson — персистный JSON-объект
// ---------------------------------------------------------------------------

static void demo_pjson(PersistentAddressSpace& pam)
{
    print_separator("Демо 5: pjson — персистный JSON");

    uintptr_t off = pam.FindTyped<pjson>("demo.config");
    fptr<pjson> fj;

    if( off == 0 )
    {
        // Создаём новый объект конфигурации
        fj.New("demo.config");
        fj->set_object();

        fj->obj_insert("version").set_int(1);
        fj->obj_insert("app").set_string("BinDiffSynchronizer");
        fj->obj_insert("debug").set_bool(false);
        fj->obj_insert("threshold").set_real(0.75);

        // Вложенный массив тегов
        pjson& tags = fj->obj_insert("tags");
        tags.set_array();
        tags.push_back().set_string("persistent");
        tags.push_back().set_string("demo");
    }
    else
    {
        fj.set_addr(off);
        // Обновляем версию при каждом запуске
        pjson* ver = fj->obj_find("version");
        if( ver && ver->is_integer() )
            ver->set_int(ver->get_int() + 1);
    }

    std::printf("JSON-объект конфигурации:\n");
    print_pjson(fj);
    std::printf("\n");
}

// ---------------------------------------------------------------------------
// Демо 6: pallocator — персистный STL-совместимый аллокатор
// ---------------------------------------------------------------------------

static void demo_pallocator()
{
    print_separator("Демо 6: pallocator — персистный STL-аллокатор");

    // std::vector, использующий персистный аллокатор
    std::vector<int, pallocator<int>> v;
    v.push_back(100);
    v.push_back(200);
    v.push_back(300);

    std::printf("std::vector<int, pallocator<int>>: [");
    for( std::size_t i = 0; i < v.size(); i++ )
    {
        if( i > 0 ) std::printf(", ");
        std::printf("%d", v[i]);
    }
    std::printf("]\n");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    std::printf("=== BinDiffSynchronizer — персистная инфраструктура ===\n");

    // Инициализируем персистное адресное пространство из файла (Тр.3).
    // При первом запуске создаётся новый файл; при последующих — загружается.
    PersistentAddressSpace::Init("demo.pap");
    auto& pam = PersistentAddressSpace::Get();

    // Запускаем все демо-примеры
    demo_counter(pam);
    demo_pstring(pam);
    demo_pvector(pam);
    demo_pmap(pam);
    demo_pjson(pam);
    demo_pallocator();

    // Сохраняем ПАП в файл для следующего запуска.
    pam.Save();

    std::printf("\n=== Данные сохранены в demo.pap ===\n");
    return 0;
}
