#pragma once
#include <memory.h>


// Fixed (Task 3.1.6): StaticPageDevice previously referenced __PageCount
// without qualification, which is a dependent name from the base class template
// and is not visible at the point of use without an explicit qualification.
// The fix uses PageDevice<...>::__PageCount everywhere.
template
<
	unsigned PageSize = 16,
	unsigned PoolSize = 16,
	unsigned SpaceSize = 8,
	template< class, unsigned, unsigned > class CachePolicy = Cache
>
class StaticPageDevice : public PageDevice<PageSize, PoolSize, SpaceSize, CachePolicy>
{
	static const unsigned __MyPageCount = PageDevice<PageSize, PoolSize, SpaceSize, CachePolicy>::__PageCount;

	// собственно сама статическая память
	Page<PageSize>	Pages[__MyPageCount];

public:
	StaticPageDevice()
	{
		// Проинициализирум карту памяти
		for( unsigned a = 0; a < __MyPageCount; a++ )
			memset( Pages[a].Data, 0, sizeof(Page<PageSize>) );
	};

	// Call Flush() here while the vtable still points to StaticPageDevice's
	// Load/Save implementations — before base class destructors reset the vtable.
	// (Task 3.1.4/3.1.5: Flush must be called from the concrete leaf destructor.)
	virtual ~StaticPageDevice()
	{
		this->Flush();
	};

protected:
	virtual bool  Load( unsigned Index, Page<PageSize>& Ref )
	{
		if( Index < __MyPageCount )
		{
			memcpy( &Ref, Pages[Index].Data, sizeof(Page<PageSize>) );
			return true;
		}
		return false;
	};

	virtual bool  Save( unsigned Index, Page<PageSize>& Ref )
	{
		if( Index < __MyPageCount )
		{
			memcpy( Pages[Index].Data, &Ref, sizeof(Page<PageSize>) );
			return true;
		}
		return false;
	};
};


