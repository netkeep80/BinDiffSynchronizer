# BinDiffSynchronizer — Персистная инфраструктура

Библиотека на C++17 для построения персистных структур данных на основе единого персистного адресного пространства (ПАП).

---

## Обзор

Проект реализует персистную инфраструктуру, позволяющую хранить объекты произвольных типов в едином бинарном файле-образе и восстанавливать их между перезапусками без вызова конструкторов.

Конечная цель — реализация `pjson`, персистного аналога `nlohmann::json`.

---

## Архитектура (фаза 5)

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

### `pam.h` — Персистный адресный менеджер (ПАМ)

Реализует единое ПАП через класс `PersistentAddressSpace`.

Структура файла ПАМ (расширение .pam, версия 4, фаза 5):
```
[pam_header]                  — заголовок (магия, версия, размер области данных,
                                число именованных слотов, ёмкость слотов,
                                число типов, ёмкость таблицы типов,
                                число имён, ёмкость таблицы имён)
[type_info_entry * type_cap]  — таблица типов (имя и размер элемента — по одной
                                записи на каждый уникальный тип)
[name_info_entry * name_cap]  — таблица имён (имя объекта и ссылка на слот — по
                                одной записи на каждое именованное объект)
[slot_descriptor * cap]       — динамическая таблица дескрипторов объектов
                                (name_idx вместо char name[64])
[байты объектов]              — область данных (непрерывный байтовый пул)
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
- `name_info_entry` — запись таблицы имён (имя объекта + обратная ссылка на слот; уникальность)
- `slot_descriptor` — дескриптор объекта (смещение, число элементов, индекс типа, индекс имени)
- `pam_header` — заголовок файла ПАМ (фаза 5: добавлены поля `name_count`, `name_capacity`)

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

fv->free();
fv.Delete();
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
  test_pam.cpp          — тесты PersistentAddressSpace (ПАМ), таблица типов (фаза 4), таблица имён (фаза 5)
  test_pam_dynamic.cpp  — тесты динамичности ПАМ: массовое создание именованных объектов
  test_persist.cpp      — тесты persist<T>, fptr<T>, AddressManager<T>
  test_pstring.cpp      — тесты pstring
  test_pvector.cpp      — тесты pvector<T>
  test_pmap.cpp         — тесты pmap<K,V>
  test_pallocator.cpp   — тесты pallocator<T>
  test_pjson.cpp        — тесты pjson
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
