# Phase 3 Plan: Завершение персистной обвязки и интеграция с nlohmann::json

**Status:** Tasks 3.1, 3.2, 3.3, 3.4, 3.5, and 3.6 Done ✓

---

## Цель

Данная фаза решает три взаимосвязанные задачи, которые составляют фундамент для любой дальнейшей работы над jgit:

1. **Доработать, задокументировать и протестировать** всю персистную инфраструктуру: классы `fptr`, `AddressManager`, `persist`, `Cache`, `Page`, `PageDevice`, `MemoryDevice`, `StaticPageDevice`.
2. **Переписать** классы `persistent_string`, `persistent_map`, `persistent_json_value`, `PersistentJsonStore` с использованием `persist<_T>` и `fptr<_T>` из `persist.h` (вместо `std::vector` / сырых пулов) и написать тесты для всех инстанций.
3. **Переписать инстанцирование** `using json = basic_json<>` на использование новых персистных классов; проверить корректную работоспособность `nlohmann::json` с ними; написать тесты для `persist<json>` и `fptr<json>`.

---

## Контекст

После завершения Фаз 1 и 2 проект имеет:

- Рабочее содержимо-адресное объектное хранилище (`ObjectStore`).
- Набор вспомогательных классов (`persistent_string`, `persistent_map`, `persistent_json_value`, `PersistentJsonStore`), реализованных через `std::vector`-пулы в памяти — **без использования** `persist<_T>` и `fptr<_T>` из `persist.h`.
- Оригинальную персистную инфраструктуру (`persist.h`, `PageDevice.h`, `StaticPageDevice.h`), которая:
  - содержит задокументированные баги (часть исправлена в Фазе 1);
  - не имеет полноценной документации и тестов на граничные случаи;
  - никогда не использовалась совместно с классами из `jgit/`.

Задача Фазы 3 — закрыть этот разрыв: сначала привести персистную инфраструктуру в рабочее, документированное и протестированное состояние, затем переписать на её основе классы из `jgit/`.

---

## Фаза 3 Задачи

---

### Задача 3.1 — Доработка, документация и тестирование персистной инфраструктуры

**Цель:** Привести классы `persist<_T>`, `fptr<_T>`, `AddressManager<_T>`, `Cache`, `Page`, `PageDevice`, `MemoryDevice`, `StaticPageDevice` в полностью рабочее, задокументированное и протестированное состояние.

---

#### 3.1.1 — Доработка `persist<_T>` (`persist.h`)

**Проблемы (выявлены в Фазе 1, частично исправлены):**

| # | Файл / строка | Проблема | Действие |
|---|---------------|----------|----------|
| 1 | `persist.h`, конструктор `persist(const _T& ref)` | Вызывается `new((void*)_data) _T(ref)`, но при последующем уничтожении объекта деструктор вызывается корректно; однако при загрузке (`persist()`) вызывается `new((void*)_data) _T` без последующей загрузки данных из файла — данные перезаписываются из файла сразу после, что корректно лишь для тривиально конструируемых типов | Добавить `static_assert(std::is_trivially_copyable_v<_T>)` и явно задокументировать ограничение |
| 2 | `persist.h`, `~persist()` | Запись в файл по адресу, производному от `this` — при перемещении объекта в памяти имя файла изменится и старый файл не будет перезаписан | Задокументировать ограничение (объект не должен перемещаться); добавить unit-тест, проверяющий корректное сохранение/загрузку |
| 3 | `persist.h`, `get_name()` | Имя файла строится из hex-адреса `this` — при каждом запуске процесса имя разное (ASLR), нет детерминизма | Добавить перегрузку конструктора с явным именем файла; описать в комментарии |
| 4 | `persist.h`, `operator _Tref() const` | Дублирование `const` и неконстантного оператора приведения | Проверить корректность; при необходимости исправить |

**Задачи:**
- Добавить `static_assert(std::is_trivially_copyable_v<_T>)` в тело класса `persist<_T>` — чтобы компилятор сразу сообщал об использовании с несовместимыми типами.
- Добавить конструктор `persist(const char* filename)` / `persist(const std::string& filename)` для детерминированного именования.
- Написать тесты (файл: `tests/test_persist_core.cpp`):
  1. Сохранение и загрузка `persist<int>` через перезапуск (создать, записать, уничтожить, создать с тем же именем, проверить значение).
  2. Сохранение и загрузка `persist<double>`.
  3. Сохранение и загрузка `persist<T>` для пользовательской POD-структуры.
  4. `static_assert` срабатывает для нетривиально копируемых типов (отрицательный тест, проверяется через `std::is_trivially_copyable`).
  5. Корректная работа `operator=` и `operator _Tref`.
  6. Размер файла равен `sizeof(_T)`.

---

#### 3.1.2 — Доработка `fptr<_T>` (`persist.h`)

**Проблемы:**

| # | Файл / строка | Проблема | Действие |
|---|---------------|----------|----------|
| 1 | `fptr<_T>::operator=` | Не возвращает `*this` (исправлено в Фазе 1) | Убедиться, что исправление присутствует |
| 2 | `fptr<_T>` конструктор копирования | `fptr(fptr<_T>& ptr) : __addr(ptr->__addr)` — разыменовывает `ptr` через `operator->()` вместо прямого обращения к полю | Исправить на `__addr(ptr.__addr)` |
| 3 | `fptr<_T>::~fptr()` | Вызывает `Release(__addr)` — но `Release` только декрементирует `__refs`, не освобождая объект; если `__refs` становится 0, объект не удаляется | Задокументировать семантику; добавить освобождение или явно описать, что удаление производится через `AddressManager` |
| 4 | `fptr<_T>` | Нет метода `fptr<_T>::Delete()` для явного удаления персистного объекта | Добавить метод `Delete()` и документацию |

**Задачи:**
- Исправить конструктор копирования.
- Добавить метод `Delete()`.
- Написать тесты (файл: `tests/test_fptr.cpp`):
  1. `fptr::New()` создаёт новый объект, `operator->()` возвращает корректный указатель.
  2. Присваивание `fptr = fptr` корректно (конструктор копирования).
  3. Оператор `operator*()` возвращает ссылку на объект.
  4. `fptr` по имени (`Find`) находит существующий объект.
  5. После `Delete()` объект больше не доступен.
  6. Счётчик ссылок (`__refs`) корректно инкрементируется/декрементируется.

---

#### 3.1.3 — Доработка `AddressManager<_T>` (`persist.h`)

**Проблемы:**

| # | Файл / строка | Проблема | Действие |
|---|---------------|----------|----------|
| 1 | `AddressManager::Create` | Результат `Find(__faddress)` не присваивается `addr` — баг, найденный в Фазе 1 | Исправить: `addr = Find(__faddress)` |
| 2 | `AddressManager::__itable` | Хранит `persist<__info[ADDRESS_SPACE]>` — массив структур; каждая структура содержит `_T*` (указатель — не персистен!) | Заменить `_T*` на индекс в пуле или на именованный персистный объект |
| 3 | `ADDRESS_SPACE = 1024` | Фиксированный размер; нет динамического роста | Сделать шаблонным параметром: `template<class _T, unsigned AddressSpace = 1024>` |
| 4 | `get_fname()` | Возвращает `static char` — не потокобезопасно | Заменить на `std::string` |
| 5 | `__load__obj` | `new((void*)ptr) _T` — placement new без инициализации из файла; данные читаются до конструктора | Обеспечить правильный порядок: выделить память, загрузить данные, затем вызвать placement new |

**Задачи:**
- Исправить все задокументированные баги.
- Добавить шаблонный параметр `AddressSpace`.
- Написать тесты (файл: `tests/test_address_manager.cpp`):
  1. `Create` возвращает корректный индекс ≥ 1.
  2. `Find` по имени находит созданный объект.
  3. Создание до `ADDRESS_SPACE - 1` объектов не переполняет пространство.
  4. Попытка создать `ADDRESS_SPACE`-й объект возвращает 0 (или бросает исключение).
  5. После `Release` объект недоступен.
  6. Персистность: объект сохраняется и загружается при перезапуске менеджера.

---

#### 3.1.4 — Доработка `Cache` (`PageDevice.h`)

**Проблемы:**

| # | Строка | Проблема | Действие |
|---|--------|----------|----------|
| 1 | `Cache::GetData` | Если `GetData` вызывается с `Index >= VMap.size()` — неопределённое поведение | Добавить проверку границ и обработку ошибки |
| 2 | `Cache::~Cache` | Деструктор не сбрасывает грязные страницы | Добавить проход по `Pool` и сохранение грязных страниц |
| 3 | `Cache` | Нет метода `Flush()` для принудительного сброса | Добавить `Flush()` |
| 4 | `Cache` | Алгоритм замещения: циклическое FIFO (не LRU) | Задокументировать выбор алгоритма; при необходимости заменить на LRU |

**Задачи:**
- Добавить `Flush()` и вызвать его из деструктора.
- Добавить проверки границ.
- Написать тесты (файл: `tests/test_cache.cpp`):
  1. `GetData` для нового индекса вызывает `Load`.
  2. `GetData` для кэшированного индекса не вызывает `Load` повторно.
  3. При `ForWrite = true` страница помечается грязной.
  4. При вытеснении грязной страницы вызывается `Save`.
  5. При вытеснении чистой страницы `Save` не вызывается.
  6. `Flush()` сохраняет все грязные страницы.
  7. Деструктор сохраняет грязные страницы (через `Flush`).
  8. Заполнение кэша до `CacheSize` страниц — все кэшируются, замещения нет.
  9. Замещение при переполнении (CacheSize + 1 страниц).

---

#### 3.1.5 — Доработка `Page`, `PageDevice`, `MemoryDevice` (`PageDevice.h`)

**Проблемы:**

| # | Файл / строка | Проблема | Действие |
|---|---------------|----------|----------|
| 1 | `MemoryDevice::Read/Write`, строки ~160, ~182 | `unsigned Index = (Address & PageMask) > PageSize;` — оператор `>` вместо `>>` (исправлено в Фазе 1) | Убедиться, что исправление присутствует; добавить тест |
| 2 | `MemoryDevice::Read` | Условие `while(PagePtr = ...)` — оператор присваивания в условии; если страница не загружена (`GetData` вернул `NULL`), цикл завершается без ошибки | Вернуть `false` при `NULL`, переработать условие |
| 3 | `MemoryDevice` | Нет `Read`/`Write` для типизированных объектов (`ReadObject<T>`, `WriteObject<T>`) | Добавить шаблонные методы |
| 4 | `PageDevice` | `virtual ~PageDevice()` — пустой деструктор; не вызывает `Flush` кэша | Добавить `Flush` при уничтожении |

**Задачи:**
- Добавить `ReadObject<T>` / `WriteObject<T>`.
- Переработать условие цикла в `Read`/`Write`.
- Написать тесты (файл: `tests/test_memory_device.cpp`):
  1. Запись одного байта по адресу 0 и чтение обратно.
  2. Запись блока данных, пересекающего границу страниц, и чтение обратно.
  3. `WriteObject<T>` / `ReadObject<T>` для POD-типа.
  4. Чтение за пределами адресного пространства возвращает `false`.
  5. Несколько последовательных записей в разные страницы сохраняются корректно.

---

#### 3.1.6 — Доработка `StaticPageDevice` (`StaticPageDevice.h`)

**Проблемы:**

| # | Строка | Проблема | Действие |
|---|--------|----------|----------|
| 1 | `StaticPageDevice` | Использует `__PageCount` без квалификации — зависимость от имени в базовом шаблоне | Заменить на `PageDevice<PageSize, PoolSize, SpaceSize, CachePolicy>::__PageCount` |
| 2 | `StaticPageDevice` | Нет тестов | Написать тесты |

**Задачи:**
- Исправить `__PageCount`.
- Написать тесты (файл: `tests/test_static_page_device.cpp`):
  1. Запись и чтение одной страницы.
  2. Запись нескольких страниц и чтение в обратном порядке.
  3. `Load`/`Save` при выходе за пределы массива возвращают `false`.
  4. Инициализация нулями — все страницы нулевые после конструктора.

---

### Задача 3.2 — Переписать `persistent_string`, `persistent_map`, `persistent_json_value`, `PersistentJsonStore` на `persist<_T>` и `fptr<_T>`

**Цель:** Заменить `std::vector`-пулы и сырые арифметические индексы на использование `persist<_T>` и `fptr<_T>` из `persist.h`, так чтобы состояние хранилища автоматически персистировалось на диск без явных вызовов сериализации.

---

#### 3.2.1 — `persistent_string` на `persist<_T>`

Текущая реализация (`jgit/persistent_string.h`) уже имеет нужные свойства:
- `sizeof(persistent_string)` — константа времени компиляции.
- `std::is_trivially_copyable<persistent_string>` — `true`.

**Задача:** Убедиться, что `persist<persistent_string>` компилируется и корректно работает. Написать тесты:

Файл: `tests/test_persist_persistent_string.cpp`
1. `persist<persistent_string>` — создание, присваивание, уничтожение, загрузка из файла.
2. Корректная персистность коротких строк (SSO-путь, ≤ 23 символа).
3. Корректная персистность длинных строк (LONG_BUF-путь, > 23 символа).
4. Сравнение загруженной строки с исходной через `operator==`.
5. Пустая строка сохраняется и загружается корректно.

---

#### 3.2.2 — `persistent_map<V, Capacity>` на `persist<_T>` и `fptr<_T>`

Текущая реализация использует сырой `next_node_id: uint32_t` для цепочки overflow-слябов.

**Цель:** Переписать управление цепочкой слябов с использованием `fptr<persistent_map<V, Capacity>>`:

```cpp
// Вместо:
uint32_t next_node_id;  // raw index

// Использовать:
fptr<persistent_map<V, Capacity>> next_node;  // персистный указатель
```

Каждый сляб хранится через `persist<persistent_map<V, Capacity>>` или управляется `AddressManager<persistent_map<V, Capacity>>`.

**Требования к тривиальной копируемости:** `fptr<T>` хранит только `unsigned __addr` — тривиально копируем. Убедиться, что обновлённая структура сохраняет `std::is_trivially_copyable`.

Файл: `tests/test_persist_persistent_map.cpp`
1. `persist<persistent_map<int32_t>>` — сохранение и загрузка.
2. Вставка элементов, уничтожение, загрузка — проверка что все элементы присутствуют.
3. Overflow-цепочка через `fptr`: вставка > `Capacity` элементов; загрузка всей цепочки.
4. `erase` элемента, персистность после erase.
5. Итерирование по элементам сохранённого/загруженного сляба.

---

#### 3.2.3 — `persistent_json_value` на `persist<_T>`

Текущая реализация уже тривиально копируема. `persist<persistent_json_value>` должен компилироваться без изменений.

Файл: `tests/test_persist_persistent_json_value.cpp`
1. `persist<persistent_json_value>` для null-значения.
2. `persist<persistent_json_value>` для boolean.
3. `persist<persistent_json_value>` для integer.
4. `persist<persistent_json_value>` для float.
5. `persist<persistent_json_value>` для string (SSO-путь).
6. `persist<persistent_json_value>` для string (LONG_BUF-путь).
7. `persist<persistent_json_value>` для array (сохраняется только `array_id`).
8. `persist<persistent_json_value>` для object (сохраняется только `object_id`).

---

#### 3.2.4 — `PersistentJsonStore` на `persist<_T>` и `fptr<_T>`

Текущая реализация (`jgit/persistent_json_store.h`) использует `std::vector<persistent_json_value>` и аналогичные пулы в оперативной памяти. Данные теряются при уничтожении объекта.

**Цель:** Переписать `PersistentJsonStore` так, чтобы:
- `value_pool_` стал `AddressManager<persistent_json_value>` (или `persist<persistent_json_value[N]>`).
- `array_pool_` стал `AddressManager<json_array_slab>`.
- `object_pool_` стал `AddressManager<json_object_slab>`.
- Метод `import_json` по-прежнему работает с `nlohmann::json`.
- Метод `export_json` по-прежнему возвращает `nlohmann::json`.
- Данные автоматически сохраняются на диск при уничтожении `PersistentJsonStore` и загружаются при создании.

**Новый интерфейс:**
```cpp
class PersistentJsonStore {
public:
    // Открыть или создать хранилище по пути.
    explicit PersistentJsonStore(const std::filesystem::path& base_dir);
    ~PersistentJsonStore();  // автоматически сохраняет состояние

    uint32_t import_json(const nlohmann::json& doc);
    nlohmann::json export_json(uint32_t root_id) const;

    // ... остальное API без изменений
};
```

Файл: `tests/test_persistent_json_store_v2.cpp`
1. Создание пустого `PersistentJsonStore` — хранилище инициализируется.
2. `import_json` + `export_json` round-trip для простого объекта.
3. Уничтожение и повторное создание хранилища — ранее импортированные данные доступны.
4. Сложный вложенный JSON (объект внутри массива) переживает перезапуск.
5. Несколько хранилищ в разных директориях независимы.
6. `snapshot()` + `restore()` через `ObjectStore` работают корректно с новым хранилищем.

---

### Задача 3.3 — Переписать инстанцирование `basic_json<>` на персистные классы

**Цель:** Инстанцировать `nlohmann::basic_json<>` (или специализацию `basic_json`) с заменой внутренних аллокаторов/контейнеров на персистные аналоги из `jgit/`. Проверить корректность работы `nlohmann::json` с ними. Написать тесты для `persist<json>` и `fptr<json>`.

---

#### 3.3.1 — Исследование: как `basic_json<>` параметризуется

`nlohmann::basic_json` принимает следующие шаблонные параметры:

```cpp
template<
    template<typename U, typename V, typename... Args> class ObjectType = std::map,
    template<typename U, typename... Args>             class ArrayType  = std::vector,
    class StringType      = std::string,
    class BooleanType     = bool,
    class NumberIntegerType   = std::int64_t,
    class NumberUnsignedType  = std::uint64_t,
    class NumberFloatType     = double,
    template<typename U>  class AllocatorType = std::allocator,
    template<typename T, typename SFINAE = void> class JSONSerializer = nlohmann::adl_serializer,
    class BinaryType = std::vector<std::uint8_t>
>
class basic_json;
```

Для замены используются:
- `ObjectType` → шаблон совместимый с `persistent_map` (или std::map с кастомным аллокатором)
- `ArrayType`  → шаблон совместимый с `persistent_array`
- `StringType` → `jgit::persistent_string`

**Задача 3.3.1:** Изучить требования `basic_json` к каждому параметру. Задокументировать минимально необходимый интерфейс для замены `StringType`, `ObjectType`, `ArrayType`.

Файл: `experiments/study_basic_json_params.cpp` — эксперименты с параметрами `basic_json`.

---

#### 3.3.2 — Адаптация `persistent_string` для `StringType`

`nlohmann::basic_json` требует от `StringType`:
- `std::string`-совместимый интерфейс: `c_str()`, `size()`, `empty()`, `operator=`, `operator==`, итераторы.
- Должен быть `DefaultConstructible`, `CopyConstructible`, `MoveConstructible`.

**Задача:** Расширить `jgit::persistent_string` до уровня `StringType`:
- Добавить итераторы (`begin()`, `end()`).
- Добавить `append()`, `operator+=`, `operator+`.
- Добавить конструктор от `std::initializer_list<char>` при необходимости.
- Добавить `substr()`, `find()` при необходимости.

**Ограничение:** Сохранить `std::is_trivially_copyable` нельзя при добавлении нетривиальных методов — но это допустимо для `StringType`, поскольку `basic_json` не хранит строки через `persist<T>` напрямую.

Файл: `tests/test_persistent_string_extended.cpp`
1. `persistent_string` как `StringType` в `basic_json<>`: создание объекта `json`.
2. Присваивание строки `json["key"] = "value"`.
3. Чтение строки `json["key"].get<std::string>()`.
4. Итерирование по ключам объекта.

---

#### 3.3.3 — Объявление `persistent_json` как инстанции `basic_json`

```cpp
// jgit/persistent_basic_json.h

#include <nlohmann/json.hpp>
#include "persistent_string.h"
#include "persistent_map.h"
#include "persistent_array.h"

namespace jgit {

// Инстанция nlohmann::basic_json с персистными строками.
// ObjectType и ArrayType остаются std::map/std::vector на первом этапе,
// затем заменяются персистными аналогами (Task 3.3.4).
using persistent_json = nlohmann::basic_json<
    std::map,                       // ObjectType (std::map — первый этап)
    std::vector,                    // ArrayType  (std::vector — первый этап)
    jgit::persistent_string         // StringType — персистная строка
>;

} // namespace jgit
```

Файл: `jgit/persistent_basic_json.h`

---

#### 3.3.4 — Замена `ObjectType` и `ArrayType`

После успешной интеграции `persistent_string` как `StringType`, заменить контейнеры:

- `ObjectType` → адаптер поверх `persistent_map`, совместимый с интерфейсом `std::map`.
- `ArrayType`  → адаптер поверх `persistent_array`, совместимый с интерфейсом `std::vector`.

Адаптеры должны удовлетворять требованиям `Container` из стандарта C++ (итераторы, `begin/end`, `insert`, `erase` и т.д.).

**Примечание:** Это наиболее трудоёмкий шаг. Реализация адаптеров может потребовать значительного объёма кода для совместимости с `basic_json`. Рекомендуется начать с минимального API и расширять по мере прохождения тестов.

---

#### 3.3.5 — Тесты для `persist<json>` и `fptr<json>`

После успешной компиляции `jgit::persistent_json`:

Файл: `tests/test_persist_json.cpp`

**`persist<json>` тесты:**
1. `persist<jgit::persistent_json>` компилируется (если `persistent_json` тривиально копируема).
2. Если `persistent_json` нетривиально копируема — описать альтернативный способ персистности (через `snapshot`/`restore` из `ObjectStore`).
3. Создание `persistent_json` объекта из JSON-литерала, сохранение и загрузка.
4. Создание `persistent_json` массива из JSON-литерала, сохранение и загрузка.

**`fptr<json>` тесты:**
5. `fptr<jgit::persistent_json>` — `New()`, запись поля, `Delete()`.
6. `fptr<jgit::persistent_json>` — нахождение объекта по имени после перезапуска `AddressManager`.
7. Несколько `fptr<persistent_json>` ссылаются на разные объекты одновременно.
8. Операции `import_json`/`export_json` работают с `fptr`-управляемым объектом.

---

### Задача 3.4 — Поддержка массивов в `fptr<T>`, `AddressManager<T>`, `persistent_string`, `persistent_array` ✓

**Цель:** Реализовать поддержку динамических персистных массивов в инфраструктуре `fptr<T>` и `AddressManager<T>`. Переписать `persistent_string` с использованием `fptr<char>` вместо статического буфера 65 КБ.

**Реализовано согласно отзыву владельца репозитория:**

1. **`AddressManager<T>`**: добавлены методы `CreateArray(count, name)` / `DeleteArray(index)` / `GetArrayElement(index, elem)` / `GetCount(index)`. Каждый слот хранит поле `__count` (0 = одиночный объект, >0 = массив из `count` элементов). Массивы сохраняются в отдельных файлах (`./<type>_arr_<index>.extend`) чтобы не пересекаться с одиночными объектами.

2. **`fptr<T>`**: добавлены методы `NewArray(count)` / `DeleteArray()` / `count()` / `operator[](idx)`. `fptr<T>` остаётся тривиально копируемым (хранит только `unsigned __addr`); количество элементов хранится в метаданных слота `AddressManager`.

3. **`persistent_string`**: поле `long_buf[65535]` (65 КБ статики) заменено на `fptr<char> long_ptr` + `unsigned long_len`. Для длинных строк (> SSO_SIZE) выделяется персистная динамическая память через `AddressManager<char>::CreateArray`. Конструктор/деструктор только загружают/сохраняют — аллокация явная через методы (`assign()`, `_assign()`, `alloc_long()`, `free_long()`).

4. **`persistent_array<T,C>`**: поле `uint32_t next_slab_id` заменено на `fptr<persistent_array<T,C>> next_slab`. Оба — 4 байта; смена добавляет типобезопасность.

**Ключевой принцип (по требованию владельца):** конструктор/деструктор персистного объекта только загружают/сохраняют. Выделение и освобождение персистной памяти — через явные методы менеджера (`CreateArray`/`DeleteArray`, `fptr::NewArray()`/`fptr::DeleteArray()`).

Файл тестов: `tests/test_fptr.cpp` (добавлены тесты 3.4.1–3.4.7)

7 новых тестов — CI зелёный.

---

### Задача 3.5 — Интеграционные тесты и CI

**Цель:** Убедиться, что все новые классы корректно работают совместно в реалистичных сценариях.

Файл: `tests/test_phase3_integration.cpp`

1. Полный цикл: создать `PersistentJsonStore`, импортировать JSON, взять снимок через `ObjectStore`, уничтожить хранилище, восстановить из снимка.
2. Полный цикл с перезапуском: `PersistentJsonStore` сохраняется, процесс рестартует (симулируется через уничтожение/создание объекта с тем же путём), данные доступны.
3. `persistent_json` round-trip через `persist<persistent_json>` / `fptr<persistent_json>`.
4. Работа `nlohmann::json` алгоритмов (merge_patch, diff) с `persistent_json` вместо стандартного `json`.
5. Тест на корректность алиасов: `using json = jgit::persistent_json; json j = {...};` работает как ожидается.

**CI:** Все тесты должны проходить на Linux (GCC, Clang) и Windows (MSVC) в GitHub Actions.

---

### Задача 3.6 — Производительность и бенчмарки

Файл: `experiments/benchmark_persist_vs_std.cpp` и `tests/test.json`

| Измерение | Метрика |
|-----------|---------|
| Запись/чтение `persist<int>` vs `int` в файл | ops/sec |
| `PersistentJsonStore::import_json` vs `json::parse` | мс для 1000-элементного объекта |
| `PersistentJsonStore` с `std::vector`-пулами (Фаза 2) vs `AddressManager`-пулами (Фаза 3) | мс |
| `fptr::New` + запись + `fptr::Delete` x1000 | мс |

Зафиксировать результаты в документе `experiments/benchmark_persist_vs_std_results.md`.

---

## Порядок реализации

```
Task 3.1  →  Task 3.2  →  Task 3.3  →  Task 3.4  →  Task 3.5
Инфра       persist<T>/  basic_json<>  Интеграция   Бенчмарки
            fptr<T>      инстанции     + CI
```

Каждая подзадача коммитится отдельно с чёткими тестами до перехода к следующей.

---

## Критерии успеха

- [x] 3.1.1: `persist<_T>` — исправлен, задокументирован, покрыт тестами (6 тестов, CI зелёный).
- [x] 3.1.2: `fptr<_T>` — исправлен, задокументирован, покрыт тестами (6 тестов, CI зелёный).
- [x] 3.1.3: `AddressManager<_T>` — исправлен, задокументирован, покрыт тестами (6 тестов, CI зелёный).
- [x] 3.1.4: `Cache` — исправлен (`Flush` добавлен), покрыт тестами (9 тестов, CI зелёный).
- [x] 3.1.5: `MemoryDevice` — исправлен, покрыт тестами (5 тестов, CI зелёный).
- [x] 3.1.6: `StaticPageDevice` — исправлен, покрыт тестами (4 теста, CI зелёный).
- [x] 3.2.1: `persist<persistent_string>` — компилируется и тестируется (5 тестов, CI зелёный).
- [x] 3.2.2: `persistent_map` переписана на `fptr` — тестируется (5 тестов, CI зелёный).
- [x] 3.2.3: `persist<persistent_json_value>` — тестируется (8 тестов, CI зелёный).
- [x] 3.2.4: `PersistentJsonStore` с конструктором `filesystem::path` — тестируется (6 тестов, CI зелёный).
- [x] 3.3.1: Документация параметров `basic_json<>` создана (`experiments/study_basic_json_params.cpp`).
- [x] 3.3.2: `persistent_string` расширена до `StringType`, тесты проходят (7 тестов, CI зелёный).
- [x] 3.3.3: `jgit::persistent_json` объявлена в `jgit/persistent_basic_json.h`.
- [ ] 3.3.4: `ObjectType`/`ArrayType` заменены персистными адаптерами.
- [x] 3.3.5: Тесты `persistent_json` — dump/parse, merge_patch, diff, alias pattern, PersistentJsonStore round-trip (8 тестов, CI зелёный).
- [x] 3.4: `fptr<T>` array support (`NewArray`/`DeleteArray`/`operator[]`); `persistent_string` uses `fptr<char>`; `persistent_array` uses `fptr<T>` for slab chain — 7 тестов (3.4.1–3.4.7), CI зелёный.
- [x] 3.5: Интеграционные тесты проходят; CI зелёный на GCC/Clang/MSVC (212 тестов, CI зелёный).
- [x] 3.6: Бенчмарк зафиксирован в репозитории (`experiments/benchmark_persist_vs_std.cpp`, результаты в `experiments/benchmark_persist_vs_std_results.md`).

---

## Связь с plan.md и предыдущими фазами

| Задача | Связь с plan.md |
|--------|-----------------|
| 3.1 Инфра | Направление 2 «Модернизация», Высокий приоритет — тестирование (п. 5); исправление багов (п. 1) |
| 3.2 persist<T>/fptr<T> | Направление 1 «jgit», концепция `fptr<T>` для `$ref`-ссылок (п. 7) |
| 3.3 basic_json инстанции | Направление 1 «jgit», финальная интеграция nlohmann/json с персистными классами |
| 3.4 CI | Направление 2 «Модернизация», Средний приоритет — CI/CD (п. 6) |
| 3.5 Бенчмарки | Направление 5 «Образование», визуализация; Направление 3, оптимизация |

**Примечание:** Реализация системы коммитов jgit (из предыдущей версии Phase 3 плана) переносится в **Фазу 4**. Она опирается на работающую персистную инфраструктуру (Фаза 3), поэтому порядок фаз: сначала завершить Фазу 3, затем возобновить работу над коммитами.

---

*Документ создан: 2026-02-25. Переработан согласно требованиям issue #27.*
