#pragma once
#include <memory.h>


template
< 
	unsigned PageSize = 16, 
	unsigned PoolSize = 16,
	unsigned SpaceSize = 8,
	template< class, unsigned, unsigned > class CachePolicy = Cache
>
class StaticPageDevice : public PageDevice<PageSize, PoolSize, SpaceSize, CachePolicy>
{
	// собственно сама статическая память
	Page<PageSize>	Pages[__PageCount];
	
public:
	StaticPageDevice()
	{
		// Проинициализирум карту памяти
		for( int a = 0; a < __PageCount; a++ )
			memset( Pages[a].Data, 0, sizeof(Page<PageSize>) );
	};

	virtual ~StaticPageDevice()	{};

protected:
	virtual bool  Load( unsigned Index, Page<PageSize>& Ref )
	{
		if( Index < __PageCount )
		{
			memcpy( &Ref, Pages[Index].Data, sizeof(Page<PageSize>) );
			return true;
		}
		return false;
	};

	virtual bool  Save( unsigned Index, Page<PageSize>& Ref )
	{
		if( Index < __PageCount )
		{
			memcpy( Pages[Index].Data, &Ref, sizeof(Page<PageSize>) );
			return true;
		}
		return false;
	};
};


