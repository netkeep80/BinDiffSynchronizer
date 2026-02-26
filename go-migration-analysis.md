# Анализ перехода `persist<>` и `fptr<>` на Go

**Статус:** Анализ
**Версия:** 1.0
**Дата:** 2026-02-26
**Связанные issue:** [#40](https://github.com/netkeep80/BinDiffSynchronizer/issues/40)
**Контекст:** [persistent_json_analysis.md](persistent_json_analysis.md), [jhub-tz.md](jhub-tz.md), [plan.md](plan.md)

---

## Содержание

1. [Обзор: что делают `persist<>` и `fptr<>`](#1-обзор-что-делают-persist-и-fptr)
2. [Ключевые концепции C++, не имеющие прямого аналога в Go](#2-ключевые-концепции-c-не-имеющие-прямого-аналога-в-go)
3. [Аналоги на Go: существующие библиотеки и подходы](#3-аналоги-на-go-существующие-библиотеки-и-подходы)
4. [Проектирование Go-версии: `pstring`, `parray`, `pmap`, `pjson`](#4-проектирование-go-версии-pstring-parray-pmap-pjson)
5. [Проектирование менеджеров персистной памяти на Go](#5-проектирование-менеджеров-персистной-памяти-на-go)
6. [Сравнение подходов: C++ vs Go](#6-сравнение-подходов-c-vs-go)
7. [Proof of Concept: Go-код для ключевых концепций](#7-proof-of-concept-go-код-для-ключевых-концепций)
8. [Оценка рисков и ограничений](#8-оценка-рисков-и-ограничений)
9. [Рекомендации и выводы](#9-рекомендации-и-выводы)
10. [Дорожная карта реализации](#10-дорожная-карта-реализации)

---

## 1. Обзор: что делают `persist<>` и `fptr<>`

### 1.1 Архитектура C++ персистного хранилища

В текущей реализации (`persist.h`, `PageDevice.h`) реализована многоуровневая система персистного хранения:

```
┌─────────────────────────────────────────────────────────────────┐
│  Пользовательский слой                                          │
│  persist<T> — RAII-обёртка: load при создании, save при удалении│
│  fptr<T>    — персистный указатель (целочисленный адрес в пуле)  │
├─────────────────────────────────────────────────────────────────┤
│  Слой менеджера адресов                                          │
│  AddressManager<T> — пул объектов типа T фиксированного размера │
│  Слот: {ptr, used, refs, count, name[64]}                       │
├─────────────────────────────────────────────────────────────────┤
│  Слой страничного устройства                                     │
│  PageDevice<> — страничный кэш с политикой вытеснения           │
│  MemoryDevice / StaticPageDevice — конкретные реализации        │
├─────────────────────────────────────────────────────────────────┤
│  Файловая система / mmap                                        │
│  Бинарные файлы .persist — сериализованные тривиально копируемые│
│  типы (raw bytes, фиксированного размера sizeof(T))             │
└─────────────────────────────────────────────────────────────────┘
```

### 1.2 Ключевые свойства `persist<T>`

1. **Требование тривиальной копируемости**: `static_assert(std::is_trivially_copyable<_T>)` — сохраняет/загружает сырые байты (`sizeof(T)` байт). Это работает только для типов без heap-аллокаций.

2. **RAII-семантика**: конструктор загружает из файла, деструктор сохраняет в файл. Имя файла — либо производное от адреса объекта, либо явно заданное.

3. **Прозрачный доступ**: `operator _Tref()` позволяет использовать `persist<T>` как сам объект типа `T`.

4. **Именованная персистность**: конструктор `persist(const char* filename)` обеспечивает детерминированное сохранение/восстановление между запусками процесса.

### 1.3 Ключевые свойства `fptr<T>`

1. **Целочисленный адрес**: `unsigned __addr` — индекс в таблице `AddressManager<T>.__itable`.
2. **Нулевой адрес = nullptr**: `__addr == 0` означает null-указатель (пустой fptr).
3. **Тривиально копируем**: `fptr<T>` содержит только `unsigned` → можно использовать в `persist<T>`.
4. **Разыменование**: обращается к `AddressManager<T>::GetManager()[__addr]` — выгружает объект из файла в память.
5. **Управление жизненным циклом**: `New()`, `Delete()`, `NewArray()`, `DeleteArray()` — явное выделение/освобождение.

### 1.4 jgit-типы, построенные на `persist<T>` и `fptr<T>`

| Тип | Описание | sizeof | Тривиально копируем |
|-----|----------|--------|---------------------|
| `persistent_string` | SSO-строка (≤23 байт inline) + `fptr<char>` для длинных | ~36 байт | ✅ |
| `persistent_json_value` | JSON-узел: тип + union(bool/int64/double/string/id) | ~72 байта | ✅ |
| `persistent_map<V, N>` | Сортированный массив пар ключ-значение + цепочка slabs | переменный | ✅ |
| `persistent_array<T, N>` | Массив фиксированной ёмкости + цепочка slabs | переменный | ✅ |
| `persistent_json` (basic_json<>) | nlohmann::basic_json с `persistent_string` как StringType | — | ❌ |

---

## 2. Ключевые концепции C++, не имеющие прямого аналога в Go

### 2.1 Тривиальная копируемость (`std::is_trivially_copyable`)

**В C++**: `memcpy(dst, src, sizeof(T))` корректен для тривиально копируемых типов. Это фундаментальное требование `persist<T>` — сохранять/загружать сырые байты.

**В Go**: Концепции «тривиально копируемого» типа нет. Любой тип можно скопировать присваиванием (`v2 = v1`), однако:
- Интерфейсы и слайсы содержат скрытые указатели — `memcpy` не гарантирует корректность.
- Структуры без указателей — фактически эквивалент тривиально копируемых типов C++.
- Go нет `unsafe.Pointer` → `memcpy` на произвольный тип, но `encoding/binary` и `unsafe` пакеты позволяют читать/писать сырые байты.

**Решение в Go**: Ограничить персистные типы структурами без указателей и слайсов. Использовать `encoding/binary` или `unsafe` для сериализации.

### 2.2 Шаблоны C++ (`template<class _T>`)

**В C++**: `persist<T>` — шаблонный класс, работающий с любым тривиально копируемым T.

**В Go**:
- До Go 1.18: нет дженериков. Использовались `interface{}` + ручное приведение типов.
- С Go 1.18+: дженерики через `[T any]` с ограничениями (`constraints`).

**Решение в Go**: Использовать Go generics (1.18+) с кастомным constraint:
```go
type Fixed interface {
    encoding.BinaryMarshaler
    encoding.BinaryUnmarshaler
}
// или ограничить конкретными типами через ~struct{}
```

### 2.3 Operator Overloading

**В C++**: `persist<T>` прозрачно преобразуется в `T&` через `operator _Tref()`.

**В Go**: Перегрузка операторов отсутствует. Нет способа сделать `persist[T]` прозрачным — всегда нужен явный метод доступа (`.Get()`, `.Value()`).

**Влияние**: Пользовательский код будет менее элегантным:
```go
// C++: int x = myPersist;  // прозрачно
// Go:  x := myPersist.Get()  // явно
```

### 2.4 RAII (Resource Acquisition Is Initialization)

**В C++**: Деструктор `persist<T>` автоматически сохраняет данные при выходе из области видимости.

**В Go**: Нет деструкторов (только `runtime.SetFinalizer`, ненадёжный). RAII через `defer`:

```go
func UsePerist() {
    p := persist.Load[MyStruct]("filename")
    defer p.Save()  // аналог деструктора — но явный
    // использование p
}
```

**Влияние**: RAII заменяется паттерном `defer`, который нужно добавлять явно. Это нарушает автоматическую семантику C++.

### 2.5 Union (объединение типов)

**В C++**: `persistent_json_value` использует `union { bool boolean_val; int64_t int_val; double float_val; persistent_string string_val; uint32_t array_id; }` — фиксированный размер, нет аллокации.

**В Go**: Нет `union`. Альтернативы:
1. **Структура с полями разных типов + тег**: аналог tagged union через `byte` + поля.
2. **`[N]byte` + `unsafe.Pointer`**: ручной union с raw bytes (небезопасно).
3. **`interface{}`**: динамическая типизация через boxing (heap allocation).

**Решение в Go**: Для максимального соответствия семантике C++ — struct с `[N]byte` payload и byte-тегом:

```go
type JSONValueType byte

const (
    TypeNull    JSONValueType = 0
    TypeBool    JSONValueType = 1
    TypeInt     JSONValueType = 2
    TypeFloat   JSONValueType = 3
    TypeString  JSONValueType = 4
    TypeArray   JSONValueType = 5
    TypeObject  JSONValueType = 6
)

// Максимальный размер payload = sizeof(persistent_string) = 36 байт
type JSONValue struct {
    Type    JSONValueType
    _pad    [7]byte
    payload [64]byte  // достаточно для largest member
}
```

### 2.6 Placement New

**В C++**: Конструирование объекта по заданному адресу памяти (в пуле) без аллокации.

**В Go**: Аналога нет. Управление памятью полностью на стороне GC. Однако:
- Можно предвыделить `[]byte` буфер и хранить объекты как сериализованные байты.
- `unsafe.Pointer` позволяет создать указатель на произвольный адрес в буфере.

---

## 3. Аналоги на Go: существующие библиотеки и подходы

### 3.1 Персистное хранилище на Go

| Библиотека | Описание | Соответствие `persist<T>` |
|-----------|---------|--------------------------|
| [bbolt](https://github.com/etcd-io/bbolt) | Встроенная B-tree key-value БД (Bolt) | Частичное: KV-хранилище без typed pools |
| [badger](https://github.com/dgraph-io/badger) | LSM-based key-value store | Частичное: высокопроизводительное KV |
| [mmap-go](https://github.com/edsrzf/mmap-go) | Кросс-платформенный mmap | Прямое: low-level доступ к файлам через mmap |
| [encoding/gob](https://pkg.go.dev/encoding/gob) | Go-нативная сериализация | Схожее: сериализация произвольных типов |
| [encoding/binary](https://pkg.go.dev/encoding/binary) | Бинарная сериализация структур | Прямое: `binary.Read/Write` для fixed-size struct |

### 3.2 Наиболее близкий аналог: `mmap` + `encoding/binary`

Паттерн, наиболее близкий к `persist<T>` + `AddressManager<T>` в Go:

```go
// Предвыделённый пул объектов через mmap
import (
    "encoding/binary"
    "os"
    mmap "github.com/edsrzf/mmap-go"
)

// Pool[T] — аналог AddressManager<T>
type Pool[T any] struct {
    data mmap.MMap  // mmap-файл
    size int        // sizeof(T) для слота
    cap  int        // максимальное количество слотов
}
```

### 3.3 Персистные JSON-хранилища на Go

| Библиотека | Описание | Соответствие |
|-----------|---------|-------------|
| [golangci/tiedot](https://github.com/HouzuoGuo/tiedot) | Document store на Go | Частичное: JSON docs, без fixed-size slabs |
| [tidwall/buntdb](https://github.com/tidwall/buntdb) | In-memory с persist, поддержка JSON | Ближе всего к jgit jdb |
| [tidwall/gjson](https://github.com/tidwall/gjson) | Fast JSON parser (read-only) | Нет: только чтение |
| [buger/jsonparser](https://github.com/buger/jsonparser) | Fast JSON parser | Нет: только чтение |

**Вывод**: Готового аналога всей системы `persist<T>` + `fptr<T>` + `AddressManager<T>` в мире Go не существует. Потребуется собственная реализация.

---

## 4. Проектирование Go-версии: `pstring`, `parray`, `pmap`, `pjson`

### 4.1 `pstring` — аналог `persistent_string`

**Требования**:
- SSO для строк ≤23 байт (inline хранение).
- Для длинных строк: `fptr`-подобный индекс в пуле `char`-значений.
- Фиксированный размер структуры (для binary serialization).

```go
package pjson

import "encoding/binary"

const ssoSize = 23

// PString — персистная строка с SSO.
// Фиксированный размер: 23 + 1 (isLong) + 4 (longIdx) + 4 (longLen) = 32 байта.
type PString struct {
    sso     [ssoSize]byte  // SSO-буфер (NUL-терминирован)
    isLong  bool           // true → данные в пуле по longIdx
    _pad    [3]byte        // выравнивание
    longIdx uint32         // индекс в PCharPool (0 = null)
    longLen uint32         // длина длинной строки
}

// Размер строго фиксирован.
const pstringSize = ssoSize + 1 + 3 + 4 + 4 // = 35 байт → округление до 36

func (s *PString) String() string {
    if !s.isLong {
        n := 0
        for n < ssoSize && s.sso[n] != 0 { n++ }
        return string(s.sso[:n])
    }
    // Загружаем из пула — нужен доступ к PCharPool (через контекст)
    return globalCharPool.Load(s.longIdx, s.longLen)
}

func (s *PString) Set(v string) {
    if len(v) <= ssoSize {
        s.isLong = false
        copy(s.sso[:], v)
        if len(v) < ssoSize { s.sso[len(v)] = 0 }
    } else {
        s.isLong = true
        s.longIdx = globalCharPool.Store(v)
        s.longLen = uint32(len(v))
    }
}
```

**Сравнение с C++**:

| Аспект | C++ `persistent_string` | Go `PString` |
|--------|------------------------|--------------|
| SSO-порог | 23 символа | 23 символа |
| Фиксированный sizeof | ✅ | ✅ |
| Прозрачный доступ | `operator std::string()` | `s.String()` (явно) |
| Персистный пул | `fptr<char>` + `AddressManager<char>` | `uint32` индекс + `PCharPool` |
| binary.Write совместим | memcpy | ✅ |

### 4.2 `parray` — аналог `persistent_array<T, N>`

**Требования**:
- Фиксированная ёмкость `N` элементов в одном slab.
- Цепочка slabs для данных > N.
- Нет heap-аллокаций.

```go
const defaultArrayCapacity = 64

// PArraySlab[T] — один slab персистного массива.
// Фиксированный размер: N*sizeof(T) + 4 (size) + 4 (nextSlabIdx).
type PArraySlab[T any] struct {
    data        [defaultArrayCapacity]T
    size        uint32
    nextSlabIdx uint32  // 0 = нет следующего slab
}

// Аналог persistent_array через pool + slab chaining
type PArray[T any] struct {
    pool     *SlabPool[PArraySlab[T]]
    rootIdx  uint32  // индекс корневого slab в pool
}

func (a *PArray[T]) Len() int { /* обход chain */ }
func (a *PArray[T]) Append(v T) { /* вставка в последний slab или создание нового */ }
func (a *PArray[T]) Get(i int) T { /* обход chain по индексу */ }
```

### 4.3 `pmap` — аналог `persistent_map<V, N>`

**Требования**:
- Сортированный массив пар (ключ PString, значение V).
- Фиксированная ёмкость N пар в одном slab.
- Цепочка slabs для данных > N.

```go
const defaultMapCapacity = 32

type PMapEntry[V any] struct {
    Key   PString
    Value V
}

// PMapSlab[V] — один slab персистной карты.
type PMapSlab[V any] struct {
    entries     [defaultMapCapacity]PMapEntry[V]
    size        uint32
    nextSlabIdx uint32  // 0 = нет следующего slab
}

// PMap[V] — персистная map через sorted slab array.
type PMap[V any] struct {
    pool    *SlabPool[PMapSlab[V]]
    rootIdx uint32
}

func (m *PMap[V]) Get(key string) (V, bool) { /* binary search в slabs */ }
func (m *PMap[V]) Set(key string, v V) { /* insert_or_assign с chaining */ }
func (m *PMap[V]) Del(key string) bool { /* delete entry + compact */ }
```

### 4.4 `pjson` — аналог `persistent_json_value` + `PersistentJsonStore`

**Требования**:
- Тип-тег + union-подобный payload фиксированного размера.
- Поддержка null, bool, int64, float64, string, array (id), object (id).
- Совместимость с `encoding/json` для импорта/экспорта.

```go
// PJSONType — тип JSON-узла.
type PJSONType byte

const (
    PJSONNull   PJSONType = 0
    PJSONBool   PJSONType = 1
    PJSONInt    PJSONType = 2
    PJSONFloat  PJSONType = 3
    PJSONString PJSONType = 4
    PJSONArray  PJSONType = 5
    PJSONObject PJSONType = 6
)

// PJSONValue — персистный JSON-узел.
// Фиксированный размер: 1 (type) + 7 (pad) + 64 (payload) = 72 байта.
// payload интерпретируется согласно Type:
//   Bool:   payload[0] (0/1)
//   Int:    payload[0:8] — little-endian int64
//   Float:  payload[0:8] — float64 (IEEE 754)
//   String: payload[0:sizeof(PString)] — встроенная PString
//   Array:  payload[0:4] — little-endian uint32 (array slab ID)
//   Object: payload[0:4] — little-endian uint32 (map slab ID)
type PJSONValue struct {
    Type    PJSONType
    _pad    [7]byte
    payload [64]byte
}

// Вспомогательные методы
func (v *PJSONValue) IsNull() bool   { return v.Type == PJSONNull }
func (v *PJSONValue) GetBool() bool  { return v.payload[0] != 0 }
func (v *PJSONValue) GetInt() int64 {
    return int64(binary.LittleEndian.Uint64(v.payload[:8]))
}
func (v *PJSONValue) GetFloat() float64 {
    bits := binary.LittleEndian.Uint64(v.payload[:8])
    return math.Float64frombits(bits)
}
// ... и т.д.

// PJSONStore — хранилище JSON-дерева.
// Аналог PersistentJsonStore из C++.
type PJSONStore struct {
    values  *SlabPool[PJSONValue]
    arrays  *SlabPool[PArraySlab[uint32]]   // массивы из ID значений
    objects *SlabPool[PMapSlab[uint32]]     // объекты: ключ → ID значения
}

// Import преобразует encoding/json совместимые данные в PJSONStore.
func (s *PJSONStore) Import(data []byte) (uint32, error) { /* parse + store */ }

// Export восстанавливает данные в encoding/json совместимый вид.
func (s *PJSONStore) Export(id uint32) ([]byte, error) { /* traverse + marshal */ }
```

---

## 5. Проектирование менеджеров персистной памяти на Go

### 5.1 `SlabPool[T]` — аналог `AddressManager<T>` + `PageDevice`

Центральный компонент — пул объектов фиксированного размера с персистностью через mmap или файловый I/O:

```go
// SlabPool[T] — менеджер пула объектов типа T.
// Аналог AddressManager<T> из C++.
// Требование: T — struct без указателей (FixedSize constraint).
type SlabPool[T any] struct {
    mu       sync.RWMutex
    path     string         // путь к файлу пула
    data     []byte         // mmap или []byte буфер
    itemSize int            // binary.Size(T{})
    capacity int            // максимальное число слотов
    used     []bool         // битмап занятых слотов (в памяти, не в файле)
    dirty    []bool         // битмап изменённых слотов (для lazy-write)
}

func NewSlabPool[T any](path string, capacity int) (*SlabPool[T], error) {
    var zero T
    size := binary.Size(zero)
    // Открыть или создать файл размером capacity * size байт
    // mmap файл для zero-parse reload
    return &SlabPool[T]{path: path, itemSize: size, capacity: capacity}, nil
}

// Alloc выделяет свободный слот, возвращает его индекс (1-based, 0=null).
func (p *SlabPool[T]) Alloc() (uint32, error)

// Free освобождает слот с заданным индексом.
func (p *SlabPool[T]) Free(idx uint32)

// Load загружает объект из слота idx в память.
func (p *SlabPool[T]) Load(idx uint32) (T, error)

// Store сохраняет объект в слот idx.
func (p *SlabPool[T]) Store(idx uint32, v T) error

// Flush сбрасывает dirty-слоты на диск.
func (p *SlabPool[T]) Flush() error

// Close закрывает пул и освобождает ресурсы.
func (p *SlabPool[T]) Close() error
```

**Ключевые ограничения** для типа `T`:

```go
// FixedSize — constraint для типов без указателей и слайсов.
// В Go нет встроенного "trivially copyable" constraint;
// нужно использовать ~struct{} + проверку через reflect в runtime.
// Alternatively: требовать реализацию encoding.BinaryMarshaler.
type FixedSize interface {
    // Marker interface — caller гарантирует fixed binary size
}
```

### 5.2 `PCharPool` — аналог `AddressManager<char>`

```go
// PCharPool — специализированный пул для char-массивов (длинные строки).
// Аналог AddressManager<char> + fptr<char> в C++.
type PCharPool struct {
    mu     sync.RWMutex
    path   string
    chunks map[uint32][]byte  // idx → строковые данные
    nextID uint32
}

func (p *PCharPool) Store(s string) uint32 {
    p.mu.Lock()
    defer p.mu.Unlock()
    id := p.nextID
    p.nextID++
    p.chunks[id] = []byte(s)
    return id
}

func (p *PCharPool) Load(idx uint32, length uint32) string {
    p.mu.RLock()
    defer p.mu.RUnlock()
    if b, ok := p.chunks[idx]; ok {
        return string(b[:length])
    }
    return ""
}
```

### 5.3 `Persist[T]` — аналог `persist<T>` (RAII wrapper)

```go
// Persist[T] — аналог persist<T> в C++.
// Загружает при создании, сохраняет через Close() (аналог деструктора).
type Persist[T any] struct {
    filename string
    value    T
    dirty    bool
}

// Load загружает объект из файла.
func Load[T any](filename string) (*Persist[T], error) {
    p := &Persist[T]{filename: filename}
    f, err := os.Open(filename)
    if err == nil {
        defer f.Close()
        err = binary.Read(f, binary.LittleEndian, &p.value)
    }
    return p, nil
}

// New создаёт новый Persist[T] с заданным значением.
func New[T any](filename string, v T) *Persist[T] {
    return &Persist[T]{filename: filename, value: v, dirty: true}
}

// Get возвращает ссылку на хранимое значение.
func (p *Persist[T]) Get() *T { return &p.value }

// Save сохраняет объект на диск.
func (p *Persist[T]) Save() error {
    if !p.dirty { return nil }
    f, err := os.Create(p.filename)
    if err != nil { return err }
    defer f.Close()
    return binary.Write(f, binary.LittleEndian, p.value)
}

// Паттерн использования:
// p, _ := Load[MyStruct]("data.persist")
// defer p.Save()  // аналог RAII деструктора
// p.Get().Field = 42
```

### 5.4 `FPtr[T]` — аналог `fptr<T>`

```go
// FPtr[T] — персистный указатель.
// Аналог fptr<T> в C++: хранит целочисленный адрес в пуле.
type FPtr[T any] struct {
    addr uint32  // 0 = null, иначе 1-based индекс в SlabPool[T]
    pool *SlabPool[T]  // ссылка на пул (не сохраняется в файл)
}

// Null возвращает null FPtr.
func NullFPtr[T any](pool *SlabPool[T]) FPtr[T] {
    return FPtr[T]{addr: 0, pool: pool}
}

// New выделяет новый объект в пуле.
func (f *FPtr[T]) New() error {
    idx, err := f.pool.Alloc()
    if err != nil { return err }
    f.addr = idx
    return nil
}

// Delete освобождает объект в пуле.
func (f *FPtr[T]) Delete() {
    if f.addr != 0 {
        f.pool.Free(f.addr)
        f.addr = 0
    }
}

// Deref загружает объект из пула.
func (f FPtr[T]) Deref() (T, error) {
    if f.addr == 0 {
        var zero T
        return zero, fmt.Errorf("null FPtr dereference")
    }
    return f.pool.Load(f.addr)
}

// IsNull проверяет нулевой адрес.
func (f FPtr[T]) IsNull() bool { return f.addr == 0 }
func (f FPtr[T]) Addr() uint32 { return f.addr }
```

**Важное ограничение**: В Go `FPtr[T]` нельзя сохранить как часть персистной структуры напрямую, поскольку `*SlabPool[T]` — указатель (не сериализуется). При сохранении хранится только `addr uint32`, а `pool` восстанавливается при загрузке через контекст или глобальный реестр пулов.

---

## 6. Сравнение подходов: C++ vs Go

### 6.1 Таблица сравнения

| Концепция | C++ реализация | Go аналог | Сложность перехода |
|-----------|----------------|-----------|-------------------|
| `persist<T>` RAII | Конструктор/деструктор | `Load[T]()` + `defer Save()` | ★★★☆☆ Умеренная |
| `fptr<T>` | Struct с `unsigned __addr` | `FPtr[T]` struct с `uint32 addr` | ★★☆☆☆ Простая |
| `AddressManager<T>` | Singleton + itable + файлы | `SlabPool[T]` + mmap/files | ★★★★☆ Высокая |
| Тривиальная копируемость | `static_assert` | Go struct без указателей + binary.Size | ★★★☆☆ Умеренная |
| Шаблоны C++ | `template<class T>` | Go generics `[T any]` с constraints | ★★★☆☆ Умеренная |
| Operator overloading | `operator _Tref()` | Методы `.Get()`, `.Set()` | ★★☆☆☆ Простая |
| Union + type tag | `union { ... }` с type enum | `[N]byte` payload + `byte` tag | ★★★★☆ Высокая |
| SSO строки | `char sso_buf[24]` + `fptr<char>` | `[23]byte` + `uint32 idx` | ★★☆☆☆ Простая |
| Slab chaining | `fptr<self_t> next_node` | `uint32 nextSlabIdx` | ★★☆☆☆ Простая |
| PageDevice кэш | `Cache<Policy>` + страницы | `sync.Map` + LRU cache | ★★★★☆ Высокая |
| Binary serialization | `memcpy` | `encoding/binary` + `unsafe` | ★★★☆☆ Умеренная |
| Zero-parse reload | mmap + указатель на корень | mmap + `encoding/binary` | ★★★☆☆ Умеренная |
| Content-addressed store | SHA-256 + ObjectStore | SHA-256 + file-based KV | ★★☆☆☆ Простая |
| JSON Patch/Merge | `persistent_json::diff()` | стандартная библиотека `encoding/json` + RFC 6902 | ★★☆☆☆ Простая |

### 6.2 Что выигрывает Go по сравнению с C++

| Преимущество | Описание | Важность для jgit/jdb |
|-------------|---------|----------------------|
| **Сборщик мусора** | Нет необходимости в явном `Delete()` и счётчиках ссылок | Меньше ошибок использования после освобождения |
| **Горутины** | Параллельный доступ к хранилищу через `sync.RWMutex` естественен | Параллельные запросы jdb |
| **encoding/json** | Встроенная сериализация через `json.Marshal/Unmarshal` | Простой import/export из/в std JSON |
| **crypto/sha256** | Готовый SHA-256 для content-addressed store | ObjectStore без внешних зависимостей |
| **net/http** | REST API для jhub без внешних фреймворков | FR-2.5, FR-3.3 jhub |
| **Кросс-платформенность** | Нативная кросс-компиляция `GOOS/GOARCH` | NFR-5 jhub |
| **Простота деплоя** | Статически слинкованный бинарник | Docker-образы jhub компактнее |
| **Читаемость** | Проще онбординг новых контрибьюторов | Развитие проекта |

### 6.3 Что теряется при переходе с C++ на Go

| Потеря | Описание | Серьёзность | Обходное решение |
|--------|---------|------------|-----------------|
| **Тривиальная копируемость** | Go не имеет `std::is_trivially_copyable` — нет статической проверки | Средняя | Runtime проверка через `reflect.TypeOf(v).Kind() != reflect.Ptr` |
| **RAII семантика** | Нет автоматических деструкторов — нужен явный `defer` | Высокая | `defer p.Save()` — программист обязан помнить |
| **Operator overloading** | Нет прозрачной конвертации `persist<T>` → `T&` | Низкая | Явные методы `.Get()`, `.Set()` |
| **Union** | Нет нативного union — нужен `[N]byte` payload | Высокая | `[N]byte` + helper methods, менее типобезопасно |
| **Zero-overhead abstractions** | GC паузы, runtime overhead | Средняя | Тюнинг GC (`GOGC`, `GOMEMLIMIT`), pre-allocation |
| **Встроенный mmap** | `mmap` требует `golang.org/x/sys` или сторонней библиотеки | Низкая | `edsrzf/mmap-go` или `encoding/binary` |
| **`sizeof` во время компиляции** | `binary.Size` — runtime, не compile-time constant | Низкая | Тестовые assert-функции в init() |
| **Производительность** | Go обычно на 10-50% медленнее C++ для memory-intensive операций | Средняя | Достаточно для большинства jgit/jdb сценариев |

---

## 7. Proof of Concept: Go-код для ключевых концепций

### 7.1 Минимальная реализация `Persist[T]`

```go
// package pjson — персистные JSON-примитивы для jgit на Go.
package pjson

import (
    "encoding/binary"
    "os"
)

// Persist[T] — аналог C++ persist<T>.
// T должен быть struct без указателей (fixed-size, binary-serializable).
type Persist[T any] struct {
    filename string
    value    T
}

// Load создаёт Persist[T], загружая из файла если он существует.
func Load[T any](filename string) (*Persist[T], error) {
    p := &Persist[T]{filename: filename}
    f, err := os.Open(filename)
    if err == nil {
        defer f.Close()
        _ = binary.Read(f, binary.LittleEndian, &p.value)
    }
    return p, nil
}

// Get возвращает указатель на хранимое значение для изменения.
func (p *Persist[T]) Get() *T { return &p.value }

// Save сохраняет значение в файл (аналог деструктора persist<T>).
func (p *Persist[T]) Save() error {
    f, err := os.Create(p.filename)
    if err != nil {
        return err
    }
    defer f.Close()
    return binary.Write(f, binary.LittleEndian, p.value)
}

// Пример использования:
// type Config struct { Version uint32; Debug bool; _pad [3]byte }
//
// func main() {
//     p, _ := Load[Config]("config.persist")
//     defer p.Save()
//     p.Get().Version = 42
//     p.Get().Debug = true
// }
```

### 7.2 Минимальная реализация `SlabPool[T]` (AddressManager)

```go
package pjson

import (
    "encoding/binary"
    "fmt"
    "os"
    "sync"
)

// SlabPool[T] — пул объектов фиксированного размера.
// Аналог AddressManager<T> в C++.
type SlabPool[T any] struct {
    mu       sync.RWMutex
    path     string
    items    []T    // предвыделённый слайс
    used     []bool
    capacity int
    itemSize int
}

func NewSlabPool[T any](path string, capacity int) (*SlabPool[T], error) {
    var zero T
    size := binary.Size(zero)
    if size < 0 {
        return nil, fmt.Errorf("type T is not binary-serializable (contains non-fixed-size fields)")
    }
    pool := &SlabPool[T]{
        path:     path,
        items:    make([]T, capacity+1), // slot 0 = null (unused)
        used:     make([]bool, capacity+1),
        capacity: capacity,
        itemSize: size,
    }
    // Попытка загрузить существующий файл
    _ = pool.load()
    return pool, nil
}

// Alloc выделяет свободный слот, возвращает 1-based индекс.
func (p *SlabPool[T]) Alloc() (uint32, error) {
    p.mu.Lock()
    defer p.mu.Unlock()
    for i := 1; i <= p.capacity; i++ {
        if !p.used[i] {
            p.used[i] = true
            return uint32(i), nil
        }
    }
    return 0, fmt.Errorf("SlabPool: out of capacity (%d)", p.capacity)
}

// Free освобождает слот.
func (p *SlabPool[T]) Free(idx uint32) {
    p.mu.Lock()
    defer p.mu.Unlock()
    if idx > 0 && int(idx) <= p.capacity {
        p.used[idx] = false
        var zero T
        p.items[idx] = zero
    }
}

// Store записывает объект в слот.
func (p *SlabPool[T]) Store(idx uint32, v T) {
    p.mu.Lock()
    defer p.mu.Unlock()
    if idx > 0 && int(idx) <= p.capacity {
        p.items[idx] = v
    }
}

// Load загружает объект из слота.
func (p *SlabPool[T]) Load(idx uint32) (T, bool) {
    p.mu.RLock()
    defer p.mu.RUnlock()
    if idx > 0 && int(idx) <= p.capacity && p.used[idx] {
        return p.items[idx], true
    }
    var zero T
    return zero, false
}

// Flush сохраняет все слоты на диск.
func (p *SlabPool[T]) Flush() error {
    p.mu.RLock()
    defer p.mu.RUnlock()
    f, err := os.Create(p.path)
    if err != nil {
        return err
    }
    defer f.Close()
    for i := 1; i <= p.capacity; i++ {
        if err := binary.Write(f, binary.LittleEndian, p.items[i]); err != nil {
            return err
        }
    }
    return nil
}

func (p *SlabPool[T]) load() error {
    f, err := os.Open(p.path)
    if err != nil {
        return err
    }
    defer f.Close()
    for i := 1; i <= p.capacity; i++ {
        if err := binary.Read(f, binary.LittleEndian, &p.items[i]); err != nil {
            break
        }
        p.used[i] = true
    }
    return nil
}
```

### 7.3 Минимальная реализация `PJSONStore` (PersistentJsonStore)

```go
package pjson

import (
    "encoding/json"
    "fmt"
)

// PJSONStore — Go-версия PersistentJsonStore из C++.
type PJSONStore struct {
    values  *SlabPool[PJSONValue]
    arrays  *SlabPool[PArrayValue]
    objects *SlabPool[PMapValue]
}

func NewPJSONStore(dir string) (*PJSONStore, error) {
    v, err := NewSlabPool[PJSONValue](dir+"/values.bin", 4096)
    if err != nil { return nil, err }
    a, err := NewSlabPool[PArrayValue](dir+"/arrays.bin", 4096)
    if err != nil { return nil, err }
    o, err := NewSlabPool[PMapValue](dir+"/objects.bin", 4096)
    if err != nil { return nil, err }
    return &PJSONStore{values: v, arrays: a, objects: o}, nil
}

// Import преобразует encoding/json данные в PJSONStore.
// Возвращает ID корневого узла.
func (s *PJSONStore) Import(data []byte) (uint32, error) {
    var raw interface{}
    if err := json.Unmarshal(data, &raw); err != nil {
        return 0, err
    }
    return s.importValue(raw)
}

func (s *PJSONStore) importValue(v interface{}) (uint32, error) {
    idx, err := s.values.Alloc()
    if err != nil { return 0, err }
    var node PJSONValue
    switch val := v.(type) {
    case nil:
        node = MakeNull()
    case bool:
        node = MakeBool(val)
    case float64:
        node = MakeFloat(val)
    case string:
        node = MakeString(val)
    case []interface{}:
        arrID, err := s.importArray(val)
        if err != nil { return 0, err }
        node = MakeArray(arrID)
    case map[string]interface{}:
        objID, err := s.importObject(val)
        if err != nil { return 0, err }
        node = MakeObject(objID)
    default:
        return 0, fmt.Errorf("unsupported type: %T", v)
    }
    s.values.Store(idx, node)
    return idx, nil
}

// Export восстанавливает JSON из PJSONStore.
func (s *PJSONStore) Export(id uint32) ([]byte, error) {
    v, err := s.exportValue(id)
    if err != nil { return nil, err }
    return json.Marshal(v)
}

func (s *PJSONStore) exportValue(id uint32) (interface{}, error) {
    node, ok := s.values.Load(id)
    if !ok { return nil, fmt.Errorf("value %d not found", id) }
    switch node.Type {
    case PJSONNull:   return nil, nil
    case PJSONBool:   return node.GetBool(), nil
    case PJSONFloat:  return node.GetFloat(), nil
    case PJSONString: return node.GetString(), nil
    case PJSONArray:  return s.exportArray(node.GetArrayID())
    case PJSONObject: return s.exportObject(node.GetObjectID())
    default:          return nil, fmt.Errorf("unknown type %d", node.Type)
    }
}
```

---

## 8. Оценка рисков и ограничений

### 8.1 Технические риски

| Риск | Вероятность | Влияние | Митигация |
|------|------------|---------|-----------|
| **GC паузы для больших хранилищ** | Средняя | Средняя | `GOGC=200`, pre-allocation, `sync.Pool` |
| **`binary.Write` не работает для типов с указателями** | Высокая | Критическая | Строгий constraint на T — только structs без ptr |
| **Потеря RAII — забыт `defer Save()`** | Средняя | Высокая | Дополнительный слой с явными транзакциями |
| **`FPtr.pool` не сериализуется** | Высокая | Средняя | Глобальный реестр пулов или контекст |
| **Размер бинарника для WASM** | Средняя | Низкая | TinyGo для компиляции в WASM |
| **Отсутствие `sizeof` во время компиляции** | Низкая | Низкая | Тест в `init()`, compile-time workaround через дженерики |
| **Производительность: 10-50% медленнее C++** | Высокая | Средняя | Достаточно для jgit/jdb; бенчмарки подтвердят |

### 8.2 Что НЕ переносится напрямую

| Компонент C++ | Проблема с Go-портом | Альтернатива |
|--------------|---------------------|-------------|
| `PageDevice<CachePolicy>` (политика кэша как параметр шаблона) | Go не поддерживает Policy-based design через шаблоны | Интерфейс `CachePolicy` с методами |
| `persist<T>` с RAII семантикой | Нет деструкторов | `defer` + `Close()` pattern |
| `basic_json<ObjectType, ArrayType, StringType>` инстанцирование | Go нет кастомизируемых контейнеров через параметры типа | Кастомный `PJSONStore` |
| Placement new для zero-copy pool init | Go нет placement new | `unsafe.Pointer` + `reflect` |
| `static_assert(is_trivially_copyable<T>)` | Go нет compile-time проверки | Runtime check + документация |

### 8.3 Что переносится легко

| Компонент C++ | Go аналог | Сложность |
|--------------|----------|-----------|
| `ObjectStore` (content-addressed SHA-256) | `crypto/sha256` + файловая структура | ★☆☆☆☆ Тривиальная |
| `persistent_string` SSO | `[23]byte` + `bool` + `uint32` | ★★☆☆☆ Простая |
| `persistent_json_value` (тип-тег + payload) | `byte` + `[64]byte` | ★★★☆☆ Умеренная |
| `persistent_array` slab | `[N]T` + `uint32 size` + `uint32 next` | ★★☆☆☆ Простая |
| `persistent_map` sorted slab | `[N]Entry` + `uint32 size` + `uint32 next` | ★★☆☆☆ Простая |
| `BinDiffSynchronizer` | Сравнение `[]byte` (bytes.Equal + diff) | ★★☆☆☆ Простая |
| SHA-256 content-addressing | `crypto/sha256` | ★☆☆☆☆ Тривиальная |
| JSON Patch (RFC 6902) | [jsonpatch](https://github.com/mattbaird/jsonpatch) или ручная реализация | ★★☆☆☆ Простая |

---

## 9. Рекомендации и выводы

### 9.1 Выводы

1. **Переход на Go технически возможен** и даёт реальные преимущества для jhub (кросс-платформенность, REST API, горутины, простота деплоя).

2. **Полный эквивалент `persist<T>` в Go невозможен** без компромиссов:
   - Нет автоматического RAII — нужен явный `defer Save()`.
   - Нет проверки тривиальной копируемости на уровне компилятора — нужна runtime проверка.
   - Нет union — нужен `[N]byte` payload с ручными accessors.

3. **Go generics (1.18+) достаточны** для реализации `Persist[T]`, `FPtr[T]`, `SlabPool[T]`.

4. **`encoding/binary`** обеспечивает binary serialization fixed-size struct без unsafe.

5. **Производительность Go** достаточна для jgit/jdb: ~5.8× разница с C++ по benchmark в C++ реализации — в Go ожидается дополнительное замедление на 10-50%, но это приемлемо для API-уровня.

6. **mmap** доступен через `golang.org/x/sys/unix` (Linux/macOS) и `golang.org/x/sys/windows` — zero-parse reload реализуем.

### 9.2 Рекомендации

**Рекомендованная стратегия:** Двухфазный подход.

**Фаза A: C++ продолжает развиваться** как эталонная реализация jgit (Фазы 1–4 уже идут в C++). Это даёт:
- Проверенную, быстрее C++ реализацию для production.
- Эталон для тестирования Go-версии.

**Фаза B: Go-реализация для jhub API Server и jdb** (Фаза 5–6):
- Переписать `PJSONStore`, `ObjectStore`, `SlabPool` на Go для API-уровня.
- Использовать `encoding/json`, `crypto/sha256`, `net/http` из стандартной библиотеки.
- Не переписывать низкоуровневые `PageDevice`/`AddressManager` — использовать bbolt/badger как персистный слой.

**Матрица решений**:

| Компонент | Рекомендованный язык | Обоснование |
|-----------|---------------------|-------------|
| `persist<T>`, `fptr<T>`, `AddressManager<T>` | **C++** | Максимальная производительность, тривиально копируемые типы |
| `PageDevice`, `StaticPageDevice` | **C++** | Низкоуровневая страничная память, mmap |
| `PersistentJsonStore` | **C++** для хранилища, **Go** для API | Компромисс: C++ скорость + Go экосистема |
| `ObjectStore` (SHA-256) | **Go** | Простая реализация, crypto/sha256 |
| jgit `Repository`, `Commit`, `Branch` | **Go** | REST API, сетевой протокол HTTP/2 |
| jdb `Database`, `Collection` | **Go** | Параллельный доступ, горутины |
| jhub API Server | **Go** | Cloud Native, net/http |
| jhub Web GUI | **Go → WASM** (TinyGo) или **TypeScript** | Браузерная совместимость |
| CI Runner | **Go** | Docker API, параллельные пайплайны |

### 9.3 Ответ на вопрос issue #40

**Вопрос**: Возможен ли переход `persist<>` и `fptr<>` на Go? Возможно ли написать собственные `pstring`, `parray`, `pmap` и `pjson` для Go?

**Ответ**:

✅ **Да, технически возможно.** Go generics (1.18+) + `encoding/binary` + `[N]byte` payload позволяют реализовать функциональные эквиваленты всех типов.

⚠️ **С компромиссами**: RAII → `defer`, operator overloading → методы, union → `[N]byte`, compile-time assertions → runtime checks.

✅ **`pstring`** — реализуем как `[23]byte` (SSO) + `bool` (isLong) + `uint32` (longIdx) + `uint32` (longLen). Фиксированный размер 32 байта. `binary.Write` совместим.

✅ **`parray`** — реализуем как `[N]T` + `uint32` size + `uint32` nextSlabIdx. Slab chaining через `SlabPool[PArraySlab[T]]`. Фиксированный sizeof.

✅ **`pmap`** — реализуем как сортированный `[N]PMapEntry[V]` + `uint32` size + `uint32` nextSlabIdx. Бинарный поиск. Фиксированный sizeof.

✅ **`pjson`** — реализуем как `PJSONValue` (`byte` type + `[64]byte` payload) + `PJSONStore` (три `SlabPool`). Import/Export через `encoding/json`.

✅ **Менеджеры персистной памяти** — `SlabPool[T]` как аналог `AddressManager<T>`, `Persist[T]` как аналог `persist<T>`, `FPtr[T]` как аналог `fptr<T>`.

---

## 10. Дорожная карта реализации

### Приоритет 1: Прототип (1–2 недели)

1. Реализовать `Persist[T]` + `SlabPool[T]` + тесты.
2. Реализовать `PString` + тесты (SSO, длинные строки, binary round-trip).
3. Реализовать `PJSONValue` + accessors + тесты.
4. Проверить: `binary.Write(PString{})` корректно сохраняет и загружает.

### Приоритет 2: Базовые структуры (2–4 недели)

5. Реализовать `PArraySlab[T]` + `PArray[T]` (slab chaining) + тесты.
6. Реализовать `PMapSlab[V]` + `PMap[V]` (sorted, binary search) + тесты.
7. Реализовать `PJSONStore` (import/export через `encoding/json`) + тесты.

### Приоритет 3: Content-addressed store (1–2 недели)

8. Реализовать `ObjectStore` (SHA-256 + CBOR/JSON файлы) + тесты.
9. Реализовать `PJSONStore.Snapshot()` → `ObjectStore` + `PJSONStore.Restore()`.

### Приоритет 4: jgit Layer на Go (4–8 недель)

10. Реализовать `Repository` (HEAD, refs, objects) на Go.
11. Реализовать `Commit`, `Branch`, `Tag` операции.
12. CLI: `jgit init`, `jgit commit`, `jgit log`, `jgit diff`, `jgit checkout`.

### Приоритет 5: jdb + jhub API (по плану jhub-tz.md)

13. `jdb.Database`, `jdb.Collection`, REST API (net/http).
14. CI Runner, Docker Registry, Swarm Manager.

---

## Ссылки

- [persistent_json_analysis.md](persistent_json_analysis.md) — Анализ применимости persistent_json для jgit/jdb (C++)
- [jhub-tz.md](jhub-tz.md) — Техническое задание jhub
- [plan.md](plan.md) — План развития проекта
- [analysis.md](analysis.md) — Анализ BinDiffSynchronizer
- [migration-analysis.md (jsonRVM)](https://github.com/netkeep80/jsonRVM/blob/master/migration-analysis.md) — Анализ перехода jsonRVM на Go/Rust/WASM
- [Go generics (1.18+)](https://go.dev/doc/faq#generics) — Дженерики в Go
- [encoding/binary](https://pkg.go.dev/encoding/binary) — Бинарная сериализация в Go
- [edsrzf/mmap-go](https://github.com/edsrzf/mmap-go) — mmap для Go
- [etcd-io/bbolt](https://github.com/etcd-io/bbolt) — Embedded key-value store на Go
- [RFC 6902: JSON Patch](https://datatracker.ietf.org/doc/html/rfc6902) — Формат дельт JSON
- [RFC 7396: JSON Merge Patch](https://datatracker.ietf.org/doc/html/rfc7396) — Слияние JSON

---

*Документ подготовлен в рамках issue #40. Дата: 2026-02-26.*
*Анализ основан на изучении C++ реализации в ветках `issue-34-10f4c07abb3b` (212 тестов, CI зелёный) и существующих документах: `persistent_json_analysis.md`, `jhub-tz.md`, `plan.md`.*
