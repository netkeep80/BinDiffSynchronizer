#pragma once
#include <vector>
#include <cstddef>

using namespace std;
/*
	Ответственность:
		1. Загрузка и сохранения страниц из некоего хранилища
		2. Предоставление страниц на чтение и запись адресному менеджеру (он кэширует объекты отдельно)
		3. Кэширование страниц
		3. Управление пулом закэшированных страниц
*/

///////////////////////////////////////////////////////////////////////////////
//						Cache
///////////////////////////////////////////////////////////////////////////////

template
<
	class _T,
		unsigned CacheSize = 16,
		unsigned SpaceSize = 8
>
class Cache
{
public:
	Cache(): Pool(CacheSize), VMap(1 << SpaceSize)
	{
		LastLoadedIndex = -1;
		// Проинициализируем пул
		for( int i = 0; i < (int)CacheSize; i++ )
		{
			Pool[i].Index = -1;
			Pool[i].Dirty = false;
		}
	}

	// Note: Cache::~Cache() does NOT call Flush() here because at destruction
	// time the vtable is reset to Cache's level, making virtual Load/Save calls
	// invoke the pure-virtual base — causing "pure virtual method called" abort.
	// Concrete subclasses must call Flush() in their own destructor.
	virtual ~Cache() = default;

	// Flush(): save all dirty pages to backing store and mark them clean.
	// Added in Task 3.1.4.
	void Flush()
	{
		for( unsigned i = 0; i < CacheSize; i++ )
		{
			if( Pool[i].Index >= 0 && Pool[i].Dirty )
			{
				Save( Pool[i].Index, Pool[i].Obj );
				Pool[i].Dirty = false;
			}
		}
	}

	// Fixed (Task 3.1.4): added bounds check — undefined behaviour if Index >= VMap.size().
	_T*		GetData( unsigned Index, bool ForWrite )
	{
		if( Index >= VMap.size() ) return NULL;

		if( VMap[Index] != NULL )
		{
			VMap[Index]->Dirty |= ForWrite;
			return &VMap[Index]->Obj;
		}
		else
		{
			// найдём место для загрузки
			unsigned PoolPos = (LastLoadedIndex + 1) % CacheSize;

			if( Pool[PoolPos].Index >= 0 )
			{
				if( Pool[PoolPos].Dirty )
				{
					if( !Save( Pool[PoolPos].Index, Pool[PoolPos].Obj ) )
					{
						// ошибка сохранения страницы
						return NULL;
					}
					Pool[PoolPos].Dirty = false;
				}
				VMap[Pool[PoolPos].Index] = NULL;
			}

			Pool[PoolPos].Index = Index;

			if( Load( Index, Pool[PoolPos].Obj ) )
			{	//	если загрузили страницу
				LastLoadedIndex = PoolPos;
				VMap[Index] = &Pool[PoolPos];
				VMap[Index]->Dirty = ForWrite;
				return &Pool[PoolPos].Obj;
			}
			else
			{
				return NULL;
			}
		}
	}

protected:
	virtual bool  Load( unsigned Index, _T& Ref ) = 0;
	virtual bool  Save( unsigned Index, _T& Ref ) = 0;
private:
	template<class __T>
	struct Container
	{
		__T	 Obj;
		int	 Index;
		bool Dirty;
	};
	vector< Container<_T> >  Pool;
	vector< Container<_T> * > VMap;
	int           LastLoadedIndex;
};

///////////////////////////////////////////////////////////////////////////////
//						PageDevice
///////////////////////////////////////////////////////////////////////////////

template < unsigned PageSize = 16 >
class Page
{
public:
	unsigned char Data[1 << PageSize];
};

template
<
unsigned PageSize = 16,
unsigned PoolSize = 16,
unsigned SpaceSize = 8,
template< class, unsigned, unsigned > class CachePolicy = Cache
>
class PageDevice : public CachePolicy< Page<PageSize>, PoolSize, SpaceSize >
{
public:
	static const unsigned __PageCount = 1 << SpaceSize;

	PageDevice()
	{
	}

	// Note: PageDevice::~PageDevice() does NOT call Flush() here — same reason
	// as Cache::~Cache(): the vtable is already partially reset.
	// Concrete subclasses must call Flush() in their own destructor.
	virtual ~PageDevice() = default;

protected:
	virtual bool  Load( unsigned Index, Page<PageSize>& Ref ) = 0;
	virtual bool  Save( unsigned Index, Page<PageSize>& Ref ) = 0;
};

///////////////////////////////////////////////////////////////////////////////
//						MemoryDevice
///////////////////////////////////////////////////////////////////////////////


template
<
	unsigned MemorySize = 24,
	unsigned PageSize = 16,
	unsigned PoolSize = 16,
	template< class, unsigned, unsigned > class CachePolicy = Cache,
	template < unsigned, unsigned, unsigned, template< class, unsigned, unsigned > class > class _PageDevice = PageDevice
>
class MemoryDevice
{
	static const unsigned OffsetMask = (1 << PageSize) - 1;
	static const unsigned PageMask = ((1 << (MemorySize - PageSize)) - 1) << PageSize;
	_PageDevice< PageSize, PoolSize, MemorySize - PageSize, CachePolicy > PageDev;

public:
	// Fixed (Task 3.1.5): the original loop condition `while( PagePtr = ... )`
	// had two bugs:
	//   1. It called `->Data` on a potentially NULL pointer (null-deref UB).
	//   2. A non-null pointer is always truthy, so the loop ran forever.
	// The fix: call GetData() first, check for NULL before accessing ->Data,
	// and loop only while there is remaining data to transfer.
	inline	bool	Read( unsigned Address, unsigned char* Data, unsigned Size )
	{
		unsigned Index = (Address & PageMask) >> PageSize;
		unsigned Offset = Address & OffsetMask;
		unsigned Step = (1 << PageSize) - Offset;
		if( Step > Size ) Step = Size;

		while( Size > 0 )
		{
			Page<PageSize>* PageObj = PageDev.GetData( Index++, false );
			if( PageObj == NULL ) return false;
			unsigned char* PagePtr = PageObj->Data;
			memcpy( Data, &PagePtr[Offset], Step );
			Size -= Step;
			if( Size == 0 ) return true;
			// Fixed (Task 3.1.5): advance Data by Step (bytes actually read),
			// not by a full page — the original code used `Data += 1 << PageSize`
			// which caused a buffer overrun on the caller's buffer.
			Data += Step;
			if( Size < (1 << PageSize) ) Step = Size;
			else Step = 1 << PageSize;
			Offset = 0;
		}

		return true;
	}
	inline	bool	Write( unsigned Address, unsigned char* Data, unsigned Size )
	{
		unsigned Index = (Address & PageMask) >> PageSize;
		unsigned Offset = Address & OffsetMask;
		unsigned Step = (1 << PageSize) - Offset;
		if( Step > Size ) Step = Size;

		while( Size > 0 )
		{
			Page<PageSize>* PageObj = PageDev.GetData( Index++, true );
			if( PageObj == NULL ) return false;
			unsigned char* PagePtr = PageObj->Data;
			memcpy( &PagePtr[Offset], Data, Step );
			Size -= Step;
			if( Size == 0 ) return true;
			// Fixed (Task 3.1.5): advance Data by Step (bytes actually written),
			// not by a full page — the original code used `Data += 1 << PageSize`
			// which caused a buffer overrun on the caller's buffer.
			Data += Step;
			if( Size < (1 << PageSize) ) Step = Size;
			else Step = 1 << PageSize;
			Offset = 0;
		}

		return true;
	}

	// ReadObject<T> / WriteObject<T>: typed convenience wrappers.
	// Added in Task 3.1.5.
	template<typename T>
	inline bool ReadObject( unsigned Address, T& obj )
	{
		return Read( Address, reinterpret_cast<unsigned char*>(&obj), sizeof(T) );
	}

	template<typename T>
	inline bool WriteObject( unsigned Address, const T& obj )
	{
		return Write( Address, reinterpret_cast<unsigned char*>(const_cast<T*>(&obj)), sizeof(T) );
	}
};
