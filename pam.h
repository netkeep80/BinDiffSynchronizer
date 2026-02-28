#pragma once

/*
 * pam.h — Персистный адресный менеджер (ПАМ), фаза 8.
 *
 * Начиная с фазы 8, pam.h выступает как «точка входа» для полноценного ПАМ:
 *   — включает pam_core.h  (базовый API: Create/Delete/Find/Resolve/...)
 *   — включает pmap.h      (персистная карта для будущего рефакторинга реестров)
 *
 * Цепочка включений (без циклических зависимостей):
 *   pam_core.h   ← базовый класс PersistentAddressSpace
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
 * Внутренние структуры pam_core.h (malloc-массивы phase 7):
 *   type_info_entry[], name_info_entry[], slot_descriptor[]
 * будут заменены на pmap<>/pvector<> в последующих фазах рефакторинга.
 */

#include "pam_core.h"
#include "pmap.h"
