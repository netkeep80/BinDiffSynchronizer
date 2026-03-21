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

### 1.1 Удаление pam_adapter.h

**Текущая ситуация:** `pam_adapter.h` конвертирует между `pptr<T>` (гранульные индексы)
и `uintptr_t` (байтовые смещения).

**После pmm 4.4:** pmm будет предоставлять `pptr::byte_offset()` и
`PersistMemoryManager::pptr_from_byte_offset<T>()`.

**Действия:**
- Заменить все вызовы `pptr_to_offset()` / `offset_to_pptr()` на новые методы pmm
- Удалить `pam_adapter.h`
- Обновить все файлы, использующие конверсию

### 1.2 Упрощение pam_pmm.h — использование корневого объекта pmm

**Текущая ситуация:** `pam_pmm.h` хранит `pam_pmm_root` с magic/version/registry_offset
в отдельном блоке. При инициализации ищет его в ПАП.

**После pmm 3.7:** pmm будет предоставлять `set_root<T>()` / `get_root<T>()`.

**Действия:**
- `pam_pmm_root` → хранить через `Mgr::set_root(root_pptr)` вместо ручного поиска
- Упростить `pam_pmm_init()` — загрузка корня через `Mgr::get_root<pam_pmm_root>()`
- Удалить magic-number поиск по ПАП

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

### 3.1 Обновление pjson_node

**Действия:**
- Обновить union в `pjson_node` для использования типов pmm:
  - `node_tag::string` → `pmm::pstring` вместо `pstring_pmm`
  - `node_tag::array` → `pmm::parray<node_id>` вместо `pvector_pmm<node_id>`
  - `node_tag::object` → `pmm::pmap<pstringview, node_id>` или `pmm::parray<entry>` вместо `pmap_pmm`
  - `node_tag::binary` → `pmm::parray<uint8_t>` вместо `pvector_pmm<uint8_t>`
- Обеспечить, что все типы остаются POD (trivially copyable)

### 3.2 Обновление pjson_codec

**Действия:**
- Обновить парсер/сериализатор для работы с новыми типами pmm
- Проверить, что Base64 кодирование/декодирование работает с `parray<uint8_t>`

### 3.3 Обновление pjson_db_pmm (публичный API)

**Действия:**
- Обновить `put()`, `get()`, `erase()` для работы с новыми типами
- Обновить `batch_begin()`/`batch_end()` — совместимость с `ppool`
- Обновить метрики — использовать `Mgr::get_stats()` вместо ручного подсчёта

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

| Метрика | До | После |
|---------|-----|-------|
| Файлы в уровне A | 5 | 1–2 |
| Файлы в уровне B | 5 | 1 (pstringview_pmm wrapper) |
| Строки кода (уровни A+B) | ~2500 | ~300 |
| Собственные персистентные типы | 7 | 1–2 (тонкие обёртки) |

### Качественные

- **Единый API** — все персистентные типы предоставляются pmm
- **Меньше кода для поддержки** — основная логика в pmm, BinDiffSynchronizer фокусируется на JSON
- **Лучшая тестируемость** — типы pmm тестируются отдельно
- **Согласованность** — единые гарантии потокобезопасности, персистентности, управления памятью
- **Проще интеграция** — новые проекты на базе pmm могут переиспользовать те же типы

---

## Порядок выполнения

```
pmm Фаза 3 (параллельно с планированием миграции)
    │
    ├── 3.1 pstring    ──────────── Этап 2.1 (замена pstring_pmm)
    ├── 3.2 parray     ──────────── Этап 2.2 (замена pmem_array_pmm)
    ├── 3.3 pmap::erase ─────────── Этап 2.3 (миграция pmap_pmm)
    ├── 3.5 pallocator  ─────────── Этап 1.3 (замена pallocator_pmm)
    ├── 3.6 ppool      ──────────── Этап 2.4 (замена pjson_pool_pmm)
    ├── 3.7 root object ─────────── Этап 1.2 (упрощение pam_pmm)
    │
    └── pmm 4.4 byte offset ─────── Этап 1.1 (удаление pam_adapter)
                                         │
                                    Этап 3 (обновление pjson_node, codec, db)
                                         │
                                    Этап 4 (тестирование и валидация)
```

Этапы 1 и 2 можно выполнять параллельно по мере готовности соответствующих типов в pmm.
Этап 3 выполняется после завершения этапов 1 и 2.
Этап 4 — финальная валидация.

---

*Документ создан 2026-03-19 на основе анализа BinDiffSynchronizer и плана pmm Фазы 3.*
