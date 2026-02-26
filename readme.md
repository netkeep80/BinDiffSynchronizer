# BinDiffSynchronizer — Персистная инфраструктура

Библиотека на C++17 для построения персистных структур данных на основе единого персистного адресного пространства (ПАП).

---

## Обзор

Проект реализует персистную инфраструктуру, позволяющую хранить объекты произвольных типов в едином бинарном файле-образе и восстанавливать их между перезапусками без вызова конструкторов.

Конечная цель — реализация `pjson`, персистного аналога `nlohmann::json`.

---

## Архитектура (фаза 2)

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

---

## Файлы проекта

### `pam.h` — Персистный адресный менеджер (ПАМ)

Реализует единое ПАП через класс `PersistentAddressSpace`.

Структура файла ПАП:
```
[pap_header]          — заголовок (магия, версия, размер области данных)
[slot_descriptor * N] — таблица дескрипторов объектов
[байты объектов]      — область данных (непрерывный байтовый пул)
```

```cpp
// Инициализация ПАМ из файла (Тр.3)
PersistentAddressSpace::Init("myapp.pap");
auto& pam = PersistentAddressSpace::Get();

// Создание объекта с именем (Тр.2, Тр.14)
uintptr_t off = pam.Create<int>("счётчик");
*pam.Resolve<int>(off) = 42;

// Поиск по имени (Тр.15)
uintptr_t found = pam.Find("счётчик");

// Сохранение ПАП
pam.Save();
```

**Ключевые типы:**
- `slot_descriptor` — дескриптор объекта (смещение, размер, тип, имя)
- `pap_header` — заголовок файла ПАП

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

Поддерживаемые типы значений:
- `null`, `boolean`, `integer`, `uinteger`, `real`
- `string` — через `fptr<char>`
- `array` — через `fptr<pjson_data>`
- `object` — отсортированный массив `pjson_kv_pair`

```cpp
pjson_data d{};
pjson v(d);

v.set_object();
pjson(v.obj_insert("имя")).set_string("Alice");
pjson(v.obj_insert("возраст")).set_int(30);

pjson_data* name = v.obj_find("имя");
std::cout << pjson(*name).get_string() << "\n";  // Alice

v.free();
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
  test_pam.cpp          — тесты PersistentAddressSpace (ПАМ)
  test_persist.cpp      — тесты persist<T>, fptr<T>, AddressManager<T>
  test_pstring.cpp      — тесты pstring
  test_pvector.cpp      — тесты pvector<T>
  test_pmap.cpp         — тесты pmap<K,V>
  test_pallocator.cpp   — тесты pallocator<T>
  test_pjson.cpp        — тесты pjson
```

Тестовый фреймворк: [Catch2 v3](https://github.com/catchorg/Catch2).

---

## Лицензия

Unlicense — общественное достояние. Подробности в файле `LICENSE`.
