#ifndef __PERSIST_H__
#define __PERSIST_H__

#include <iostream>
#include <fstream>
#include <sstream>
#include <typeinfo>
#include <cstring>
#include <filesystem>
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


// persist template class for primitive c++ types
template <class _T>
class persist
{
    friend class fptr<_T>;
    typedef _T& _Tref;
    typedef _T* _Tptr;
    //union{
	//	_T val;
		unsigned char _data[sizeof(_T)];
    //};

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

public:
    persist()
    {
		char faddress[faddress_size];
		faddress[0] = 0;
		get_name( faddress );
		// вызываем принудительно конструктор
		new((void*)_data) _T;
		ifstream( faddress ).read( (char*)(&_data[0]), sizeof(_T) );
    }
    persist(const _T& ref)
    {
        new((void*)_data) _T(ref);
    }
    ~persist()
    {
		char faddress[faddress_size];
		get_name( faddress );
		ofstream( faddress ).write( (char*)(&_data[0]), sizeof(_T) );
		// вызываем принудительно деструктор
		((_T*)_data)->~_T();
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

template<class _T>
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
    persist< __info[ADDRESS_SPACE] >   __itable;

public:
    AddressManager()
    {   //  очищаем старые указатели
        for( int i = 1; i < ADDRESS_SPACE; i++ )
        {
            __itable[i].__refs = 0;
            __itable[i].__ptr = NULL;
        }
    }

    ~AddressManager()
    {   //  сохраняем и освобождаем загруженные объекты
        for( int i = 1; i < ADDRESS_SPACE; i++ )
        {
            if( __itable[i].__ptr )
            {
                __save__obj( i );
                __itable[i].__ptr->~_T();
                delete[] (char*)__itable[i].__ptr;
            }
        }
    }

private:
    char* get_fname( unsigned index )
    {
        static  char	faddress[faddress_size];
        std::string result = std::string("./") + typeid(_T).name() + ".extend";
        std::strncpy( faddress, result.c_str(), faddress_size - 1 );
        faddress[faddress_size - 1] = '\0';
        return faddress;
    }

    void __load__obj( unsigned index )
    {
        if( index == 0 )    return;
        if( !__itable[index].__used )    return;    // не существует такого объекта
        ifstream in( get_fname( index ) );
        in.seekg( (index - 1) * sizeof(_T) );
        if( in.good() )
        {
            __itable[index].__ptr = (_T*)new char[sizeof(_T)];
            in.read( (char*)__itable[index].__ptr, sizeof(_T) );
            __itable[index].__ptr = new((void*)__itable[index].__ptr) _T;
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

//static:
    static AddressManager<_T>& GetManager()
    {
        static AddressManager<_T> __one;
        return __one;
    }

    static unsigned Create( char* __faddress )
    {
        unsigned    addr = 0;
        if( __faddress != NULL ) Find( __faddress );
        if( !addr )
        {
            for( int i = 1; i < ADDRESS_SPACE; i++ )
            {
                if( !AddressManager<_T>::GetManager().__itable[i].__used )
                {
                    AddressManager<_T>::GetManager().__itable[i].__used = true;
                    AddressManager<_T>::GetManager().__itable[i].__ptr = new _T();
                    std::strncpy( AddressManager<_T>::GetManager().__itable[i].__name, __faddress, faddress_size - 1 );
                    AddressManager<_T>::GetManager().__itable[i].__name[faddress_size - 1] = '\0';
                    return i;
                }
            }
        }
        return addr;
    }

    static void Release( unsigned index )
    {
        AddressManager<_T>::GetManager().__itable[index].__refs--;
    }

    static unsigned Find( char* __faddress )
    {
        for( int i = 1; i < ADDRESS_SPACE; i++ )
            if( AddressManager<_T>::GetManager().__itable[i].__used )
                if( !strcmp( AddressManager<_T>::GetManager().__itable[i].__name, __faddress ) )
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
    inline fptr() {};
    inline fptr( char* __faddress ) { __addr = AddressManager<_T>::Find( __faddress ); };
    inline fptr( fptr<_T>& ptr ) : __addr( ptr->__addr ) {};
    inline ~fptr() { AddressManager<_T>::Release(__addr); };

    inline operator _Tptr() { return &AddressManager<_T>::GetManager()[__addr]; }
    inline operator _Tptr() const { return &AddressManager<_T>::GetManager()[__addr]; }

    inline _T& operator*() { return AddressManager<_T>::GetManager()[__addr]; }
    inline _T* operator->() { return &AddressManager<_T>::GetManager()[__addr]; }

    inline fptr<_T>& operator=( char* __faddress ) { __addr = AddressManager<_T>::Find( __faddress ); return *this; }

    void    New( char* __faddress )
    {
        __addr = AddressManager<_T>::Create( __faddress );
    }
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
