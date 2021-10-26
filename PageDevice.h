#pragma once 
#include <vector>

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
		for( int i = 0; i < CacheSize; i++ )
		{
			Pool[i].Index = -1;
			Pool[i].Durty = false;
		}
	}

	virtual ~Cache()
	{
		// сливаем кэш
	}

	_T*		GetData( unsigned Index, bool ForWrite )
	{
		if( VMap[Index] != NULL )
		{
			VMap[Index]->Durty |= ForWrite;
			return &VMap[Index]->Obj;
		}
		else
		{
			// найдём место для загрузки
			unsigned PoolPos = (LastLoadedIndex + 1) % CacheSize;

			if( Pool[PoolPos].Index >= 0 )
			{
				if( Pool[PoolPos].Durty )
				{
					if( !Save( Pool[PoolPos].Index, Pool[PoolPos].Obj ) )
					{
						// ошибка сохранения страницы
						return NULL;
					}
					Pool[PoolPos].Durty = false;
				}
				VMap[Pool[PoolPos].Index] = NULL;
			}

			Pool[PoolPos].Index = Index;

			if( Load( Index, Pool[PoolPos].Obj ) )
			{	//	если загрузили страницу
				LastLoadedIndex = PoolPos;
				VMap[Index] = &Pool[PoolPos];
				VMap[Index]->Durty = ForWrite;
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
		bool Durty;
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

	virtual ~PageDevice()
	{
	}

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
	__forceinline	bool	Read( unsigned Address, unsigned char* Data, unsigned Size )
	{
		unsigned char*	PagePtr;
		unsigned Index = (Address & PageMask) > PageSize;
		unsigned Offset = Address & OffsetMask;
		unsigned Step = (1 << PageSize) - Offset;
		if( Step > Size ) Step = Size;

		while( PagePtr = PageDev.GetData( Index++, false )->Data )
		{ 
			memcpy( Data, &PagePtr[Offset], Step );
			Size -= Step;
			if( Size == 0 ) return true;
			if( Size < (1 << PageSize) ) Step = Size;
			else Step = 1 << PageSize;
			Data += 1 << PageSize;
			Offset = 0;
		}

		return false;
	}
	__forceinline	bool	Write( unsigned Address, unsigned char* Data, unsigned Size )
	{
		unsigned char*	PagePtr;
		unsigned Index = (Address & PageMask) > PageSize;
		unsigned Offset = Address & OffsetMask;
		unsigned Step = (1 << PageSize) - Offset;
		if( Step > Size ) Step = Size;

		while( PagePtr = PageDev.GetData( Index++, true )->Data )
		{ 
			memcpy( &PagePtr[Offset], Data, Step );
			Size -= Step;
			if( Size == 0 ) return true;
			if( Size < (1 << PageSize) ) Step = Size;
			else Step = 1 << PageSize;
			Data += 1 << PageSize;
			Offset = 0;
		}

		return false;
	}
};