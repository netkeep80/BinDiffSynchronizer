#pragma once
/**
 * @file pam_pmm.h
 * @brief Фасад управления персистным адресным пространством на базе PMM.
 *
 * Этот файл реализует API для управления персистным адресным пространством,
 * используя PersistMemoryManager (PMM) в качестве бэкенда.
 *
 * Основные функции:
 *   - pam_pmm_init(filename) — инициализация / загрузка хранилища
 *   - pam_pmm_create<T>(name) / pam_pmm_create_array<T>(count, name) — создание объектов
 *   - pmm_resolve<T>(offset) / pmm_resolve_const<T>(offset) — разрешение смещения в указатель
 *   - pam_pmm_delete(offset) — удаление объектов
 *   - pam_pmm_find(name) / pam_pmm_find_typed<T>(name) — поиск по имени
 *   - pam_pmm_save() — сохранение в файл
 *
 * @see pam_pmm_config.h — определение PamManager
 * @see pmap_pmm.h — персистная карта для реестра имён
 */

#include "pam_pmm_config.h"
#include "pmap_pmm.h"
#include "pstringview_pmm.h"
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace pjson
{

// ═══════════════════════════════════════════════════════════════════════════
// РАЗРЕШЕНИЕ БАЙТОВЫХ СМЕЩЕНИЙ В УКАЗАТЕЛИ
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Разрешить байтовое смещение в указатель T* через PMM.
 *
 * Поддерживает как выровненные (гранульные) смещения, так и невыровненные
 * (смещения на подобъекты внутри гранулы, напр. поля структуры).
 *
 * @tparam T Тип данных.
 * @param off Байтовое смещение.
 * @return T* — сырой указатель или nullptr для off==0.
 */
template <typename T> inline T* pmm_resolve( uintptr_t off )
{
    if ( off == 0 )
        return nullptr;
    std::uint8_t* base = PamManager::backend().base_ptr();
    if ( base == nullptr )
        return nullptr;
    return reinterpret_cast<T*>( base + off );
}

/**
 * @brief Разрешить байтовое смещение в const T* через PMM.
 */
template <typename T> inline const T* pmm_resolve_const( uintptr_t off )
{
    if ( off == 0 )
        return nullptr;
    const std::uint8_t* base = PamManager::backend().base_ptr();
    if ( base == nullptr )
        return nullptr;
    return reinterpret_cast<const T*>( base + off );
}

// ═══════════════════════════════════════════════════════════════════════════
// КОНСТАНТЫ ДЛЯ PAM_PMM
// ═══════════════════════════════════════════════════════════════════════════

/// Максимальная длина имени объекта.
constexpr unsigned PAM_PMM_NAME_SIZE = 64u;

/// Начальный размер PMM (в байтах) при создании нового хранилища.
constexpr std::size_t PAM_PMM_INITIAL_SIZE = 64u * 1024u;

// ═══════════════════════════════════════════════════════════════════════════
// СТРУКТУРЫ ДЛЯ РЕЕСТРА ИМЕНОВАННЫХ ОБЪЕКТОВ
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Ключ для карты имён — фиксированный массив символов.
 */
struct pam_pmm_name_key
{
    char name[PAM_PMM_NAME_SIZE]; ///< Имя объекта (нуль-терминированная строка)

    bool operator<( const pam_pmm_name_key& other ) const
    {
        return std::strncmp( name, other.name, PAM_PMM_NAME_SIZE ) < 0;
    }
    bool operator==( const pam_pmm_name_key& other ) const
    {
        return std::strncmp( name, other.name, PAM_PMM_NAME_SIZE ) == 0;
    }
};

static_assert( std::is_trivially_copyable<pam_pmm_name_key>::value,
               "pam_pmm_name_key должен быть тривиально копируемым" );

/**
 * @brief Информация о слоте (объекте) в реестре.
 *
 * Хранит байтовое смещение объекта, его размер и количество элементов.
 */
struct pam_pmm_slot_info
{
    uintptr_t offset;    ///< Байтовое смещение объекта в ПАП
    uintptr_t elem_size; ///< Размер одного элемента в байтах
    uintptr_t count;     ///< Количество элементов (для массивов)
};

static_assert( std::is_trivially_copyable<pam_pmm_slot_info>::value,
               "pam_pmm_slot_info должен быть тривиально копируемым" );

// ═══════════════════════════════════════════════════════════════════════════
// pam_pmm_registry — Реестр именованных объектов на базе PMM
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Корневая структура PMM хранилища.
 *
 * Содержит магическое число для валидации и смещение реестра.
 * Аллоцируется первой при создании хранилища и используется для
 * восстановления состояния при загрузке.
 */
struct pam_pmm_root
{
    uint32_t  magic;        ///< Магическое число для валидации (PAM_PMM_MAGIC)
    uint32_t  version;      ///< Версия формата
    uintptr_t registry_off; ///< Байтовое смещение реестра
    uintptr_t reserved[4];  ///< Зарезервировано для будущего использования
};

/// Магическое число для идентификации PMM-хранилища pjson.
constexpr uint32_t PAM_PMM_MAGIC   = 0x504A534Eu; // 'PJSN'
constexpr uint32_t PAM_PMM_VERSION = 1u;

static_assert( std::is_trivially_copyable<pam_pmm_root>::value, "pam_pmm_root должен быть тривиально копируемым" );

/**
 * @brief Реестр именованных и безымянных объектов в PMM.
 *
 * Содержит две карты:
 *   - name_map_: pmap_pmm<pam_pmm_name_key, pam_pmm_slot_info> — имя → слот
 *   - slot_map_: pmap_pmm<uintptr_t, pam_pmm_slot_info> — offset → слот (для всех объектов)
 *
 * Структура живёт в ПАП; создаётся через pam_pmm_registry_create().
 */
struct pam_pmm_registry
{
    pmap_pmm<pam_pmm_name_key, pam_pmm_slot_info> name_map_; ///< Карта имя → слот
    pmap_pmm<uintptr_t, pam_pmm_slot_info>        slot_map_; ///< Карта offset → слот
};

static_assert( std::is_trivially_copyable<pam_pmm_registry>::value,
               "pam_pmm_registry должен быть тривиально копируемым" );

// ═══════════════════════════════════════════════════════════════════════════
// ГЛОБАЛЬНОЕ СОСТОЯНИЕ PAM_PMM
// ═══════════════════════════════════════════════════════════════════════════

namespace detail
{

/// Имя файла хранилища (глобальное состояние).
inline char& pam_pmm_filename_char( std::size_t idx )
{
    static char filename[256] = {};
    return filename[idx];
}

inline char* pam_pmm_filename()
{
    return &pam_pmm_filename_char( 0 );
}

/// Смещение корневой структуры в ПАП (глобальное состояние).
/// Корневая структура аллоцируется первой и хранится по известному смещению.
inline uintptr_t& pam_pmm_root_offset()
{
    static uintptr_t offset = 0;
    return offset;
}

/// Флаг инициализации.
inline bool& pam_pmm_initialized()
{
    static bool initialized = false;
    return initialized;
}

} // namespace detail

// ═══════════════════════════════════════════════════════════════════════════
// ОСНОВНЫЕ ФУНКЦИИ PAM_PMM
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Получить указатель на корневую структуру.
 */
inline pam_pmm_root* pam_pmm_get_root()
{
    uintptr_t off = detail::pam_pmm_root_offset();
    if ( off == 0 )
        return nullptr;
    return pmm_resolve<pam_pmm_root>( off );
}

/**
 * @brief Получить указатель на реестр.
 */
inline pam_pmm_registry* pam_pmm_get_registry()
{
    pam_pmm_root* root = pam_pmm_get_root();
    if ( root == nullptr || root->registry_off == 0 )
        return nullptr;
    return pmm_resolve<pam_pmm_registry>( root->registry_off );
}

/**
 * @brief Создать новый реестр в ПАП.
 *
 * Вызывается один раз при первой инициализации хранилища.
 *
 * @return Байтовое смещение реестра; 0 при ошибке.
 */
inline uintptr_t pam_pmm_registry_create()
{
    auto reg_pptr = PamManager::template allocate_typed<pam_pmm_registry>();
    if ( reg_pptr.is_null() )
        return 0;

    uintptr_t reg_off = reg_pptr.byte_offset();

    // Инициализируем карты нулями (они уже инициализированы конструктором по умолчанию).
    pam_pmm_registry* reg = reg_pptr.resolve();
    if ( reg != nullptr )
    {
        // Инициализируем поля напрямую вместо memset для избежания предупреждений.
        reg->name_map_ = pmap_pmm<pam_pmm_name_key, pam_pmm_slot_info>{};
        reg->slot_map_ = pmap_pmm<uintptr_t, pam_pmm_slot_info>{};
    }

    return reg_off;
}

/**
 * @brief Создать корневую структуру и реестр при инициализации нового хранилища.
 *
 * @return Байтовое смещение корневой структуры; 0 при ошибке.
 */
inline uintptr_t pam_pmm_create_root_and_registry()
{
    // Создаём корневую структуру.
    auto root_pptr = PamManager::template allocate_typed<pam_pmm_root>();
    if ( root_pptr.is_null() )
        return 0;

    uintptr_t root_off = root_pptr.byte_offset();

    // Инициализируем корневую структуру.
    pam_pmm_root* root = root_pptr.resolve();
    if ( root != nullptr )
    {
        root->magic        = PAM_PMM_MAGIC;
        root->version      = PAM_PMM_VERSION;
        root->registry_off = 0;
        for ( int i = 0; i < 4; i++ )
            root->reserved[i] = 0;
    }

    // Создаём реестр.
    uintptr_t reg_off = pam_pmm_registry_create();
    if ( reg_off == 0 )
        return 0;

    // Связываем корневую структуру с реестром.
    root = pmm_resolve<pam_pmm_root>( root_off );
    if ( root != nullptr )
        root->registry_off = reg_off;

    // Сохраняем корень в заголовке PMM (Issue #163, Plan Stage 1.2).
    PamManager::set_root( root_pptr );

    return root_off;
}

/**
 * @brief Инициализировать PMM из файла или создать новое хранилище.
 *
 * @param filename Путь к файлу хранилища (может быть nullptr для in-memory).
 */
inline void pam_pmm_init( const char* filename )
{
    // Сохраняем имя файла.
    if ( filename != nullptr )
    {
        std::strncpy( detail::pam_pmm_filename(), filename, 255 );
        detail::pam_pmm_filename()[255] = '\0';
    }
    else
    {
        detail::pam_pmm_filename()[0] = '\0';
    }

    // Пытаемся загрузить существующий файл.
    bool loaded = false;
    if ( filename != nullptr && filename[0] != '\0' )
    {
        // Проверяем, существует ли файл.
        std::FILE* f = std::fopen( filename, "rb" );
        if ( f != nullptr )
        {
            // Файл существует — определяем его размер.
            std::fseek( f, 0, SEEK_END );
            long file_size = std::ftell( f );
            std::fclose( f );

            if ( file_size > 0 )
            {
                // Создаём менеджер с размером не меньше файла.
                std::size_t size = static_cast<std::size_t>( file_size );
                if ( size < PAM_PMM_INITIAL_SIZE )
                    size = PAM_PMM_INITIAL_SIZE;
                PamManager::create( size );

                // Загружаем данные из файла.
                if ( pmm::load_manager_from_file<PamManager>( filename ) )
                {
                    // Получаем корневую структуру через API корневого объекта PMM
                    // (Issue #163, Plan Stage 1.2 — замена magic-number поиска).
                    auto root_pptr = PamManager::template get_root<pam_pmm_root>();
                    if ( !root_pptr.is_null() )
                    {
                        pam_pmm_root* root = root_pptr.resolve();
                        if ( root != nullptr && root->magic == PAM_PMM_MAGIC && root->version == PAM_PMM_VERSION )
                        {
                            detail::pam_pmm_root_offset() = root_pptr.byte_offset();
                            loaded                        = true;
                        }
                    }
                }

                if ( !loaded )
                {
                    // Не удалось найти корневую структуру — это старый или повреждённый файл.
                    PamManager::destroy();
                }
            }
        }
    }

    // Если файл не загружен — создаём новое хранилище.
    if ( !loaded )
    {
        PamManager::create( PAM_PMM_INITIAL_SIZE );

        // Создаём корневую структуру и реестр.
        uintptr_t root_off            = pam_pmm_create_root_and_registry();
        detail::pam_pmm_root_offset() = root_off;
    }

    detail::pam_pmm_initialized() = true;
}

/**
 * @brief Сохранить PMM в файл.
 *
 * Сохраняет PMM данные на диск.
 */
inline void pam_pmm_save()
{
    const char* filename = detail::pam_pmm_filename();
    if ( filename[0] == '\0' )
        return;

    pmm::save_manager<PamManager>( filename );
}

/**
 * @brief Уничтожить PMM и освободить ресурсы.
 *
 * Сохраняет данные перед уничтожением, если указан файл.
 */
inline void pam_pmm_destroy()
{
    pam_pmm_save();
    PamManager::destroy();

    detail::pam_pmm_filename()[0] = '\0';
    detail::pam_pmm_root_offset() = 0;
    detail::pam_pmm_initialized() = false;
}

/**
 * @brief Сбросить PMM к пустому состоянию за O(1).
 *
 * Пересоздаёт хранилище с чистым состоянием.
 */
inline void pam_pmm_reset()
{
    // Уничтожаем и создаём заново.
    PamManager::destroy();
    PamManager::create( PAM_PMM_INITIAL_SIZE );

    // Создаём корневую структуру и реестр.
    uintptr_t root_off            = pam_pmm_create_root_and_registry();
    detail::pam_pmm_root_offset() = root_off;
    detail::pam_pmm_initialized() = true;
}

/**
 * @brief Проверить, инициализирован ли PMM.
 */
inline bool pam_pmm_is_initialized()
{
    return detail::pam_pmm_initialized() && PamManager::is_initialized();
}

// ═══════════════════════════════════════════════════════════════════════════
// СОЗДАНИЕ ОБЪЕКТОВ
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Создать один объект типа T в ПАП.
 *
 * @tparam T Тип создаваемого объекта. Должен быть тривиально копируемым.
 * @param name Имя объекта (может быть nullptr для безымянного).
 * @return Байтовое смещение объекта в ПАП; 0 при ошибке.
 */
template <typename T> inline uintptr_t pam_pmm_create( const char* name = nullptr )
{
    static_assert( std::is_trivially_copyable<T>::value,
                   "pam_pmm_create<T> требует, чтобы T был тривиально копируемым" );

    pam_pmm_registry* reg = pam_pmm_get_registry();
    if ( reg == nullptr )
        return 0;

    // Проверяем, не занято ли имя.
    if ( name != nullptr && name[0] != '\0' )
    {
        pam_pmm_name_key nk{};
        std::strncpy( nk.name, name, PAM_PMM_NAME_SIZE - 1 );

        if ( reg->name_map_.find( nk ) != nullptr )
            return 0; // Имя уже занято.
    }

    // Аллоцируем объект.
    auto obj_pptr = PamManager::template allocate_typed<T>();
    if ( obj_pptr.is_null() )
        return 0;

    uintptr_t obj_off = obj_pptr.byte_offset();

    // Инициализируем объект нулями (T гарантированно trivially copyable по static_assert).
    T* obj = obj_pptr.resolve();
    if ( obj != nullptr )
        std::memset( static_cast<void*>( obj ), 0, sizeof( T ) );

    // Перезапрашиваем указатель на реестр после аллокации.
    reg = pam_pmm_get_registry();
    if ( reg == nullptr )
        return 0;

    // Создаём запись в slot_map_.
    pam_pmm_slot_info slot_info{};
    slot_info.offset    = obj_off;
    slot_info.elem_size = sizeof( T );
    slot_info.count     = 1;

    reg->slot_map_.insert_direct( obj_off, slot_info );

    // Если есть имя — добавляем в name_map_.
    if ( name != nullptr && name[0] != '\0' )
    {
        // Перезапрашиваем указатель после вставки в slot_map_.
        reg = pam_pmm_get_registry();
        if ( reg == nullptr )
            return 0;

        pam_pmm_name_key nk{};
        std::strncpy( nk.name, name, PAM_PMM_NAME_SIZE - 1 );
        reg->name_map_.insert_direct( nk, slot_info );
    }

    return obj_off;
}

/**
 * @brief Создать массив из count объектов типа T в ПАП.
 *
 * @tparam T Тип элементов массива. Должен быть тривиально копируемым.
 * @param count Количество элементов.
 * @param name Имя массива (может быть nullptr).
 * @return Байтовое смещение первого элемента; 0 при ошибке.
 */
template <typename T> inline uintptr_t pam_pmm_create_array( unsigned count, const char* name = nullptr )
{
    static_assert( std::is_trivially_copyable<T>::value,
                   "pam_pmm_create_array<T> требует, чтобы T был тривиально копируемым" );

    if ( count == 0 )
        return 0;

    pam_pmm_registry* reg = pam_pmm_get_registry();
    if ( reg == nullptr )
        return 0;

    // Проверяем, не занято ли имя.
    if ( name != nullptr && name[0] != '\0' )
    {
        pam_pmm_name_key nk{};
        std::strncpy( nk.name, name, PAM_PMM_NAME_SIZE - 1 );

        if ( reg->name_map_.find( nk ) != nullptr )
            return 0; // Имя уже занято.
    }

    // Аллоцируем массив.
    auto arr_pptr = PamManager::template allocate_typed<T>( count );
    if ( arr_pptr.is_null() )
        return 0;

    uintptr_t arr_off = arr_pptr.byte_offset();

    // Инициализируем массив нулями (T гарантированно trivially copyable по static_assert).
    T* arr = arr_pptr.resolve();
    if ( arr != nullptr )
        std::memset( static_cast<void*>( arr ), 0, sizeof( T ) * count );

    // Перезапрашиваем указатель на реестр после аллокации.
    reg = pam_pmm_get_registry();
    if ( reg == nullptr )
        return 0;

    // Создаём запись в slot_map_.
    pam_pmm_slot_info slot_info{};
    slot_info.offset    = arr_off;
    slot_info.elem_size = sizeof( T );
    slot_info.count     = count;

    reg->slot_map_.insert_direct( arr_off, slot_info );

    // Если есть имя — добавляем в name_map_.
    if ( name != nullptr && name[0] != '\0' )
    {
        // Перезапрашиваем указатель после вставки.
        reg = pam_pmm_get_registry();
        if ( reg == nullptr )
            return 0;

        pam_pmm_name_key nk{};
        std::strncpy( nk.name, name, PAM_PMM_NAME_SIZE - 1 );
        reg->name_map_.insert_direct( nk, slot_info );
    }

    return arr_off;
}

// ═══════════════════════════════════════════════════════════════════════════
// УДАЛЕНИЕ ОБЪЕКТОВ
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Удалить объект по байтовому смещению.
 *
 * @param offset Байтовое смещение объекта.
 */
inline void pam_pmm_delete( uintptr_t offset )
{
    if ( offset == 0 )
        return;

    pam_pmm_registry* reg = pam_pmm_get_registry();
    if ( reg == nullptr )
        return;

    // Ищем слот в slot_map_.
    const pam_pmm_slot_info* slot = reg->slot_map_.find( offset );
    if ( slot == nullptr )
        return;

    // Запоминаем информацию о слоте перед удалением.
    (void)slot->elem_size; // Информация доступна, но не используется.
    (void)slot->count;     // Информация доступна, но не используется.

    // Удаляем из name_map_ (линейный поиск по значению).
    // Это O(n), но используется редко.
    for ( auto it = reg->name_map_.begin(); it != reg->name_map_.end(); ++it )
    {
        if ( it->value.offset == offset )
        {
            reg->name_map_.erase( it->key );
            break;
        }
    }

    // Перезапрашиваем реестр после удаления из name_map_.
    reg = pam_pmm_get_registry();
    if ( reg == nullptr )
        return;

    // Удаляем из slot_map_.
    reg->slot_map_.erase( offset );

    // Деаллоцируем память.
    // Используем char для деаллокации, так как тип неизвестен.
    auto p = PamManager::pptr_from_byte_offset<char>( offset );
    PamManager::template deallocate_typed<char>( p );
}

// ═══════════════════════════════════════════════════════════════════════════
// ПОИСК ОБЪЕКТОВ
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Найти объект по имени.
 *
 * @param name Имя объекта.
 * @return Байтовое смещение объекта; 0 если не найден.
 */
inline uintptr_t pam_pmm_find( const char* name )
{
    if ( name == nullptr || name[0] == '\0' )
        return 0;

    pam_pmm_registry* reg = pam_pmm_get_registry();
    if ( reg == nullptr )
        return 0;

    pam_pmm_name_key nk{};
    std::strncpy( nk.name, name, PAM_PMM_NAME_SIZE - 1 );

    const pam_pmm_slot_info* slot = reg->name_map_.find( nk );
    if ( slot == nullptr )
        return 0;

    return slot->offset;
}

/**
 * @brief Найти объект по имени с проверкой размера элемента.
 *
 * @tparam T Ожидаемый тип объекта.
 * @param name Имя объекта.
 * @return Байтовое смещение объекта; 0 если не найден или размер не совпадает.
 */
template <typename T> inline uintptr_t pam_pmm_find_typed( const char* name )
{
    if ( name == nullptr || name[0] == '\0' )
        return 0;

    pam_pmm_registry* reg = pam_pmm_get_registry();
    if ( reg == nullptr )
        return 0;

    pam_pmm_name_key nk{};
    std::strncpy( nk.name, name, PAM_PMM_NAME_SIZE - 1 );

    const pam_pmm_slot_info* slot = reg->name_map_.find( nk );
    if ( slot == nullptr )
        return 0;

    // Проверяем размер элемента.
    if ( slot->elem_size != sizeof( T ) )
        return 0;

    return slot->offset;
}

/**
 * @brief Получить имя объекта по смещению.
 *
 * @param offset Байтовое смещение объекта.
 * @return Указатель на строку имени или nullptr.
 */
inline const char* pam_pmm_get_name( uintptr_t offset )
{
    if ( offset == 0 )
        return nullptr;

    pam_pmm_registry* reg = pam_pmm_get_registry();
    if ( reg == nullptr )
        return nullptr;

    // Линейный поиск по name_map_ (O(n)).
    for ( auto it = reg->name_map_.begin(); it != reg->name_map_.end(); ++it )
    {
        if ( it->value.offset == offset )
            return it->key.name;
    }

    return nullptr;
}

/**
 * @brief Получить количество элементов для слота.
 *
 * @param offset Байтовое смещение объекта.
 * @return Количество элементов; 0 если не найден.
 */
inline uintptr_t pam_pmm_get_count( uintptr_t offset )
{
    if ( offset == 0 )
        return 0;

    pam_pmm_registry* reg = pam_pmm_get_registry();
    if ( reg == nullptr )
        return 0;

    const pam_pmm_slot_info* slot = reg->slot_map_.find( offset );
    if ( slot == nullptr )
        return 0;

    return slot->count;
}

/**
 * @brief Получить размер элемента для слота.
 *
 * @param offset Байтовое смещение объекта.
 * @return Размер одного элемента в байтах; 0 если не найден.
 */
inline uintptr_t pam_pmm_get_elem_size( uintptr_t offset )
{
    if ( offset == 0 )
        return 0;

    pam_pmm_registry* reg = pam_pmm_get_registry();
    if ( reg == nullptr )
        return 0;

    const pam_pmm_slot_info* slot = reg->slot_map_.find( offset );
    if ( slot == nullptr )
        return 0;

    return slot->elem_size;
}

// ═══════════════════════════════════════════════════════════════════════════
// МЕТРИКИ
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Получить количество аллоцированных слотов.
 *
 * Возвращает число записей в slot_map_.
 */
inline uintptr_t pam_pmm_slot_count()
{
    pam_pmm_registry* reg = pam_pmm_get_registry();
    if ( reg == nullptr )
        return 0;
    return reg->slot_map_.size();
}

/**
 * @brief Получить количество именованных объектов.
 *
 * Возвращает число записей в name_map_.
 */
inline uintptr_t pam_pmm_named_count()
{
    pam_pmm_registry* reg = pam_pmm_get_registry();
    if ( reg == nullptr )
        return 0;
    return reg->name_map_.size();
}

/**
 * @brief Получить позицию bump-указателя (используемый размер).
 *
 * Возвращает текущий используемый размер PMM.
 */
inline uintptr_t pam_pmm_get_bump()
{
    return PamManager::used_size();
}

/**
 * @brief Получить общий размер области данных.
 *
 * Возвращает общий размер PMM области данных.
 */
inline uintptr_t pam_pmm_get_data_size()
{
    return PamManager::total_size();
}

/**
 * @brief Получить смещение таблицы интернирования строк.
 *
 * Для PMM это смещение pstringview хранилища.
 * Возвращает 0, так как PMM хранит pstringview автоматически.
 */
inline uintptr_t pam_pmm_get_string_table_offset()
{
    // PMM интегрирует pstringview напрямую — отдельной таблицы нет.
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// РАЗЫМЕНОВАНИЕ АДРЕСОВ
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Преобразовать смещение в T*.
 *
 * @tparam T Тип данных.
 * @param offset Байтовое смещение.
 * @return Указатель на T или nullptr.
 */
template <typename T> inline T* pam_pmm_resolve( uintptr_t offset )
{
    return pmm_resolve<T>( offset );
}

/**
 * @brief Преобразовать смещение в const T*.
 */
template <typename T> inline const T* pam_pmm_resolve_const( uintptr_t offset )
{
    return pmm_resolve_const<T>( offset );
}

// ═══════════════════════════════════════════════════════════════════════════
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Преобразовать указатель в смещение.
 *
 * @param p Указатель.
 * @return Байтовое смещение или 0 если указатель вне PMM.
 */
inline uintptr_t pam_pmm_ptr_to_offset( const void* p )
{
    if ( p == nullptr )
        return 0;

    const std::uint8_t* base = PamManager::backend().base_ptr();
    std::size_t         size = PamManager::backend().total_size();

    if ( base == nullptr )
        return 0;

    const std::uint8_t* ptr = static_cast<const std::uint8_t*>( p );
    if ( ptr < base || ptr >= base + size )
        return 0;

    return static_cast<uintptr_t>( ptr - base );
}

/**
 * @brief Перевыделить память (Realloc).
 *
 * Реализуется через аллокацию нового блока, копирование и деаллокацию старого.
 *
 * @tparam T Тип элементов.
 * @param old_offset Байтовое смещение старого блока.
 * @param old_count Старое количество элементов.
 * @param new_count Новое количество элементов.
 * @return Байтовое смещение нового блока; 0 при ошибке.
 *
 * @note При успехе старый блок деаллоцируется автоматически.
 *       При ошибке старый блок остаётся нетронутым.
 */
template <typename T> inline uintptr_t pam_pmm_realloc( uintptr_t old_offset, uintptr_t old_count, uintptr_t new_count )
{
    if ( old_offset == 0 || new_count == 0 )
        return 0;

    if ( new_count <= old_count )
        return old_offset; // Уменьшение не поддерживается.

    // Аллоцируем новый блок.
    auto new_pptr = PamManager::template allocate_typed<T>( new_count );
    if ( new_pptr.is_null() )
        return 0;

    uintptr_t new_offset = new_pptr.byte_offset();

    // Копируем данные из старого блока.
    T* new_data = new_pptr.resolve();
    T* old_data = pmm_resolve<T>( old_offset );
    if ( new_data != nullptr && old_data != nullptr )
    {
        std::memcpy( new_data, old_data, old_count * sizeof( T ) );
        // Инициализируем новые элементы нулями.
        std::memset( new_data + old_count, 0, ( new_count - old_count ) * sizeof( T ) );
    }

    // Деаллоцируем старый блок.
    auto old_pptr = PamManager::pptr_from_byte_offset<T>( old_offset );
    PamManager::template deallocate_typed<T>( old_pptr );

    // Обновляем запись в slot_map_.
    pam_pmm_registry* reg = pam_pmm_get_registry();
    if ( reg != nullptr )
    {
        // Удаляем старую запись.
        pam_pmm_slot_info old_info{};
        const auto*       old_slot = reg->slot_map_.find( old_offset );
        if ( old_slot != nullptr )
            old_info = *old_slot;

        reg->slot_map_.erase( old_offset );

        // Добавляем новую запись.
        reg = pam_pmm_get_registry();
        if ( reg != nullptr )
        {
            old_info.offset = new_offset;
            old_info.count  = new_count;
            reg->slot_map_.insert_direct( new_offset, old_info );
        }
    }

    return new_offset;
}

/**
 * @brief Зарезервировать ёмкость (заглушка для совместимости).
 *
 * В PMM не требуется, так как карты расширяются автоматически.
 * Сохранена для совместимости API.
 */
inline void pam_pmm_reserve_slots( uintptr_t /*min_slots*/ )
{
    // PMM автоматически управляет памятью.
}

/**
 * @brief Валидация хранилища (заглушка).
 *
 * Проверяет базовую корректность состояния PMM.
 */
inline bool pam_pmm_validate()
{
    return pam_pmm_is_initialized() && PamManager::is_initialized();
}

// ═══════════════════════════════════════════════════════════════════════════
// Персистентность корня AVL-дерева pstringview (Задача 15.5)
// ═══════════════════════════════════════════════════════════════════════════

/// Именованный слот в ПАП для хранения корневого индекса AVL-дерева интернирования.
static constexpr const char* PSTRINGVIEW_ROOT_SLOT_NAME = "pstringview_root";

/// Структура для хранения корневого индекса AVL-дерева pstringview в ПАП.
struct pstringview_root_slot
{
    uintptr_t root_idx; ///< Гранульный индекс корня AVL-дерева
};

static_assert( std::is_trivially_copyable<pstringview_root_slot>::value,
               "pstringview_root_slot должен быть тривиально копируемым" );

/// Сохранить текущий _root_idx в именованный слот ПАП.
inline void pstringview_pmm_save_root()
{
    if ( !pam_pmm_is_initialized() )
        return;

    uintptr_t slot_off = pam_pmm_find( PSTRINGVIEW_ROOT_SLOT_NAME );
    if ( slot_off == 0 )
    {
        slot_off = pam_pmm_create<pstringview_root_slot>( PSTRINGVIEW_ROOT_SLOT_NAME );
    }
    if ( slot_off != 0 )
    {
        pstringview_root_slot* slot = pmm_resolve<pstringview_root_slot>( slot_off );
        if ( slot != nullptr )
            slot->root_idx = static_cast<uintptr_t>( pmm_pstringview::_root_idx );
    }
}

/// Флаг: было ли выполнено восстановление _root_idx.
inline bool& pstringview_pmm_root_restored_flag()
{
    static bool restored = false;
    return restored;
}

/// Восстановить _root_idx из именованного слота ПАП (после загрузки).
/// Вызывается лениво, один раз после каждого reset()+init().
inline void pstringview_pmm_restore_root()
{
    if ( pstringview_pmm_root_restored_flag() )
        return;
    pstringview_pmm_root_restored_flag() = true;

    if ( !pam_pmm_is_initialized() )
        return;

    uintptr_t slot_off = pam_pmm_find( PSTRINGVIEW_ROOT_SLOT_NAME );
    if ( slot_off != 0 )
    {
        const pstringview_root_slot* slot = pmm_resolve_const<pstringview_root_slot>( slot_off );
        if ( slot != nullptr && slot->root_idx != 0 )
        {
            using index_type           = typename PamManager::index_type;
            pmm_pstringview::_root_idx = static_cast<index_type>( slot->root_idx );
        }
    }
}

/// Сбросить флаг восстановления и состояние pam_pmm при reset().
///
/// Сбрасывает флаг восстановления корня AVL-дерева,
/// а также инвалидирует глобальное состояние pam_pmm (initialized flag, root offset).
/// Это предотвращает SIGSEGV при повторном использовании PamManager::create()
/// без вызова pam_pmm_init() (Issue #161).
inline void pstringview_pmm_reset_restored_flag()
{
    pstringview_pmm_root_restored_flag() = false;
    detail::pam_pmm_initialized()        = false;
    detail::pam_pmm_root_offset()        = 0;
}

/// Регистрация callbacks для персистентности корня AVL-дерева pstringview.
/// Используется inline-переменной для гарантии однократного выполнения.
inline bool pstringview_pmm_hooks_registered = []()
{
    pstringview_pmm_pre_intern_hook()  = pstringview_pmm_restore_root;
    pstringview_pmm_post_intern_hook() = pstringview_pmm_save_root;
    pstringview_pmm_reset_hook()       = pstringview_pmm_reset_restored_flag;
    return true;
}();

// ═══════════════════════════════════════════════════════════════════════════
// API словаря строк: pam_intern_string, pam_search_strings, pam_all_strings
// ═══════════════════════════════════════════════════════════════════════════

/// Результат поиска строки в словаре ПАП (совместимость с pstringview.h).
struct pstringview_search_result
{
    std::string value;        ///< Найденная строка
    uintptr_t   chars_offset; ///< Смещение символьных данных в ПАП
    uintptr_t   length;       ///< Длина строки
};

/// Результат интернирования строки через ПАМ (совместимость с pstringview.h).
struct pam_intern_result
{
    uintptr_t chars_offset;
    uintptr_t length;
};

/// Интернировать строку s через ПАМ: вернуть {chars_offset, length}.
///
/// Метод уровня ПАМ для интернирования строк.
///
/// chars_offset указывает на символьные данные (нуль-терминированную строку)
/// внутри блока pmm::pstringview в ПАП. Это обеспечивает обратную совместимость
/// с кодом, использующим pmm_resolve<char>(chars_offset) для доступа к строке.
inline pam_intern_result pam_intern_string( const char* s )
{
    if ( s == nullptr )
        s = "";

    // Восстанавливаем корень AVL-дерева из ПАП при первом использовании.
    pstringview_pmm_restore_root();

    pmm_pstringview_pptr p = pmm_pstringview::intern( s );

    pam_intern_result result{};
    if ( p.is_null() )
        return result;

    // Получаем указатель на pmm_pstringview для доступа к строковым данным.
    pmm_pstringview* sv = p.resolve();
    if ( sv == nullptr )
        return result;

    // chars_offset указывает на str[] внутри блока pmm_pstringview,
    // а не на начало блока. Это обеспечивает совместимость с pmm_resolve<char>().
    const char*         str_ptr = sv->c_str();
    const std::uint8_t* base    = PamManager::backend().base_ptr();
    result.chars_offset         = static_cast<uintptr_t>( reinterpret_cast<const std::uint8_t*>( str_ptr ) - base );
    result.length               = static_cast<uintptr_t>( sv->size() );

    // Сохраняем корень AVL-дерева в ПАП (персистентность).
    pstringview_pmm_save_root();

    return result;
}

/// Найти все интернированные строки, содержащие подстроку pattern.
///
/// Задача 2.5, 15.5: обход AVL-дерева PMM для полнотекстового поиска.
/// chars_offset в результатах указывает на символьные данные (совместимость с pmm_resolve<char>()).
inline std::vector<pstringview_search_result> pam_search_strings( const char* pattern )
{
    // Восстанавливаем корень AVL-дерева из ПАП при первом использовании.
    pstringview_pmm_restore_root();

    auto pmm_results = pstringview_pmm_search( pattern );

    const std::uint8_t* base = PamManager::backend().base_ptr();

    std::vector<pstringview_search_result> results;
    results.reserve( pmm_results.size() );
    for ( auto& r : pmm_results )
    {
        pstringview_search_result sr;
        sr.value  = std::move( r.value );
        sr.length = r.length;

        // Пересчитываем chars_offset для указания на str[] внутри блока.
        if ( r.chars_offset != 0 && base != nullptr )
        {
            auto                   p  = PamManager::pptr_from_byte_offset<pmm_pstringview>( r.chars_offset );
            const pmm_pstringview* sv = p.resolve();
            if ( sv != nullptr )
            {
                sr.chars_offset = static_cast<uintptr_t>( reinterpret_cast<const std::uint8_t*>( sv->c_str() ) - base );
            }
            else
            {
                sr.chars_offset = r.chars_offset;
            }
        }
        else
        {
            sr.chars_offset = r.chars_offset;
        }

        results.push_back( std::move( sr ) );
    }
    return results;
}

/// Вернуть все интернированные строки из словаря ПАП.
///
/// Задача 2.5: pam_all_strings() — перебор всех pstringview в словаре.
inline std::vector<pstringview_search_result> pam_all_strings()
{
    return pam_search_strings( "" );
}

} // namespace pjson
