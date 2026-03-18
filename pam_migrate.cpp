/**
 * @file pam_migrate.cpp
 * @brief Утилита миграции/экспорта файлов .pam в формате PMM.
 *
 * Задача 14.9 из plan.md: утилита миграции старых .pam файлов.
 *
 * После удаления старого API PersistentAddressSpace (Задача 14.10),
 * утилита работает исключительно с форматом PMM:
 *   - Загружает .pam файл через pam_pmm_init().
 *   - Экспортирует дерево JSON в stdout или новый файл.
 *
 * Использование:
 *   ./pam_migrate <файл.pam>              — вывести JSON на stdout
 *   ./pam_migrate <файл.pam> <новый.pam>  — скопировать БД в новый файл
 *
 * Все комментарии на русском языке (Тр.6).
 */

#include <cstdio>
#include <cstring>
#include <string>
#include <chrono>

// API PMM (pam_pmm.h, pjson_db_pmm.h).
#include "pjson_db_pmm.h"

using namespace pjson;

// ===========================================================================
// Вспомогательные функции
// ===========================================================================

/// Напечатать сообщение об использовании.
static void print_usage( const char* program_name )
{
    std::fprintf( stderr, "Использование:\n" );
    std::fprintf( stderr, "  %s <файл.pam>              — вывести JSON на stdout\n", program_name );
    std::fprintf( stderr, "  %s <файл.pam> <новый.pam>  — скопировать БД в новый файл\n\n", program_name );
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
// Экспорт JSON на stdout
// ===========================================================================

static bool export_json( const char* pam_file )
{
    if ( !file_exists( pam_file ) )
    {
        std::fprintf( stderr, "Ошибка: файл '%s' не найден.\n", pam_file );
        return false;
    }

    pjson_db_pmm db = pjson_db_pmm::open( pam_file );

    node_id root = db.root_id();
    if ( root == 0 )
    {
        std::fprintf( stderr, "Ошибка: не удалось найти корневой узел.\n" );
        return false;
    }

    std::string json_str = db.dump();
    std::printf( "%s\n", json_str.c_str() );

    return true;
}

// ===========================================================================
// Копирование БД в новый файл
// ===========================================================================

static bool copy_pam_file( const char* src_file, const char* dst_file )
{
    if ( !file_exists( src_file ) )
    {
        std::fprintf( stderr, "Ошибка: файл '%s' не найден.\n", src_file );
        return false;
    }

    if ( std::strcmp( src_file, dst_file ) == 0 )
    {
        std::fprintf( stderr, "Ошибка: исходный и целевой файлы не должны совпадать.\n" );
        return false;
    }

    long src_size = get_file_size( src_file );
    std::fprintf( stderr, "Исходный файл: %s (%ld байт)\n", src_file, src_size );
    std::fprintf( stderr, "Целевой файл:  %s\n", dst_file );

    auto start_time = std::chrono::high_resolution_clock::now();

    // Шаг 1: Загружаем исходный файл.
    std::fprintf( stderr, "\n[1/4] Загрузка исходного файла...\n" );
    pjson_db_pmm src_db = pjson_db_pmm::open( src_file );

    node_id root = src_db.root_id();
    if ( root == 0 )
    {
        std::fprintf( stderr, "Ошибка: не удалось найти корневой узел.\n" );
        return false;
    }

    // Шаг 2: Экспортируем JSON.
    std::fprintf( stderr, "\n[2/4] Экспорт дерева JSON...\n" );
    std::string json_str = src_db.dump();
    std::fprintf( stderr, "  Размер JSON: %zu байт\n", json_str.size() );

    // Шаг 3: Сбрасываем PMM и создаём новый файл.
    std::fprintf( stderr, "\n[3/4] Создание нового файла и импорт...\n" );
    pstringview_manager::reset();
    pam_pmm_reset();

    pjson_db_pmm dst_db = pjson_db_pmm::open( dst_file );

    if ( !json_str.empty() )
    {
        node_id new_root = dst_db.root_id();
        if ( new_root == 0 )
        {
            std::fprintf( stderr, "Ошибка: не удалось создать корневой узел.\n" );
            return false;
        }

        bool parse_ok = node_from_string( json_str.c_str(), new_root );
        if ( !parse_ok )
        {
            std::fprintf( stderr, "Ошибка: не удалось импортировать JSON.\n" );
            return false;
        }
    }

    dst_db.resolve_all_refs();

    // Шаг 4: Сохраняем.
    std::fprintf( stderr, "\n[4/4] Сохранение...\n" );
    dst_db.save();

    long dst_size = get_file_size( dst_file );

    auto                          end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed  = end_time - start_time;

    std::fprintf( stderr, "\n=== Копирование завершено ===\n" );
    std::fprintf( stderr, "  Время: %.3f сек\n", elapsed.count() );
    std::fprintf( stderr, "  Исходный размер: %ld байт\n", src_size );
    std::fprintf( stderr, "  Целевой размер:  %ld байт\n", dst_size );

    return true;
}

// ===========================================================================
// main
// ===========================================================================

int main( int argc, char* argv[] )
{
    if ( argc == 2 )
    {
        return export_json( argv[1] ) ? 0 : 1;
    }
    else if ( argc == 3 )
    {
        // Предупреждение о перезаписи.
        if ( file_exists( argv[2] ) )
        {
            std::fprintf( stderr, "Предупреждение: файл '%s' будет перезаписан.\n", argv[2] );
            std::fprintf( stderr, "Продолжить? (y/n): " );
            std::fflush( stderr );

            char answer = 'n';
            if ( std::scanf( " %c", &answer ) != 1 )
            {
                std::fprintf( stderr, "Ошибка чтения ответа.\n" );
                return 1;
            }

            if ( answer != 'y' && answer != 'Y' )
            {
                std::fprintf( stderr, "Отменено.\n" );
                return 0;
            }
        }

        return copy_pam_file( argv[1], argv[2] ) ? 0 : 1;
    }
    else
    {
        print_usage( argv[0] );
        return 1;
    }
}
