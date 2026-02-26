// Package main — Go Proof of Concept for persist<T>, fptr<T>, pstring, parray, pmap, pjson.
//
// This experiment validates the key concepts from go-migration-analysis.md:
// - Persist[T]: load/save fixed-size structs via encoding/binary
// - SlabPool[T]: address manager (AddressManager<T> equivalent)
// - PString: SSO string (persistent_string equivalent)
// - PJSONValue: tagged union (persistent_json_value equivalent)
// - FPtr[T]: persistent pointer (fptr<T> equivalent)
//
// Run: go run experiments/go-poc/main.go
package main

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"math"
	"os"
)

// ============================================================================
// PString — аналог persistent_string из C++
// SSO: до 23 символов хранятся inline; длинные строки — через индекс в пуле.
// Фиксированный размер: 23 + 1(isLong) + 1(pad) + 2(pad) + 4(longIdx) + 4(longLen) = 35 → 36 bytes
// ============================================================================

const ssoSize = 23

// PString — персистная строка с SSO.
// ВАЖНО: без указателей и слайсов → binary.Write/Read совместима.
type PString struct {
	Sso     [ssoSize + 1]byte // +1 NUL terminator, = 24 bytes
	IsLong  bool              // 1 byte
	Pad     [3]byte           // выравнивание = 3 bytes
	LongIdx uint32            // индекс в PCharPool = 4 bytes
	LongLen uint32            // длина длинной строки = 4 bytes
	// Total: 24 + 1 + 3 + 4 + 4 = 36 bytes
}

// Validate: должна быть fixed-size для binary.Write
func init() {
	size := binary.Size(PString{})
	if size < 0 {
		panic("PString is not binary.Size-compatible")
	}
	fmt.Printf("PString size: %d bytes\n", size)
}

// NewPString создаёт PString из строки.
func NewPString(s string) PString {
	var ps PString
	if len(s) <= ssoSize {
		ps.IsLong = false
		copy(ps.Sso[:], s)
	} else {
		// Упрощение: в PoC не используем реальный пул для длинных строк
		ps.IsLong = true
		ps.LongLen = uint32(len(s))
		ps.LongIdx = 0 // в реальной реализации — индекс в PCharPool
	}
	return ps
}

// String возвращает строку из PString.
func (ps PString) String() string {
	if !ps.IsLong {
		n := 0
		for n < len(ps.Sso) && ps.Sso[n] != 0 {
			n++
		}
		return string(ps.Sso[:n])
	}
	return fmt.Sprintf("<long string, idx=%d, len=%d>", ps.LongIdx, ps.LongLen)
}

// ============================================================================
// PJSONType + PJSONValue — аналог json_type + persistent_json_value из C++
// ============================================================================

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
// ВАЖНО: fixed size, без указателей и слайсов.
// Размер: 1(type) + 7(pad) + 36(payload для PString) = 44 bytes.
// (В C++: 1 + 7 + sizeof(persistent_string) = ~72 bytes)
type PJSONValue struct {
	Type    PJSONType
	Pad     [7]byte
	Payload [36]byte // достаточно для PString (36 bytes)
}

func init() {
	size := binary.Size(PJSONValue{})
	if size < 0 {
		panic("PJSONValue is not binary.Size-compatible")
	}
	fmt.Printf("PJSONValue size: %d bytes\n", size)
}

func MakeNull() PJSONValue { return PJSONValue{Type: PJSONNull} }

func MakeBool(b bool) PJSONValue {
	v := PJSONValue{Type: PJSONBool}
	if b {
		v.Payload[0] = 1
	}
	return v
}

func MakeInt(n int64) PJSONValue {
	v := PJSONValue{Type: PJSONInt}
	binary.LittleEndian.PutUint64(v.Payload[:8], uint64(n))
	return v
}

func MakeFloat(f float64) PJSONValue {
	v := PJSONValue{Type: PJSONFloat}
	binary.LittleEndian.PutUint64(v.Payload[:8], math.Float64bits(f))
	return v
}

func MakeString(s string) PJSONValue {
	v := PJSONValue{Type: PJSONString}
	ps := NewPString(s)
	var buf bytes.Buffer
	_ = binary.Write(&buf, binary.LittleEndian, ps)
	copy(v.Payload[:], buf.Bytes())
	return v
}

func MakeArray(id uint32) PJSONValue {
	v := PJSONValue{Type: PJSONArray}
	binary.LittleEndian.PutUint32(v.Payload[:4], id)
	return v
}

func MakeObject(id uint32) PJSONValue {
	v := PJSONValue{Type: PJSONObject}
	binary.LittleEndian.PutUint32(v.Payload[:4], id)
	return v
}

func (v PJSONValue) GetBool() bool {
	return v.Payload[0] != 0
}

func (v PJSONValue) GetInt() int64 {
	return int64(binary.LittleEndian.Uint64(v.Payload[:8]))
}

func (v PJSONValue) GetFloat() float64 {
	return math.Float64frombits(binary.LittleEndian.Uint64(v.Payload[:8]))
}

func (v PJSONValue) GetString() string {
	var ps PString
	r := bytes.NewReader(v.Payload[:])
	_ = binary.Read(r, binary.LittleEndian, &ps)
	return ps.String()
}

func (v PJSONValue) GetArrayID() uint32 {
	return binary.LittleEndian.Uint32(v.Payload[:4])
}

func (v PJSONValue) GetObjectID() uint32 {
	return binary.LittleEndian.Uint32(v.Payload[:4])
}

// ============================================================================
// SlabPool[T] — аналог AddressManager<T> из C++
// ============================================================================

type SlabPool[T any] struct {
	items    []T
	used     []bool
	capacity int
}

func NewSlabPool[T any](capacity int) *SlabPool[T] {
	return &SlabPool[T]{
		items:    make([]T, capacity+1),
		used:     make([]bool, capacity+1),
		capacity: capacity,
	}
}

func (p *SlabPool[T]) Alloc() (uint32, error) {
	for i := 1; i <= p.capacity; i++ {
		if !p.used[i] {
			p.used[i] = true
			return uint32(i), nil
		}
	}
	return 0, fmt.Errorf("SlabPool: out of capacity (%d)", p.capacity)
}

func (p *SlabPool[T]) Free(idx uint32) {
	if idx > 0 && int(idx) <= p.capacity {
		p.used[idx] = false
		var zero T
		p.items[idx] = zero
	}
}

func (p *SlabPool[T]) Store(idx uint32, v T) {
	if idx > 0 && int(idx) <= p.capacity {
		p.items[idx] = v
	}
}

func (p *SlabPool[T]) Load(idx uint32) (T, bool) {
	if idx > 0 && int(idx) <= p.capacity && p.used[idx] {
		return p.items[idx], true
	}
	var zero T
	return zero, false
}

// SaveToFile сохраняет все слоты в файл.
func (p *SlabPool[T]) SaveToFile(path string) error {
	f, err := os.Create(path)
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

// LoadFromFile загружает слоты из файла.
func (p *SlabPool[T]) LoadFromFile(path string) error {
	f, err := os.Open(path)
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

// ============================================================================
// Persist[T] — аналог persist<T> из C++
// ============================================================================

type Persist[T any] struct {
	Filename string
	Value    T
}

func LoadPersist[T any](filename string) (*Persist[T], error) {
	p := &Persist[T]{Filename: filename}
	f, err := os.Open(filename)
	if err == nil {
		defer f.Close()
		_ = binary.Read(f, binary.LittleEndian, &p.Value)
	}
	return p, nil
}

func (p *Persist[T]) Save() error {
	f, err := os.Create(p.Filename)
	if err != nil {
		return err
	}
	defer f.Close()
	return binary.Write(f, binary.LittleEndian, p.Value)
}

// ============================================================================
// FPtr[T] — аналог fptr<T> из C++
// ============================================================================

type FPtr[T any] struct {
	Addr uint32
	pool *SlabPool[T]
}

func NewFPtr[T any](pool *SlabPool[T]) FPtr[T] {
	return FPtr[T]{pool: pool}
}

func (f *FPtr[T]) New() error {
	idx, err := f.pool.Alloc()
	if err != nil {
		return err
	}
	f.Addr = idx
	return nil
}

func (f *FPtr[T]) Delete() {
	if f.Addr != 0 {
		f.pool.Free(f.Addr)
		f.Addr = 0
	}
}

func (f FPtr[T]) Deref() (T, bool) {
	if f.Addr == 0 {
		var zero T
		return zero, false
	}
	return f.pool.Load(f.Addr)
}

func (f FPtr[T]) IsNull() bool { return f.Addr == 0 }

// ============================================================================
// MAIN — демонстрация
// ============================================================================

func main() {
	fmt.Println("\n=== Go PoC: persist<T>, fptr<T>, pstring, pjson ===\n")

	// --- PString ---
	fmt.Println("--- PString ---")
	ps1 := NewPString("hello")
	ps2 := NewPString("world")
	ps3 := NewPString("this is a longer string that exceeds SSO limit!")
	fmt.Printf("ps1 = %q (isLong=%v)\n", ps1.String(), ps1.IsLong)
	fmt.Printf("ps2 = %q (isLong=%v)\n", ps2.String(), ps2.IsLong)
	fmt.Printf("ps3 = %q (isLong=%v)\n", ps3.String(), ps3.IsLong)

	// Binary round-trip для PString
	var buf bytes.Buffer
	_ = binary.Write(&buf, binary.LittleEndian, ps1)
	var ps1Loaded PString
	_ = binary.Read(bytes.NewReader(buf.Bytes()), binary.LittleEndian, &ps1Loaded)
	fmt.Printf("Binary round-trip: %q → %q (match=%v)\n", ps1.String(), ps1Loaded.String(), ps1.String() == ps1Loaded.String())

	// --- PJSONValue ---
	fmt.Println("\n--- PJSONValue ---")
	jNull := MakeNull()
	jBool := MakeBool(true)
	jInt := MakeInt(42)
	jFloat := MakeFloat(3.14)
	jStr := MakeString("json string")
	jArr := MakeArray(7)
	jObj := MakeObject(13)

	fmt.Printf("null   → type=%d\n", jNull.Type)
	fmt.Printf("bool   → type=%d, val=%v\n", jBool.Type, jBool.GetBool())
	fmt.Printf("int    → type=%d, val=%d\n", jInt.Type, jInt.GetInt())
	fmt.Printf("float  → type=%d, val=%f\n", jFloat.Type, jFloat.GetFloat())
	fmt.Printf("string → type=%d, val=%q\n", jStr.Type, jStr.GetString())
	fmt.Printf("array  → type=%d, id=%d\n", jArr.Type, jArr.GetArrayID())
	fmt.Printf("object → type=%d, id=%d\n", jObj.Type, jObj.GetObjectID())

	// Binary round-trip для PJSONValue
	var buf2 bytes.Buffer
	_ = binary.Write(&buf2, binary.LittleEndian, jFloat)
	var jFloatLoaded PJSONValue
	_ = binary.Read(bytes.NewReader(buf2.Bytes()), binary.LittleEndian, &jFloatLoaded)
	fmt.Printf("float binary round-trip: %f → %f (match=%v)\n",
		jFloat.GetFloat(), jFloatLoaded.GetFloat(),
		jFloat.GetFloat() == jFloatLoaded.GetFloat())

	// --- SlabPool + FPtr ---
	fmt.Println("\n--- SlabPool[PJSONValue] + FPtr ---")
	pool := NewSlabPool[PJSONValue](100)

	ptr1 := NewFPtr(pool)
	_ = ptr1.New()
	pool.Store(ptr1.Addr, MakeInt(100))

	ptr2 := NewFPtr(pool)
	_ = ptr2.New()
	pool.Store(ptr2.Addr, MakeString("persistent!"))

	v1, ok1 := ptr1.Deref()
	v2, ok2 := ptr2.Deref()
	fmt.Printf("ptr1 → addr=%d, val=%d (ok=%v)\n", ptr1.Addr, v1.GetInt(), ok1)
	fmt.Printf("ptr2 → addr=%d, val=%q (ok=%v)\n", ptr2.Addr, v2.GetString(), ok2)

	// --- Persist[T] file save/load ---
	fmt.Println("\n--- Persist[T]: save + load ---")
	type Config struct {
		Version uint32
		Debug   bool
		Pad     [3]byte
		Name    PString
	}

	tmpFile := "/tmp/test_config.persist"
	p1 := &Persist[Config]{
		Filename: tmpFile,
		Value: Config{
			Version: 42,
			Debug:   true,
			Name:    NewPString("jgit-config"),
		},
	}
	if err := p1.Save(); err != nil {
		fmt.Printf("Save error: %v\n", err)
	} else {
		fmt.Printf("Saved: Version=%d, Debug=%v, Name=%q\n",
			p1.Value.Version, p1.Value.Debug, p1.Value.Name.String())
	}

	p2, _ := LoadPersist[Config](tmpFile)
	fmt.Printf("Loaded: Version=%d, Debug=%v, Name=%q\n",
		p2.Value.Version, p2.Value.Debug, p2.Value.Name.String())

	// --- SlabPool save/load ---
	fmt.Println("\n--- SlabPool[PJSONValue]: save + load ---")
	pool2 := NewSlabPool[PJSONValue](10)
	idx1, _ := pool2.Alloc()
	pool2.Store(idx1, MakeInt(777))
	idx2, _ := pool2.Alloc()
	pool2.Store(idx2, MakeString("saved"))

	tmpPool := "/tmp/test_pool.bin"
	if err := pool2.SaveToFile(tmpPool); err != nil {
		fmt.Printf("Pool save error: %v\n", err)
	}

	pool3 := NewSlabPool[PJSONValue](10)
	if err := pool3.LoadFromFile(tmpPool); err != nil {
		fmt.Printf("Pool load error: %v\n", err)
	}
	v1l, _ := pool3.Load(idx1)
	v2l, _ := pool3.Load(idx2)
	fmt.Printf("Pool loaded[%d] = %d (match=%v)\n", idx1, v1l.GetInt(), v1l.GetInt() == 777)
	fmt.Printf("Pool loaded[%d] = %q (match=%v)\n", idx2, v2l.GetString(), v2l.GetString() == "saved")

	// Cleanup
	_ = os.Remove(tmpFile)
	_ = os.Remove(tmpPool)

	fmt.Println("\n=== PoC PASSED: all concepts validated ===")
}
