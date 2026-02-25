#ifndef __PERSIST_H__
#define __PERSIST_H__

#include <iostream>
#include <fstream>
#include <sstream>
#include <typeinfo>
#include <cstring>
#include <filesystem>
#include <type_traits>
#include "PageDevice.h"

/*
Словарь:
АМ - адресный менеджер
АП - адресное пространство
ПАП - персистное адресное пространство
ПАМ - персистный адресный менеджер

    Персистные объекты отличаются от обычных тем что обычный конструктор и деструктор у таких объектов
    не меняет их состояния. Персистные объекты вообще не имеют деструкторы в обычном понимании, так как
    то что они персистные уже подразумевает что они вечные. Т.е. они либо используются либо нет.

    Вместо обычного конструктора у персистных объектов есть специальные методы для инициализации
    их состояния при создании объекта в персистном хранилище.

    Важно понимать разницу между персистным указателем на объект и указателем на персистный объект,


Создание и удаление:
    Создание и удаление персистных объектов и персистных указателей ничем не отличается от создания и удаление
    обычных объектов. Они создаются в одной и той же памяти, могут создаваться статически либо динамически.
    Другое дело создание и удаление объектов на которые указывают персистные указатели. Этим заведуют менеджеры
    адресных пространств соотвествующих типов объектов, т.к. для каждого типа объекта на который ссылается персистный
    указатель используется свой менеждер адресного пространства. Физически адресное пространство может быть диском,
    сетевым именем компьютера, объектной или обычной БД или ещё чем то ещё. Менеждер АП имеет специальные статические
    методы для выделения и освобождения памяти для объектов.


Конструктор    |

Инициализация  |
ономить
Деструктор     |

Удаление       |


ПАП будет хранить только объекты предназначенные для хранения в ПАП.

struct BlockInfo
{
      unsigned ObjectID; // Если = 0 значит область ПАП свободна и является кандидатом на объединение
      unsigned PersistAddress; // Адрес в ПАП
      unsigned ObjectSize; // Размер объекта
      unsigned NextBlobk; // Указатель на следующее звено состояния менеджера в ПАП
};
*/

using namespace std;

template <class _T> class persist;
template <class _T> class fptr;
const unsigned faddress_size = 64;


// persist template class for trivially-copyable c++ types.
//
// IMPORTANT CONSTRAINT: _T must be trivially copyable (the compiler will
// enforce this via static_assert below). persist<T> saves and loads the raw
// sizeof(T) bytes to/from a file. This is only valid when the entire object
// state is contained in its fixed-size in-memory representation — i.e. no
// heap-allocated members.
//
// IMPORTANT CONSTRAINT: A persist<T> object must not be moved in memory after
// construction. The filename is derived from the address of `this`; moving the
// object would change `this` and make the object write to a different file on
// destruction.
//
// Constructors:
//   persist()                    — load from address-derived filename (ASLR-dependent)
//   persist(const _T& ref)       — initialise from value, no file I/O on construction
//   persist(const char* filename) — load from a named file (deterministic across restarts)
//   persist(const std::string& filename) — same as above
template <class _T>
class persist
{
    // Enforce trivial copyability: persist<T> saves/loads raw bytes, which is
    // only correct for trivially copyable types.
    static_assert(std::is_trivially_copyable<_T>::value,
                  "persist<T> requires T to be trivially copyable");

    friend class fptr<_T>;
    typedef _T& _Tref;
    typedef _T* _Tptr;
    //union{
	//	_T val;
		unsigned char _data[sizeof(_T)];
    //};

    // Address-derived filename (ASLR-dependent — different on every run).
    // Used only by the default constructor/destructor pair.
    void get_name( char* faddress )
    {
		union convert
        {
			persist<_T>* a;
			std::uintptr_t b;
		} c;
		c.a = this;

		std::ostringstream oss;
		oss << std::hex << c.b;
		std::string hexStr = oss.str();

		std::string result = std::string("./Obj_") + hexStr + ".persist";
		std::strncpy( faddress, result.c_str(), faddress_size - 1 );
		faddress[faddress_size - 1] = '\0';
    }

    // The explicit filename used by named constructors (empty == use get_name).
    char _fname[faddress_size];

public:
    // Default constructor: derive filename from address of this (ASLR-dependent).
    // Loads existing data if the file exists.
    persist()
    {
		_fname[0] = 0;
		get_name( _fname );
		// Zero-initialise the storage then load from file.
		// Since _T is trivially copyable, zero-init is a valid default state.
		std::memset( _data, 0, sizeof(_T) );
		// Open in binary mode to preserve raw bytes on all platforms (including
		// Windows, where text-mode I/O translates 0x0A ↔ 0x0D 0x0A).
		ifstream( _fname, ios::binary ).read( (char*)(&_data[0]), sizeof(_T) );
    }

    // Value constructor: initialise from ref, no file I/O on construction.
    // The object will be saved to an address-derived file on destruction.
    persist(const _T& ref)
    {
		_fname[0] = 0;
		get_name( _fname );
        // Copy-initialise the raw bytes from ref.
        std::memcpy( _data, &ref, sizeof(_T) );
    }

    // Named constructor (C-string): load from / save to the given filename.
    // Provides deterministic persistence across process restarts.
    explicit persist(const char* filename)
    {
        std::strncpy( _fname, filename, faddress_size - 1 );
        _fname[faddress_size - 1] = '\0';
        std::memset( _data, 0, sizeof(_T) );
        // Open in binary mode to preserve raw bytes on all platforms (including
        // Windows, where text-mode I/O translates 0x0A ↔ 0x0D 0x0A).
        ifstream( _fname, ios::binary ).read( (char*)(&_data[0]), sizeof(_T) );
    }

    // Named constructor (std::string): convenience overload.
    explicit persist(const std::string& filename) : persist(filename.c_str()) {}

    ~persist()
    {
		// Open in binary mode to preserve raw bytes on all platforms (including
		// Windows, where text-mode I/O translates 0x0A ↔ 0x0D 0x0A).
		ofstream( _fname, ios::binary ).write( (char*)(&_data[0]), sizeof(_T) );
		// Since _T is required to be trivially copyable (enforced by
		// static_assert above), its destructor is trivial — no explicit
		// destructor call is needed. The compiler will handle cleanup of
		// _data (a raw byte array) automatically.
    }

    operator _Tref() { return (*(_T*)_data); }
    operator _Tref() const { return (*(_T*)_data); }
    _T* operator&() { return &(*(_T*)_data); }
    _Tref operator=( const _T& ref ) { return (*(_T*)_data) = ref; }
};

/*
template <class _T>
class Buffer
{
public:
	typedef Buffer<_T> Buffer_T;
	Buffer() { Allocated = Used = 0; Ptr = NULL; };
	~Buffer() { if( Allocated ) delete[] Ptr; };

	unsigned		Push( _T Item )
	{
		if( Used >= Allocated ) Resize( Allocated + 1 );
		Ptr[Used] = Item;
		return Used++;
	};

	_T*			Next()
	{
		if( Used >= Allocated ) Resize( Allocated + 1 );
		_T*	NextPtr = &Ptr[Used++];
		return	NextPtr;
	};

	_T*			Last() { return (Used > 0 ? &Ptr[Used-1] : NULL); };
	_T*			GetPtr() { return Ptr; };
	unsigned	Size() const { return Used; };
	void		Clear() { Used = 0; };
	inline _T&	operator[]( unsigned pos ) { return Ptr[pos]; };

private:
	unsigned	Allocated;
	unsigned	Used;
	_T*			Ptr;

	void		Resize( unsigned size )
	{
		unsigned	oldSize = Allocated;
		_T*			oldPtr = Ptr;

		if( Allocated < 32 ) Allocated = 32;
		while( Allocated < size ) Allocated += Allocated >> 2;
		Ptr = new _T[Allocated];

		if( !Ptr )
		{
			Ptr = oldPtr;
			Allocated = oldSize;
			throw( "Not enough memory!" );
		}

		if( oldSize )
		{
			memcpy( Ptr, oldPtr, Used * sizeof(_T) );
			delete[] oldPtr;
		}
	};
};
*/

/*

для того что бы обеспечить наследование и полиморфизм персистных объектов
необходимо сделать единое адресное пространство для всех используемых объектов.
Для этого необходимо для каждого персистного указателя в таблице дескрипторов
хранить идентификатор конструктора объекта и его размер.
Идентификатором типа объекта может служить например название его класса.

    Адресный менеджер должен обеспечивать загрузку и сохранение
    объектов из/в персистного хранилища (заархивированный закриптованный файл например)

  Пока сделаем упрощенный вариант, сделаем менеджер вектором персистных объектов

  адресный менеджер должен иметь персистную таблицу имён объектов и признака существования
*/
#define ADDRESS_SPACE   1024

// AddressManager<_T, AddressSpace> — manages a persistent address space of up
// to AddressSpace slots of type _T. Slot 0 is reserved (means "null/invalid").
//
// Bugs fixed in Task 3.1.3:
//   - Create(): result of Find() was discarded; now assigned to addr.
//   - get_fname(): was returning a static char[] — not thread-safe; now returns std::string.
//   - __load__obj(): placement-new was called AFTER reading data from file,
//     overwriting the loaded bytes; order is now: allocate, placement-new (to
//     initialise the object to a known state), then read from file.
//   - AddressSpace is now a template parameter (default 1024 for backward compat).
template<class _T, unsigned AddressSpace = ADDRESS_SPACE>
class AddressManager
{
    friend class persist<_T>;
    friend class fptr<_T>;

    struct __info
    {
        int     __refs;
        _T*     __ptr;
        bool    __used;
        char    __name[faddress_size];
    };
    persist< __info[AddressSpace] >   __itable;

public:
    AddressManager()
    {   //  очищаем старые указатели
        for( unsigned i = 1; i < AddressSpace; i++ )
        {
            __itable[i].__refs = 0;
            __itable[i].__ptr = NULL;
        }
    }

    ~AddressManager()
    {   //  сохраняем и освобождаем загруженные объекты
        for( unsigned i = 1; i < AddressSpace; i++ )
        {
            if( __itable[i].__ptr )
            {
                __save__obj( i );
                // _T is trivially copyable (enforce by persist<T>), so its
                // destructor is trivial — no explicit destructor call needed.
                delete[] (char*)__itable[i].__ptr;
            }
        }
    }

private:
    // Returns the filename used for the flat object-data file.
    // Fixed: was returning a static char[] (not thread-safe); now returns std::string.
    std::string get_fname( unsigned /*index*/ )
    {
        return std::string("./") + typeid(_T).name() + ".extend";
    }

    void __load__obj( unsigned index )
    {
        if( index == 0 )    return;
        if( !__itable[index].__used )    return;    // не существует такого объекта
        ifstream in( get_fname( index ) );
        in.seekg( (index - 1) * sizeof(_T) );
        if( in.good() )
        {
            // Allocate raw storage, zero-initialise, then load bytes from file.
            // Fixed: was calling placement-new AFTER reading, which reset the data.
            // Placement-new on array types is also invalid C++; since _T is
            // trivially copyable, zero-init + raw read is correct.
            char* raw = new char[sizeof(_T)];
            std::memset( raw, 0, sizeof(_T) );
            in.read( raw, sizeof(_T) );
            __itable[index].__ptr = reinterpret_cast<_T*>(raw);
        }
    }

    void __save__obj( unsigned index )
    {
        ofstream out( get_fname( index ), ios::out | ios::binary );
        out.seekp( (index - 1) * sizeof(_T) );
        if( out.good() ) out.write( (char*)__itable[index].__ptr, sizeof(_T) );
        else             cout << "AddressManager::__save__obj() can't save obj" << endl;
    }

    inline _T&	operator[]( unsigned index )
    {
        if( __itable[index].__ptr == NULL ) __load__obj( index );
        return *__itable[index].__ptr;
    };

public:
    static AddressManager<_T, AddressSpace>& GetManager()
    {
        static AddressManager<_T, AddressSpace> __one;
        return __one;
    }

    static unsigned Create( char* __faddress )
    {
        unsigned    addr = 0;
        // Fixed: result of Find() was discarded; now assigned to addr.
        if( __faddress != NULL ) addr = Find( __faddress );
        if( !addr )
        {
            for( unsigned i = 1; i < AddressSpace; i++ )
            {
                if( !AddressManager<_T, AddressSpace>::GetManager().__itable[i].__used )
                {
                    AddressManager<_T, AddressSpace>::GetManager().__itable[i].__used = true;
                    AddressManager<_T, AddressSpace>::GetManager().__itable[i].__ptr = new _T();
                    std::strncpy( AddressManager<_T, AddressSpace>::GetManager().__itable[i].__name, __faddress, faddress_size - 1 );
                    AddressManager<_T, AddressSpace>::GetManager().__itable[i].__name[faddress_size - 1] = '\0';
                    return i;
                }
            }
        }
        return addr;
    }

    static void Release( unsigned index )
    {
        if( index != 0 )
            AddressManager<_T, AddressSpace>::GetManager().__itable[index].__refs--;
    }

    // Delete(): explicitly release a slot and its storage.
    // Sets the slot __used flag to false and frees the in-memory object.
    // Since _T is trivially copyable, no explicit destructor call is needed.
    static void Delete( unsigned index )
    {
        if( index == 0 ) return;
        auto& mgr = AddressManager<_T, AddressSpace>::GetManager();
        if( mgr.__itable[index].__ptr )
        {
            // _T is trivially copyable so its destructor is trivial; skip call.
            delete[] (char*)mgr.__itable[index].__ptr;
            mgr.__itable[index].__ptr = NULL;
        }
        mgr.__itable[index].__used = false;
        mgr.__itable[index].__refs = 0;
        mgr.__itable[index].__name[0] = '\0';
    }

    static unsigned Find( char* __faddress )
    {
        for( unsigned i = 1; i < AddressSpace; i++ )
            if( AddressManager<_T, AddressSpace>::GetManager().__itable[i].__used )
                if( !strcmp( AddressManager<_T, AddressSpace>::GetManager().__itable[i].__name, __faddress ) )
                    return i;

        return 0;
    }
};

// реестр фабрик классов


/*
    Тело самого персистного указателя состоит из адреса (порядкового номера)
    объекта в персистном хранилище экстенда.

1. так как персистный указатель сам по себе есть персистный объект то его конструктор
  и деструктор не должны менять его состояния.
*/

template <class _T>
class fptr
{
    //  local types
    typedef _T& _Tref;
    typedef _T* _Tptr;

    //  persist address
    unsigned    __addr;

public:
    inline fptr() : __addr(0) {};
    inline fptr( char* __faddress ) : __addr(0) { __addr = AddressManager<_T>::Find( __faddress ); };
    // Task 3.2.2: copy constructor and destructor are defaulted (trivial) so
    // that fptr<T> satisfies std::is_trivially_copyable and can be embedded
    // inside trivially-copyable structs such as persistent_map<V,C> which
    // must satisfy std::is_trivially_copyable for use with persist<T>.
    //
    // The original non-const copy constructor `fptr(fptr<_T>& ptr)` and
    // the original destructor `~fptr() { Release(__addr); }` have been
    // replaced with defaulted (trivial) versions.  Callers that need
    // explicit reference-count management must call Delete() or Release().
    inline fptr( const fptr<_T>& ) = default;
    inline ~fptr() = default;

    inline operator _Tptr() { return &AddressManager<_T>::GetManager()[__addr]; }
    inline operator _Tptr() const { return &AddressManager<_T>::GetManager()[__addr]; }

    inline _T& operator*() { return AddressManager<_T>::GetManager()[__addr]; }
    inline _T* operator->() { return &AddressManager<_T>::GetManager()[__addr]; }

    inline fptr<_T>& operator=( char* __faddress ) { __addr = AddressManager<_T>::Find( __faddress ); return *this; }

    void    New( char* __faddress )
    {
        __addr = AddressManager<_T>::Create( __faddress );
    }

    // Delete(): explicitly delete the persistent object this fptr refers to.
    // After Delete(), the slot is freed and __addr is set to 0 (null).
    void    Delete()
    {
        AddressManager<_T>::Delete( __addr );
        __addr = 0;
    }

    unsigned addr() const { return __addr; }

    // set_addr(): directly assign an address index without going through
    // AddressManager::Find/Create.  Used when an fptr field stores an
    // already-known slot index (e.g. when chaining persistent_map slabs
    // that were allocated externally by a pool manager).
    void set_addr(unsigned a) { __addr = a; }
};

typedef persist<char>				pchar;
typedef persist<unsigned char>		puchar;
typedef persist<short>				pshort;
typedef persist<unsigned short>		pushort;
typedef persist<int>				pint;
typedef persist<unsigned>			punsigned;
typedef persist<long>				plong;
typedef persist<unsigned long>		pulong;
typedef persist<float>				pfloat;
typedef persist<double>				pdouble;

//#define Persist(type,filename)

#endif
