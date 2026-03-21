# План переделки BinDiffSynchronizer под новый pmm

Документ описывает план миграции [BinDiffSynchronizer](https://github.com/netkeep80/BinDiffSynchronizer)
(pjson_db_pmm) на обновлённый PersistMemoryManager после выполнения [Фазы 3 плана pmm](plan.md).

---

## Текущее состояние BinDiffSynchronizer

BinDiffSynchronizer (pjson_db_pmm) — header-only C++20 библиотека, реализующая персистентную
JSON-базу данных поверх PMM. Текущая архитектура имеет 4 уровня:

```
Уровень D: pjson_db_pmm        (высокоуровневый API: path-навигация, $ref, метрики)
Уровень C: pjson_node + codec   (модель узлов, парсинг/сериализация JSON)
Уровень B: Персистентные примитивы (pstringview_pmm, pstring_pmm, pvector_pmm, pmap_pmm, pmem_array_pmm)
Уровень A: Адаптер PMM          (pam_adapter.h, pam_pmm.h, pam_pmm_config.h, fptr_pmm.h, pallocator_pmm.h)
```

### Проблема

Уровни A и B содержат значительный объём кода (~2500 строк), дублирующего или
оборачивающего функциональность, которая должна предоставляться самим pmm.
Это приводит к:

- **Дублированию кода** — `pstring_pmm`, `pmem_array_pmm`, `pmap_pmm`, `pallocator_pmm`
  повторяют логику, которую pmm мог бы предоставить
- **Несогласованности API** — BinDiffSynchronizer работает с байтовыми смещениями (`uintptr_t`),
  а pmm — с гранульными индексами, требуя постоянных конверсий
- **Сложности поддержки** — два набора персистентных контейнеров с разными API и гарантиями

---

## Предварительные условия

Перед началом миграции BinDiffSynchronizer должны быть выполнены следующие задачи
из [Фазы 3 плана pmm](plan.md):

| Задача pmm | Заменяет в BinDiffSynchronizer |
|------------|-------------------------------|
| 3.1 `pstring<ManagerT>` | `pstring_pmm` |
| 3.2 `parray<T, ManagerT>` | `pmem_array_pmm` |
| 3.3 `pmap::erase()` + итератор | `pmap_pmm` (частично) |
| 3.5 `pallocator<T, ManagerT>` | `pallocator_pmm` |
| 3.6 `ppool<T, ManagerT>` | `pjson_pool_pmm` |
| 3.7 Корневой объект | `pam_pmm_root` в `pam_pmm.h` |
| 4.4 Конверсия pptr ↔ байтовые смещения | `pam_adapter.h` |

---

## Этап 1: Миграция уровня A (адаптер PMM)

### 1.1 Удаление pam_adapter.h ✅

**Выполнено (Issue #169):** `pam_adapter.h` удалён — конверсия через PMM v0.43.0 API (Phase 4.4, Issue pmm#211).

**Что сделано:**
- Все вызовы `pptr_to_offset(p)` заменены на `p.byte_offset()` (PMM метод pptr)
- Все вызовы `offset_to_pptr<T>(off)` заменены на `PamManager::pptr_from_byte_offset<T>(off)`
- `pmm_resolve<T>()` и `pmm_resolve_const<T>()` перенесены в `pam_pmm.h` (по-прежнему нужны для node_id → raw pointer)
- Удалены: `PMM_GRANULE_SIZE`, `pmm_index_type`, `is_aligned_offset()`, `align_offset_up()`, `get_granule_size()`, `char_pptr`, `byte_pptr`
- Обновлены все файлы: `pam_pmm.h`, `pmap_pmm.h`, `pstringview_pmm.h`, `pjson_pool_pmm.h`, `pjson_node.h`, `fptr_pmm.h`
- Тесты обновлены для нового API
- Все 574 теста проходят

### 1.2 Упрощение pam_pmm.h — использование корневого объекта pmm ✅

**Выполнено (Issue #163):** `pam_pmm.h` использует `PamManager::set_root()` / `get_root()` для хранения и загрузки корневой структуры.

**Что сделано:**
- `pam_pmm_create_root_and_registry()` сохраняет корень через `PamManager::set_root( root_pptr )` вместо ручного поиска по ПАП
- `pam_pmm_init()` загружает корень через `PamManager::template get_root<pam_pmm_root>()` — magic-number поиск по всему ПАП удалён
- Валидация magic/version сохранена: проверяется на корне, полученном через API (не через сканирование)
- Структура `pam_pmm_root` сохранена (magic, version, registry_off, reserved) для обратной совместимости формата файла

### 1.3 Удаление pallocator_pmm.h ✅

**Выполнено (Issue #143):** `pallocator_pmm.h` удалён.

**Что сделано:**
- `pallocator_pmm<T>` уже являлся алиасом для `PamManager::pallocator<T>` (с Issue #163)
- Все тесты обновлены: используют `PamManager::pallocator<T>` напрямую
- Удалён `pallocator_pmm.h` — обёртка больше не нужна
- Удалён алиас `pjson::pallocator<T>` (использовался только в тестах)

### 1.4 Упрощение fptr_pmm.h ✅

**Выполнено (Issue #143):** `fptr_pmm<T>` переделан в тонкую обёртку над `pptr<T>`.

**Что сделано:**
- Внутреннее хранение заменено с `uintptr_t __addr` на `PamManager::pptr<T> _p`
- Разыменование делегировано операторам `pptr<T>` (operator*, operator->)
- Удалена прямая зависимость от `pmm_resolve`/`pmm_resolve_const`
- sizeof(fptr_pmm<T>) == sizeof(pptr<T>) вместо sizeof(void*)
- Обратная совместимость через `addr()`/`set_addr()` (байтовые смещения)
- Добавлены методы `pptr()`/`set_pptr()` для доступа к внутреннему pptr<T>
- Удобные методы `New()`/`Delete()`/`find()` сохранены

---

## Этап 2: Миграция уровня B (персистентные примитивы)

### 2.1 Замена pstring_pmm на pmm::pstring ✅

**Выполнено (Issue #144):** `pstring_pmm` заменён на `PamManager::pstring` (pmm::pstring<PamManager>).

**Что сделано:**
- `node::string_val` теперь имеет тип `PamManager::pstring` (12 байт: uint32_t _length + _capacity + _data_idx) вместо `pstring_pmm` (16 байт: uintptr_t length + chars_off)
- `node_view::as_string()` использует `pstring::c_str()` и `pstring::size()` вместо прямого доступа к полям
- `node_set_string()` использует `pstring::assign()` через стековую копию (realloc-безопасность)
- Деаллокация строковых данных в `pjson_db_pmm.h` через `pstring::free_data()`
- Удалён `pstring_pmm.h` — обёртка больше не нужна
- Устранены предупреждения memset для non-trivial node в `pjson_pool_pmm.h`
- Все 596 тестов проходят

### 2.2 Замена pmem_array_pmm на pmm::parray ✅

**Выполнено (Issue #145):** `pmem_array_pmm` заменён на `PamManager::parray<T>` (pmm::parray<T, PamManager>).

**Что сделано:**
- `node::array_val`, `node::object_val`, `node::binary_val` теперь имеют тип `PamManager::parray<T>` (12 байт: uint32_t _size + _capacity + _data_idx) вместо `pmem_array_hdr_pmm` (24 байта: uintptr_t size + capacity + data_off)
- `pvector_pmm<T>` переписан как обёртка над `PamManager::parray<T>`, все операции делегируются методам parray
- `pmap_pmm<K,V>` переписан с хранением `PamManager::parray<Entry>` вместо `pmem_array_hdr_pmm`, с безопасной вставкой при росте PMM-пула (self-offset pattern)
- Добавлена свободная функция `parray_insert_sorted_object_entry()` для sorted insert в object_val
- `pjson_node.h`: структура `object_entry` перемещена перед `node` (необходимо для `parray<object_entry>`)
- `pjson_db_pmm.h`: деаллокация данных через `free_data()` вместо ручного `offset_to_pptr + deallocate_typed`
- `pjson_codec.h`, `pjson_db_helpers.h`, `pjson_pool_pmm.h`: обновлены для нового API
- Удалён `pmem_array_pmm.h` — обёртка больше не нужна
- Все тесты обновлены: размерные проверки отражают 12-байтовый parray
- Все 593 теста проходят

### 2.3 Миграция pmap_pmm ✅

**Выполнено (Issue #166):** Выбран Вариант B — pmap_pmm остаётся как sorted-array над `PamManager::parray<Entry>`.

**Что сделано:**
- Исследован Вариант A (замена на pmm::pmap<K,V>, AVL-дерево): обнаружена фундаментальная несовместимость с save/reload — `PMM::load()` → `rebuild_free_tree()` вызывает `reset_avl_fields_of()` на ВСЕХ блоках, разрушая структуру AVL-дерева пользовательских pmap
- Принято решение оставить Вариант B: sorted-array на базе `PamManager::parray<Entry>` с бинарным поиском O(log n) для find, O(n) для insert/erase
- Исправлен `erase()`: прямой доступ `arr_._size--` заменён на `arr_.pop_back()` (корректное использование публичного API parray)
- pmap_pmm уже использует `PamManager::parray<Entry>` после задачи 2.2 — дополнительная миграция внутреннего хранения не требуется
- Все 593 теста проходят

**Обоснование выбора Варианта B:**
- pmm::pmap<K,V> хранит AVL-узлы в TreeNode-полях заголовков блоков PMM (left_offset, right_offset, parent_offset, avl_height)
- Функция `rebuild_free_tree()` при загрузке файла сбрасывает TreeNode-поля ВСЕХ блоков, включая пользовательские AVL-деревья
- Это делает pmm::pmap несовместимой с персистентным save/reload — после загрузки файла данные карты теряются
- Sorted-array подход корректно сохраняет и восстанавливает данные, так как использует только содержимое блока данных parray

### 2.4 Замена pjson_pool_pmm на pmm::ppool ✅

**Выполнено (Issue #166):** `pjson_pool_pmm` заменён на `PamManager::ppool<node>` (pmm::ppool<node, PamManager>).

**Что сделано:**
- `pjson_pool_pmm` переопределён как алиас `PamManager::ppool<node>` — пул использует чанковую аллокацию O(1) из pmm::ppool вместо ручного массива с удвоением
- Функции `pjson_pool_pmm_alloc()` / `_free()` делегируют в `ppool::allocate()` / `ppool::deallocate()`
- Конверсия node* ↔ node_id (байтовое смещение) через `node_ptr_to_id()` и `pmm_resolve<node>()`
- `pjson_pool_pmm_init()` использует placement new для корректной инициализации ppool (включая `_objects_per_chunk = 64`)
- Защита от zero-init: `pjson_pool_pmm_alloc()` устанавливает `_objects_per_chunk` по умолчанию, если он равен 0 (после `pam_pmm_create` memset)
- Метрики: `total_count` → `ppool::total_capacity()`, `used_count` → `ppool::allocated_count()`, `free_in_pool` → `ppool::free_count()`
- Тесты обновлены: `total_count` теперь отражает чанковую ёмкость (`>=` вместо `==`), удалён тест на `node_tag::_free`
- `pjson_pool_pmm.h` сохранён как тонкая обёртка над `PamManager::ppool<node>` (не удалён, т.к. предоставляет node_id-совместимый API)
- Все 593 теста проходят

### 2.5 Упрощение pstringview_pmm ✅

**Выполнено (Issue #167):** Структура `pstringview_pmm` удалена, `pstringview_pmm.h` упрощён до тонкой обёртки.

**Что сделано:**
- Удалена структура `pstringview_pmm` (16-байтовая обёртка с `length` + `chars_offset`) — не использовалась вне тестов
- Удалены дублирующие функции `pstringview_pmm_intern()`, `pstringview_pmm_intern_result` — вместо них `pam_intern_string()` из `pam_pmm.h`
- `pstringview_pmm.h` сохранён как тонкая обёртка: типовые алиасы (`pmm_pstringview`, `pmm_pstringview_pptr`), hooks для персистентности корня AVL-дерева, `pstringview_pmm_reset()`, AVL-обход для поиска строк, `pstringview_manager`
- `pjson_node.h`: include заменён с `pstringview_pmm.h` на `pam_adapter.h` (реальная зависимость)
- Тесты переведены на `pam_intern_string()` / `pam_search_strings()` / `pam_all_strings()`
- Все 587 тестов проходят

### 2.6 Удаление pvector_pmm ✅

**Выполнено (Issue #167):** `pvector_pmm.h` удалён — обёртка над `PamManager::parray<T>` больше не нужна.

**Что сделано:**
- Удалён `pvector_pmm.h` — тонкая обёртка (210 строк) над `PamManager::parray<T>` с собственными итераторами
- `pjson_node.h`: удалён мёртвый `#include "pvector_pmm.h"` (тип `pvector_pmm` не использовался в production-коде после задачи 2.2); удалён дублирующий `#include "pam_adapter.h"`
- `tests/test_pvector.cpp`: переписан — тесты используют `PamManager::parray<T>` напрямую (capacity growth, front/back через data(), pointer iteration, double type, clear+push_back, pop_back on empty)
- `tests/test_pmem_array_pmm.cpp`: удалены 7 тестов `pvector_pmm` (layout, push_back, front/back, pop_back, clear, iterators, capacity) — их функциональность покрыта тестами parray
- Все 576 тестов проходят

---

## Этап 3: Обновление уровней C и D

### 3.1 Обновление pjson_node ✅

**Выполнено (Issues #144, #145, #166, #167):** Все типы в union `node` обновлены на типы PMM в ходе этапа 2.

**Что сделано:**
- `node::string_val` → `PamManager::pstring` (Issue #144, План 2.1)
- `node::array_val` → `PamManager::parray<node_id>` (Issue #145, План 2.2)
- `node::object_val` → `PamManager::parray<object_entry>` (Issue #145, План 2.2)
- `node::binary_val` → `PamManager::parray<uint8_t>` (Issue #145, План 2.2)
- Все типы остаются POD (trivially copyable) — подтверждено static_assert
- `object_entry` перемещена перед `node` (необходимо для `parray<object_entry>`)
- Удалены все зависимости от `pstring_pmm`, `pvector_pmm`, `pmem_array_pmm`

### 3.2 Обновление pjson_codec ✅

**Выполнено (Issues #144, #145):** Парсер/сериализатор обновлён в ходе миграции типов на этапе 2.

**Что сделано:**
- Парсинг строк использует `node_set_string()` → `PamManager::pstring::assign()`
- Сериализация строк использует `node_view::as_string()` → `PamManager::pstring::c_str()`
- Base64 кодирование/декодирование работает с `PamManager::parray<uint8_t>` через `.size()` и `.data()`
- Массивы и объекты работают через `node_view::at(i)` и `node_view::size()`, которые используют parray прозрачно
- Старые типы не используются: нет ссылок на `pstring_pmm`, `pvector_pmm`, `pmem_array_pmm`

### 3.3 Обновление pjson_db_pmm (публичный API) ✅

**Выполнено (Issues #144, #145, #166):** Публичный API обновлён в ходе этапа 2.

**Что сделано:**
- `put()`, `get()`, `erase()` работают с новыми типами PMM прозрачно через node_view
- `batch_begin()`/`batch_end()` совместимы с `PamManager::ppool<node>` (Issue #166, План 2.4)
- Деаллокация данных через `pstring::free_data()` и `parray::free_data()` вместо ручного `offset_to_pptr + deallocate_typed`
- Пул узлов: `pjson_pool_pmm` = `PamManager::ppool<node>` — чанковая аллокация O(1)
- Метрики используют `ppool::total_capacity()`, `ppool::allocated_count()`, `ppool::free_count()`

---

## Этап 4: Тестирование и валидация

### 4.1 Регрессионное тестирование

- Все существующие тесты BinDiffSynchronizer должны проходить после миграции
- Дополнительные тесты на граничные случаи новых типов pmm

### 4.2 Тестирование совместимости

- Проверить save/load совместимость с существующими файлами БД
- Определить стратегию миграции для существующих данных (если формат изменился)

### 4.3 Бенчмарки

- Сравнить производительность до и после миграции
- Измерить: вставка JSON-объектов, поиск по пути, итерация, save/load

---

## Ожидаемые результаты

### Количественные

| Метрика | До | Ожидание | Текущее состояние |
|---------|-----|----------|-------------------|
| Файлы в уровне A | 5 | 1–2 | 3 (pam_pmm_config.h, pam_pmm.h фасад, fptr_pmm.h обёртка) |
| Файлы в уровне B | 5 | 1 (pstringview_pmm wrapper) | 2 (pstringview_pmm + pmap_pmm — тонкие обёртки) |
| Строки кода (уровни A+B) | ~2500 | ~300 | ~625 (обёртки, без pam_pmm.h фасада) |
| Собственные персистентные типы | 7 | 1–2 (тонкие обёртки) | 2 (pmap_pmm sorted array, pstringview_pmm алиасы) |

**Удалённые файлы:** `pallocator_pmm.h`, `pstring_pmm.h`, `pmem_array_pmm.h`, `pvector_pmm.h`, `pam_adapter.h` — 5 файлов, ~1900 строк кода.

### Качественные

- ✅ **Единый API** — все персистентные типы (`pstring`, `parray`, `ppool`, `pallocator`) предоставляются PMM
- ✅ **Меньше кода для поддержки** — основная логика в pmm, BinDiffSynchronizer фокусируется на JSON
- ✅ **Лучшая тестируемость** — типы pmm тестируются отдельно
- ✅ **Согласованность** — единые гарантии потокобезопасности, персистентности, управления памятью
- ✅ **Проще интеграция** — новые проекты на базе pmm могут переиспользовать те же типы
- ✅ **Полное удаление адаптера** — `pam_adapter.h` удалён (PMM v0.43.0, Phase 4.4)

---

## Порядок выполнения

```
pmm Фаза 3 (параллельно с планированием миграции)
    │
    ├── 3.1 pstring    ──────────── Этап 2.1 (замена pstring_pmm)         ✅
    ├── 3.2 parray     ──────────── Этап 2.2 (замена pmem_array_pmm)      ✅
    ├── 3.3 pmap::erase ─────────── Этап 2.3 (миграция pmap_pmm)          ✅
    ├── 3.5 pallocator  ─────────── Этап 1.3 (замена pallocator_pmm)      ✅
    ├── 3.6 ppool      ──────────── Этап 2.4 (замена pjson_pool_pmm)      ✅
    ├── 3.7 root object ─────────── Этап 1.2 (упрощение pam_pmm)          ✅
    │
    └── pmm 4.4 byte offset ─────── Этап 1.1 (удаление pam_adapter)      ✅
                                         │
                                    Этап 3 (обновление pjson_node, codec, db) ✅
                                         │
                                    Этап 4 (тестирование и валидация)
```

Все этапы 1, 2 и 3 выполнены. Обновление типов в этапе 3 было выполнено
параллельно с этапом 2, т.к. замена типов в node/codec/db была частью каждой задачи этапа 2.
Этап 1.1 выполнен после обновления PMM до v0.43.0 (Phase 4.4: `pptr::byte_offset()` и
`PersistMemoryManager::pptr_from_byte_offset<T>()`).

**Оставшиеся задачи:** Этап 4 — тестирование и валидация (регрессионное, совместимость, бенчмарки).

---

*Документ создан 2026-03-19 на основе анализа BinDiffSynchronizer и плана pmm Фазы 3.*
*Обновлён 2026-03-21: отмечены выполненные задачи 1.2, 3.1, 3.2, 3.3 (Issue #168).*
*Обновлён 2026-03-21: выполнена задача 1.1 — удалён pam_adapter.h после обновления PMM до v0.43.0 (Issue #169).*
