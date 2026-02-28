#pragma once

/*
 * pam.h — Персистный адресный менеджер (ПАМ), фаза 8.3.
 *
 * Начиная с фазы 8, pam.h выступает как «точка входа» для полноценного ПАМ:
 *   — включает pam_core.h  (API: Create/Delete/Find/Resolve/... и карты слотов и имён)
 *   — включает pmap.h      (персистная карта, совместима с картами фаз 8.2–8.3)
 *
 * Цепочка включений (без циклических зависимостей):
 *   pam_core.h   ← базовый класс PersistentAddressSpace (карта слотов + карта имён внутри ПАП)
 *     ↑
 *   persist.h    ← fptr<T>, AddressManager<T>
 *     ↑
 *   pstring.h, pvector.h, pmap.h
 *     ↑
 *   pam.h        ← pam_core.h + pmap.h (без цикла!)
 *
 * Для пользовательского кода поведение не изменилось:
 *   #include "pam.h"              // полноценный ПАМ
 *   PersistentAddressSpace::Init("myapp.pam");
 *   auto& pam = PersistentAddressSpace::Get();
 *   uintptr_t off = pam.Create<int>("counter");
 *
 * Фаза 8.2: карта слотов slot_descriptor[] заменена на pmap-совместимую
 * структуру внутри ПАП (pmap<uintptr_t, SlotInfo>). Данные слотов хранятся
 * в области данных ПАМ, что даёт O(log n) для операций Find/Delete/GetName/GetCount.
 *
 * Фаза 8.3: карта имён name_info_entry[] заменена на pmap-совместимую
 * структуру внутри ПАП (pmap<name_key, uintptr_t>). Данные имён хранятся
 * в области данных ПАМ, что даёт O(log n) для поиска Find() по имени.
 * Поля name_count/name_capacity удалены из pam_header.
 *
 * Совместимость раскладки карты слотов:
 *   запись карты: slot_entry{uintptr_t key, SlotInfo value}
 *              == pmap_entry<uintptr_t, SlotInfo>{uintptr_t key, SlotInfo value}
 *
 * Совместимость раскладки карты имён:
 *   запись карты: name_entry{name_key key, uintptr_t slot_offset}
 *              == pmap_entry<name_key, uintptr_t>{name_key key, uintptr_t value}
 */

#include "pam_core.h"
#include "pmap.h"

// ---------------------------------------------------------------------------
// Проверка совместимости раскладки карты слотов с pmap<uintptr_t, SlotInfo>
// ---------------------------------------------------------------------------

// slot_entry должна иметь ту же раскладку, что и pmap_entry<uintptr_t, SlotInfo>.
using _slot_pmap_entry = pmap_entry<uintptr_t, SlotInfo>;
static_assert( sizeof( slot_entry ) == sizeof( _slot_pmap_entry ),
               "slot_entry должна совпадать по размеру с pmap_entry<uintptr_t, SlotInfo>" );
static_assert( std::is_trivially_copyable<_slot_pmap_entry>::value,
               "pmap_entry<uintptr_t, SlotInfo> должен быть тривиально копируемым" );

// ---------------------------------------------------------------------------
// Проверка совместимости раскладки карты имён с pmap<name_key, uintptr_t>
// ---------------------------------------------------------------------------

// name_entry должна иметь ту же раскладку, что и pmap_entry<name_key, uintptr_t>.
using _name_pmap_entry = pmap_entry<name_key, uintptr_t>;
static_assert( sizeof( name_entry ) == sizeof( _name_pmap_entry ),
               "name_entry должна совпадать по размеру с pmap_entry<name_key, uintptr_t>" );
static_assert( std::is_trivially_copyable<_name_pmap_entry>::value,
               "pmap_entry<name_key, uintptr_t> должен быть тривиально копируемым" );
