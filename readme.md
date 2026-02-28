# BinDiffSynchronizer — Персистная инфраструктура

Библиотека на C++17 для построения персистных структур данных на основе единого персистного адресного пространства (ПАП).

---

## Обзор

Проект реализует персистную инфраструктуру, позволяющую хранить объекты произвольных типов в едином бинарном файле-образе и восстанавливать их между перезапусками без вызова конструкторов.

Конечная цель — реализация `pjson`, персистного аналога `nlohmann::json`.

---

## Архитектура (фаза 8.3)

### Требования

| №     | Требование |
|-------|------------|
| Тр.1  | Персистные объекты используют только персистные указатели |
| Тр.2  | Создание/удаление объектов — через специальные методы аллокатора |
| Тр.3  | При запуске аллокатор инициализируется именем файла-хранилища |
| Тр.4  | Единое ПАП для объектов разных типов |
| Тр.5  | `sizeof(fptr<T>) == sizeof(void*)` |
| Тр.6  | Все комментарии — на русском языке |
| Тр.7  | Никакой логики с именами файлов в `persist<T>` |
| Тр.8  | `sizeof(persist<T>) == sizeof(T)` |
| Тр.9  | Никакой логики с именами файлов в `fptr<T>` |
| Тр.10 | При загрузке образа ПАП конструкторы/деструкторы не вызываются |
| Тр.11 | Объекты `persist<T>` живут только в образе ПАП |
| Тр.12 | Доступ к персистным объектам — только через `fptr<T>` |
| Тр.13 | `fptr<T>` может находиться как в обычной памяти, так и в ПАП |
| Тр.14 | ПАМ — персистный объект, хранит имена объектов |
| Тр.15 | `fptr<T>` инициализируется строковым именем объекта через ПАМ |
| Тр.16 | ПАМ хранит карту объектов и их имена |

### Изменения Phase 8.2 (замена slot_descriptor[] на pmap, issue #24)

Таблица слотов `PersistentAddressSpace` перенесена внутрь ПАП:
вместо внешнего malloc-массива `slot_descriptor[]` используется
персистная отсортированная карта `pmap<uintptr_t, SlotInfo>`, хранящаяся
в области данных ПАМ. Это даёт O(log n) для операций
Find/Delete/GetName/GetCount/GetElemSize.

#### Новые типы (фаза 8.2)

| Тип | Описание |
|-----|----------|
| `SlotInfo` | Значение в карте слотов: `count`, `type_idx`, `name_idx` |
| `slot_entry` | Запись карты: `{uintptr_t key, SlotInfo value}` (раскладка = `pmap_entry<uintptr_t, SlotInfo>`) |

#### Изменения файлов (фаза 8.2)

| Файл | Изменение |
|------|-----------|
| `pam_core.h` | Добавлены `SlotInfo`, `slot_entry`. Удалены `slot_descriptor* _slots`, `_slot_capacity`. Карта слотов хранится внутри `_data`. `name_info_entry.slot_idx` → `slot_offset`. Версия формата: 4 → 5. |
| `pam.h` | Добавлен `static_assert` совместимости `slot_entry` с `pmap_entry<uintptr_t, SlotInfo>`. |
| `tests/test_pam.cpp` | Добавлены тесты `[phase82]`: `SlotInfo`, `slot_entry` тривиально копируемы; рост карты слотов внутри ПАП; Save/Init восстанавливают карту слотов. |

#### Формат файла ПАМ (фаза 8.2, версия 5)

```
[pam_header]                  — заголовок (убраны slot_count/slot_capacity)
[type_info_entry * type_cap]  — таблица типов
[name_info_entry * name_cap]  — таблица имён (slot_idx → slot_offset)
[байты объектов]              — область данных (включает карту слотов внутри ПАП)
  ├─ [uintptr_t size][uintptr_t capacity][uintptr_t entries_off] — объект карты
  └─ [slot_entry * capacity]  — отсортированный массив {offset → SlotInfo}
```

Карта слотов размещается в начале области данных (сразу после `pam_header`)
и хранится как часть `_data` — сохраняется и восстанавливается вместе с ней.

#### Оставшиеся фазы рефакторинга

| Фаза | Задача |
|------|--------|
| 8.3 ✓ | Замена `name_info_entry[]` на `pmap<name_key, uintptr_t>` внутри ПАП |
| 8.4 | Замена `type_info_entry[]` на `pvector<TypeInfo>` внутри ПАП |
| 8.5 | Упрощение `pam_header` (офсеты встроены в данные ПАП) |

---

### Изменения Phase 8.3 (замена name_info_entry[] на карту имён внутри ПАП, issue #68)

Таблица имён `PersistentAddressSpace` перенесена внутрь ПАП:
вместо внешнего malloc-массива `name_info_entry[]` используется
персистная отсортированная карта `pmap<name_key, uintptr_t>`, хранящаяся
в области данных ПАМ. Это даёт O(log n) для операций Find/GetName.

#### Новые типы (фаза 8.3)

| Тип | Описание |
|-----|----------|
| `name_key` | Ключ карты имён: `char name[PAM_NAME_SIZE]` (тривиально копируемый) |
| `name_entry` | Запись карты: `{name_key key, uintptr_t slot_offset}` (раскладка = `pmap_entry<name_key, uintptr_t>`) |

#### Изменения файлов (фаза 8.3)

| Файл | Изменение |
|------|-----------|
| `pam_core.h` | Добавлены `name_key`, `name_entry`. Удалены `name_info_entry* _names`, `_name_capacity`. Карта имён хранится внутри `_data`. Поля `name_count`/`name_capacity` удалены из `pam_header`, добавлены `slot_map_offset` и `name_map_offset`. Версия формата: 5 → 6. |
| `pam.h` | Добавлен `static_assert` совместимости `name_entry` с `pmap_entry<name_key, uintptr_t>`. |
| `tests/test_pam.cpp` | Добавлены тесты `[phase83]`: `name_key`, `name_entry` тривиально копируемы; рост карты имён внутри ПАП; Save/Init восстанавливают карту имён. |

#### Формат файла ПАМ (фаза 8.3, версия 6)

```
[pam_header]                  — заголовок (убраны name_count/name_capacity,
                                добавлены slot_map_offset, name_map_offset)
[type_info_entry * type_cap]  — таблица типов
[байты объектов]              — область данных (включает карты слотов и имён)
  ├─ [uintptr_t size][uintptr_t capacity][uintptr_t entries_off] — объект карты слотов
  ├─ [slot_entry * capacity]  — отсортированный массив {offset → SlotInfo}
  ├─ [uintptr_t size][uintptr_t capacity][uintptr_t entries_off] — объект карты имён
  └─ [name_entry * capacity]  — отсортированный массив {name_key → slot_offset}
```

Карта имён размещается в области данных ПАМ сразу после карты слотов
и хранится как часть `_data` — сохраняется и восстанавливается вместе с ней.
Смещения обеих карт (`slot_map_offset`, `name_map_offset`) сохраняются в `pam_header`.

---

### Изменения Phase 8 (разделение pam.h → pam_core.h, issue #23)

Устранена циклическая зависимость заголовков, которая препятствовала использованию
`pmap<>` и `pvector<>` внутри `PersistentAddressSpace`.

#### Проблема (до Phase 8)

Зависимость заголовков:
```
pmap.h → pvector.h → persist.h → pam.h
```
Включить `pmap.h` внутри `pam.h` было невозможно — это создало бы цикл.

#### Решение: разделение pam.h

| Файл | Содержимое |
|------|------------|
| `pam_core.h` | Полный API `PersistentAddressSpace` (Create, Delete, Find, Resolve, PtrToOffset, Save, ...) с реализацией на основе malloc-массивов (phase 7). Включается в `persist.h`. |
| `pam.h` | «Точка входа» — включает `pam_core.h` + `pmap.h`. Пользовательский код включает этот файл без изменений. |

**Новая цепочка включений (без цикла):**
```
pam_core.h ← persist.h ← pvector.h ← pmap.h ← pam.h
```

#### Изменения файлов

| Файл | Изменение |
|------|-----------|
| `pam_core.h` | **Новый файл.** Содержит полный `PersistentAddressSpace` + top-level типы: `type_info_entry`, `name_info_entry`, `slot_descriptor`, `pam_header` и все константы (`PAM_MAGIC`, `PAM_VERSION`, `PAM_INVALID_IDX`, ...). |
| `pam.h` | **Переработан:** теперь включает `pam_core.h` + `pmap.h`. Пользовательский API не изменился. |
| `persist.h` | **Изменён:** включает `pam_core.h` вместо `pam.h` (разрывает цикл). |

#### Состояние внутренних реализаций

Внутренние реестры `PersistentAddressSpace` сохраняют реализацию Phase 7
(malloc-массивы). Разделение на `pam_core.h` + `pam.h` закладывает основу для
дальнейшего рефакторинга (Phase 8.2–8.5):

| Фаза | Задача |
|------|--------|
| 8.2 ✓ | Замена `slot_descriptor[]` на `pmap<uintptr_t, SlotInfo>` внутри ПАП |
| 8.3 ✓ | Замена `name_info_entry[]` на `pmap<name_key, uintptr_t>` внутри ПАП |
| 8.4 | Замена `type_info_entry[]` на `pvector<TypeInfo>` внутри ПАП |
| 8.5 | Упрощение `pam_header` (офсеты встроены в данные ПАП) |

После завершения рефакторинга `PersistentAddressSpace` будет использовать
`pmap<>` и `pvector<>` внутри себя, что даст O(log n) вместо O(n) для
операций Find/GetName/GetCount/Delete.

---

### Изменения Phase 7 (сериализация pjson, issue #22)

Добавлены методы сериализации/десериализации в `pjson.h`:

| Метод | Описание |
|-------|----------|
| `std::string to_string() const` | Сериализация `pjson` → строка JSON (через nlohmann::json) |
| `static void from_string(const char* s, uintptr_t dst_offset)` | Десериализация строки JSON → `pjson` по смещению в ПАМ |

Метод `from_string` принимает смещение в ПАМ (не сырой указатель) для защиты от
инвалидации указателей при реаллокации буфера ПАМ.

Методы реализованы через внутренние вспомогательные функции:
- `_to_nlohmann()` — рекурсивная конвертация `pjson` → `nlohmann::json`
- `_from_nlohmann(src, dst_offset)` — рекурсивная конвертация `nlohmann::json` → `pjson`

### Изменения Phase 6 (рефакторинг pjson, issue #60)

`pjson` переработан для использования `pstring`, `pvector`, `pmap` и `pallocator`:

| Поле | До (Phase 5) | После (Phase 6) |
|------|-------------|----------------|
| `payload.string_val` | `struct { uintptr_t length; uintptr_t chars_slot; }` | `pstring` |
| `payload.array_val`  | `struct { uintptr_t size; uintptr_t data_slot; }` | pvector-совместимая раскладка (size + capacity + data) |
| `payload.object_val` | `struct { uintptr_t size; uintptr_t pairs_slot; }` | pvector-совместимая раскладка (size + capacity + data) |
| `pjson_kv_entry.key` | `uintptr_t key_length` + `fptr<char> key_chars` | `pstring key` |

Ключевые изменения:
- `string_val` теперь имеет тип `pstring` (идентичная раскладка, используются методы `pstring`)
- `array_val` и `object_val` хранят поле `capacity` (pvector-стратегия роста через удвоение)
- Ключи объекта хранятся как `pstring`, освобождение через `pstring::clear()`
- Освобождение ресурсов использует `pstring::clear()` вместо ручного `DeleteArray`
- `pallocator<T>` доступен для STL-совместимых контейнеров в персистном пространстве

### Изменения Phase 3

Поля размера и смещений в контейнерах приведены к типу `uintptr_t` для полной совместимости
с Phase 2 PAM API (`PersistentAddressSpace` использует `uintptr_t` для всех смещений):

| Тип | Поле | До (Phase 2) | После (Phase 3) |
|-----|------|-------------|----------------|
| `pstring` | `length` | `unsigned` | `uintptr_t` |
| `pvector<T>` | `size`, `capacity` | `unsigned` | `uintptr_t` |
| `pjson.payload` | `string_val.length`, `string_val.chars` | `unsigned` | `uintptr_t` |
| `pjson.payload` | `array_val.size`, `array_val.capacity`, `array_val.data` | `unsigned` | `uintptr_t` |
| `pjson.payload` | `object_val.size`, `object_val.capacity`, `object_val.data` | `unsigned` | `uintptr_t` |

### Изменения Phase 5 (таблица имён, issue #58)

#### Таблица имён (name_registry)

В Phase 4 имена объектов хранились непосредственно в `slot_descriptor` как `char name[64]`.
При N аллоцированных объектах (включая безымянные) каждый слот занимал лишние 64 байта.

В Phase 5 введена отдельная динамическая таблица имён (`name_info_entry[]`), где имя
объекта хранится **один раз** и только для **именованных** объектов. В `slot_descriptor`
поле `char name[PAM_NAME_SIZE]` (64 байта) заменено на `uintptr_t name_idx` (8 байт) —
индекс в таблицу имён (`PAM_INVALID_IDX` для безымянных). Экономия: **64 байта на каждый
безымянный слот** и **56 байт на каждый именованный слот**.

**Двусторонняя связь «имя ↔ слот»**:
- `name_info_entry.slot_idx` → индекс в `slot_descriptor` (поиск объекта по имени)
- `slot_descriptor.name_idx` → индекс в `name_info_entry` (получение имени по слоту)

**Уникальность имён**: ПАМ гарантирует, что каждое непустое имя встречается не более
одного раза. Попытка создать объект с уже существующим именем возвращает 0 (ошибка).

| Структура | Поля до (Phase 4) | Поля после (Phase 5) |
|-----------|-------------------|----------------------|
| `slot_descriptor` | `offset`, `count`, `type_idx`, `char name[64]` | `offset`, `count`, `type_idx`, `name_idx` |
| `name_info_entry` | *(новая)* | `used`, `slot_idx`, `name[64]` |
| `pam_header` | `magic`, `version`, `data_area_size`, `slot_count/cap`, `type_count/cap` | + `name_count`, `name_capacity` |

**Формат файла ПАМ (фаза 5, версия 4):**
```
[pam_header]                  — заголовок (добавлены name_count, name_capacity)
[type_info_entry * type_cap]  — таблица типов (один раз на уникальный тип)
[name_info_entry * name_cap]  — таблица имён (один раз на каждое имя + ссылка на слот)
[slot_descriptor * cap]       — таблица слотов (name_idx вместо char name[64])
[байты объектов]              — область данных
```

Новый метод `GetName(offset)` возвращает имя объекта по его смещению через таблицу имён.

---

### Изменения Phase 4 (code review `pam.h`, issue #58)

#### Таблица типов (type_registry)

В `slot_descriptor` фазы 3 каждый слот хранил копию `char type_id[64]` и `uintptr_t size`,
что тратило 72+ байт на каждый слот даже если тысячи слотов относятся к одному типу.

В Phase 4 введена отдельная таблица типов (`type_info_entry[]`), где имя типа и размер
элемента хранятся **один раз** для каждого уникального типа. В `slot_descriptor` остаётся
только `uintptr_t type_idx` — индекс в таблицу типов (экономия 64 байта на каждый слот).

| Структура | Поля до (Phase 3) | Поля после (Phase 4) |
|-----------|-------------------|----------------------|
| `slot_descriptor` | `offset`, `size`, `count`, `char type_id[64]`, `name[64]` | `offset`, `count`, `type_idx`, `name[64]` |
| `type_info_entry` | *(новая)* | `used`, `elem_size`, `name[64]` |
| `pam_header` | `magic`, `version`, `data_area_size`, `slot_count`, `slot_capacity` | + `type_count`, `type_capacity` |

**Формат файла ПАМ (фаза 4):**
```
[pam_header]                  — заголовок (добавлены type_count, type_capacity)
[type_info_entry * type_cap]  — таблица типов (один раз на уникальный тип)
[slot_descriptor * cap]       — таблица слотов (type_idx вместо char type_id[64])
[байты объектов]              — область данных
```

Новый метод `GetElemSize(offset)` возвращает размер элемента типа через таблицу типов.

#### Исправление: проверка границ в `ResolveElement`

В Phase 3 метод `ResolveElement<T>(offset, index)` не проверял, находится ли вычисленное
смещение в пределах области данных ПАМ. В Phase 4 добавлена проверка границ:
при выходе за пределы возвращается ссылка на первый байт буфера (вместо разыменования
за пределами выделенной памяти).

#### Уточнение документации слотов

В Phase 3 комментарий в `pam.h` допускал неоднозначность: слоты создаются для **всех**
аллоцированных объектов (не только именованных). В Phase 4 документация уточнена:
`slot_count` в заголовке считает только именованные слоты (`name[0] != '\0'`).

### Исправление: realloc-безопасность `pstring::assign()` (issue #56)

Метод `pstring::assign()` вызывает `chars.NewArray()`, который внутренне аллоцирует
память в буфере данных ПАМ через `realloc`. При создании большого числа объектов
`realloc` перемещает буфер, делая указатель `this` недействительным до окончания
работы метода.

Исправление: перед вызовом `NewArray()` сохраняем смещение `this` в ПАМ через
`PtrToOffset(this)`, а после возможного перемещения буфера — переприводим
указатель через `Resolve<pstring>(self_offset)`. Запись символов выполняется
через `Resolve<char>()` (offset-based, безопасен после realloc).

Это устраняет потенциальное переполнение на 64-битных платформах при размере ПАП > 4 ГБ.

---

## Файлы проекта

### `pam_core.h` — Ядро персистного адресного менеджера (ПАМ)

Содержит полный API `PersistentAddressSpace` (Create, Delete, Find, Resolve, Save, ...)
с реализацией таблицы слотов (фаза 8.2: `pmap<uintptr_t, SlotInfo>`) и карты имён
(фаза 8.3: `pmap<name_key, uintptr_t>`) внутри ПАП.
Включается в `persist.h`, `pstring.h`, `pvector.h`, `pmap.h` — без циклических зависимостей.

Определяет top-level типы и константы, используемые в тестах:
`type_info_entry`, `name_info_entry`, `SlotInfo`, `slot_entry`, `slot_descriptor`,
`name_key`, `name_entry`, `pam_header`,
`PAM_MAGIC`, `PAM_VERSION`, `PAM_INVALID_IDX`, `PAM_NAME_SIZE` и др.

### `pam.h` — Точка входа ПАМ (фаза 8.3)

«Точка входа» для пользовательского кода — включает `pam_core.h` + `pmap.h`.
Пользовательский API не изменился; `pam.h` устанавливает полную цепочку включений
без циклических зависимостей. Содержит `static_assert`, проверяющие совместимость
раскладки `slot_entry` с `pmap_entry<uintptr_t, SlotInfo>` (фаза 8.2) и
`name_entry` с `pmap_entry<name_key, uintptr_t>` (фаза 8.3).

Структура файла ПАМ (расширение .pam, версия 6, фаза 8.3):
```
[pam_header]                  — заголовок (магия, версия, размер области данных,
                                число типов, ёмкость таблицы типов,
                                смещение карты слотов, смещение карты имён)
[type_info_entry * type_cap]  — таблица типов (имя и размер элемента — по одной
                                записи на каждый уникальный тип)
[байты объектов]              — область данных (непрерывный байтовый пул)
  ├─ [uintptr_t size][uintptr_t capacity][uintptr_t entries_off]
  │                           — объект карты слотов (pmap-совместимая раскладка)
  ├─ [slot_entry * capacity]  — отсортированный массив {offset → SlotInfo}
  │                             (поиск O(log n) бинарным поиском)
  ├─ [uintptr_t size][uintptr_t capacity][uintptr_t entries_off]
  │                           — объект карты имён (pmap-совместимая раскладка, фаза 8.3)
  └─ [name_entry * capacity]  — отсортированный массив {name_key → slot_offset}
                                (поиск O(log n) бинарным поиском)
```

```cpp
// Инициализация ПАМ из файла (Тр.3)
PersistentAddressSpace::Init("myapp.pam");
auto& pam = PersistentAddressSpace::Get();

// Создание объекта с именем (Тр.2, Тр.14)
uintptr_t off = pam.Create<int>("счётчик");
*pam.Resolve<int>(off) = 42;

// Поиск по имени (Тр.15)
uintptr_t found = pam.Find("счётчик");

// Получение размера элемента через таблицу типов (фаза 4)
uintptr_t elem_size = pam.GetElemSize(off);  // == sizeof(int)

// Получение имени объекта по смещению (фаза 5, двусторонняя связь)
const char* name = pam.GetName(off);  // == "счётчик"

// Сохранение ПАП
pam.Save();
```

**Ключевые типы:**
- `type_info_entry` — запись таблицы типов (имя типа + размер элемента, хранится один раз)
- `name_info_entry` — запись таблицы имён (совместимость с тестами фаз 5–8.2)
- `SlotInfo` — значение карты слотов (число элементов, индекс типа, индекс имени; фаза 8.2)
- `slot_entry` — запись карты слотов (`{uintptr_t key, SlotInfo value}`; совместима с `pmap_entry`; фаза 8.2)
- `slot_descriptor` — POD-дескриптор (совместимость с тестами, не используется в карте фазы 8.2+)
- `name_key` — ключ карты имён: `char name[PAM_NAME_SIZE]` (тривиально копируемый; фаза 8.3)
- `name_entry` — запись карты имён (`{name_key key, uintptr_t slot_offset}`; совместима с `pmap_entry`; фаза 8.3)
- `pam_header` — заголовок файла ПАМ (фаза 8.3: добавлены `slot_map_offset`/`name_map_offset`, версия = 6)

---

### `persist.h` — Персистные примитивы

#### `persist<T>` — Обёртка для тривиально копируемого типа (Тр.7, Тр.8)

```cpp
static_assert(sizeof(persist<int>) == sizeof(int));   // Тр.8
```

Объекты `persist<T>` создаются только в ПАП, не на стеке (Тр.11).

#### `fptr<T>` — Персистный указатель (Тр.5, Тр.9, Тр.15)

Хранит смещение объекта в ПАП (`uintptr_t`).

```cpp
static_assert(sizeof(fptr<int>) == sizeof(void*));    // Тр.5

fptr<int> p;
p.New("мой_объект");           // создать в ПАП (Тр.2)
*p = 100;                      // запись
p.find("мой_объект");          // поиск по имени (Тр.15)
p.Delete();                    // удалить

// Строковая инициализация fptr: автоматический поиск по слотам ПАМ (Тр.15)
fptr<int> q("мой_объект");    // сразу инициализируется по имени

fptr<int> arr;
arr.NewArray(10);              // массив из 10 элементов
arr[0] = 42;
arr.DeleteArray();
```

#### `AddressManager<T>` — Тонкий адаптер (совместимость с фазой 1)

Делегирует все операции в `PersistentAddressSpace` (Тр.4):

```cpp
uintptr_t off = AddressManager<double>::Create("число");
AddressManager<double>::GetObject(off) = 3.14;
AddressManager<double>::Delete(off);
```

---

### `pstring.h` — Персистная строка

Хранит символы в ПАП через `fptr<char>`.

```cpp
pstring_data sd{};
pstring ps(sd);
ps.assign("привет");
std::cout << ps.c_str() << "\n";  // "привет"
ps.clear();
```

---

### `pvector.h` — Персистный динамический массив

Хранит элементы в ПАП. Стратегия роста: удвоение ёмкости.

```cpp
pvector_data<int> vd{};
pvector<int> v(vd);
v.push_back(1);
v.push_back(2);
std::cout << v[0] << "\n";  // 1
v.free();
```

---

### `pmap.h` — Персистный ключ-значение контейнер

Хранит пары (ключ, значение) в отсортированном массиве в ПАП.
Поиск — O(log n) бинарным поиском.

```cpp
pmap_data<int, double> md{};
pmap<int, double> m(md);
m.insert(1, 3.14);
double* v = m.find(1);   // &3.14
m.erase(1);
```

---

### `pallocator.h` — Персистный STL-аллокатор

STL-совместимый аллокатор, использующий ПАП.

```cpp
std::vector<int, pallocator<int>> v;
v.push_back(42);
```

---

### `pjson.h` — Персистный JSON

Персистная дискриминантная структура — аналог `nlohmann::json`.
Использует `pstring`, `pvector`, `pmap` и `pallocator` для хранения данных в ПАП.

Поддерживаемые типы значений:
- `null`, `boolean`, `integer`, `uinteger`, `real`
- `string` — через `pstring` (length + chars в ПАП)
- `array` — через `pvector<pjson>`-совместимую раскладку (size + capacity + data в ПАП)
- `object` — отсортированный `pvector<pjson_kv_entry>`, ключи хранятся как `pstring`

Структура данных `pjson`:
- `payload.string_val` — тип `pstring` (2 × `sizeof(void*)`)
- `payload.array_val`  — pvector-совместимая раскладка (3 × `sizeof(void*)`)
- `payload.object_val` — pvector-совместимая раскладка (3 × `sizeof(void*)`)

Структура `pjson_kv_entry`:
- `key`   — тип `pstring` (ключ как персистная строка)
- `value` — тип `pjson` (значение)

```cpp
fptr<pjson> fv;
fv.New();

fv->set_object();
fv->obj_insert("имя").set_string("Alice");
fv->obj_insert("возраст").set_int(30);

pjson* name = fv->obj_find("имя");
std::cout << name->get_string() << "\n";  // Alice

// Сериализация в строку JSON (фаза 7)
std::string json_str = fv->to_string();
// {"возраст":30,"имя":"Alice"}

fv->free();
fv.Delete();

// Десериализация из строки JSON (фаза 7)
fptr<pjson> fv2;
fv2.New();
pjson::from_string("{\"x\":1,\"arr\":[1,2,3]}", fv2.addr());
std::cout << fv2->to_string() << "\n";  // {"arr":[1,2,3],"x":1}
fv2->free();
fv2.Delete();
```

---

### `PageDevice.h`, `StaticPageDevice.h` — Страничное устройство

Инфраструктура кэширования страниц:

- `Cache<T, CacheSize, SpaceSize>` — кэш страниц с вытеснением
- `PageDevice<PageSize, PoolSize, SpaceSize, CachePolicy>` — абстрактное страничное устройство
- `MemoryDevice<...>` — чтение/запись диапазонов байт через страницы
- `StaticPageDevice<...>` — реализация с хранением в оперативной памяти

---

## Сборка

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Требования: компилятор с поддержкой C++17.

---

## Тесты

```
tests/
  test_pam.cpp           — тесты PersistentAddressSpace (ПАМ), таблица типов (фаза 4), таблица имён (фаза 5), карта слотов (фаза 8.2), карта имён (фаза 8.3)
  test_pam_dynamic.cpp   — тесты динамичности ПАМ: массовое создание именованных объектов
  test_persist.cpp       — тесты persist<T>, fptr<T>, AddressManager<T>
  test_pstring.cpp       — тесты pstring
  test_pvector.cpp       — тесты pvector<T>
  test_pmap.cpp          — тесты pmap<K,V>
  test_pallocator.cpp    — тесты pallocator<T>
  test_pjson.cpp         — тесты pjson
  test_pjson_large.cpp   — тесты загрузки большого JSON (round-trip через nlohmann)
  test_pjson_serial.cpp  — тесты to_string() и from_string() (фаза 7)
```

Тестовый фреймворк: [Catch2 v3](https://github.com/catchorg/Catch2).

### Тест динамичности ПАМ (`test_pam_dynamic.cpp`)

Проверяет динамическое расширение таблицы слотов и области данных ПАМ при
создании большого числа именованных объектов:

| Тест | Описание | Тег |
|------|----------|-----|
| `PAM dynamic: create and verify 10000 named pstrings` | 10 000 `pstring` с уникальными именами и содержимым | `[pam][dynamic]` |
| `PAM dynamic: pstring content is unique for each named entry` | Уникальность содержимого для 1 000 записей | `[pam][dynamic][unique]` |
| `PAM dynamic: slot table grows beyond initial capacity` | Расширение таблицы слотов за пределы начальной ёмкости (16) | `[pam][dynamic][slots]` |
| `PAM dynamic stress: create and verify 1 million named pstrings` | Нагрузочный тест: 1 000 000 `pstring` | `[pam][dynamic][stress]` |

Нагрузочный тест (тег `[stress]`) не запускается при обычном `ctest`.
Для ручного запуска:

```bash
./build/tests/tests "[stress]"
```

Стратегия верификации:
1. Создать N `pstring` с уникальными именами-слотами ПАМ (формат `sNNNNNN`).
2. В каждую `pstring` записать её же имя в качестве содержимого.
3. Найти каждую `pstring` по имени через `PAM.Find()` и проверить содержимое.
4. Для нагрузочного теста (1 млн) верификация использует сохранённые смещения
   (offset-based) вместо линейного `Find()` на каждой итерации.

---

## Лицензия

Unlicense — общественное достояние. Подробности в файле `LICENSE`.
