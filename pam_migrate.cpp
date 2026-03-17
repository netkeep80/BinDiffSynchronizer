/**
 * @file pam_migrate.cpp
 * @brief Утилита миграции файлов .pam со старого формата ПАМ на новый формат PMM.
 *
 * Задача 14.9 из plan.md: утилита миграции старых .pam файлов.
 *
 * Алгоритм миграции:
 *   1. Загрузить старый .pam файл через PersistentAddressSpace (pam_core.h).
 *   2. Экспортировать всё дерево JSON в строку: node_to_string(root_id).
 *   3. Создать новый .pam файл через PMM: pam_pmm_init(new_filename).
 *   4. Импортировать JSON в новую БД: pjson_db_pmm::parse_into(root, json_str).
 *   5. Сохранить: pam_pmm_save().
 *
 * Использование:
 *   ./pam_migrate <old_file.pam> <new_file.pam>
 *
 * Примечание:
 *   - Миграция односторонняя: новый формат несовместим со старым.
 *   - Рекомендуется сделать резервную копию перед миграцией.
 *   - При большом объёме данных миграция может занять некоторое время.
 *
 * Все комментарии на русском языке (Тр.6).
 */

#include <cstdio>
#include <cstring>
#include <string>
#include <chrono>

// Старый API ПАМ (pam_core.h, persist.h).
#include "pjson_db.h"

// Новый API PMM (pam_pmm.h, pjson_db_pmm.h).
#include "pjson_db_pmm.h"

// ===========================================================================
// Вспомогательные функции
// ===========================================================================

/// Напечатать сообщение об использовании.
static void print_usage( const char* program_name )
{
    std::fprintf( stderr, "Использование: %s <старый_файл.pam> <новый_файл.pam>\n\n", program_name );
    std::fprintf( stderr, "Мигрирует базу данных со старого формата ПАМ на новый формат PMM.\n" );
    std::fprintf( stderr, "Рекомендуется сделать резервную копию перед миграцией.\n" );
}

/// Проверить, существует ли файл.
static bool file_exists( const char* filename )
{
    FILE* f = std::fopen( filename, "rb" );
    if ( f != nullptr )
    {
        std::fclose( f );
        return true;
    }
    return false;
}

/// Получить размер файла.
static long get_file_size( const char* filename )
{
    FILE* f = std::fopen( filename, "rb" );
    if ( f == nullptr )
        return -1;
    std::fseek( f, 0, SEEK_END );
    long size = std::ftell( f );
    std::fclose( f );
    return size;
}

// ===========================================================================
// Основная логика миграции
// ===========================================================================

/**
 * @brief Выполнить миграцию .pam файла.
 *
 * @param old_file Путь к старому файлу в формате PersistentAddressSpace.
 * @param new_file Путь к новому файлу в формате PMM.
 * @return true при успешной миграции, false при ошибке.
 */
static bool migrate_pam_file( const char* old_file, const char* new_file )
{
    // -----------------------------------------------------------------------
    // Шаг 1: Проверка входных параметров
    // -----------------------------------------------------------------------

    if ( !file_exists( old_file ) )
    {
        std::fprintf( stderr, "Ошибка: файл '%s' не найден.\n", old_file );
        return false;
    }

    if ( std::strcmp( old_file, new_file ) == 0 )
    {
        std::fprintf( stderr, "Ошибка: старый и новый файлы не должны совпадать.\n" );
        return false;
    }

    long old_size = get_file_size( old_file );
    std::printf( "Старый файл: %s (%ld байт)\n", old_file, old_size );
    std::printf( "Новый файл:  %s\n", new_file );

    auto start_time = std::chrono::high_resolution_clock::now();

    // -----------------------------------------------------------------------
    // Шаг 2: Загрузить старый .pam через PersistentAddressSpace
    // -----------------------------------------------------------------------

    std::printf( "\n[1/5] Загрузка старого файла через PersistentAddressSpace...\n" );

    pjson_db old_db = pjson_db::open( old_file );

    node_id root = old_db.root_id();
    if ( root == 0 )
    {
        std::fprintf( stderr, "Ошибка: не удалось найти корневой узел в старом файле.\n" );
        return false;
    }

    node_view root_view{ root };
    std::printf( "  Корневой узел найден (id = %lu)\n", static_cast<unsigned long>( root ) );

    // Получаем метрики для информации.
    uint64_t node_count = old_db.get( "/$metrics/node_count_total" ).as_uint();
    uint64_t str_count  = old_db.get( "/$metrics/string_count_total" ).as_uint();
    std::printf( "  Узлов в дереве:      %llu\n", static_cast<unsigned long long>( node_count ) );
    std::printf( "  Строк в словаре:     %llu\n", static_cast<unsigned long long>( str_count ) );

    // -----------------------------------------------------------------------
    // Шаг 3: Экспортировать дерево JSON в строку
    // -----------------------------------------------------------------------

    std::printf( "\n[2/5] Экспорт дерева JSON в строку...\n" );

    std::string json_str = old_db.dump();

    if ( json_str.empty() )
    {
        std::fprintf( stderr, "Предупреждение: экспортированный JSON пуст.\n" );
    }
    else
    {
        std::printf( "  Размер JSON: %zu байт\n", json_str.size() );
        // Показать первые 100 символов для превью.
        if ( json_str.size() > 100 )
        {
            std::printf( "  Превью: %.100s...\n", json_str.c_str() );
        }
        else
        {
            std::printf( "  Содержимое: %s\n", json_str.c_str() );
        }
    }

    // -----------------------------------------------------------------------
    // Шаг 4: Уничтожить старый ПАМ перед созданием нового
    // -----------------------------------------------------------------------

    std::printf( "\n[3/5] Освобождение старого ПАМ...\n" );

    // Сбрасываем синглтон PersistentAddressSpace, чтобы освободить память.
    PersistentAddressSpace::Get().Reset();

    // -----------------------------------------------------------------------
    // Шаг 5: Создать новый .pam через PMM и импортировать JSON
    // -----------------------------------------------------------------------

    std::printf( "\n[4/5] Создание нового файла через PMM и импорт JSON...\n" );

    // Инициализируем PMM с новым файлом.
    pjson::pjson_db_pmm new_db = pjson::pjson_db_pmm::open( new_file );

    // Парсим JSON в корень нового дерева.
    if ( !json_str.empty() )
    {
        // Парсим в корень.
        // Поскольку pjson_db_pmm создаёт корень как пустой объект,
        // нам нужно переписать его содержимое.
        // Используем низкоуровневый node_from_string для этого.
        node_id new_root = new_db.root_id();
        if ( new_root == 0 )
        {
            std::fprintf( stderr, "Ошибка: не удалось создать корневой узел в новом файле.\n" );
            return false;
        }

        bool parse_ok = node_from_string( json_str.c_str(), new_root );
        if ( !parse_ok )
        {
            std::fprintf( stderr, "Ошибка: не удалось распарсить JSON в новый файл.\n" );
            return false;
        }

        std::printf( "  JSON успешно импортирован.\n" );
    }
    else
    {
        std::printf( "  JSON пуст, создан пустой корневой объект.\n" );
    }

    // Разрешаем все $ref ссылки.
    new_db.resolve_all_refs();

    // -----------------------------------------------------------------------
    // Шаг 6: Сохранить новый файл
    // -----------------------------------------------------------------------

    std::printf( "\n[5/5] Сохранение нового файла...\n" );

    new_db.save();

    // Проверяем результат.
    long new_size = get_file_size( new_file );
    std::printf( "  Новый файл сохранён: %s (%ld байт)\n", new_file, new_size );

    // -----------------------------------------------------------------------
    // Итоги
    // -----------------------------------------------------------------------

    auto                          end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed  = end_time - start_time;

    std::printf( "\n=== Миграция завершена успешно ===\n" );
    std::printf( "  Время миграции: %.3f сек\n", elapsed.count() );
    std::printf( "  Старый размер:  %ld байт\n", old_size );
    std::printf( "  Новый размер:   %ld байт\n", new_size );

    if ( old_size > 0 && new_size > 0 )
    {
        double ratio = static_cast<double>( new_size ) / static_cast<double>( old_size );
        std::printf( "  Соотношение:    %.2f%% (%.2fx)\n", ratio * 100.0, ratio );
    }

    return true;
}

// ===========================================================================
// main
// ===========================================================================

int main( int argc, char* argv[] )
{
    std::printf( "=== pam_migrate — Утилита миграции .pam файлов ===\n\n" );

    // Проверяем аргументы командной строки.
    if ( argc != 3 )
    {
        print_usage( argv[0] );
        return 1;
    }

    const char* old_file = argv[1];
    const char* new_file = argv[2];

    // Предупреждение о перезаписи.
    if ( file_exists( new_file ) )
    {
        std::printf( "Предупреждение: файл '%s' будет перезаписан.\n", new_file );
        std::printf( "Продолжить? (y/n): " );
        std::fflush( stdout );

        char answer = 'n';
        if ( std::scanf( " %c", &answer ) != 1 )
        {
            std::fprintf( stderr, "Ошибка чтения ответа.\n" );
            return 1;
        }

        if ( answer != 'y' && answer != 'Y' )
        {
            std::printf( "Миграция отменена.\n" );
            return 0;
        }
    }

    // Выполняем миграцию.
    bool ok = migrate_pam_file( old_file, new_file );

    return ok ? 0 : 1;
}
